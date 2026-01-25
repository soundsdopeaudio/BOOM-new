#include "DrumStyles.h"
#include "GridUtils.h"
#include "PluginProcessor.h"
#include "DrumStyleEnforcer.h"
#include "DrumStyleProfileResolver.h"
#include <random>
#include <cstdint>

// Helper function for generating random boolean values
static bool nextBool(std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, 1);

    return dist(rng) == 1;
}
namespace boom {
    namespace drums
    {
        // Utility
        static inline int clamp01i(int v) { return juce::jlimit(0, 100, v); }
        static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }


        enum class RollRate
        {
            SixteenthTriplet,   // 1/16T  (quarter / 6)
            ThirtySecond,       // 1/32   (quarter / 8)
            ThirtySecondTriplet,// 1/32T  (quarter / 12)
            SixtyFourth,        // 1/64   (quarter / 16)  rare
            SixtyFourthTriplet  // 1/64T  (quarter / 24)  rare
        };

        enum class RollMotion
        {
            Stationary,
            Ascending,
            Descending
        };

        struct RollPlan
        {
            RollRate   rate;
            RollMotion motion;
        };

        static RollPlan pickRollPlan(std::mt19937& rng)
        {
            // weights: feel free to tweak
            // 16T and 32 are common, 32T less common, 64/64T very rare
            struct Choice { RollRate r; int w; };
            static const Choice choices[] = {
                { RollRate::SixteenthTriplet,    28 },
                { RollRate::ThirtySecond,        40 },
                { RollRate::ThirtySecondTriplet, 20 },
                { RollRate::SixtyFourth,          6 },
                { RollRate::SixtyFourthTriplet,   6 }
            };

            int total = 0;
            for (auto& c : choices) total += c.w;
            std::uniform_int_distribution<int> d(0, total - 1);
            int pick = d(rng);

            RollRate rate = RollRate::ThirtySecond;
            for (auto& c : choices)
            {
                if (pick < c.w) { rate = c.r; break; }
                pick -= c.w;
            }

            // motion equally likely (or weight if you want)
            std::uniform_int_distribution<int> m(0, 2);
            RollMotion motion = (RollMotion)m(rng);

            return { rate, motion };
        }

        static int ticksPerRollStep(RollRate r, int ticksPerQuarter)
        {
            // These are “absolute” musical values derived from quarter note length.
            switch (r)
            {
            case RollRate::SixteenthTriplet:     return juce::jmax(1, ticksPerQuarter / 6);
            case RollRate::ThirtySecond:         return juce::jmax(1, ticksPerQuarter / 8);
            case RollRate::ThirtySecondTriplet:  return juce::jmax(1, ticksPerQuarter / 12);
            case RollRate::SixtyFourth:          return juce::jmax(1, ticksPerQuarter / 16);
            case RollRate::SixtyFourthTriplet:   return juce::jmax(1, ticksPerQuarter / 24);
            default:                             return juce::jmax(1, ticksPerQuarter / 8);
            }
        }

        static int rollRowForHit(int baseRow, int hitIndex, RollMotion motion)
        {
            // A small “palette” for movement. Adjust to match YOUR rows.
            // Example assumes these exist:
            // ClosedHat, Perc, OpenHat
            // If you have Perc1/Perc2/Perc3, even better: put those here.
            static const int palette[] = { ClosedHat, Perc, OpenHat };
            constexpr int N = (int)std::size(palette);

            // If the base row isn't one we want to move around, stay stationary.
            auto inPalette = [&](int r)
                {
                    for (int i = 0; i < N; ++i) if (palette[i] == r) return true;
                    return false;
                };

            if (!inPalette(baseRow) || motion == RollMotion::Stationary)
                return baseRow;

            // Find base index
            int baseIdx = 0;
            for (int i = 0; i < N; ++i) if (palette[i] == baseRow) { baseIdx = i; break; }

            int offset = 0;
            if (motion == RollMotion::Ascending)   offset = hitIndex;
            if (motion == RollMotion::Descending)  offset = -hitIndex;

            int idx = (baseIdx + offset) % N;
            if (idx < 0) idx += N;
            return palette[idx];
        }


        // A handy builder for evenly-weighted pulses
        static void pulses(RowSpec& rs, int every16, float onProb, int velMin = 92, int velMax = 120)
        {
            for (int i = 0; i < kMaxStepsPerBar; i++)
                rs.p[i] = ((i % every16) == 0) ? onProb : 0.0f;
            rs.velMin = velMin; rs.velMax = velMax;
        }

        // Backbeat helper: strong hits on 2 and 4 (steps 4 and 12 at 16ths)
        static void backbeat(RowSpec& rs, float on = 1.0f, int velMin = 100, int velMax = 127)
        {
            for (int i = 0; i < kMaxStepsPerBar; i++) rs.p[i] = 0.0f;
            rs.p[4] = on;
            rs.p[12] = on;
            rs.velMin = velMin; rs.velMax = velMax;
        }

        // Probability sprinkles for groove
        static void sprinkle(RowSpec& rs, const int* steps, int n, float prob, int velMin, int velMax)
        {
            for (int i = 0; i < n; i++)
                rs.p[juce::jlimit(0, kMaxStepsPerBar - 1, steps[i])] = juce::jmax(rs.p[steps[i]], prob);
            rs.velMin = juce::jmin(rs.velMin, velMin);
            rs.velMax = juce::jmax(rs.velMax, velMax);
        }

        static inline int stepsPerBarFromTimeSig(int numerator, int denominator)
        {
            numerator = juce::jlimit(1, 32, numerator);
            denominator = juce::jlimit(1, 32, denominator);

            // We generate on a 16th-note grid (relative to a whole note).
            // stepsPerBar = 16 * (numerator/denominator)
            const double spb = 16.0 * (double)numerator / (double)denominator;
            return juce::jlimit(1, kMaxStepsPerBar, (int)std::round(spb));
        }

        static inline int clampStep(int s, int stepsPerBar)
        {
            return juce::jlimit(0, stepsPerBar - 1, s);
        }


        // ====== STYLE DEFINITIONS ==================================================

        // Trap: fast hats/rolls, backbeat snare/clap, syncopated kicks, occasional open hat on offbeats.
        static DrumStyleSpec makeTrap()
        {
            DrumStyleSpec s; s.name = "trap";
            s.swingPct = 10; s.tripletBias = 0.25f; s.dottedBias = 0.1f; s.bpmMin = 120; s.bpmMax = 160;
            s.lockBackbeat = false; // Trap uses snare on beat 3 only, not 2 & 4

            // Kick (Trap): strong 1, late pushes, tasteful bounce (avoid "EDM quarters")
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;

            // Anchors
            s.rows[Kick].p[0] = 0.92f;  // beat 1
            s.rows[Kick].p[8] = 0.35f;  // beat 3 support
            s.rows[Kick].p[12] = 0.22f;  // beat 4 support

            // Late-bar drive / trap push (these make it feel like trap instead of random)
            int kDrive[] = { 10, 11, 14, 15 };
            sprinkle(s.rows[Kick], kDrive, (int)std::size(kDrive), 0.40f, 92, 122);

            // Classic trap bounce options (moderate)
            int kBounceA[] = { 3, 5, 6, 9, 13 };
            sprinkle(s.rows[Kick], kBounceA, (int)std::size(kBounceA), 0.22f, 88, 118);

            // Occasional pickup into the bar (very light)
            int kPickup[] = { 7 };
            sprinkle(s.rows[Kick], kPickup, (int)std::size(kPickup), 0.14f, 86, 112);

            // Velocity window
            s.rows[Kick].velMin = 92;
            s.rows[Kick].velMax = 122;
            // Snare: strong backbeat
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].p[8] = 1.0f;
            s.rows[Snare].velMin = 100;
            s.rows[Snare].velMax = 127;

            // Clap: layered with snare lower prob
            backbeat(s.rows[Clap], 0.6f, 96, 115);

            // Closed hat: strong 1/8 with 1/16 & 1/32 rolls
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.85f : 0.35f);
            s.rows[ClosedHat].rollProb = 0.45f; s.rows[ClosedHat].maxRollSub = 2; // 32nds
            s.rows[ClosedHat].velMin = 75; s.rows[ClosedHat].velMax = 105;

            // Open hat (Trap): splashes around offbeats + late-bar energy, not constant noise
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            // Common trap splash spots
            s.rows[OpenHat].p[6] = 0.18f; // around beat 2-and
            s.rows[OpenHat].p[10] = 0.16f; // around beat 3-and
            s.rows[OpenHat].p[14] = 0.22f; // around beat 4-and
            s.rows[OpenHat].p[15] = 0.10f; // end-of-bar tail (rare)

            s.rows[OpenHat].lenTicks = 36;
            s.rows[OpenHat].velMin = 72;  s.rows[OpenHat].velMax = 106;

            // Perc (Trap): small fills, mostly offbeats/late-bar, low velocity
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pFill[] = { 2, 5, 9, 13, 15 };
            sprinkle(s.rows[Perc], pFill, (int)std::size(pFill), 0.18f, 60, 92);


            return s;
        }

        // Drill (UK/NY): triplet feel, choppy, snares often late (beat 4 of the bar emphasized).
        static DrumStyleSpec makeDrill()
        {
            DrumStyleSpec s; s.name = "drill";
            s.swingPct = 5; s.tripletBias = 0.55f; s.dottedBias = 0.1f; s.bpmMin = 130; s.bpmMax = 145;
            s.lockBackbeat = false; // Drill avoids clean 2 & 4 backbeat

            // Kick: choppy syncopations
// DRILL KICKS: anchored + syncopated (not 4-on-the-floor)
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Kick].p[i] = 0.0f;

            // Anchor points (bar downbeat + mid/late support)
            s.rows[Kick].p[0] = 0.90f;  // beat 1
            s.rows[Kick].p[4] = 0.18f;  // beat 2 (light)
            s.rows[Kick].p[8] = 0.45f;  // beat 3 support
            s.rows[Kick].p[12] = 0.35f;  // beat 4 support

            // Drill bounce / pickups (kept moderate so it doesn't become trash)
            int ksA[] = { 3, 7, 11, 15 };
            sprinkle(s.rows[Kick], ksA, 4, 0.30f, 92, 118);

            // Extra syncopation options (very light)
            int ksB[] = { 5, 9, 13 };
            sprinkle(s.rows[Kick], ksB, 3, 0.14f, 88, 114);

            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].p[8] = 1.0f;   // beat 3
            s.rows[Snare].p[15] = 0.18f;  // rare late snare (drill flavor)
            s.rows[Snare].velMin = 100;
            s.rows[Snare].velMax = 127;

            // Clap layered lighter
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 90; s.rows[Clap].velMax = 115;

            // Hats: triplet bias, sparse 1/8 with many micro-rolls
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.6f : 0.25f);
            s.rows[ClosedHat].rollProb = 0.6f; s.rows[ClosedHat].maxRollSub = 3; // triplet rolls
            s.rows[ClosedHat].velMin = 70; s.rows[ClosedHat].velMax = 100;

            // Open hat (Drill): gated splashes near snare + late-bar stabs, triplet-friendly feel
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            // Around the snare on 8: pre/post + late
            s.rows[OpenHat].p[7] = 0.18f;
            s.rows[OpenHat].p[11] = 0.22f;
            s.rows[OpenHat].p[13] = 0.20f;
            s.rows[OpenHat].p[15] = 0.14f;

            s.rows[OpenHat].lenTicks = 28;
            s.rows[OpenHat].velMin = 78;  s.rows[OpenHat].velMax = 112;

            // Perc (Drill): choppy little stabs, low-mid velocity
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pDrill[] = { 1, 3, 6, 9, 12, 14, 15 };
            sprinkle(s.rows[Perc], pDrill, (int)std::size(pDrill), 0.16f, 58, 90);


            return s;
        }

        // EDM (house-ish): 4-on-the-floor, claps on 2&4, steady hats on off-beats
        static DrumStyleSpec makeEDM()
        {
            DrumStyleSpec s; s.name = "edm";
            s.swingPct = 0; s.tripletBias = 0.0f; s.dottedBias = 0.05f; s.bpmMin = 120; s.bpmMax = 128;

            // Kick (EDM): 4-on-the-floor, with very light optional pre-kick energy
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;

            // Four on the floor anchors
            s.rows[Kick].p[0] = 0.98f;
            s.rows[Kick].p[4] = 0.96f;
            s.rows[Kick].p[8] = 0.96f;
            s.rows[Kick].p[12] = 0.96f;

            // Optional tiny pickups (rare; keeps it from being dead)
            int kLift[] = { 15 };
            sprinkle(s.rows[Kick], kLift, (int)std::size(kLift), 0.10f, 90, 110);

            s.rows[Kick].velMin = 105;
            s.rows[Kick].velMax = 122;

            backbeat(s.rows[Snare], 0.9f, 100, 118);
            backbeat(s.rows[Clap], 0.9f, 96, 115);

            // ClosedHat (EDM): consistent 16ths with controlled gaps + energy accents
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[ClosedHat].p[i] = 0.0f;

            // 16ths but not always-on
            for (int i = 0; i < kMaxStepsPerBar; ++i)
                s.rows[ClosedHat].p[i] = ((i % 2) == 0) ? 0.86f : 0.62f;

            // Energy accents (common in EDM hats)
            s.rows[ClosedHat].p[14] = juce::jmax(s.rows[ClosedHat].p[14], 0.78f);
            s.rows[ClosedHat].p[15] = juce::jmax(s.rows[ClosedHat].p[15], 0.70f);

            // Tiny chance to drop a couple 16ths so it breathes
            int hhDrop[] = { 3, 11 };
            sprinkle(s.rows[ClosedHat], hhDrop, (int)std::size(hhDrop), 0.08f, 40, 60);

            s.rows[ClosedHat].lenTicks = 18;
            s.rows[ClosedHat].velMin = 60;
            s.rows[ClosedHat].velMax = 102;


            // Open hat (EDM): classic offbeat opens (2& and 4&)
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            s.rows[OpenHat].p[6] = 0.32f; // 2&
            s.rows[OpenHat].p[14] = 0.32f; // 4&
            s.rows[OpenHat].p[10] = 0.10f; // occasional 3& accent

            s.rows[OpenHat].lenTicks = 36;
            s.rows[OpenHat].velMin = 80;  s.rows[OpenHat].velMax = 110;

            // Perc (EDM): very light spice (rare)
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pEdm[] = { 10, 15 };
            sprinkle(s.rows[Perc], pEdm, (int)std::size(pEdm), 0.10f, 60, 90);
            return s;
        }

        // Reggaeton (dembow): boom-ch-boom-chick pattern (3+3+2 feel)
        static DrumStyleSpec makeReggaeton()
        {
            DrumStyleSpec s; s.name = "reggaeton";
            s.swingPct = 0; s.tripletBias = 0.15f; s.dottedBias = 0.1f; s.bpmMin = 85; s.bpmMax = 105;
            s.lockBackbeat = false; // Reggaeton uses dembow pattern, not backbeat

            // Kick (Reggaeton): dembow backbone with tasteful reinforcement
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;

            // Backbone
            s.rows[Kick].p[0] = 0.97f; // beat 1
            s.rows[Kick].p[7] = 0.88f; // late beat 2 / "a" of 2 feel
            s.rows[Kick].p[8] = 0.35f; // beat 3 reinforcement (light)
            s.rows[Kick].p[14] = 0.22f; // late beat 4 push (light)

            // Optional small variations (very controlled)
            int kVar[] = { 6, 15 };
            sprinkle(s.rows[Kick], kVar, (int)std::size(kVar), 0.14f, 90, 112);

            s.rows[Kick].velMin = 96;
            s.rows[Kick].velMax = 120;

            // Snare/Clap: dembow accent on "2-and" (step 6)
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].p[6] = 1.0f;   // "2-and"
            s.rows[Snare].velMin = 98;
            s.rows[Snare].velMax = 120;

            // Clap layered lighter
            s.rows[Clap] = s.rows[Snare];
            s.rows[Clap].velMin = 90;
            s.rows[Clap].velMax = 112;

            // ClosedHat (Reggaeton): dembow-friendly 8ths + light offbeat accents
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[ClosedHat].p[i] = 0.0f;

            // Base 8ths (not too loud)
            for (int i = 0; i < kMaxStepsPerBar; i += 2)
                s.rows[ClosedHat].p[i] = 0.78f;

            // Accent around late beat 2 / late beat 4
            s.rows[ClosedHat].p[7] = 0.22f;
            s.rows[ClosedHat].p[15] = 0.18f;

            // Rare extra taps
            int hhTap[] = { 5, 13 };
            sprinkle(s.rows[ClosedHat], hhTap, (int)std::size(hhTap), 0.10f, 54, 78);

            s.rows[ClosedHat].lenTicks = 22;
            s.rows[ClosedHat].velMin = 58;
            s.rows[ClosedHat].velMax = 92;


            // Open hat (Reggaeton): end-of-bar + light push points
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            s.rows[OpenHat].p[15] = 0.30f; // end-of-bar
            s.rows[OpenHat].p[7] = 0.08f; // supports dembow feel
            s.rows[OpenHat].p[11] = 0.06f; // light variation

            s.rows[OpenHat].lenTicks = 34;
            s.rows[OpenHat].velMin = 72;  s.rows[OpenHat].velMax = 105;

            // Perc (Reggaeton): low velocity percussion supports dembow groove
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pReg[] = { 2, 4, 10, 12, 14 };
            sprinkle(s.rows[Perc], pReg, (int)std::size(pReg), 0.16f, 58, 88);


            return s;
        }

        // R&B (modern): laid-back swing, gentle ghost notes
        static DrumStyleSpec makeRNB()
        {
            DrumStyleSpec s; s.name = "r&b";
            s.swingPct = 18; s.tripletBias = 0.2f; s.dottedBias = 0.15f; s.bpmMin = 70; s.bpmMax = 95;


            backbeat(s.rows[Snare], 0.95f, 98, 118);
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 85; s.rows[Clap].velMax = 108;

            // Kick (RNB): laid-back pocket, not busy, strong downbeat
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;

            s.rows[Kick].p[0] = 0.92f; // beat 1
            s.rows[Kick].p[8] = 0.38f; // beat 3 support

            // pocket choices
            int kPocket[] = { 6, 10, 14 };
            sprinkle(s.rows[Kick], kPocket, (int)std::size(kPocket), 0.16f, 84, 110);

            // very light anticipations
            int kAnt[] = { 7, 15 };
            sprinkle(s.rows[Kick], kAnt, (int)std::size(kAnt), 0.10f, 82, 108);

            s.rows[Kick].velMin = 92;
            s.rows[Kick].velMax = 118;

            // Hats: swung 1/8 with ghost 1/16
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.7f : 0.25f);
            s.rows[ClosedHat].velMin = 70; s.rows[ClosedHat].velMax = 96;
            s.rows[ClosedHat].rollProb = 0.2f; s.rows[ClosedHat].maxRollSub = 2;

            // Open hat (R&B): gentle, laid-back, not busy
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            s.rows[OpenHat].p[2] = 0.16f;
            s.rows[OpenHat].p[10] = 0.18f;
            s.rows[OpenHat].p[14] = 0.10f;

            s.rows[OpenHat].lenTicks = 30;
            s.rows[OpenHat].velMin = 68;  s.rows[OpenHat].velMax = 98;
            
            // Perc (R&B): soft ghosts, pocket fillers
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pRnb[] = { 2, 6, 10, 14 };
            sprinkle(s.rows[Perc], pRnb, (int)std::size(pRnb), 0.18f, 52, 82);

            return s;
        }

        // Pop: clean backbeat, on-grid hats, tasteful fills
        static DrumStyleSpec makePop()
        {
            DrumStyleSpec s; s.name = "pop";
            s.swingPct = 5; s.tripletBias = 0.05f; s.dottedBias = 0.05f; s.bpmMin = 90; s.bpmMax = 120;

            backbeat(s.rows[Snare], 0.95f, 98, 118);
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 90; s.rows[Clap].velMax = 112;
            // Kick (Pop): strong 1 & 3, supportive 2/4 options, not too busy
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;

            // Common pop anchors
            s.rows[Kick].p[0] = 0.95f; // beat 1
            s.rows[Kick].p[8] = 0.78f; // beat 3

            // Light support on 2/4 depending on groove
            s.rows[Kick].p[4] = 0.22f;
            s.rows[Kick].p[12] = 0.18f;

            // Small anticipations (very light)
            int kAnt[] = { 3, 7, 11, 15 };
            sprinkle(s.rows[Kick], kAnt, (int)std::size(kAnt), 0.10f, 88, 110);

            s.rows[Kick].velMin = 98;
            s.rows[Kick].velMax = 120;

            // ClosedHat (Pop): clean 8ths + tasteful 16th sparkle
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[ClosedHat].p[i] = 0.0f;

            // Strong 8ths
            for (int i = 0; i < kMaxStepsPerBar; i += 2)
                s.rows[ClosedHat].p[i] = 0.88f;

            // Light 16ths for sparkle
            int hhSpark[] = { 1, 5, 9, 13 };
            sprinkle(s.rows[ClosedHat], hhSpark, (int)std::size(hhSpark), 0.14f, 58, 86);

            // Rare end-of-bar tick
            int hhEnd[] = { 15 };
            sprinkle(s.rows[ClosedHat], hhEnd, (int)std::size(hhEnd), 0.10f, 56, 82);

            s.rows[ClosedHat].lenTicks = 22;
            s.rows[ClosedHat].velMin = 62;
            s.rows[ClosedHat].velMax = 98;

            // Open hat (Pop): clean splashes, occasional energy
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            s.rows[OpenHat].p[2] = 0.22f;
            s.rows[OpenHat].p[10] = 0.22f;
            s.rows[OpenHat].p[6] = 0.08f;
            s.rows[OpenHat].p[14] = 0.10f;

            s.rows[OpenHat].lenTicks = 32;
            s.rows[OpenHat].velMin = 74;  s.rows[OpenHat].velMax = 108;
            // Perc (Pop): very tasteful end-of-bar fill support
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pPop[] = { 14, 15, 6 };
            sprinkle(s.rows[Perc], pPop, (int)std::size(pPop), 0.10f, 60, 90);

            return s;
        }

        // Rock: strong 2 & 4 backbeat, hats straight 8ths, occasional open hat on &4
        static DrumStyleSpec makeRock()
        {
            DrumStyleSpec s; s.name = "rock";
            s.swingPct = 0; s.tripletBias = 0.0f; s.dottedBias = 0.0f; s.bpmMin = 90; s.bpmMax = 140;

            backbeat(s.rows[Snare], 1.0f, 100, 124);
            // Kick (Rock): solid 1 & 3, some 8th-note drive, minimal randomness
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;

            // Strong anchors
            s.rows[Kick].p[0] = 0.96f;
            s.rows[Kick].p[8] = 0.82f;

            // Rock drive (occasional 8th feel)
            s.rows[Kick].p[2] = 0.18f;
            s.rows[Kick].p[6] = 0.14f;
            s.rows[Kick].p[10] = 0.14f;
            s.rows[Kick].p[14] = 0.18f;

            // Very rare extra push
            int kRare[] = { 12 };
            sprinkle(s.rows[Kick], kRare, (int)std::size(kRare), 0.08f, 90, 110);

            s.rows[Kick].velMin = 98;
            s.rows[Kick].velMax = 120;

            // ClosedHat (Rock): steady 8ths with light 16th drive
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[ClosedHat].p[i] = 0.0f;

            // Core 8ths (rock needs consistency)
            for (int i = 0; i < kMaxStepsPerBar; i += 2)
                s.rows[ClosedHat].p[i] = 0.92f;

            // Occasional 16th drive (rare, to avoid turning into EDM)
            int hhDrive[] = { 3, 7, 11, 15 };
            sprinkle(s.rows[ClosedHat], hhDrive, (int)std::size(hhDrive), 0.12f, 60, 88);

            s.rows[ClosedHat].lenTicks = 24;
            s.rows[ClosedHat].velMin = 66;
            s.rows[ClosedHat].velMax = 104;

            // Open hat (Rock): occasional opens, usually on the & of 4 / end-of-bar
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            s.rows[OpenHat].p[7] = 0.22f;
            s.rows[OpenHat].p[15] = 0.30f;
            s.rows[OpenHat].p[11] = 0.10f; // occasional energy

            s.rows[OpenHat].lenTicks = 40;
            s.rows[OpenHat].velMin = 78;  s.rows[OpenHat].velMax = 112;
            // Perc (Rock): extremely rare (keeps rock from becoming EDM-ish)
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pRock[] = { 15 };
            sprinkle(s.rows[Perc], pRock, (int)std::size(pRock), 0.06f, 60, 88);

            return s;
        }

        // Wxstie (modern West Coast bounce): sparser hats, swingy pocket, syncopated kicks, claps/snare layered
        static DrumStyleSpec makeWxstie()
        {
            DrumStyleSpec s; s.name = "wxstie";
            s.swingPct = 18; s.tripletBias = 0.10f; s.dottedBias = 0.10f; s.bpmMin = 90; s.bpmMax = 120;

            s.lockBackbeat = false;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;
            int kCore[] = { 0, 7, 11, 14 };
            sprinkle(s.rows[Kick], kCore, (int)std::size(kCore), 0.45f, 88, 118);
            int kExtra[] = { 3, 9 };
            sprinkle(s.rows[Kick], kExtra, (int)std::size(kExtra), 0.22f, 85, 115);
            s.rows[Kick].velMin = 90; s.rows[Kick].velMax = 125;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].velMin = 95; s.rows[Snare].velMax = 127;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Clap].p[i] = 0.0f;
            s.rows[Clap].p[4] = 0.15f;
            s.rows[Clap].p[12] = 0.15f;
            s.rows[Clap].velMin = 85; s.rows[Clap].velMax = 112;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[ClosedHat].p[i] = 0.0f;

            // Make wxstie hats tighter: mostly 8ths, with occasional 16th "fills"
            for (int i = 0; i < 16; ++i)
            {
                // 16-step bar assumed for 4/4 feel; generator will only read stepsPerBar anyway
                if ((i % 2) == 0)
                    s.rows[ClosedHat].p[i] = 0.64f;  // more gaps on the 8ths
                else
                    s.rows[ClosedHat].p[i] = 0.12f;  // more gaps on the 16ths// occasional 16ths between (keeps it lively)
            }

            // Extra little late-bar energy sometimes (still wxstie-ish)
            s.rows[ClosedHat].p[14] = juce::jmax(s.rows[ClosedHat].p[14], 0.28f);
            s.rows[ClosedHat].p[15] = juce::jmax(s.rows[ClosedHat].p[15], 0.22f);

            s.rows[ClosedHat].velMin = 68;
            s.rows[ClosedHat].velMax = 100;

            // More roll chance
            s.rows[ClosedHat].rollProb = 0.42f;

            // Allow faster roll grids (2=32nds, 3=triplet-ish roll rate)
            s.rows[ClosedHat].maxRollSub = 3;


            // Open hat (Wxstie): sparse, bouncy, mostly “moments” not constant
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            s.rows[OpenHat].p[2] = 0.10f;
            s.rows[OpenHat].p[6] = 0.06f; // rare
            s.rows[OpenHat].p[10] = 0.14f; // slightly favored
            s.rows[OpenHat].p[14] = 0.06f; // rare
            s.rows[OpenHat].p[15] = 0.08f;

            s.rows[OpenHat].lenTicks = 34;
            s.rows[OpenHat].velMin = 68;  s.rows[OpenHat].velMax = 104;

            // Perc (Wxstie): bouncy stabs that support the pocket
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;

            int pWx[] = { 1, 3, 6, 9, 11, 13, 14 };
            sprinkle(s.rows[Perc], pWx, (int)std::size(pWx), 0.20f, 58, 92);


            return s;
        }

        // Hiphop (general, non-trap): simpler hats, steady backbeat, less rolls
        static DrumStyleSpec makeHipHop()
        {
            DrumStyleSpec s; s.name = "hip hop";
            s.swingPct = 8; s.tripletBias = 0.05f; s.dottedBias = 0.05f; s.bpmMin = 85; s.bpmMax = 100;

            backbeat(s.rows[Snare], 0.95f, 98, 118);
            // Kick (Hip-Hop): solid pocket, not busy, avoids random trash
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Kick].p[i] = 0.0f;

            // Anchors
            s.rows[Kick].p[0] = 0.96f; // beat 1
            s.rows[Kick].p[8] = 0.44f; // beat 3 support

            // Pocket placements (classic hip-hop)
            int kPocket[] = { 6, 10, 14 };
            sprinkle(s.rows[Kick], kPocket, (int)std::size(kPocket), 0.18f, 86, 114);

            // Rare pickups
            int kPick[] = { 7, 15 };
            sprinkle(s.rows[Kick], kPick, (int)std::size(kPick), 0.10f, 82, 108);

            s.rows[Kick].velMin = 92;
            s.rows[Kick].velMax = 122;

            // ClosedHat (Hip-Hop): pocket 8ths/16ths with intentional gaps
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[ClosedHat].p[i] = 0.0f;

            // Base 8ths (solid but not robotic)
            s.rows[ClosedHat].p[0] = 0.82f;
            s.rows[ClosedHat].p[2] = 0.60f;
            s.rows[ClosedHat].p[4] = 0.78f;
            s.rows[ClosedHat].p[6] = 0.56f;
            s.rows[ClosedHat].p[8] = 0.82f;
            s.rows[ClosedHat].p[10] = 0.62f;
            s.rows[ClosedHat].p[12] = 0.78f;
            s.rows[ClosedHat].p[14] = 0.58f;

            // Light 16th ghosting (adds life, still gappy)
            int hhGhost[] = { 1, 3, 7, 9, 11, 13, 15 };
            sprinkle(s.rows[ClosedHat], hhGhost, (int)std::size(hhGhost), 0.10f, 52, 76);

            s.rows[ClosedHat].lenTicks = 22;
            s.rows[ClosedHat].velMin = 58;
            s.rows[ClosedHat].velMax = 92;

            // Open hat (Hip Hop): rare, pocket-friendly
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;

            s.rows[OpenHat].p[10] = 0.18f;
            s.rows[OpenHat].p[2] = 0.10f;
            s.rows[OpenHat].p[14] = 0.08f;

            s.rows[OpenHat].lenTicks = 30;
            s.rows[OpenHat].velMin = 70;  s.rows[OpenHat].velMax = 104;
            
            // Perc (Hip Hop): very light fills, mostly late-bar
            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pHip[] = { 6, 14, 15 };
            sprinkle(s.rows[Perc], pHip, (int)std::size(pHip), 0.10f, 58, 88);

            return s;
        }

        const juce::StringArray styleNames()
        {
            return { "trap","drill","edm","reggaeton","r&b","pop","rock","wxstie","hip hop" };
        }


        DrumStyleSpec getSpec(const juce::String& styleName)
        {
            const auto name = styleName.trim().toLowerCase();
            if (name == "trap")      return makeTrap();
            if (name == "drill")     return makeDrill();
            if (name == "edm")       return makeEDM();
            if (name == "reggaeton") return makeReggaeton();
            if (name == "r&b")       return makeRNB();
            if (name == "pop")       return makePop();
            if (name == "rock")      return makeRock();
            if (name == "wxstie")    return makeWxstie();
            if (name == "hip hop")   return makeHipHop();
            // default
            return makeHipHop();
        }
        // ============================================================================
        // Preferred snare templates (NOT mandatory)
        // Uses the EXACT time signature string (including additive like "3+2/8").
        // We boost snare/clap probabilities at time-signature-aware positions.
        // ============================================================================



        // Parse "7/8", "3+2/8", "2+2+3/8" into:
        // - total numerator (sum of parts)
        // - denominator
        // - groups (additive parts) if present, else a meter-aware split heuristic
        static bool parseTimeSigText(const juce::String& tsText, int& outNum, int& outDen, juce::Array<int>& outGroups)
        {
            outGroups.clearQuick();
            outNum = 4; outDen = 4;

            auto s = tsText.trim();
            if (s.isEmpty()) return false;

            auto parts = juce::StringArray::fromTokens(s, "/", "");
            if (parts.size() != 2) return false;

            auto numPart = parts[0].trim();
            auto denPart = parts[1].trim();
            const int den = denPart.getIntValue();
            if (den <= 0) return false;

            // additive numerator?
            if (numPart.containsChar('+'))
            {
                auto nums = juce::StringArray::fromTokens(numPart, "+", "");
                int sum = 0;
                for (auto n : nums)
                {
                    int v = n.trim().getIntValue();
                    if (v <= 0) continue;
                    outGroups.add(v);
                    sum += v;
                }
                if (sum <= 0) return false;
                outNum = sum;
                outDen = den;
                return true;
            }

            // plain numerator
            const int num = numPart.getIntValue();
            if (num <= 0) return false;

            outNum = num;
            outDen = den;
            return true;
        }

        // If not additive, split large numerators into musical groups (your "2+3", "3+4", etc. idea)
        static juce::Array<int> splitBeatsIntoGroupsHeuristic(int tsNum)
        {
            juce::Array<int> g;
            tsNum = juce::jlimit(1, 64, tsNum);

            if (tsNum <= 4) { g.add(tsNum); return g; }

            // common feels
            if (tsNum == 5) { g.add(2); g.add(3); return g; }
            if (tsNum == 7) { g.add(3); g.add(4); return g; }
            if (tsNum == 9) { g.add(4); g.add(5); return g; }
            if (tsNum == 11) { g.add(5); g.add(6); return g; }
            if (tsNum == 13) { g.add(5); g.add(4); g.add(4); return g; }

            // peel off 4s
            int rem = tsNum;
            while (rem > 0)
            {
                if (rem == 5) { g.add(2); g.add(3); break; }
                if (rem == 7) { g.add(3); g.add(4); break; }
                if (rem <= 4) { g.add(rem); break; }
                g.add(4);
                rem -= 4;
            }
            return g;
        }

        // Convert a 1-based "beat index" (in numerator units) to step16.
        // Example 6/8 beat 4 => (4-1)/6 = 0.5 bar => step 8.
        static inline int clampStepToBar(int s, int stepsPerBar)
        {
            return juce::jlimit(0, juce::jmax(1, stepsPerBar) - 1, s);
        }

        // Convert a 1-based "beat index" (in numerator units) to a step index
        // IN THE CURRENT BAR'S step domain (stepsPerBar), not kMaxStepsPerBar (64).
        static inline int beatIndex1BasedToStepInBar(int beatIndex1Based, int tsNum, int stepsPerBar)
        {
            tsNum = juce::jmax(1, tsNum);
            stepsPerBar = juce::jmax(1, stepsPerBar);

            const float beatPos0Based = (float)juce::jlimit(1, tsNum, beatIndex1Based) - 1.0f;
            const float t = beatPos0Based / (float)tsNum; // [0..1)
            const int step = (int)juce::roundToInt(t * (float)stepsPerBar);
            return clampStepToBar(step, stepsPerBar);
        }

        static inline void addUniqueStepInBar(juce::Array<int>& a, int s, int stepsPerBar)
        {
            s = clampStepToBar(s, stepsPerBar);
            if (!a.contains(s)) a.add(s);
        }


        // Slight mutation so it "thinks" and doesn't repeat templates forever
        static void maybeMutatePreferredSteps(juce::Array<int>& steps, juce::Random& rng, int stepsPerBar)
        {
            if (steps.size() < 2) return;
            if (rng.nextFloat() > 0.22f) return; // ~1 in 5

            stepsPerBar = juce::jmax(1, stepsPerBar);

            const int idx = rng.nextInt(steps.size());
            const int dir = rng.nextBool() ? 1 : -1;
            int s = clampStepToBar(steps[idx] + dir, stepsPerBar);

            if (!steps.contains(s))
                steps.set(idx, s);
        }

        // Build template pool for the EXACT time signature (shared across styles)
        // using your notes when available; fallback heuristic if not.
        static void buildBaseTemplatesForTimeSig(const juce::String& timeSigText,
            int tsNum, int tsDen,
            const juce::Array<int>& groups,
            int stepsPerBar,
            juce::Array<juce::Array<int>>& outTemplates)
        {
            juce::ignoreUnused(tsDen);
            outTemplates.clearQuick();

            auto ts = timeSigText.trim().toLowerCase();

            auto makeFromBeats = [&](std::initializer_list<int> beats1Based)
                {
                    juce::Array<int> t;
                    for (int b : beats1Based)
                        addUniqueStepInBar(t, beatIndex1BasedToStepInBar(b, tsNum, stepsPerBar), stepsPerBar);

                    if (t.size() >= 2)
                        outTemplates.add(t);
                };



            // ----------------------
            // YOUR NOTES (exact)
            // ----------------------
            if (ts == "3/4")
            {
                makeFromBeats({ 2, 3 });
                makeFromBeats({ 3, 2 });
                makeFromBeats({ 3, 3 }); // doubles become "strong 3"
                makeFromBeats({ 2, 2 }); // strong 2 feel
                makeFromBeats({ 3, 1 }); // late + bar start (rare but usable)
                return;
            }

            if (ts == "6/8")
            {
                makeFromBeats({ 4, 6 });
                makeFromBeats({ 4, 5 });
                makeFromBeats({ 4, 3 });
                makeFromBeats({ 4, 2 });
                makeFromBeats({ 2, 4 });
                return;
            }

            if (ts == "7/8")
            {
                makeFromBeats({ 3, 7 });
                makeFromBeats({ 5, 7 });
                makeFromBeats({ 3, 5 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 2, 7 });
                return;
            }

            if (ts == "5/4")
            {
                makeFromBeats({ 3, 5 });
                makeFromBeats({ 2, 5 });
                makeFromBeats({ 3, 4 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 5, 3 });
                return;
            }

            if (ts == "9/8")
            {
                makeFromBeats({ 3, 7 });
                makeFromBeats({ 4, 7 });
                makeFromBeats({ 4, 9 });
                makeFromBeats({ 5, 9 });
                makeFromBeats({ 3, 6 }); // waltz-ish
                return;
            }

            if (ts == "12/8")
            {
                makeFromBeats({ 2, 8 });
                makeFromBeats({ 5, 10 });
                makeFromBeats({ 2, 5 });
                makeFromBeats({ 8, 10 });
                makeFromBeats({ 2, 10 });
                return;
            }

            if (ts == "5/8")
            {
                makeFromBeats({ 3, 5 });
                makeFromBeats({ 4, 5 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 4, 4 });
                makeFromBeats({ 5, 3 });
                return;
            }

            if (ts == "10/8")
            {
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 2, 5 });
                makeFromBeats({ 4, 8 });
                makeFromBeats({ 2, 8 });
                makeFromBeats({ 5, 10 });
                return;
            }

            if (ts == "11/8")
            {
                makeFromBeats({ 4, 7 });
                makeFromBeats({ 1, 7 });
                makeFromBeats({ 7, 10 });
                makeFromBeats({ 5, 11 });
                makeFromBeats({ 2, 6 });
                return;
            }

            if (ts == "13/8")
            {
                makeFromBeats({ 6, 12 });
                makeFromBeats({ 6, 13 });
                makeFromBeats({ 7, 12 });
                makeFromBeats({ 7, 13 });
                makeFromBeats({ 4, 10 });
                return;
            }

            if (ts == "15/8")
            {
                makeFromBeats({ 6, 15 });
                makeFromBeats({ 7, 15 });
                makeFromBeats({ 8, 15 });
                makeFromBeats({ 7, 14 });
                makeFromBeats({ 8, 14 });
                return;
            }

            if (ts == "17/8")
            {
                makeFromBeats({ 9, 17 });
                makeFromBeats({ 8, 17 });
                makeFromBeats({ 5, 13 });
                makeFromBeats({ 1, 9 });
                makeFromBeats({ 13, 17 });
                return;
            }

            if (ts == "2/4")
            {
                makeFromBeats({ 2, 2 });
                makeFromBeats({ 1, 2 });
                makeFromBeats({ 2, 1 });
                makeFromBeats({ 2, 2 }); // strong backbeat emphasis
                makeFromBeats({ 1, 1 }); // rare double on start
                return;
            }

            if (ts == "7/4")
            {
                makeFromBeats({ 3, 7 });
                makeFromBeats({ 5, 7 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 4, 7 });
                makeFromBeats({ 2, 7 });
                return;
            }

            if (ts == "9/4")
            {
                makeFromBeats({ 3, 9 });
                makeFromBeats({ 5, 9 });
                makeFromBeats({ 2, 6 });
                makeFromBeats({ 4, 8 });
                makeFromBeats({ 6, 9 });
                return;
            }

            if (ts == "19/8")
            {
                makeFromBeats({ 7, 19 });
                makeFromBeats({ 9, 19 });
                makeFromBeats({ 11, 19 });
                makeFromBeats({ 5, 13 });
                makeFromBeats({ 13, 19 });
                return;
            }

            if (ts == "21/8")
            {
                makeFromBeats({ 7, 21 });
                makeFromBeats({ 9, 21 });
                makeFromBeats({ 11, 21 });
                makeFromBeats({ 5, 13 });
                makeFromBeats({ 15, 21 });
                return;
            }

            if (ts == "5/16")
            {
                makeFromBeats({ 3, 5 });
                makeFromBeats({ 2, 5 });
                makeFromBeats({ 4, 5 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 5, 5 });
                return;
            }

            if (ts == "7/16")
            {
                makeFromBeats({ 3, 7 });
                makeFromBeats({ 5, 7 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 4, 7 });
                makeFromBeats({ 2, 7 });
                return;
            }

            if (ts == "9/16")
            {
                makeFromBeats({ 3, 9 });
                makeFromBeats({ 5, 9 });
                makeFromBeats({ 2, 6 });
                makeFromBeats({ 4, 8 });
                makeFromBeats({ 6, 9 });
                return;
            }

            if (ts == "11/16")
            {
                makeFromBeats({ 4, 11 });
                makeFromBeats({ 1, 7 });
                makeFromBeats({ 7, 10 });
                makeFromBeats({ 5, 11 });
                makeFromBeats({ 2, 6 });
                return;
            }

            if (ts == "13/16")
            {
                makeFromBeats({ 6, 12 });
                makeFromBeats({ 6, 13 });
                makeFromBeats({ 7, 12 });
                makeFromBeats({ 7, 13 });
                makeFromBeats({ 4, 10 });
                return;
            }

            if (ts == "15/16")
            {
                makeFromBeats({ 6, 15 });
                makeFromBeats({ 7, 15 });
                makeFromBeats({ 8, 15 });
                makeFromBeats({ 7, 14 });
                makeFromBeats({ 8, 14 });
                return;
            }

            if (ts == "17/16")
            {
                makeFromBeats({ 9, 17 });
                makeFromBeats({ 8, 17 });
                makeFromBeats({ 5, 13 });
                makeFromBeats({ 1, 9 });
                makeFromBeats({ 13, 17 });
                return;
            }

            if (ts == "19/16")
            {
                makeFromBeats({ 7, 19 });
                makeFromBeats({ 9, 19 });
                makeFromBeats({ 11, 19 });
                makeFromBeats({ 5, 13 });
                makeFromBeats({ 13, 19 });
                return;
            }

            // ----------------------
// ADDITIVE METERS (e.g. "3+3+2/8", "2+2+3/8", "3+2+2+3/16")
// We build MULTIPLE snare templates aligned to group boundaries.
// Uses existing makeFromBeats() so we don't change any other logic.
// ----------------------
            if (ts.containsChar('+') && groups.size() > 0)
            {
                // groups sum to tsNum in your parser; each "beat" here is one denominator unit.
                // Build group start beats (1-based) and end beats (1-based).
                juce::Array<int> gStarts;
                juce::Array<int> gEnds;

                int acc = 0;
                for (int i = 0; i < groups.size(); ++i)
                {
                    gStarts.add(acc + 1);          // start beat of group (1-based)
                    acc += groups[i];
                    gEnds.add(acc);                // end beat of group (1-based)
                }

                const int lastEnd = gEnds.getLast();
                const int secondLastEnd = (gEnds.size() >= 2) ? gEnds[gEnds.size() - 2] : juce::jmax(1, lastEnd - 1);

                // A) Very stable: land on ends of early groups + bar end
                if (gEnds.size() >= 2)
                {
                    makeFromBeats({ gEnds[0], gEnds[1] });
                    makeFromBeats({ gEnds[0], lastEnd });
                    makeFromBeats({ gEnds[1], lastEnd });
                }

                // B) Call/response: later start + bar end
                if (gStarts.size() >= 2)
                {
                    makeFromBeats({ gStarts[1], lastEnd });
                }
                if (gStarts.size() >= 3)
                {
                    makeFromBeats({ gStarts[2], lastEnd });
                }

                // C) “Driving” end pocket: last-1 + last
                makeFromBeats({ juce::jmax(1, lastEnd - 1), lastEnd });

                // D) “Turnaround”: second-last end + last end
                makeFromBeats({ secondLastEnd, lastEnd });

                // E) If 3+ groups: cascade ends (feels very additive-correct)
                if (gEnds.size() >= 3)
                {
                    makeFromBeats({ gEnds[0], gEnds[2] });
                    makeFromBeats({ gEnds[0], gEnds[1] }); // extra option
                    makeFromBeats({ gEnds[1], gEnds[2] }); // extra option
                }

                // If we successfully created templates, we're done.
                if (outTemplates.size() > 0)
                    return;
                // else fall through to your existing fallback logic below.
            }


            // ----------------------
            // FALLBACK (for everything else on your list)
            // Use groups (additive if present), else heuristic split.
            // We will place "one in each chunk" + a late anchor, making 10 templates total.
            // ----------------------
            juce::Array<int> g = groups;
            if (g.size() == 0)
                g = splitBeatsIntoGroupsHeuristic(tsNum);

            // cumulative ends: e.g. 3+2 => ends at 3,5
            juce::Array<int> ends;
            int acc = 0;
            for (int i = 0; i < g.size(); ++i) { acc += g[i]; ends.add(acc); }

            auto groupEndBeat = [&](int i) { return juce::jlimit(1, tsNum, ends[i]); };
            auto groupMidBeat = [&](int i)
                {
                    int start = (i == 0 ? 1 : ends[i - 1] + 1);
                    int end = ends[i];
                    int mid = (start + end) / 2;
                    return juce::jlimit(1, tsNum, mid);
                };

            // 4) mid first + mid last
            makeFromBeats({ groupMidBeat(0), groupMidBeat(ends.size() - 1) });

            // 5) if 2+ groups: end of group1 + end of group2
            if (ends.size() >= 3) makeFromBeats({ groupEndBeat(0), groupEndBeat(2) });

            // 7) late pocket: last end-1 + last end
            makeFromBeats({ juce::jmax(1, groupEndBeat(ends.size() - 1) - 1), groupEndBeat(ends.size() - 1) });
            // 8) late pocket: last mid + last end
            makeFromBeats({ groupMidBeat(ends.size() - 1), groupEndBeat(ends.size() - 1) });

            // 9) "driving": two hits in last group
            {
                int lastStart = (ends.size() >= 2 ? ends[ends.size() - 2] + 1 : 1);
                int lastEnd = ends[ends.size() - 1];
                int a = juce::jlimit(1, tsNum, lastStart);
                int b = juce::jlimit(1, tsNum, lastEnd);
                makeFromBeats({ a, b });
            }

            // 10) "alt": end group1 + (last end-2)
            makeFromBeats({ groupEndBeat(0), juce::jmax(1, groupEndBeat(ends.size() - 1) - 2) });
        }

        static int pickWeighted(juce::Random& rng, const juce::Array<int>& weights)
        {
            int total = 0;
            for (int w : weights) total += juce::jmax(0, w);
            if (total <= 0) return 0;

            int r = rng.nextInt(total);
            for (int i = 0; i < weights.size(); ++i)
            {
                r -= juce::jmax(0, weights[i]);
                if (r < 0) return i;
            }
            return 0;
        }

        static int weightedPickIndex(const juce::Array<int>& weights, juce::Random& rng)
        {
            int total = 0;
            for (int i = 0; i < weights.size(); ++i)
                total += juce::jmax(0, weights[i]);

            if (total <= 0)
                return 0;

            int r = rng.nextInt(total);
            for (int i = 0; i < weights.size(); ++i)
            {
                const int w = juce::jmax(0, weights[i]);
                if (r < w) return i;
                r -= w;
            }
            return juce::jlimit(0, weights.size() - 1, 0);
        }

        // ============================================================
// Additive time signature support (e.g. "3+3+2/8", "2+2+3/8")
// ============================================================

        struct AdditiveSig
        {
            bool valid = false;
            int denominator = 4;
            juce::Array<int> groups;
            int unitsPerBar = 0;
        };

        static AdditiveSig parseAdditiveSig(const juce::String& ts)
        {
            AdditiveSig a;
            if (!ts.containsChar('+') || !ts.containsChar('/'))
                return a;

            auto parts = juce::StringArray::fromTokens(ts, "/", "");
            if (parts.size() != 2)
                return a;

            auto groupParts = juce::StringArray::fromTokens(parts[0], "+", "");
            if (groupParts.size() < 2)
                return a;

            juce::Array<int> groups;
            int sum = 0;

            for (auto g : groupParts)
            {
                const int v = g.getIntValue();
                if (v <= 0) return a;
                groups.add(v);
                sum += v;
            }

            const int denom = parts[1].getIntValue();
            if (denom <= 0) return a;

            a.valid = true;
            a.denominator = denom;
            a.groups = groups;
            a.unitsPerBar = sum;
            return a;
        }

        static juce::Array<int> additiveGroupStarts(const juce::Array<int>& groups)
        {
            juce::Array<int> out;
            int acc = 0;
            for (int g : groups)
            {
                out.add(acc);
                acc += g;
            }
            return out;
        }

        static juce::Array<int> additiveGroupEnds(const juce::Array<int>& groups)
        {
            juce::Array<int> out;
            int acc = 0;
            for (int g : groups)
            {
                out.add(acc + g - 1);
                acc += g;
            }
            return out;
        }


        static void applyPreferredSnareBoostsByTimeSigText(DrumStyleSpec& s,
            const juce::String& timeSigText,
            int seed)
        {



            int tsNum = 4, tsDen = 4;
            juce::Array<int> groups;

            if (groups.size() == 0 && timeSigText.containsChar('+'))
                parseTimeSigText(timeSigText, tsNum, tsDen, groups);

            if (groups.size() == 0 && !timeSigText.containsChar('+'))
                groups = splitBeatsIntoGroupsHeuristic(tsNum);

            parseTimeSigText(timeSigText, tsNum, tsDen, groups);
            const int stepsPerBar = stepsPerBarFromTimeSig(tsNum, tsDen);

            juce::Random rng(seed == -1 ? (int)juce::Time::getMillisecondCounter() : seed);

            const auto name = s.name.trim().toLowerCase();
            const bool is44 = (tsNum == 4 && tsDen == 4);

            // NEW LOGIC: In 4/4, keep the original authored snare patterns (don't apply templates)
            // In ALL other time signatures, use the templates from buildBaseTemplatesForTimeSig
            if (is44)
            {
                // In 4/4, keep existing snare patterns - don't modify them
                return;
            }

            // For all non-4/4 time signatures, use templates
            juce::Array<juce::Array<int>> templates;
            buildBaseTemplatesForTimeSig(timeSigText, tsNum, tsDen, groups, stepsPerBar, templates);
            if (templates.size() == 0) return;

            const int pick = juce::jlimit(0, templates.size() - 1, rng.nextInt(templates.size()));
            auto steps = templates[pick];

            maybeMutatePreferredSteps(steps, rng, stepsPerBar);

            float boost = 0.82f;
            if (name == "wxstie") boost = 0.90f;
            if (name == "hip hop" || name == "hiphop") boost = 0.88f;
            if (name == "pop" || name == "rock") boost = 0.92f;
            if (name == "edm") boost = 0.94f;
            if (name == "r&b" || name == "rnb") boost = 0.86f;
            if (name == "reggaeton") boost = 0.94f;

            for (int i = 0; i < steps.size(); ++i)
            {
                const int st = clampStepToBar(steps[i], stepsPerBar);
                s.rows[Snare].p[st] = juce::jmax<float>(s.rows[Snare].p[st], boost);

                // If you truly don't have a clap row, delete this block.
                const float clapBoost = juce::jlimit(0.0f, 1.0f, boost * 0.70f);
                s.rows[Clap].p[st] = juce::jmax<float>(s.rows[Clap].p[st], clapBoost);
            }
        }


        static void remapSpecToStepsPerBar(boom::drums::DrumStyleSpec& s, int stepsPerBar)
        {
            stepsPerBar = juce::jlimit(1, boom::drums::kMaxStepsPerBar, stepsPerBar);

            // We treat the authored patterns as 16-step "intent" (0..15).
            constexpr int kSrcSteps = 16;

            for (int row = 0; row < boom::drums::NumRows; ++row)
            {
                // snapshot the first 16 steps as the "source pattern"
                float src[kSrcSteps]{};
                for (int i = 0; i < kSrcSteps; ++i)
                    src[i] = s.rows[row].p[i];

                // clear destination
                for (int i = 0; i < boom::drums::kMaxStepsPerBar; ++i)
                    s.rows[row].p[i] = 0.0f;

                // remap 0..stepsPerBar-1 -> 0..15
                for (int dst = 0; dst < stepsPerBar; ++dst)
                {
                    const float t = (stepsPerBar <= 1) ? 0.0f : (float)dst / (float)(stepsPerBar - 1);
                    const int srcIdx = juce::jlimit(0, kSrcSteps - 1, (int)std::round(t * (kSrcSteps - 1)));
                    s.rows[row].p[dst] = src[srcIdx];
                }
            }
        }

        // Public API
        DrumStyleSpec getSpecForTimeSigText(const juce::String& styleName,
            const juce::String& timeSigText,
            int seed)
        {
            DrumStyleSpec s = getSpec(styleName);

            int tsNum = 4, tsDen = 4;
            juce::Array<int> groups;
            if (!parseTimeSigText(timeSigText, tsNum, tsDen, groups))
            {
                // fallback: if UI gave garbage, treat as 4/4
                tsNum = 4; tsDen = 4;
                groups.clearQuick();
            }

            const bool is44 = (tsNum == 4 && tsDen == 4);
            const int stepsPerBar = stepsPerBarFromTimeSig(tsNum, tsDen);

            // IMPORTANT: disable any "backbeat locks" outside 4/4
            if (!is44)
                s.lockBackbeat = false;

            // If you want other rows to adapt, you can remap them here
            // (ONLY if your RowSpec.p[] is large enough — see crash section below)
            // remapSpecToStepsPerBar(s, stepsPerBar);

            // NEW LOGIC: Only clear snare/clap patterns in NON-4/4 time signatures
            // In 4/4, we keep the original authored patterns
            if (!is44)
            {
                // In non-4/4, clear any authored snare (and clap) probabilities so 4/4 doesn't leak in
                for (int i = 0; i < kMaxStepsPerBar; ++i)
                {
                    s.rows[Snare].p[i] = 0.0f;
                    // If you truly don't have Clap, delete this line and all Clap usage.
                    s.rows[Clap].p[i] = 0.0f;
                }
            }

            // Now apply meter-aware snare templates
            // (in 4/4, this function will return early and keep original patterns)
            // (in other meters, this will apply the templates from buildBaseTemplatesForTimeSig)
            applyPreferredSnareBoostsByTimeSigText(s, timeSigText, seed);

            return s;
        }






        DrumStyleSpec getSpecForTimeSig(const juce::String& styleName, int tsNum, int tsDen, int seed)
        {
            const juce::String tsText = juce::String(tsNum) + "/" + juce::String(tsDen);
            return getSpecForTimeSigText(styleName, tsText, seed);
        }




        // Helper to convert style name to DrumStyle enum for profile lookup
        static DrumStyle styleNameToEnum(const juce::String& name)
        {
            const auto n = name.trim().toLowerCase();
            if (n == "trap") return DrumStyle::Trap;
            if (n == "drill") return DrumStyle::Drill;
            if (n == "edm") return DrumStyle::EDM;
            if (n == "reggaeton") return DrumStyle::Reggaeton;
            if (n == "r&b" || n == "rnb") return DrumStyle::RnB;
            if (n == "pop") return DrumStyle::Pop;
            if (n == "rock") return DrumStyle::Rock;
            if (n == "wxstie") return DrumStyle::Wxstie;
            if (n == "hip hop" || n == "hiphop") return DrumStyle::HipHop;
            return DrumStyle::HipHop; // default
        }

        // === Generator =============================================================

        static int randRange(std::mt19937& rng, int a, int b) // inclusive
        {
            std::uniform_int_distribution<int> d(a, b); return d(rng);
        }
        static float rand01(std::mt19937& rng)
        {
            std::uniform_real_distribution<float> d(0.0f, 1.0f); return d(rng);
        }

        // ------------------------------------------------------------
// WXSTIE 4/4 snare behavior
// 80%: classic 2&4 backbeat
// 10%: small mutation (add/shift/remove)
// 10%: riskier syncopation
// ------------------------------------------------------------
        static void applyWxstieSnarePlan(DrumPattern& out,
            const DrumStyleSpec& spec,
            int bars,
            int barTicks,
            int stepsPerBar,
            int ticksPerStep,
            std::mt19937& rng)
        {
            const int snareRow = Snare;

            // Only meaningful if we have 4 beats worth of grid (4/4 -> stepsPerBar should be 16)
            // We still compute positions generically from stepsPerBar.
            const int perBeat = juce::jmax(1, stepsPerBar / 4);
            const int stepBeat2 = 1 * perBeat;  // beat 2 start
            const int stepBeat4 = 3 * perBeat;  // beat 4 start

            auto barStartTick = [&](int bar) { return bar * barTicks; };
            auto tickForStep = [&](int bar, int step) { return barStartTick(bar) + step * ticksPerStep; };

            auto removeSnareAtTick = [&](int row, int targetTick)
                {
                    for (int i = out.size(); --i >= 0;)
                    {
                        const auto& n = out.getReference(i);
                        if (n.row == row && n.startTick == targetTick)
                            out.remove(i);
                    }
                };

            auto clearSnaresInBar = [&](int bar)
                {
                    const int start = barStartTick(bar);
                    const int end = start + barTicks;

                    for (int i = out.size(); --i >= 0;)
                    {
                        const auto& n = out.getReference(i);
                        if (n.row == snareRow && n.startTick >= start && n.startTick < end)
                            out.remove(i);
                    }
                };

            auto addSnare = [&](int bar, int step, int velOverride /* -1 = random */)
                {
                    step = juce::jlimit(0, stepsPerBar - 1, step);
                    const int st = tickForStep(bar, step);

                    // prevent duplicates at the exact same tick
                    for (const auto& n : out)
                        if (n.row == snareRow && n.startTick == st)
                            return;

                    const int vel = (velOverride >= 1)
                        ? velOverride
                        : randRange(rng, spec.rows[snareRow].velMin, spec.rows[snareRow].velMax);

                    out.add({ snareRow, st, spec.rows[snareRow].lenTicks, vel });
                };

            // Candidate “riskier” snare steps (in a 16-step mindset)
            // We’ll filter them to stepsPerBar at runtime.
            const int rawRiskSteps[] = { 1, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };

            auto pickRiskStep = [&]()
                {
                    // build a filtered list each call (tiny, safe, fast enough)
                    int candidates[32]{};
                    int count = 0;

                    for (int s : rawRiskSteps)
                        if (s >= 0 && s < stepsPerBar && s != stepBeat2 && s != stepBeat4)
                            candidates[count++] = s;

                    if (count <= 0)
                        return juce::jlimit(0, stepsPerBar - 1, stepBeat4);

                    return candidates[randRange(rng, 0, count - 1)];
                };

            for (int bar = 0; bar < bars; ++bar)
            {
                const float roll = rand01(rng);

                // --- 80% SAFE: hard backbeat on 2 & 4 ---
                if (roll < 0.80f)
                {
                    clearSnaresInBar(bar);
                    addSnare(bar, stepBeat2, -1);
                    addSnare(bar, stepBeat4, -1);
                    continue;
                }

                // --- 10% MUTATE: start from 2&4, then small change ---
                if (roll < 0.90f)
                {
                    clearSnaresInBar(bar);

                    // Start from the classic backbeat
                    addSnare(bar, stepBeat2, -1);
                    addSnare(bar, stepBeat4, -1);

                    const float m = rand01(rng);

                    // 40% add an extra snare somewhere
                    if (m < 0.40f)
                    {
                        const int extra = pickRiskStep();
                        addSnare(bar, extra, randRange(rng, 70, 105)); // slightly softer accent
                    }
                    // 30% remove one of the backbeats
                    else if (m < 0.70f)
                    {
                        const bool remove2 = (rand01(rng) < 0.5f);
                        removeSnareAtTick(snareRow, tickForStep(bar, remove2 ? stepBeat2 : stepBeat4));
                    }
                    // 30% shift one of the backbeats by +/- 1 step
                    else
                    {
                        const bool shift2 = (rand01(rng) < 0.5f);
                        const int baseStep = shift2 ? stepBeat2 : stepBeat4;

                        int delta = (rand01(rng) < 0.5f) ? -1 : 1;
                        int shifted = juce::jlimit(0, stepsPerBar - 1, baseStep + delta);

                        // don’t collide into the other backbeat step
                        if (shift2 && shifted == stepBeat4) shifted = baseStep;
                        if (!shift2 && shifted == stepBeat2) shifted = baseStep;

                        removeSnareAtTick(snareRow, tickForStep(bar, baseStep));
                        addSnare(bar, shifted, -1);
                    }

                    continue;
                }

                // --- 10% RISKY: clear the bar and place syncopated snares ---
                {
                    clearSnaresInBar(bar);

                    // Optional: still keep *some* backbeat feel sometimes
                    if (rand01(rng) < 0.25f) addSnare(bar, stepBeat2, -1);
                    if (rand01(rng) < 0.25f) addSnare(bar, stepBeat4, -1);

                    // Add 1–3 risky snares
                    const int hits = randRange(rng, 1, 3);
                    for (int i = 0; i < hits; ++i)
                    {
                        const int s = pickRiskStep();
                        addSnare(bar, s, randRange(rng, 75, 120));
                    }

                    // Guarantee at least one snare in the bar if everything got filtered out
                    bool any = false;
                    const int start = barStartTick(bar);
                    const int end = start + barTicks;
                    for (const auto& n : out)
                    {
                        if (n.row == snareRow && n.startTick >= start && n.startTick < end)
                        {
                            any = true;
                            break;
                        }
                    }
                    if (!any)
                        addSnare(bar, stepBeat4, -1);
                }
            }
        }

        // ------------------------------------------------------------
// DRILL 4/4 snare plan:
// Default = 2-bar repeating anchor across full sequence:
//   Bar 1 (0): Beat 3
//   Bar 2 (1): Beat 4
//   Repeat...
//
// Then per-pattern mutation roll:
//   70% = none
//   20% = mild mutation
//   10% = risky mutation
// ------------------------------------------------------------
        static void applyDrillSnarePlan(DrumPattern& out,
            const DrumStyleSpec& spec,
            int bars,
            int barTicks,
            int stepsPerBar,
            int ticksPerStep,
            std::mt19937& rng)
        {
            const int snareRow = Snare;

            // Beat steps for current stepsPerBar (works even if stepsPerBar isn't 16)
            const int perBeat = juce::jmax(1, stepsPerBar / 4);
            const int stepBeat3 = 2 * perBeat; // beat 3 start
            const int stepBeat4 = 3 * perBeat; // beat 4 start

            auto barStartTick = [&](int bar) { return bar * barTicks; };
            auto tickForStep = [&](int bar, int step)
                {
                    step = juce::jlimit(0, stepsPerBar - 1, step);
                    return barStartTick(bar) + step * ticksPerStep;
                };

            auto clearSnaresInBar = [&](int bar)
                {
                    const int start = barStartTick(bar);
                    const int end = start + barTicks;

                    for (int i = out.size(); --i >= 0;)
                    {
                        const auto& n = out.getReference(i);
                        if (n.row == snareRow && n.startTick >= start && n.startTick < end)
                            out.remove(i);
                    }
                };

            auto addSnare = [&](int bar, int step, int velOverride /* -1 = random */)
                {
                    step = juce::jlimit(0, stepsPerBar - 1, step);
                    const int st = tickForStep(bar, step);

                    // prevent duplicates at the exact same tick
                    for (const auto& n : out)
                        if (n.row == snareRow && n.startTick == st)
                            return;

                    const int vel = (velOverride >= 1)
                        ? velOverride
                        : randRange(rng, spec.rows[snareRow].velMin, spec.rows[snareRow].velMax);

                    out.add({ snareRow, st, spec.rows[snareRow].lenTicks, juce::jlimit(1, 127, vel) });
                };

            auto removeSnareAtTick = [&](int targetTick)
                {
                    for (int i = out.size(); --i >= 0;)
                    {
                        const auto& n = out.getReference(i);
                        if (n.row == snareRow && n.startTick == targetTick)
                            out.remove(i);
                    }
                };

            // Decide mutation once per generated pattern (not per bar)
            const float roll = rand01(rng);
            enum class Mut { None, Mild, Risky };
            Mut mut = Mut::None;
            if (roll < 0.70f) mut = Mut::None;
            else if (roll < 0.90f) mut = Mut::Mild;
            else mut = Mut::Risky;

            // Helper: anchor step alternates every bar: 0->beat3, 1->beat4, 2->beat3, ...
            auto anchorStepForBar = [&](int bar)
                {
                    return ((bar & 1) == 0) ? stepBeat3 : stepBeat4;
                };

            // Build the base 2-bar repeating anchor across the entire sequence
            for (int bar = 0; bar < bars; ++bar)
            {
                clearSnaresInBar(bar);

                const int anchorStep = anchorStepForBar(bar);
                addSnare(bar, anchorStep, -1);

                // Keep the existing drill "rare late snare" flavor (step last), but subtle
                if (stepsPerBar >= 4 && rand01(rng) < 0.18f)
                {
                    const int lateStep = stepsPerBar - 1;
                    if (lateStep != anchorStep)
                        addSnare(bar, lateStep, randRange(rng, 70, 105));
                }
            }

            // No mutations: done
            if (mut == Mut::None)
                return;

            // Mild / risky mutation is applied to only a couple of bars so the "identity" stays drill
            const int barsToMutate = (mut == Mut::Mild) ? 1 : randRange(rng, 1, 2);

            for (int i = 0; i < barsToMutate; ++i)
            {
                const int bar = randRange(rng, 0, juce::jmax(0, bars - 1));
                const int anchor = anchorStepForBar(bar);

                if (mut == Mut::Mild)
                {
                    const float m = rand01(rng);

                    // 50%: add a ghost just before/after the anchor
                    if (m < 0.50f)
                    {
                        int delta = (rand01(rng) < 0.5f) ? -1 : 1;
                        int ghostStep = juce::jlimit(0, stepsPerBar - 1, anchor + delta);
                        if (ghostStep != anchor)
                            addSnare(bar, ghostStep, randRange(rng, 55, 85));
                    }
                    // 30%: small shift of the anchor by +/- 1 (still close)
                    else if (m < 0.80f)
                    {
                        int delta = (rand01(rng) < 0.5f) ? -1 : 1;
                        int shifted = juce::jlimit(0, stepsPerBar - 1, anchor + delta);

                        // remove original anchor and place shifted
                        removeSnareAtTick(tickForStep(bar, anchor));
                        addSnare(bar, shifted, -1);

                        // optional tiny ghost on original spot
                        if (rand01(rng) < 0.35f)
                            addSnare(bar, anchor, randRange(rng, 55, 80));
                    }
                    // 20%: add a light pickup near the end of the bar
                    else
                    {
                        int pick = juce::jlimit(0, stepsPerBar - 1, stepsPerBar - 2);
                        if (pick != anchor)
                            addSnare(bar, pick, randRange(rng, 60, 92));
                    }
                }
                else // Risky
                {
                    const float m = rand01(rng);

                    // 45%: shift anchor by +/- 2 steps
                    if (m < 0.45f)
                    {
                        int delta = (rand01(rng) < 0.5f) ? -2 : 2;
                        int shifted = juce::jlimit(0, stepsPerBar - 1, anchor + delta);

                        removeSnareAtTick(tickForStep(bar, anchor));
                        addSnare(bar, shifted, -1);

                        // add a ghost around it to make it feel intentional
                        if (rand01(rng) < 0.45f)
                        {
                            int ghost = juce::jlimit(0, stepsPerBar - 1, shifted + ((rand01(rng) < 0.5f) ? -1 : 1));
                            if (ghost != shifted)
                                addSnare(bar, ghost, randRange(rng, 55, 85));
                        }
                    }
                    // 35%: short fill around anchor (double / triple)
                    else if (m < 0.80f)
                    {
                        // keep anchor, but add two fast neighbors
                        const int s1 = juce::jlimit(0, stepsPerBar - 1, anchor - 1);
                        const int s2 = juce::jlimit(0, stepsPerBar - 1, anchor + 1);

                        if (s1 != anchor) addSnare(bar, s1, randRange(rng, 60, 95));
                        addSnare(bar, anchor, -1);
                        if (s2 != anchor) addSnare(bar, s2, randRange(rng, 70, 110));
                    }
                    // 20%: replace anchor with a late bar hit (very drill-y)
                    else
                    {
                        const int late = juce::jlimit(0, stepsPerBar - 1, stepsPerBar - 1);
                        removeSnareAtTick(tickForStep(bar, anchor));
                        addSnare(bar, late, -1);

                        // optional early ghost so it doesn't feel empty
                        if (rand01(rng) < 0.50f)
                        {
                            int early = juce::jlimit(0, stepsPerBar - 1, anchor - 2);
                            if (early != late)
                                addSnare(bar, early, randRange(rng, 55, 85));
                        }
                    }
                }
            }
        }


        // ------------------------------------------------------------
        // DRILL HI-HAT PLAN (MAIN GENERATOR) - TRESILLO 100%
        // Tresillo rhythm: two dotted 1/8 notes, then one regular 1/8
        // => 3/16 + 3/16 + 2/16 within an 8x16th (2-beat) cell
        //
        // 80% clean tresillo
        // 10% mild mutations (still tresillo identity)
        // 10% risky mutations (still tresillo-based, but more aggressive)
        //
        // Rolls allowed in all cases, placed best:
        // - directly before a snare/clap
        // - directly after a snare/clap
        // - just before bar end
        //
        // Roll sizes allowed:
        // - 1/32, 1/32 triplet, 1/16 triplet, very rare 1/64
        // Motion: ascending/descending/stationary (velocity ramp)
        // ------------------------------------------------------------
        static void applyDrillTresilloHatPlan(DrumPattern& out,
            const DrumStyleSpec& spec,
            int bars,
            int barTicks,
            int ticksPerQuarter,
            int numerator,
            int denominator,
            std::mt19937& rng)
        {
            const juce::String style = spec.name.trim().toLowerCase();
            if (style != "drill")
                return;

            const int totalTicks = bars * barTicks;

            // Beat length for the time signature denominator
            const int ticksPerBeat = juce::jmax(1, (int)std::llround((double)ticksPerQuarter * (4.0 / (double)denominator)));

            // 16th-of-beat grid
            const int t16 = juce::jmax(1, ticksPerBeat / 4);

            // Tresillo cell = 2 beats = 8*16th
            const int cell = 8 * t16;

            // Offsets inside each 2-beat cell: 0, +3/16, +6/16
            const int offA = 0;
            const int offB = 3 * t16;         // dotted 1/8 = 3/16
            const int offC = 6 * t16;         // second dotted 1/8 completes 6/16 (then 2/16 to finish cell)

            // Roll grids allowed
            const int t32 = juce::jmax(1, ticksPerBeat / 8);
            const int t32T = juce::jmax(1, ticksPerBeat / 12);
            const int t16T = juce::jmax(1, ticksPerBeat / 6);
            const int t64 = juce::jmax(1, ticksPerBeat / 16);

            auto chooseRollGrid = [&]() -> int
                {
                    const int r = randRange(rng, 0, 99);
                    if (r < 45) return t32;
                    if (r < 75) return t32T;
                    if (r < 97) return t16T;
                    return t64; // very rare
                };

            // Velocity “motion” for rolls (ascending/descending/stationary)
            auto rollVel = [&](int idx, int count, int baseVel) -> int
                {
                    const int motion = randRange(rng, 0, 2); // 0 asc, 1 desc, 2 stationary
                    if (motion == 2 || count <= 1)
                        return baseVel;

                    const float u = (float)idx / (float)juce::jmax(1, count - 1);
                    const int delta = 28;

                    if (motion == 0) // ascending
                        return juce::jlimit(1, 127, baseVel - 10 + (int)std::lround(u * delta));

                    // descending
                    return juce::jlimit(1, 127, baseVel + 10 - (int)std::lround(u * delta));
                };

            // --- Roll insertion helper (places extra hat-like hits) ---
            auto addHatRoll = [&](int absStart, int absEnd)
                {
                    absStart = juce::jlimit(0, totalTicks - 1, absStart);
                    absEnd = juce::jlimit(0, totalTicks, absEnd);
                    if (absEnd <= absStart + 2) return;

                    const int grid = chooseRollGrid();
                    const int count = juce::jmax(1, (absEnd - absStart) / grid);

                    // Use ClosedHat row spec ranges but a bit quieter
                    const RowSpec& rs = spec.rows[ClosedHat];
                    const int baseVel = juce::jlimit(35, 110, randRange(rng, rs.velMin, rs.velMax) - 18);
                    const int baseLen = juce::jmax(6, juce::jmin(rs.lenTicks, grid));

                    for (int i = 0; i < count; ++i)
                    {
                        const int t = absStart + i * grid;
                        if (t >= absEnd) break;

                        const int v = rollVel(i, count, baseVel);

                        // rollRowForHit gives “motion” in row-space too (ClosedHat/OpenHat/Perc) if your project uses it
                        const int rr = rollRowForHit(ClosedHat, i, pickRollPlan(rng).motion);
                        out.add({ rr, t, baseLen, v });
                    }
                };

            // Remove existing ClosedHat notes (we rebuild them as tresillo)
            for (int i = out.size(); --i >= 0;)
            {
                const auto& n = out.getReference(i);
                if (n.row == ClosedHat)
                    out.remove(i);
            }

            // Decide mutation mode ONCE per pattern (80/10/10 overall)
            const int mroll = randRange(rng, 0, 99);
            enum class Mut { Clean, Mild, Risky };
            Mut mut = Mut::Clean;
            if (mroll < 80) mut = Mut::Clean;
            else if (mroll < 90) mut = Mut::Mild;
            else mut = Mut::Risky;

            // Build hats per bar
            for (int bar = 0; bar < bars; ++bar)
            {
                const int barStart = bar * barTicks;
                const int barEnd = barStart + barTicks;

                // Collect snare/clap ticks in this bar for roll placement
                juce::Array<int> snTicks;
                for (const auto& n : out)
                {
                    if ((n.row == Snare || n.row == Clap) && n.startTick >= barStart && n.startTick < barEnd)
                        snTicks.add(n.startTick);
                }

                // Build base tresillo ticks across the bar
                juce::Array<int> ticks;
                auto addTickUnique = [&](int t)
                    {
                        t = juce::jlimit(barStart, barEnd - 1, t);
                        if (!ticks.contains(t)) ticks.add(t);
                    };

                for (int base = 0; base < barTicks; base += cell)
                {
                    addTickUnique(barStart + base + offA);
                    addTickUnique(barStart + base + offB);
                    addTickUnique(barStart + base + offC);
                }

                // Mutations (keep identity)
                if (mut == Mut::Mild)
                {
                    if (rand01(rng) < 0.55f && ticks.size() > 0)
                    {
                        // Add quiet ghost 1/16 before a random hit
                        const int idx = randRange(rng, 0, ticks.size() - 1);
                        addTickUnique(ticks[idx] - t16);
                    }
                    if (rand01(rng) < 0.35f && ticks.size() > 0)
                    {
                        // Shift one hit by +/- 1/16
                        const int idx = randRange(rng, 0, ticks.size() - 1);
                        ticks.set(idx, juce::jlimit(barStart, barEnd - 1, ticks[idx] + ((rand01(rng) < 0.5f) ? -t16 : +t16)));
                    }
                }
                else if (mut == Mut::Risky)
                {
                    if (rand01(rng) < 0.70f && ticks.size() > 0)
                    {
                        // Double-hit around one hit
                        const int idx = randRange(rng, 0, ticks.size() - 1);
                        addTickUnique(ticks[idx] + t16);
                        if (rand01(rng) < 0.45f) addTickUnique(ticks[idx] - t16);
                    }

                    if (rand01(rng) < 0.55f)
                    {
                        // Add an extra late accent (snapped to 16th grid)
                        const int lateStart = juce::jmax(barStart, barEnd - 2 * ticksPerBeat);
                        int t = randRange(rng, lateStart, barEnd - 1);
                        t = (t / t16) * t16;
                        addTickUnique(t);
                    }
                }

                ticks.sort();

                // Place hats
                const RowSpec& rs = spec.rows[ClosedHat];
                for (int i = 0; i < ticks.size(); ++i)
                {
                    int vel = randRange(rng, rs.velMin, rs.velMax);

                    // small accent on bar start
                    if (ticks[i] == barStart)
                        vel = juce::jmin(127, vel + 12);

                    out.add({ ClosedHat, ticks[i], rs.lenTicks, vel });
                }

                // Rolls allowed in all modes
                if (rand01(rng) < 0.40f)
                {
                    // Place roll: before/after snare (preferred) else bar end
                    const int where = randRange(rng, 0, 2); // 0 before, 1 after, 2 end

                    if (snTicks.size() > 0 && (where == 0 || where == 1))
                    {
                        const int sn = snTicks[randRange(rng, 0, snTicks.size() - 1)];
                        const int dur = juce::jmin(ticksPerBeat / 2, ticksPerBeat); // cap

                        if (where == 0)
                        {
                            // directly before snare
                            addHatRoll(juce::jmax(barStart, sn - dur), sn);
                        }
                        else
                        {
                            // directly after snare
                            addHatRoll(sn, juce::jmin(barEnd, sn + dur));
                        }
                    }
                    else
                    {
                        // end-of-bar roll
                        const int dur = juce::jmin(ticksPerBeat / 2, barTicks);
                        addHatRoll(barEnd - dur, barEnd);
                    }
                }
            }
        }

        // ------------------------------------------------------------
// TRAP HI-HATS (MAIN DRUM ENGINE OVERRIDE)
// Rebuild ClosedHat pattern using your requested behavior:
// - Roll types: Stationary / Ascending / Descending (uses rollRowForHit motion)
// - Roll note sizes: 16T, 32, 32T, 64 (64 is rare ~10%)
// - 75% of generations: ONE roll subdivision used for the whole generation
// - 25% of generations: rolls can mix subdivisions (2..4 kinds)
//
// Pattern behavior (trap only, main engine only):
// - 65%: steady pulse NO gaps
//     - 75%: 8ths
//     - 10%: 8th triplets OR 16ths
//     - 15%: quarters
// - 20%: same pulse choices, BUT with musical gaps (randomized placement)
// - 10%: riskier rhythms + more gaps
// - 5% : even riskier + more gaps + higher roll chance
//
// Syncopation:
// - most of the time none
// - occasional mild, rarer stronger (more in risky tiers)
//
// Roll placement bias (4/4 only):
// - trap: favor rolls around beat 3 (before/after) or end of bar
// ------------------------------------------------------------
        static void applyTrapHatMainEngineOverride(DrumPattern& out,
            const DrumStyleSpec& spec,
            int bars,
            int barTicks,
            int ticksPerQuarter,
            int numerator,
            int denominator,
            std::mt19937& rng)
        {
            const juce::String style = spec.name.trim().toLowerCase();
            if (style != "trap")
                return;

            // Remove existing ClosedHat notes (we fully rebuild them)
            for (int i = out.size(); --i >= 0;)
                if (out.getReference(i).row == ClosedHat)
                    out.remove(i);

            const RowSpec& hat = spec.rows[ClosedHat];

            auto randVel = [&]() -> int
                {
                    int v = randRange(rng, hat.velMin, hat.velMax);
                    if (rand01(rng) < 0.12f) v = juce::jmin(127, v + 12);
                    return juce::jlimit(1, 127, v);
                };

            // Musical tick sizes (relative to quarter note)
            const int tQuarter = juce::jmax(1, ticksPerQuarter);
            const int tEighth = juce::jmax(1, ticksPerQuarter / 2);
            const int tSixteenth = juce::jmax(1, ticksPerQuarter / 4);
            const int tEighthTriplet = juce::jmax(1, ticksPerQuarter / 3);
            const int tSixteenthTriplet = juce::jmax(1, ticksPerQuarter / 6);
            const int tThirtySecond = juce::jmax(1, ticksPerQuarter / 8);
            const int tThirtySecondTriplet = juce::jmax(1, ticksPerQuarter / 12);
            const int tSixtyFourth = juce::jmax(1, ticksPerQuarter / 16);

            // Pick the behavior tier: 65/20/10/5
            const int tierRoll = randRange(rng, 0, 99);
            enum class Tier { SteadyNoGaps, Gaps, RiskyGaps, VeryRisky };
            Tier tier = Tier::SteadyNoGaps;
            if (tierRoll < 65) tier = Tier::SteadyNoGaps;
            else if (tierRoll < 85) tier = Tier::Gaps;
            else if (tierRoll < 95) tier = Tier::RiskyGaps;
            else tier = Tier::VeryRisky;

            // Roll chance: same unless specified; last tier increases
            float rollChance = 0.18f;
            if (tier == Tier::VeryRisky) rollChance = 0.35f;

            // Gaps intensity
            float gapDrop = 0.0f;
            if (tier == Tier::Gaps) gapDrop = 0.16f;
            else if (tier == Tier::RiskyGaps) gapDrop = 0.34f;
            else if (tier == Tier::VeryRisky) gapDrop = 0.50f;

            // Syncopation choice (MOST OF THE TIME none)
            // Increase syncopation in riskier tiers
            int syncMode = 0; // 0 none, 1 mild, 2 stronger
            {
                int r = randRange(rng, 0, 99);
                if (tier == Tier::SteadyNoGaps) { if (r < 75) syncMode = 0; else if (r < 95) syncMode = 1; else syncMode = 2; }
                else if (tier == Tier::Gaps) { if (r < 65) syncMode = 0; else if (r < 92) syncMode = 1; else syncMode = 2; }
                else if (tier == Tier::RiskyGaps) { if (r < 50) syncMode = 0; else if (r < 86) syncMode = 1; else syncMode = 2; }
                else { if (r < 35) syncMode = 0; else if (r < 80) syncMode = 1; else syncMode = 2; }
            }

            // Pulse selection for steady rules
            auto pickBasePulse = [&]() -> int
                {
                    const int r = randRange(rng, 0, 99);

                    // 65% tier wants steady beat rules, but we reuse this in others too
                    // 75%: 8ths
                    // 10%: 8th triplets OR 16ths
                    // 15%: quarters
                    if (r < 75) return tEighth;
                    if (r < 85) return (rand01(rng) < 0.5f) ? tEighthTriplet : tSixteenth;
                    return tQuarter;
                };

            // Riskier tiers can sometimes pick riskier pulses mid-bar
            auto pickRiskyPulse = [&]() -> int
                {
                    // still biased to musical trap values
                    const int r = randRange(rng, 0, 99);
                    if (r < 50) return tEighth;
                    if (r < 70) return tSixteenth;
                    if (r < 88) return tEighthTriplet;
                    return tQuarter;
                };

            // ------------------------------------------------------------
            // Roll subdivision rules
            // 75%: one unit for entire generation
            // 25%: allow mixed (2..4 types)
            // ------------------------------------------------------------
            const bool singleRollUnitGen = (randRange(rng, 0, 99) < 75);

            auto pickRollUnitWeighted = [&]() -> int
                {
                    // requested set: 16T, 32, 32T, 64 (64 ~10%)
                    const int r = randRange(rng, 0, 99);
                    if (r < 30) return tSixteenthTriplet;        // 30%
                    if (r < 70) return tThirtySecond;           // 40%
                    if (r < 90) return tThirtySecondTriplet;    // 20%
                    return tSixtyFourth;                        // 10%
                };

            const int singleRollUnit = pickRollUnitWeighted();

            auto pickRollMotion = [&]() -> RollMotion
                {
                    const int r = randRange(rng, 0, 99);
                    if (r < 34) return RollMotion::Ascending;
                    if (r < 67) return RollMotion::Descending;
                    return RollMotion::Stationary;
                };

            auto addHatHit = [&](int row, int tick, int len, int vel)
                {
                    if (tick < 0) return;
                    const int barEnd = ((tick / barTicks) + 1) * barTicks;
                    if (tick >= barEnd) return;

                    out.add({
                        row,
                        tick,
                        juce::jmax(12, juce::jmin(len, tEighth)), // keep hats reasonable
                        juce::jlimit(1, 127, vel)
                        });
                };

            auto addRoll = [&](int startTick, int durTicks)
                {
                    if (durTicks <= 0) return;

                    const int barStart = (startTick / barTicks) * barTicks;
                    const int barEnd = barStart + barTicks;
                    if (startTick < barStart) return;

                    durTicks = juce::jmin(durTicks, barEnd - startTick);
                    if (durTicks < juce::jmax(1, tSixteenth)) return;

                    const RollMotion motion = pickRollMotion();
                    const int baseVel = juce::jlimit(40, 124, randVel() - 10);

                    // If mixed mode, we choose a new unit at the start of each roll.
                    // If single mode, unit is fixed for the whole generation.
                    int unit = singleRollUnitGen ? singleRollUnit : pickRollUnitWeighted();
                    unit = juce::jmax(1, unit);

                    const int steps = juce::jlimit(2, 64, durTicks / unit);

                    for (int i = 0; i < steps; ++i)
                    {
                        const int t = startTick + i * unit;
                        if (t >= startTick + durTicks) break;

                        // rollRowForHit gives us “motion” across rows (ClosedHat/Perc/OpenHat) in this engine
                        const int rr = rollRowForHit(ClosedHat, i, motion);

                        int v = baseVel;
                        if (motion == RollMotion::Descending) v = juce::jlimit(40, 127, baseVel - i * 4);
                        else if (motion == RollMotion::Ascending) v = juce::jlimit(40, 127, baseVel - (steps - 1 - i) * 4);
                        else v = juce::jlimit(40, 127, baseVel - i * 2);

                        addHatHit(rr, t, juce::jmax(12, hat.lenTicks - 3 * i), v);
                    }

                    // 25%: occasionally change unit mid-roll too (gives “2 or more subdivisions” inside one roll)
                    if (!singleRollUnitGen && rand01(rng) < 0.25f)
                    {
                        // do a second tiny roll segment with a different unit right after
                        int unit2 = pickRollUnitWeighted();
                        if (unit2 != unit)
                        {
                            const int segStart = startTick + steps * unit;
                            const int remain = (startTick + durTicks) - segStart;
                            if (remain >= unit2 * 2)
                            {
                                const int steps2 = juce::jlimit(2, 24, remain / unit2);
                                for (int i = 0; i < steps2; ++i)
                                {
                                    const int t = segStart + i * unit2;
                                    if (t >= startTick + durTicks) break;

                                    const int rr = rollRowForHit(ClosedHat, i, motion);
                                    int v = juce::jlimit(40, 127, baseVel - i * 3);
                                    addHatHit(rr, t, juce::jmax(12, hat.lenTicks - 2 * i), v);
                                }
                            }
                        }
                    }
                };

            // ------------------------------------------------------------
            // Build hats per bar based on tier rules
            // ------------------------------------------------------------
            for (int bar = 0; bar < bars; ++bar)
            {
                const int barStart = bar * barTicks;
                const int barEnd = barStart + barTicks;

                // Base pulse selection
                int pulse = pickBasePulse();

                // Risky tiers can vary pulse within bar sometimes
                const bool varyWithinBar = (tier == Tier::RiskyGaps || tier == Tier::VeryRisky) && (rand01(rng) < 0.55f);

                // Generate base ticks
                juce::Array<int> ticks;

                auto addTick = [&](int t)
                    {
                        if (t < barStart || t >= barEnd) return;
                        if (!ticks.contains(t)) ticks.add(t);
                    };

                if (!varyWithinBar)
                {
                    for (int t = barStart; t < barEnd; t += pulse)
                        addTick(t);
                }
                else
                {
                    // Split bar into 2..4 segments and pick a pulse per segment
                    const int segments = randRange(rng, 2, 4);
                    for (int seg = 0; seg < segments; ++seg)
                    {
                        const int segStart = barStart + (seg * barTicks) / segments;
                        const int segEnd = barStart + ((seg + 1) * barTicks) / segments;
                        const int segPulse = pickRiskyPulse();

                        for (int t = segStart; t < segEnd; t += segPulse)
                            addTick(t);
                    }
                }

                ticks.sort();

                // Apply NO gaps tier: do nothing
                // Apply gaps tiers: drop hits but keep musical anchors
                if (gapDrop > 0.0f)
                {
                    // Protect downbeat + a mid-bar anchor so it still grooves
                    const int protectA = barStart;
                    const int protectB = barStart + juce::jmin(barTicks - 1, 2 * tQuarter); // approx beat 3 in 4/4; harmless elsewhere

                    for (int i = ticks.size(); --i >= 0;)
                    {
                        const int t = ticks[i];
                        if (t == protectA || t == protectB) continue;

                        // More random gaps, but not always same spot
                        float pDrop = gapDrop;
                        if (syncMode == 2) pDrop = juce::jmin(0.85f, pDrop + 0.10f);

                        if (rand01(rng) < pDrop)
                            ticks.remove(i);
                    }

                    // Ensure not empty
                    if (ticks.isEmpty())
                    {
                        addTick(barStart);
                        addTick(barStart + juce::jmin(barTicks - 1, tEighth));
                        ticks.sort();
                    }
                }

                // Syncopation overlay (mostly none)
                if (syncMode > 0)
                {
                    const int t16 = juce::jmax(1, tSixteenth);

                    // mild: occasional extra offbeat or slight push
                    if (syncMode == 1)
                    {
                        if (rand01(rng) < 0.30f && ticks.size() > 2)
                        {
                            const int idx = randRange(rng, 1, ticks.size() - 1);
                            const int t = ticks[idx] + t16;
                            if (t < barEnd) addTick(t);
                        }
                    }
                    else
                    {
                        // stronger: a couple of extras + occasional displacement
                        if (rand01(rng) < 0.55f && ticks.size() > 2)
                        {
                            const int idx = randRange(rng, 0, ticks.size() - 1);
                            const int t = ticks[idx] + (rand01(rng) < 0.5f ? t16 : (2 * t16));
                            if (t < barEnd) addTick(t);
                        }
                        if (rand01(rng) < 0.35f && ticks.size() > 3)
                        {
                            const int idx = randRange(rng, 1, ticks.size() - 2);
                            ticks.set(idx, juce::jlimit(barStart, barEnd - 1, ticks[idx] + (rand01(rng) < 0.5f ? -t16 : +t16)));
                        }
                    }

                    ticks.sort();
                }

                // Place the closed hats
                for (int i = 0; i < ticks.size(); ++i)
                {
                    const int t = ticks[i];
                    int vel = randVel();
                    if (t == barStart) vel = juce::jmin(127, vel + 12);

                    addHatHit(ClosedHat, t, hat.lenTicks, vel);
                }

                // Rolls (bias in 4/4 around beat 3, before/after/end)
                if (rand01(rng) < rollChance)
                {
                    // Choose duration similar to hats window feel (quarter/half-ish)
                    int dur = tQuarter / 2;
                    const int rDur = randRange(rng, 0, 99);
                    if (rDur < 35) dur = tQuarter / 2;
                    else if (rDur < 75) dur = tQuarter;
                    else dur = (tQuarter * 3) / 2;

                    // Default start: somewhere inside bar
                    int start = barStart + juce::jmax(1, tEighth);

                    if (numerator == 4 && denominator == 4)
                    {
                        const int beat3 = barStart + 2 * tQuarter;
                        const int before3 = juce::jmax(barStart, beat3 - dur);
                        const int after3 = juce::jmin(barEnd - dur, beat3 + juce::jmax(1, tSixteenth));
                        const int endBar = juce::jmax(barStart, barEnd - dur);

                        const int r = randRange(rng, 0, 99);
                        if (r < 40) start = before3;
                        else if (r < 70) start = after3;
                        else start = endBar;

                        // small jitter so it doesn't sound robotic
                        if (rand01(rng) < 0.55f)
                        {
                            const int jitter = (rand01(rng) < 0.5f ? -1 : 1) * juce::jmax(1, tSixteenth);
                            start = juce::jlimit(barStart, barEnd - dur, start + jitter);
                        }
                    }
                    else
                    {
                        // non-4/4: bias near end-of-bar a bit
                        if (rand01(rng) < 0.55f)
                            start = juce::jmax(barStart, barEnd - dur);
                    }

                    // Clamp so it ends before next bar
                    start = juce::jlimit(barStart, barEnd - juce::jmax(1, dur), start);

                    addRoll(start, dur);
                }
            }
        }

        // ------------------------------------------------------------
// Kick "bar 1-4 repeats into bar 5-8" rule (ALL STYLES)
// Only applies when bars==8 and time signature is 4/4.
//
// Distribution (exactly as requested):
// - 75%: bars 5-8 = exact copy of bars 1-4
// - 20%: bars 5-8 = copy + 1–2 slight mutations (bars 5-8 only)
// - 5% total:
//     - 2.5%: copy + heavier mutations + chance of kick rolls (prefer bars 5-8)
//     - 2.5%: leave original (no forced repeat)
// ------------------------------------------------------------
        static void applyKickRepeatRuleFor8Bars44(boom::drums::DrumPattern& out,
            int bars,
            int numerator,
            int denominator,
            int barTicks,
            int ticksPerQuarter,
            int ticksPerStep,
            std::mt19937& rng)
        {
            using namespace boom::drums;

            if (!(bars == 8 && numerator == 4 && denominator == 4))
                return;

            const int firstHalfStart = 0;
            const int firstHalfEnd = 4 * barTicks;     // exclusive
            const int secondHalfStart = 4 * barTicks;
            const int secondHalfEnd = 8 * barTicks;

            // Pull kick notes from bars 1-4 (store as relative)
            juce::Array<DrumNote> firstHalfKicks;
            firstHalfKicks.ensureStorageAllocated(256);

            for (const auto& n : out)
            {
                if (n.row != Kick) continue;
                if (n.startTick >= firstHalfStart && n.startTick < firstHalfEnd)
                {
                    DrumNote k = n;
                    k.startTick -= firstHalfStart;
                    firstHalfKicks.add(k);
                }
            }

            if (firstHalfKicks.isEmpty())
                return;

            // Decide behavior
            const int r = randRange(rng, 0, 9999); // high resolution for 2.5%

            // 0..7499 => 75% exact
            // 7500..9499 => 20% slight mutate
            // 9500..9749 => 2.5% heavy mutate + rolls
            // 9750..9999 => 2.5% leave original
            enum class Mode { Exact, Slight, Heavy, LeaveOriginal };
            Mode mode = Mode::Exact;

            if (r < 7500) mode = Mode::Exact;
            else if (r < 9500) mode = Mode::Slight;
            else if (r < 9750) mode = Mode::Heavy;
            else mode = Mode::LeaveOriginal;

            if (mode == Mode::LeaveOriginal)
                return;

            // Remove existing kicks in bars 5-8 (we will rebuild them)
            for (int i = out.size(); --i >= 0;)
            {
                const auto& n = out.getReference(i);
                if (n.row == Kick && n.startTick >= secondHalfStart && n.startTick < secondHalfEnd)
                    out.remove(i);
            }

            auto addKick = [&](int absTick, int len, int vel)
                {
                    if (absTick < secondHalfStart || absTick >= secondHalfEnd) return;
                    out.add({ Kick, absTick, juce::jmax(6, len), juce::jlimit(1, 127, vel) });
                };

            // Copy bars 1-4 kicks into bars 5-8
            for (const auto& k : firstHalfKicks)
                addKick(secondHalfStart + k.startTick, k.lenTicks, k.vel);

            // Helpers to mutate kicks (only in bars 5-8)
            auto collectSecondHalfKickIndices = [&]() -> juce::Array<int>
                {
                    juce::Array<int> idx;
                    for (int i = 0; i < out.size(); ++i)
                    {
                        const auto& n = out.getReference(i);
                        if (n.row == Kick && n.startTick >= secondHalfStart && n.startTick < secondHalfEnd)
                            idx.add(i);
                    }
                    return idx;
                };

            auto shiftOneKick = [&](int shiftTicks)
                {
                    auto idx = collectSecondHalfKickIndices();
                    if (idx.isEmpty()) return;
                    const int pick = idx[randRange(rng, 0, idx.size() - 1)];
                    auto& n = out.getReference(pick);
                    n.startTick = juce::jlimit(secondHalfStart, secondHalfEnd - 1, n.startTick + shiftTicks);
                };

            auto removeOneKick = [&]()
                {
                    auto idx = collectSecondHalfKickIndices();
                    if (idx.isEmpty()) return;
                    const int pick = idx[randRange(rng, 0, idx.size() - 1)];
                    out.remove(pick);
                };

            auto addOneKickOnGrid = [&]()
                {
                    const int stepCount = (secondHalfEnd - secondHalfStart) / juce::jmax(1, ticksPerStep);
                    if (stepCount <= 0) return;

                    const int step = randRange(rng, 0, stepCount - 1);
                    const int tick = secondHalfStart + step * ticksPerStep;

                    for (const auto& n : out)
                        if (n.row == Kick && n.startTick == tick) return;

                    addKick(tick, juce::jmax(6, ticksPerStep / 2), randRange(rng, 85, 125));
                };

            auto tweakOneVelocity = [&]()
                {
                    auto idx = collectSecondHalfKickIndices();
                    if (idx.isEmpty()) return;
                    const int pick = idx[randRange(rng, 0, idx.size() - 1)];
                    auto& n = out.getReference(pick);
                    n.vel = juce::jlimit(1, 127, n.vel + randRange(rng, -12, +12));
                };

            auto doOneSlightMutation = [&]()
                {
                    const int m = randRange(rng, 0, 99);
                    if (m < 35) removeOneKick();
                    else if (m < 65) shiftOneKick((rand01(rng) < 0.5f) ? -ticksPerStep : +ticksPerStep);
                    else if (m < 90) addOneKickOnGrid();
                    else tweakOneVelocity();
                };

            auto addKickRollPreferSecondHalf = [&]()
                {
                    // pick a bar inside 5-8
                    const int bar = randRange(rng, 4, 7); // bars 5..8
                    const int barStart = bar * barTicks;
                    const int barEnd = barStart + barTicks;

                    // roll grid: 16T, 32, 32T, 64 (64 is "rare", not ultra-rare)
                    const int t16T = juce::jmax(1, ticksPerQuarter / 6);
                    const int t32 = juce::jmax(1, ticksPerQuarter / 8);
                    const int t32T = juce::jmax(1, ticksPerQuarter / 12);
                    const int t64 = juce::jmax(1, ticksPerQuarter / 16);

                    const int rr = randRange(rng, 0, 99);
                    int unit = t32;
                    if (rr < 25) unit = t16T;
                    else if (rr < 70) unit = t32;
                    else if (rr < 90) unit = t32T;
                    else unit = t64;

                    const int hits = randRange(rng, 2, 5);
                    const int dur = hits * unit;

                    int start = barEnd - dur - juce::jmax(1, ticksPerStep);
                    start = juce::jlimit(barStart, barEnd - dur, start);

                    const int baseVel = randRange(rng, 85, 120);

                    for (int i = 0; i < hits; ++i)
                    {
                        const int t = start + i * unit;
                        int v = baseVel;

                        // optional small ramp
                        if (rand01(rng) < 0.5f)
                            v = juce::jlimit(1, 127, baseVel - (hits - 1 - i) * 5);

                        addKick(t, juce::jmax(6, unit), v);
                    }
                };

            // Apply mutations based on mode
            if (mode == Mode::Slight)
            {
                const int muts = randRange(rng, 1, 2);
                for (int i = 0; i < muts; ++i)
                    doOneSlightMutation();
            }
            else if (mode == Mode::Heavy)
            {
                const int muts = randRange(rng, 3, 7);
                for (int i = 0; i < muts; ++i)
                    doOneSlightMutation();

                // chance of rolls (prefer bars 5-8)
                if (rand01(rng) < 0.65f)
                    addKickRollPreferSecondHalf();
            }

            // Keep pattern tidy (optional)
            std::sort(out.begin(), out.end(), [](const DrumNote& a, const DrumNote& b)
                {
                    if (a.startTick != b.startTick) return a.startTick < b.startTick;
                    return a.row < b.row;
                });
        }



        void generate(const DrumStyleSpec& spec, int bars,
            int restPct, int dottedPct, int tripletPct, int swingPct,
            int seed, int numerator, int denominator,
            DrumPattern& out)
        {
            out.clearQuick();
            bars = juce::jlimit(1, 16, bars);

            std::mt19937 rng(static_cast<std::uint32_t>(
                seed == -1 ? juce::Time::getMillisecondCounter() : seed));

            numerator = juce::jlimit(1, 32, numerator);
            denominator = juce::jlimit(1, 32, denominator);


            // Normalize user/global biases
            const float restBias = clamp01i(restPct) / 100.0f;
            const float swingFeel = juce::jlimit(0.0f, 100.0f, (float)swingPct);
            const float swingAsFrac = swingFeel * 0.01f;

            // Use helper to compute ticks per 16th from canonical PPQ/cells-per-beat
            const int ppq = BoomAudioProcessor::PPQ;          // 96 in your project
            const int ticksPerQuarter = ppq;
            const int ticksPer16 = ticksPerQuarter / 4;
            const RollPlan rollPlan = pickRollPlan(rng);
            // True bar length for the selected time signature:
            const double barTicksD = (double)ticksPerQuarter * (double)numerator * (4.0 / (double)denominator);
            const int barTicks = juce::jmax(1, (int)std::round(barTicksD));


            const int stepsPerBar = juce::jlimit(1, kMaxStepsPerBar, stepsPerBarFromTimeSig(numerator, denominator));
            const int ticksPerStep = juce::jmax(1, (int)std::llround((double)barTicks / (double)stepsPerBar));

            // For each bar + row + step: Bernoulli on row probability -> create hit
            for (int bar = 0; bar < bars; ++bar)
            {
                for (int row = 0; row < NumRows; ++row)
                {
                    const RowSpec& rs = spec.rows[row];

                    for (int step = 0; step < stepsPerBar; ++step)
                    {
                        // Base probability
                        float p = rs.p[step];


                        // Rest density pulls probability down
                        p *= (1.0f - restBias);

                        if (rand01(rng) <= p)
                        {
                            // Spawn a hit
                            int vel = randRange(rng, rs.velMin, rs.velMax);

                            // Basic swing on even 8th offbeats for hats/perc/openhat
                            int startTick = bar * barTicks + step * ticksPerStep;

                            if ((row == ClosedHat || row == OpenHat || row == Perc) && (step & 1))
                            {
                                const int swingTicks = (int)std::round((ticksPerStep * 0.5f) * swingAsFrac);
                                startTick += swingTicks;
                            }

                            int len = rs.lenTicks;

                            // Occasional micro-rolls (esp. hats)
                            if (rs.rollProb > 0.0f && rand01(rng) < rs.rollProb && rs.maxRollSub > 1)
                            {
                                const int divTicks = ticksPerRollStep(rollPlan.rate, ticksPerQuarter);

                                // Hit count can be tied to speed: faster division -> allow more hits
                                int hits = randRange(rng, 2, 4);
                                if (divTicks <= ticksPerQuarter / 12) hits = randRange(rng, 3, 6);   // 32T/64-ish
                                if (divTicks <= ticksPerQuarter / 16) hits = randRange(rng, 4, 8);   // 64-ish

                                for (int rr = 0; rr < hits; ++rr)
                                {
                                    const int st = startTick + rr * divTicks;
                                    if (st >= (bar + 1) * barTicks)
                                        break;

                                    const int rollRow = rollRowForHit(row, rr, rollPlan.motion);

                                    // You can shape velocity for “descending/ascending” feel too
                                    int v = vel;
                                    if (rollPlan.motion == RollMotion::Descending) v = juce::jlimit(40, 127, vel - rr * 4);
                                    else if (rollPlan.motion == RollMotion::Ascending) v = juce::jlimit(40, 127, vel - (hits - 1 - rr) * 4);
                                    else v = juce::jlimit(40, 127, vel - rr * 2);

                                    out.add({ rollRow, st, juce::jmax(12, len - 4 * rr), v });
                                }
                            }

                            else
                            {
                                out.add({ row, startTick, len, vel });
                            }
                        }
                    }

                    // Lock backbeat if requested (ensure at least one snare/clap on 2 & 4)
// Lock backbeat ONLY for 4/4 (because the anchors are defined as "2 and 4" in a 16-step 4/4 grid)
                    if (spec.lockBackbeat && (row == Snare || row == Clap) && numerator == 4 && denominator == 4)
                    {
                        // In 4/4, stepsPerBar should be 16, and ticksPerStep will match ticksPer16 (usually 24 at PPQ=96)
                        const int b2 = bar * barTicks + 4 * ticksPerStep;   // step 4
                        const int b4 = bar * barTicks + 12 * ticksPerStep;   // step 12

                        bool has2 = false, has4 = false;
                        for (auto& n : out)
                        {
                            if (n.row == row && (n.startTick == b2 || n.startTick == b4))
                            {
                                if (n.startTick == b2) has2 = true;
                                else has4 = true;
                            }
                        }

                        if (!has2) out.add({ row, b2, spec.rows[row].lenTicks, randRange(rng, spec.rows[row].velMin, spec.rows[row].velMax) });
                        if (!has4) out.add({ row, b4, spec.rows[row].lenTicks, randRange(rng, spec.rows[row].velMin, spec.rows[row].velMax) });
                    }

                }

            }

            // ============================================================
// Kick quality rule: every bar must have at least ONE kick hit
// (prevents "empty bars" that make patterns feel broken)
// ============================================================
            for (int bar = 0; bar < bars; ++bar)
            {
                const int barStart = bar * barTicks;
                const int barEnd = barStart + barTicks;

                bool hasKick = false;
                for (int i = 0; i < out.size(); ++i)
                {
                    const auto& n = out.getReference(i);
                    if (n.row == Kick && n.startTick >= barStart && n.startTick < barEnd)
                    {
                        hasKick = true;
                        break;
                    }
                }

                if (!hasKick)
                {
                    // Put the kick on the bar downbeat
                    const int kickLen = juce::jmax(1, spec.rows[Kick].lenTicks);
                    const int kickVel = juce::jlimit(1, 127, juce::jmax(100, spec.rows[Kick].velMin));

                    out.add({ Kick, barStart, kickLen, kickVel });
                }
            }


            // WXSTIE special-case snare logic in 4/4 (your 80/10/10 rule)
            if (spec.name.trim().toLowerCase() == "wxstie" && numerator == 4 && denominator == 4)
            {
                applyWxstieSnarePlan(out, spec, bars, barTicks, stepsPerBar, ticksPerStep, rng);
            }

            applyTrapHatMainEngineOverride(out, spec, bars, barTicks, ticksPerQuarter, numerator, denominator, rng);

            if (spec.name.trim().toLowerCase() == "drill" && numerator == 4 && denominator == 4)
            {
                applyDrillSnarePlan(out, spec, bars, barTicks, stepsPerBar, ticksPerStep, rng);
                // DRILL: override closed hats with 100% tresillo (+ 80/10/10 mutations + rolls)
                applyDrillTresilloHatPlan(out, spec, bars, barTicks, ticksPerQuarter, numerator, denominator, rng);
            }

            applyKickRepeatRuleFor8Bars44(out, bars, numerator, denominator, barTicks, ticksPerQuarter, ticksPerStep, rng);
            // -----------------------------------------------------------------
            // NOTE: Style profile enforcement (mandatory/forbidden/preferred steps)
            // has been REMOVED from this generator function.
            //
            // Rationale: The caller (BoomAudioProcessor::generateDrums) now handles
            // profile enforcement correctly using the user's actual UI selection,
            // not the generator's internal DrumStyleSpec name.
            //
            // Enforcing twice was causing conflicts and preventing user choice
            // from being respected.
            // -----------------------------------------------------------------
// Per-note Triplet / Dotted application (density-based)
//
// Triplets SHIFT note timing to triplet grid (doesn't add notes)
// Dotted notes EXTEND note length by 1.5x
// -----------------------------------------------------------------
            {
                const float tripletBase = clamp01i(tripletPct) / 100.0f;
                const float dottedBase = clamp01i(dottedPct) / 100.0f;

                if (tripletBase > 0.0f || dottedBase > 0.0f)
                {
                    const int ticksPerBeat = (int)std::round((double)ticksPerQuarter * (4.0 / (double)denominator));
                    const int tripletTicks = juce::jmax(1, ticksPerBeat / 3);

                    // DEBUG: Count how many notes per row get triplet-shifted
                    int tripletCount[NumRows] = { 0 };

                    for (int i = 0; i < out.size(); ++i)
                    {
                        auto& n = out.getReference(i);

                        const bool hatLike = (n.row == ClosedHat || n.row == OpenHat || n.row == Perc);

                        // Trap hats are explicitly authored (steady/gaps/risky). Don't post-snap them.
                        if (spec.name.trim().toLowerCase() == "trap" && n.row == ClosedHat)
                            continue;

                        // If DRILL, we just built a strict tresillo hat plan.
// Do NOT triplet-snap or dot-extend closed hats, or it will destroy the rhythm.
                        if (spec.name.trim().toLowerCase() == "drill" && n.row == ClosedHat)
                        {
                            continue;
                        }


                        // Apply triplets to ALL rows based on slider value
                        float tChance = tripletBase;
                        if (hatLike) tChance *= 1.15f;
                        else         tChance *= 0.85f;   // << was 0.35f (too low)
                        tChance = juce::jlimit(0.0f, 1.0f, tChance);
                        float dChance = dottedBase * (hatLike ? 1.10f : 0.90f);

                        tChance = juce::jlimit(0.0f, 1.0f, tChance);
                        dChance = juce::jlimit(0.0f, 1.0f, dChance);

                        // --- Triplet timing conversion (snap within beat) ---
                        if (tChance > 0.0f && rand01(rng) < tChance)
                        {
                            const int beatStart = (n.startTick / ticksPerBeat) * ticksPerBeat;
                            const int posInBeat = n.startTick - beatStart;

                            int triIndex = (int)std::round((double)posInBeat / (double)tripletTicks);
                            triIndex = juce::jlimit(0, 2, triIndex);

                            const int snapped = beatStart + triIndex * tripletTicks;

                            // If snapping didn't change anything, optionally try a neighboring triplet slot
                            if (snapped == n.startTick)
                            {
                                // move to adjacent triplet position inside the beat (musical + guarantees change)
                                const int dir = (rand01(rng) < 0.5f) ? -1 : 1;
                                triIndex = juce::jlimit(0, 2, triIndex + dir);
                            }

                            n.startTick = beatStart + triIndex * tripletTicks;
                            tripletCount[n.row]++;
                        }

                        // --- Dotted length conversion (1.5x length, clamped) ---
                        if (dChance > 0.0f && rand01(rng) < dChance)
                        {
                            const int newLen = (int)std::round((double)n.lenTicks * 1.5);
                            n.lenTicks = juce::jlimit(6, ticksPerBeat * 2, newLen);
                        }
                    }

                    // DEBUG: Log triplet application per row
                    if (tripletBase > 0.0f)
                    {
                        DBG("Triplet application (density=" << tripletPct << "%): "
                            << "Kick:" << tripletCount[Kick]
                            << " Snare:" << tripletCount[Snare]
                            << " Clap:" << tripletCount[Clap]
                            << " ClosedHat:" << tripletCount[ClosedHat]
                            << " OpenHat:" << tripletCount[OpenHat]
                            << " Perc:" << tripletCount[Perc]);
                    }

                }
                // ============================================================
// HARD RULE: Kick MUST hit at tick 0 (bar 1 beat 1)
// ============================================================
                {
                    bool hasKickOnOne = false;
                    for (int i = 0; i < out.size(); ++i)
                    {
                        const auto& n = out.getReference(i);
                        if (n.row == Kick && n.startTick == 0)
                        {
                            hasKickOnOne = true;
                            break;
                        }
                    }

                    if (!hasKickOnOne)
                    {
                        // Use the kick row's configured length + a strong downbeat velocity
                        const int kickLen = juce::jmax(1, spec.rows[Kick].lenTicks);
                        const int kickVel = juce::jlimit(1, 127, juce::jmax(110, spec.rows[Kick].velMin));

                        out.add({ Kick, 0, kickLen, kickVel });
                    }
                }

            }
        }
    }
} // namespace

