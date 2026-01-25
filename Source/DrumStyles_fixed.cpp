#include "DrumStyles.h"
#include "GridUtils.h"
#include "PluginProcessor.h"
#include "DrumStyleEnforcer.h"
#include "DrumStyleProfileResolver.h"
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
                rs.p[juce::jlimit(0, kMaxStepsPerBar - 1, steps[i])] = juce::jmax<float>(rs.p[steps[i]], prob);
            rs.velMin = juce::jmin(rs.velMin, velMin);
            rs.velMax = juce::jmax(rs.velMax, velMax);
        }

        // ====== STYLE DEFINITIONS ==================================================

        // Trap: fast hats/rolls, backbeat snare/clap, syncopated kicks, occasional open hat on offbeats.
        static DrumStyleSpec makeTrap()
        {
            DrumStyleSpec s; s.name = "trap";
            s.swingPct = 10; s.tripletBias = 0.25f; s.dottedBias = 0.1f; s.bpmMin = 120; s.bpmMax = 160;
            s.lockBackbeat = false; // Trap uses snare on beat 3 only, not 2 & 4

            // Kick: sparse but syncopated base; later randomness fills
            pulses(s.rows[Kick], 4, 0.55f, 95, 120); // quarters as a base chance
            int kAdds[] = { 1,3,6,7,9,11,14,15 }; sprinkle(s.rows[Kick], kAdds, (int)std::size(kAdds), 0.35f, 92, 118);

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

            // Open hat: off-beat splashes
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[OpenHat].p[i] = (i % 4 == 2 ? 0.45f : 0.05f);
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
            s.lockBackbeat = false; // Drill avoids clean 2 & 4 backbeat

            // Kick: choppy syncopations
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Kick].p[i] = (i % 4 == 0 ? 0.6f : 0.0f);
            int ks[] = { 3,5,7,8,11,13,15 }; sprinkle(s.rows[Kick], ks, 7, 0.4f, 95, 120);

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

            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 1 ? 0.9f : 0.05f); // off-beats
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
            s.lockBackbeat = false; // Reggaeton uses dembow pattern, not backbeat

            // Kick: 1 and the "and" of 2 (approx), repeat (simplified dembow backbone)
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Kick].p[i] = 0.0f;
            s.rows[Kick].p[0] = 0.95f;
            s.rows[Kick].p[7] = 0.85f; // "a" of beat 2 (dembow kick pattern)
            s.rows[Kick].velMin = 96; s.rows[Kick].velMax = 118;

            // Snare/Clap: dembow accent on "2-and" (step 6)
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].p[6] = 1.0f;   // "2-and"
            s.rows[Snare].velMin = 98;
            s.rows[Snare].velMax = 120;

            // Clap layered lighter
            s.rows[Clap] = s.rows[Snare];
            s.rows[Clap].velMin = 90;
            s.rows[Clap].velMax = 112;

            // Hats: light, steady
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.55f : 0.2f);

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
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[Kick].p[i] = 0.0f;
            int ks[] = { 0,3,8,11,14 }; sprinkle(s.rows[Kick], ks, 5, 0.5f, 92, 115);

            // Hats: swung 1/8 with ghost 1/16
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.7f : 0.25f);
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
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.8f : 0.2f);
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
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.95f : 0.0f);
            s.rows[OpenHat].p[7] = 0.35f; s.rows[OpenHat].p[15] = 0.35f;
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
            sprinkle(s.rows[Kick], kCore, (int)std::size(kCore), 0.35f, 88, 118);
            s.rows[Kick].velMin = 90; s.rows[Kick].velMax = 125;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Snare].p[i] = 0.0f;
            s.rows[Snare].p[14] = 0.10f;
            s.rows[Snare].velMin = 95; s.rows[Snare].velMax = 127;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Clap].p[i] = 0.0f;
            s.rows[Clap].p[4] = 0.15f;
            s.rows[Clap].p[12] = 0.15f;
            s.rows[Clap].velMin = 85; s.rows[Clap].velMax = 112;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[ClosedHat].p[i] = 0.0f;
            int hSparse[] = { 0, 2, 5, 7, 10, 13, 15 };
            sprinkle(s.rows[ClosedHat], hSparse, (int)std::size(hSparse), 0.28f, 70, 98);
            s.rows[ClosedHat].velMin = 68; s.rows[ClosedHat].velMax = 98;
            s.rows[ClosedHat].rollProb = 0.22f; s.rows[ClosedHat].maxRollSub = 2;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[OpenHat].p[i] = 0.0f;
            s.rows[OpenHat].p[2] = 0.12f;
            s.rows[OpenHat].p[10] = 0.12f;
            s.rows[OpenHat].p[15] = 0.08f;
            s.rows[OpenHat].lenTicks = 32;
            s.rows[OpenHat].velMin = 70; s.rows[OpenHat].velMax = 105;

            for (int i = 0; i < kMaxStepsPerBar; ++i) s.rows[Perc].p[i] = 0.0f;
            int pA[] = { 1, 3, 6, 9, 11, 14 };
            sprinkle(s.rows[Perc], pA, (int)std::size(pA), 0.18f, 60, 95);

            return s;
        }


        // Hiphop (general, non-trap): simpler hats, steady backbeat, less rolls
        static DrumStyleSpec makeHipHop()
        {
            DrumStyleSpec s; s.name = "hip hop";
            s.swingPct = 8; s.tripletBias = 0.05f; s.dottedBias = 0.05f; s.bpmMin = 85; s.bpmMax = 100;

            backbeat(s.rows[Snare], 0.95f, 98, 118);
            pulses(s.rows[Kick], 4, 0.7f, 96, 115);
            for (int i = 0; i < kMaxStepsPerBar; i++) s.rows[ClosedHat].p[i] = (i % 2 == 0 ? 0.75f : 0.05f);
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

        // ============================================================================
// Preferred snare templates (NOT mandatory)
// Uses the EXACT time signature string (including additive like "3+2/8").
// We boost snare/clap probabilities at time-signature-aware positions.
// ============================================================================

        static inline int clampStep16(int s) { return juce::jlimit(0, boom::drums::kMaxStepsPerBar - 1, s); }

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

        // If not additive, split large numerators into musical groups (your “2+3”, “3+4”, etc. idea)
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

        // Convert a 1-based “beat index” (in numerator units) to step16.
        // Example 6/8 beat 4 => (4-1)/6 = 0.5 bar => step 8.
        static inline int beatIndex1BasedToStep16(int beatIndex1Based, int tsNum)
        {
            tsNum = juce::jmax(1, tsNum);
            const float beatPos0Based = (float)juce::jlimit(1, tsNum, beatIndex1Based) - 1.0f;
            const float t = beatPos0Based / (float)tsNum; // [0..1)
            const int step = (int)juce::roundToInt(t * (float)boom::drums::kMaxStepsPerBar);
            return clampStep16(step);
        }

        static inline void addUniqueStep(juce::Array<int>& a, int s)
        {
            s = clampStep16(s);
            if (!a.contains(s)) a.add(s);
        }

        // Slight mutation so it “thinks” and doesn’t repeat templates forever
        static void maybeMutatePreferredSteps(juce::Array<int>& steps, juce::Random& rng)
        {
            if (steps.size() < 2) return;
            if (rng.nextFloat() > 0.22f) return; // ~1 in 5

            const int idx = rng.nextInt(steps.size());
            const int dir = rng.nextBool() ? 1 : -1;
            int s = clampStep16(steps[idx] + dir);

            if (!steps.contains(s))
                steps.set(idx, s);
        }

        // Build template pool for the EXACT time signature (shared across styles)
        // using your notes when available; fallback heuristic if not.
        static void buildBaseTemplatesForTimeSig(const juce::String& timeSigText,
            int tsNum, int tsDen,
            const juce::Array<int>& groups,
            juce::Array<juce::Array<int>>& outTemplates)
        {
            juce::ignoreUnused(tsDen);
            outTemplates.clearQuick();

            auto ts = timeSigText.trim().toLowerCase();

            auto makeFromBeats = [&](std::initializer_list<int> beats1Based)
                {
                    juce::Array<int> t;
                    for (int b : beats1Based)
                        addUniqueStep(t, beatIndex1BasedToStep16(b, tsNum));
                    if (t.size() >= 2) outTemplates.add(t);
                };

            // ----------------------
            // YOUR NOTES (exact)
            // ----------------------
            if (ts == "3/4")
            {
                makeFromBeats({ 2, 3 });
                makeFromBeats({ 3, 2 });
                makeFromBeats({ 3, 3 }); // doubles become “strong 3”
                makeFromBeats({ 2, 2 }); // strong 2 feel
                makeFromBeats({ 3, 1 }); // late + bar start (rare but usable)
                return;
            }

            if (ts == "6/8")
            {
                // you: “typically on beat 4”
                makeFromBeats({ 4, 6 });
                makeFromBeats({ 4, 5 });
                makeFromBeats({ 4, 3 });
                makeFromBeats({ 4, 2 });
                makeFromBeats({ 2, 4 });
                return;
            }

            if (ts == "7/8")
            {
                // you: 3,5,7 OR 2,4,7 OR 3&7
                makeFromBeats({ 3, 7 });
                makeFromBeats({ 5, 7 });
                makeFromBeats({ 3, 5 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 2, 7 });
                return;
            }

            if (ts == "5/4")
            {
                // you: 3&5, 2&5
                makeFromBeats({ 3, 5 });
                makeFromBeats({ 2, 5 });
                makeFromBeats({ 3, 4 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 5, 3 });
                return;
            }

            if (ts == "9/8")
            {
                // you: 3,4,7 + waltz 3,6,9 etc
                makeFromBeats({ 3, 7 });
                makeFromBeats({ 4, 7 });
                makeFromBeats({ 4, 9 });
                makeFromBeats({ 5, 9 });
                makeFromBeats({ 3, 6 }); // waltz-ish
                return;
            }

            if (ts == "12/8")
            {
                // you: 2,5,8,10 triplet feel (we’ll also give 2&8 variants)
                makeFromBeats({ 2, 8 });
                makeFromBeats({ 5, 10 });
                makeFromBeats({ 2, 5 });
                makeFromBeats({ 8, 10 });
                makeFromBeats({ 2, 10 });
                return;
            }

            if (ts == "5/8")
            {
                // you: 3&7 / 4&7 doesn’t fit 5/8 wording; interpret as “3 & 5” type pockets
                makeFromBeats({ 3, 5 });
                makeFromBeats({ 4, 5 });
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 4, 4 });
                makeFromBeats({ 5, 3 });
                return;
            }

            if (ts == "10/8")
            {
                // you: 2&4 OR 2& and 4&
                makeFromBeats({ 2, 4 });
                makeFromBeats({ 2, 5 });
                makeFromBeats({ 4, 8 });
                makeFromBeats({ 2, 8 });
                makeFromBeats({ 5, 10 });
                return;
            }

            if (ts == "11/8")
            {
                // you: 1,4,7 OR 1,4,7,10 OR 5&11 OR 2,4,6,8,10
                makeFromBeats({ 4, 7 });
                makeFromBeats({ 1, 7 });
                makeFromBeats({ 7, 10 });
                makeFromBeats({ 5, 11 });
                makeFromBeats({ 2, 6 });
                return;
            }

            if (ts == "13/8")
            {
                // you: 2,4,6,8,10,12 OR 6&12 etc
                makeFromBeats({ 6, 12 });
                makeFromBeats({ 6, 13 });
                makeFromBeats({ 7, 12 });
                makeFromBeats({ 7, 13 });
                makeFromBeats({ 4, 10 });
                return;
            }

            if (ts == "15/8")
            {
                // you: 3,6,9,12,15 etc, 7&15, 8&15, 7&14, 8&14
                makeFromBeats({ 6, 15 });
                makeFromBeats({ 7, 15 });
                makeFromBeats({ 8, 15 });
                makeFromBeats({ 7, 14 });
                makeFromBeats({ 8, 14 });
                return;
            }

            if (ts == "17/8")
            {
                // you: 1,5,9,13 OR 9&17 or 8&17
                makeFromBeats({ 9, 17 });
                makeFromBeats({ 8, 17 });
                makeFromBeats({ 5, 13 });
                makeFromBeats({ 1, 9 });
                makeFromBeats({ 13, 17 });
                return;
            }

            // ----------------------
            // FALLBACK (for everything else on your list)
            // Use groups (additive if present), else heuristic split.
            // We will place “one in each chunk” + a late anchor, making 10 templates total.
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

            // 10 templates (all “>=2 hits”)
            // 1) first end + last end
            makeFromBeats({ groupEndBeat(0), groupEndBeat(ends.size() - 1) });
            // 2) mid first + end last
            makeFromBeats({ groupMidBeat(0), groupEndBeat(ends.size() - 1) });
            // 3) end first + mid last
            makeFromBeats({ groupEndBeat(0), groupMidBeat(ends.size() - 1) });
            // 4) mid first + mid last
            makeFromBeats({ groupMidBeat(0), groupMidBeat(ends.size() - 1) });

            // 5) if 2+ groups: end of group1 + end of group2
            if (ends.size() >= 2) makeFromBeats({ groupEndBeat(0), groupEndBeat(1) });
            // 6) if 3+ groups: end group1 + end group3
            if (ends.size() >= 3) makeFromBeats({ groupEndBeat(0), groupEndBeat(2) });

            // 7) late pocket: last end-1 + last end
            makeFromBeats({ juce::jmax(1, groupEndBeat(ends.size() - 1) - 1), groupEndBeat(ends.size() - 1) });
            // 8) late pocket: last mid + last end
            makeFromBeats({ groupMidBeat(ends.size() - 1), groupEndBeat(ends.size() - 1) });

            // 9) “driving”: two hits in last group
            {
                int lastStart = (ends.size() >= 2 ? ends[ends.size() - 2] + 1 : 1);
                int lastEnd = ends[ends.size() - 1];
                int a = juce::jlimit(1, tsNum, lastStart);
                int b = juce::jlimit(1, tsNum, lastEnd);
                makeFromBeats({ a, b });
            }

            // 10) “alt”: end group1 + (last end-2)
            makeFromBeats({ groupEndBeat(0), juce::jmax(1, groupEndBeat(ends.size() - 1) - 2) });
        }

        static void applyPreferredSnareBoostsByTimeSigText(DrumStyleSpec& s,
            const juce::String& timeSigText,
            int seed)
        {
            int tsNum = 4, tsDen = 4;
            juce::Array<int> groups;
            parseTimeSigText(timeSigText, tsNum, tsDen, groups);

            if (groups.size() == 0 && timeSigText.containsChar('+'))
            {
                // safety: parse additive again if needed
                parseTimeSigText(timeSigText, tsNum, tsDen, groups);
            }
            if (groups.size() == 0 && !timeSigText.containsChar('+'))
            {
                // use heuristic split when non-additive
                groups = splitBeatsIntoGroupsHeuristic(tsNum);
            }

            juce::Random rng(seed == -1 ? (int)juce::Time::getMillisecondCounter() : seed);

            juce::Array<juce::Array<int>> templates;
            buildBaseTemplatesForTimeSig(timeSigText, tsNum, tsDen, groups, templates);
            if (templates.size() == 0) return;

            // Pick a template
            int pick = rng.nextInt(templates.size());
            auto steps = templates[pick];

            // mutate sometimes
            maybeMutatePreferredSteps(steps, rng);

            // Style-specific boost strength (still preferred, not forced)
            const auto n = s.name.trim().toLowerCase();
            float boost = 0.82f;
            if (n == "wxstie") boost = 0.90f;
            if (n == "hip hop" || n == "hiphop") boost = 0.88f;
            if (n == "pop" || n == "rock") boost = 0.92f;
            if (n == "edm") boost = 0.94f;
            if (n == "r&b" || n == "rnb") boost = 0.86f;
            if (n == "reggaeton") boost = 0.94f;
            if (n == "trap" || n == "drill") boost = 0.96f;

            for (int i = 0; i < steps.size(); ++i)
            {
                const int st = clampStep16(steps[i]);
                s.rows[Snare].p[st] = juce::jmax<float>(s.rows[Snare].p[st], boost);

                // clap layer slightly lower so it doesn’t become double-snare
                const float clapBoost = juce::jlimit(0.0f, 1.0f, boost * 0.70f);
                s.rows[Clap].p[st] = juce::jmax<float>(s.rows[Clap].p[st], clapBoost);
            }
        }

        // Public API
        DrumStyleSpec getSpecForTimeSigText(const juce::String& styleName,
            const juce::String& timeSigText,
            int seed)
        {
            DrumStyleSpec s = getSpec(styleName);
            applyPreferredSnareBoostsByTimeSigText(s, timeSigText, seed);
            return s;
        }

        DrumStyleSpec getSpecForTimeSig(const juce::String& styleName, int tsNum, int tsDen, int seed)
        {
            // numeric fallback cannot represent additive ordering; we convert to a plain "N/D"
            const juce::String tsText = juce::String(tsNum) + "/" + juce::String(tsDen);
            return getSpecForTimeSigText(styleName, tsText, seed);
        }

                // Helper functions removed (duplicates)
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
            const float swingFeel = juce::jlimit(0.0f, 100.0f, (float)swingPct);
            const float swingAsFrac = swingFeel * 0.01f;

            // Use helper to compute ticks per 16th from canonical PPQ/cells-per-beat
            const int  ticksPer16 = boom::grid::ticksPerStepFromPpq(BoomAudioProcessor::PPQ, 4);
            const int  barTicks = ticksPer16 * kMaxStepsPerBar;

            // For each bar + row + step: Bernoulli on row probability -> create hit
            for (int bar = 0; bar < bars; ++bar)
            {
                for (int row = 0; row < NumRows; ++row)
                {
                    const RowSpec& rs = spec.rows[row];

                    for (int step = 0; step < kMaxStepsPerBar; ++step)
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
// This fixes:
//  - "every note becomes triplet/dotted" (we no longer boost hit probability)
//  - "OpenHat never gets triplets" (no more step%2 gate)
// -----------------------------------------------------------------
            {
                const float tripletBase = clamp01i(tripletPct) / 100.0f;
                const float dottedBase = clamp01i(dottedPct) / 100.0f;

                if (tripletBase > 0.0f || dottedBase > 0.0f)
                {
                    const int ticksPerBeat = ticksPer16 * 4;               // 1 beat = 4 sixteenths
                    const int tripletTicks = juce::jmax(1, ticksPerBeat / 3); // 3 equal parts of a beat

                    for (int i = 0; i < out.size(); ++i)
                    {
                        auto& n = out.getReference(i);

                        const bool hatLike = (n.row == ClosedHat || n.row == OpenHat || n.row == Perc);

                        // Weight hats/openhat/perc heavier so it sounds musical
                        float tChance = tripletBase * (hatLike ? 1.25f : 0.35f);
                        float dChance = dottedBase * (hatLike ? 1.10f : 0.55f);

                        tChance = juce::jlimit(0.0f, 1.0f, tChance);
                        dChance = juce::jlimit(0.0f, 1.0f, dChance);

                        // --- Triplet timing conversion (snap within beat) ---
                        if (tChance > 0.0f && rand01(rng) < tChance)
                        {
                            const int beatStart = (n.startTick / ticksPerBeat) * ticksPerBeat;
                            const int posInBeat = n.startTick - beatStart;

                            int triIndex = (int)std::round((double)posInBeat / (double)tripletTicks);
                            triIndex = juce::jlimit(0, 2, triIndex);

                            n.startTick = beatStart + triIndex * tripletTicks;
                        }

                        // --- Dotted length conversion (1.5x length, clamped) ---
                        if (dChance > 0.0f && rand01(rng) < dChance)
                        {
                            const int newLen = (int)std::round((double)n.lenTicks * 1.5);
                            // Clamp to avoid ridiculous overlaps; allow up to 2 beats
                            n.lenTicks = juce::jlimit(6, ticksPer16 * 8, newLen);
                        }
                    }
                }
            }
        }
    }

} // namespace


