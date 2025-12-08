#include "DrumStyles.h"
#include "GridUtils.h"
#include "PluginProcessor.h"
#include <random>
#include <cstdint>

namespace boom {
    namespace drums
    {
        // Utility
        static inline int clamp01i(int v) { return juce::jlimit(0, 100, v); }
        static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

        // A handy builder for evenly-weighted pulses
        static void pulses(RowSpec& rs, int every16, float onProb, int velMin = 92, int velMax = 120)
        {
            for (int i = 0; i < kStepsPerBar; i++)
                rs.p[i] = ((i % every16) == 0) ? onProb : 0.0f;
            rs.velMin = velMin; rs.velMax = velMax;
        }

        // Backbeat helper: strong hits on 2 and 4 (steps 4 and 12 at 16ths)
        static void backbeat(RowSpec& rs, float on = 1.0f, int velMin = 100, int velMax = 127)
        {
            for (int i = 0; i < kStepsPerBar; i++) rs.p[i] = 0.0f;
            rs.p[4] = on;
            rs.p[12] = on;
            rs.velMin = velMin; rs.velMax = velMax;
        }

        // Probability sprinkles for groove
        static void sprinkle(RowSpec& rs, const int* steps, int n, float prob, int velMin, int velMax)
        {
            for (int i = 0; i < n; i++)
                rs.p[juce::jlimit(0, kStepsPerBar - 1, steps[i])] = juce::jmax(rs.p[steps[i]], prob);
            rs.velMin = juce::jmin(rs.velMin, velMin);
            rs.velMax = juce::jmax(rs.velMax, velMax);
        }

        // ====== STYLE DEFINITIONS ==================================================

        // Trap: fast hats/rolls, backbeat snare/clap, syncopated kicks, occasional open hat on offbeats.
        static DrumStyleSpec makeTrap()
        {
            DrumStyleSpec s; s.name = "trap";
            s.swingPct = 10; s.tripletBias = 0.25f; s.dottedBias = 0.1f; s.bpmMin = 120; s.bpmMax = 160;

            // Kick: sparse but syncopated base; later randomness fills
            pulses(s.rows[Kick], 4, 0.55f, 95, 120); // quarters as a base chance
            int kAdds[] = { 1,3,6,7,9,11,14,15 }; sprinkle(s.rows[Kick], kAdds, (int)std::size(kAdds), 0.35f, 92, 118);

            // Snare: strong backbeat
            backbeat(s.rows[Snare]);

            // Clap: layered with snare lower prob
            backbeat(s.rows[Clap], 0.6f, 96, 115);

            // Closed hat: strong 1/8 with 1/16 & 1/32 rolls
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.85f : 0.35f);
            s.rows[ClosedHat].rollProb = 0.45f; s.rows[ClosedHat].maxRollSub = 2; // 32nds
            s.rows[ClosedHat].velMin = 75; s.rows[ClosedHat].velMax = 105;

            // Open hat: off-beat splashes
            for (int i = 0; i < kStepsPerBar; i++) s.rows[OpenHat].p[i] = (i % 4 == 2 ? 0.45f : 0.05f);
            s.rows[OpenHat].lenTicks = 36;

            // Perc: light fills
            int pA[] = { 2,10 }; sprinkle(s.rows[Perc], pA, 2, 0.15f, 70, 100);

            return s;
        }

        // Drill (UK/NY): triplet feel, choppy, snares often late (beat 4 of the bar emphasized).
        static DrumStyleSpec makeDrill()
        {
            DrumStyleSpec s; s.name = "drill";
            s.swingPct = 5; s.tripletBias = 0.55f; s.dottedBias = 0.1f; s.bpmMin = 130; s.bpmMax = 145;

            // Kick: choppy syncopations
            for (int i = 0; i < kStepsPerBar; i++) s.rows[Kick].p[i] = (i % 4 == 0 ? 0.6f : 0.0f);
            int ks[] = { 3,5,7,8,11,13,15 }; sprinkle(s.rows[Kick], ks, 7, 0.4f, 95, 120);

            // Snare: strong on step 12 (beat 4), with step 4 reduced (late snare feel)
            for (int i = 0; i < kStepsPerBar; i++) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].p[12] = 1.0f; s.rows[Snare].p[4] = 0.2f;
            s.rows[Snare].velMin = 100; s.rows[Snare].velMax = 127;

            // Clap layered lighter
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 90; s.rows[Clap].velMax = 115;

            // Hats: triplet bias, sparse 1/8 with many micro-rolls
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.6f : 0.25f);
            s.rows[ClosedHat].rollProb = 0.6f; s.rows[ClosedHat].maxRollSub = 3; // triplet rolls
            s.rows[ClosedHat].velMin = 70; s.rows[ClosedHat].velMax = 100;

            // Open hat: gated splashes before/after snares
            int oh[] = { 11,13 }; sprinkle(s.rows[OpenHat], oh, 2, 0.4f, 80, 105);
            s.rows[OpenHat].lenTicks = 28;

            return s;
        }

        // EDM (house-ish): 4-on-the-floor, claps on 2&4, steady hats on off-beats
        static DrumStyleSpec makeEDM()
        {
            DrumStyleSpec s; s.name = "edm";
            s.swingPct = 0; s.tripletBias = 0.0f; s.dottedBias = 0.05f; s.bpmMin = 120; s.bpmMax = 128;

            pulses(s.rows[Kick], 4, 1.0f, 105, 120); // every quarter
            backbeat(s.rows[Snare], 0.9f, 100, 118);
            backbeat(s.rows[Clap], 0.9f, 96, 115);

            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 1 ? 0.9f : 0.05f); // off-beats
            s.rows[ClosedHat].velMin = 85; s.rows[ClosedHat].velMax = 105;

            s.rows[OpenHat].p[2] = 0.25f;
            s.rows[OpenHat].p[10] = 0.25f; s.rows[OpenHat].lenTicks = 32;

            return s;
        }

        // Reggaeton (dembow): boom-ch-boom-chick pattern (3+3+2 feel)
        static DrumStyleSpec makeReggaeton()
        {
            DrumStyleSpec s; s.name = "reggaeton";
            s.swingPct = 0; s.tripletBias = 0.15f; s.dottedBias = 0.1f; s.bpmMin = 85; s.bpmMax = 105;

            // Kick: 1 and the "and" of 2 (approx), repeat (simplified dembow backbone)
            for (int i = 0; i < kStepsPerBar; i++) s.rows[Kick].p[i] = 0.0f;
            s.rows[Kick].p[0] = 0.95f;
            s.rows[Kick].p[6] = 0.65f; // "a" of 2 in 16ths (3+3+2)
            s.rows[Kick].p[8] = 0.55f; // beat 3 sometimes
            s.rows[Kick].velMin = 96; s.rows[Kick].velMax = 118;

            // Snare/Clap: the characteristic offbeats
            for (int i = 0; i < kStepsPerBar; i++) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].p[4] = 0.85f;
            s.rows[Snare].p[10] = 0.95f; // the 'chick' feel
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 90; s.rows[Clap].velMax = 112;

            // Hats: light, steady
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.55f : 0.2f);

            // Open hat: occasional end-of-bar
            s.rows[OpenHat].p[15] = 0.35f;

            return s;
        }

        // R&B (modern): laid-back swing, gentle ghost notes
        static DrumStyleSpec makeRNB()
        {
            DrumStyleSpec s; s.name = "r&b";
            s.swingPct = 18; s.tripletBias = 0.2f; s.dottedBias = 0.15f; s.bpmMin = 70; s.bpmMax = 95;

            backbeat(s.rows[Snare], 0.95f, 98, 118);
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 85; s.rows[Clap].velMax = 108;

            // Kick: fewer, deeper syncopations
            for (int i = 0; i < kStepsPerBar; i++) s.rows[Kick].p[i] = 0.0f;
            int ks[] = { 0,3,8,11,14 }; sprinkle(s.rows[Kick], ks, 5, 0.5f, 92, 115);

            // Hats: swung 1/8 with ghost 1/16
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.7f : 0.25f);
            s.rows[ClosedHat].velMin = 70; s.rows[ClosedHat].velMax = 96;
            s.rows[ClosedHat].rollProb = 0.2f; s.rows[ClosedHat].maxRollSub = 2;

            s.rows[OpenHat].p[2] = 0.2f; s.rows[OpenHat].p[10] = 0.2f; s.rows[OpenHat].lenTicks = 28;
            return s;
        }

        // Pop: clean backbeat, on-grid hats, tasteful fills
        static DrumStyleSpec makePop()
        {
            DrumStyleSpec s; s.name = "pop";
            s.swingPct = 5; s.tripletBias = 0.05f; s.dottedBias = 0.05f; s.bpmMin = 90; s.bpmMax = 120;

            backbeat(s.rows[Snare], 0.95f, 98, 118);
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 90; s.rows[Clap].velMax = 112;
            pulses(s.rows[Kick], 4, 0.85f, 98, 118);
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.8f : 0.2f);
            s.rows[OpenHat].p[2] = 0.25f; s.rows[OpenHat].p[10] = 0.25f; s.rows[OpenHat].lenTicks = 30;
            return s;
        }

        // Rock: strong 2 & 4 backbeat, hats straight 8ths, occasional open hat on &4
        static DrumStyleSpec makeRock()
        {
            DrumStyleSpec s; s.name = "rock";
            s.swingPct = 0; s.tripletBias = 0.0f; s.dottedBias = 0.0f; s.bpmMin = 90; s.bpmMax = 140;

            backbeat(s.rows[Snare], 1.0f, 100, 124);
            pulses(s.rows[Kick], 4, 0.75f, 98, 118);
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.95f : 0.0f);
            s.rows[OpenHat].p[7] = 0.35f; s.rows[OpenHat].p[15] = 0.35f;
            return s;
        }

        // Wxstie (modern West Coast bounce): sparser hats, swingy pocket, syncopated kicks, claps/snare layered
        static DrumStyleSpec makeWxstie()
        {
            DrumStyleSpec s; s.name = "wxstie";
            s.swingPct = 20; s.tripletBias = 0.15f; s.dottedBias = 0.1f; s.bpmMin = 85; s.bpmMax = 105;

            // Backbeat solid, clap layered
            backbeat(s.rows[Snare], 0.95f, 100, 124);
            s.rows[Clap] = s.rows[Snare]; s.rows[Clap].velMin = 92; s.rows[Clap].velMax = 114;

            // Kicks: “bounce” placements
            int ks[] = { 0,3,7,8,11,15 }; sprinkle(s.rows[Kick], ks, 6, 0.55f, 95, 118);

            // Hats: spaced 8ths with gaps; ghost 16ths occasionally
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.55f : 0.15f);
            s.rows[ClosedHat].rollProb = 0.25f; s.rows[ClosedHat].maxRollSub = 2;

            // Open hat: upbeat splashes
            s.rows[OpenHat].p[2] = 0.25f; s.rows[OpenHat].p[10] = 0.25f; s.rows[OpenHat].lenTicks = 28;

            // Perc: light fills for bounce
            int pc[] = { 6,14 }; sprinkle(s.rows[Perc], pc, 2, 0.2f, 75, 100);
            return s;
        }

        // Hiphop (general, non-trap): simpler hats, steady backbeat, less rolls
        static DrumStyleSpec makeHipHop()
        {
            DrumStyleSpec s; s.name = "hip hop";
            s.swingPct = 8; s.tripletBias = 0.05f; s.dottedBias = 0.05f; s.bpmMin = 85; s.bpmMax = 100;

            backbeat(s.rows[Snare], 0.95f, 98, 118);
            pulses(s.rows[Kick], 4, 0.7f, 96, 115);
            for (int i = 0; i < kStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.75f : 0.05f);
            s.rows[OpenHat].p[10] = 0.2f; s.rows[OpenHat].lenTicks = 28;
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

        // === Generator =============================================================

        static int randRange(std::mt19937& rng, int a, int b) // inclusive
        {
            std::uniform_int_distribution<int> d(a, b); return d(rng);
        }
        static float rand01(std::mt19937& rng)
        {
            std::uniform_real_distribution<float> d(0.0f, 1.0f); return d(rng);
        }

        void generate(const DrumStyleSpec& spec, int bars,
            int restPct, int dottedPct, int tripletPct, int swingPct,
            int seed, DrumPattern& out)
        {
            out.clearQuick();
            bars = juce::jlimit(1, 16, bars);

            std::mt19937 rng(static_cast<std::uint32_t>(
                seed == -1 ? juce::Time::getMillisecondCounter() : seed));

            // Normalize user/global biases
            const float restBias = clamp01i(restPct) / 100.0f;
            const float dottedFeel = juce::jlimit(0.0f, 1.0f, spec.dottedBias + (dottedPct / 100.0f) * 0.75f);
            const float tripletFeel = juce::jlimit(0.0f, 1.0f, spec.tripletBias + (tripletPct / 100.0f) * 0.75f);
            const float swingFeel = juce::jlimit(0.0f, 100.0f, (float)swingPct);
            const float swingAsFrac = swingFeel * 0.01f;

            // Use helper to compute ticks per 16th from canonical PPQ/cells-per-beat
            const int  ticksPer16 = boom::grid::ticksPerStepFromPpq(BoomAudioProcessor::PPQ, 4);
            const int  barTicks = ticksPer16 * kStepsPerBar;

            // For each bar + row + step: Bernoulli on row probability -> create hit
            for (int bar = 0; bar < bars; ++bar)
            {
                for (int row = 0; row < NumRows; ++row)
                {
                    const RowSpec& rs = spec.rows[row];

                    for (int step = 0; step < kStepsPerBar; ++step)
                    {
                        // Base probability
                        float p = rs.p[step];

                        // Apply global dotted/triplet pushes:
                        // If step falls on dotted (3/8 spacing -> steps 3,7,11,15) give it a nudge.
                        if (dottedFeel > 0.0f)
                        {
                            const bool dottedPos = (step % 4) == 3;
                            if (dottedPos) p = juce::jmin(1.0f, p + 0.35f * dottedFeel);
                        }
                        // For triplet: nudge steps near 1/12 divisions (approx by giving odd 1/8-positions a bump)
                        if (tripletFeel > 0.0f && (step % 2 == 1))
                            p = juce::jmin(1.0f, p + 0.25f * tripletFeel);

                        // Rest density pulls probability down
                        p *= (1.0f - restBias);

                        if (rand01(rng) <= p)
                        {
                            // Spawn a hit
                            int vel = randRange(rng, rs.velMin, rs.velMax);

                            // Basic swing on even 8th offbeats for hats/perc/openhat
                            int startTick = bar * barTicks + step * ticksPer16;

                            if ((row == ClosedHat || row == OpenHat || row == Perc) && (step % 2 == 1))
                            {
                                int swingTicks = (int)std::round((ticksPer16 * 0.5f) * swingAsFrac); // swing 8ths by up to 50% of a 16th
                                startTick += swingTicks;
                            }

                            int len = rs.lenTicks;

                            // Occasional micro-rolls (esp. hats)
                            if (rs.rollProb > 0.0f && rand01(rng) < rs.rollProb && rs.maxRollSub > 1)
                            {
                                // Choose sub = 2 (32nds) or 3 (triplets at ~ 1/24th multiples)
                                const int sub = juce::jlimit(2, rs.maxRollSub, randRange(rng, 2, rs.maxRollSub));
                                const int divTicks = (sub == 2 ? ticksPer16 / 2 : 16); // 12th-of-bar ? 8 ticks; we’ll use 16 ticks ~ triplet-ish
                                const int hits = randRange(rng, 2, 4);
                                for (int r = 0; r < hits; ++r)
                                {
                                    int st = startTick + r * divTicks;
                                    if (st < (bar + 1) * barTicks)
                                        out.add({ row, st, juce::jmax(12, len - 4 * r), juce::jlimit(40,127, vel - 3 * r) });
                                }
                            }
                            else
                            {
                                out.add({ row, startTick, len, vel });
                            }
                        }
                    }

                    // Lock backbeat if requested (ensure at least one snare/clap on 2 & 4)
                    if (spec.lockBackbeat && (row == Snare || row == Clap))
                    {
                        const int b2 = bar * barTicks + 4 * ticksPer16;
                        const int b4 = bar * barTicks + 12 * ticksPer16;

                        bool has2 = false, has4 = false;
                        for (auto& n : out)
                            if (n.row == row && (n.startTick == b2 || n.startTick == b4)) { if (n.startTick == b2) has2 = true; else has4 = true; }

                        if (!has2) out.add({ row, b2, spec.rows[row].lenTicks, randRange(rng, spec.rows[row].velMin, spec.rows[row].velMax) });
                        if (!has4) out.add({ row, b4, spec.rows[row].lenTicks, randRange(rng, spec.rows[row].velMin, spec.rows[row].velMax) });
                    }
                }
            }
        }
    }
} // namespace
