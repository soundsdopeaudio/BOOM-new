// DrumGenerator.h
#pragma once
#include <vector>
#include <unordered_map>
#include <random>
#include <mutex>
#include <string>   
#include "DrumStyles.h"   // define DrumPattern / DrumNote used by this header
#include "JuceHeader.h"
// NOTE: do NOT include PluginProcessor.h here to avoid circular includes

namespace boom {
    namespace drums {

        // Forward-declare GenerationSpec so it can be used in the prototype below.
        struct GenerationSpec;

        // API
        void registerDefaultTemplateBanks(); // call this once at startup to populate banks
        const std::vector<std::vector<std::string>>& getPatternBank(const std::string& style);

        // Forward-only monotonic seed generator (endless)
        uint32_t getNextGlobalSeed();

        // Main generation function: returns a pattern according to spec
        DrumPattern generate(const GenerationSpec& spec);

        static constexpr int kDefaultStepsPerBar = 16;     // 16th-grid
        static constexpr int kTicksPerQuarter = 960;       // typical PPQ; change if your project uses different PPQ
        static constexpr int kTicksPer16th = kTicksPerQuarter / 4;
        static constexpr int kTicksPerTriplet16th = kTicksPerQuarter / 6; // triplet 16th

        struct GenerationSpec
        {
            std::string style = "trap";
            int bars = 4;

            bool useTriplets = false;
            int tripletDensity = 0;   // 0..100

            bool useDotted = false;
            int dottedDensity = 0;    // 0..100

            int swingPct = 0;         // 0..100 (applied as tick offset)
            int humanizeTiming = 6;   // 0..100 (scale for jitter)
            int humanizeVelocity = 6; // 0..100

            int seed = -1;            // seed (-1 => use getNextGlobalSeed())

            float templateBlend = 1.0f; // reserved for future blending behavior
        };

        // Simple single-hit structure used internally (converted into DrumPattern entries)
        struct Hit {
            int row;
            int stepIndex;   // 0..(stepsPerBar*bars-1) (16th-grid index by default)
            int lenSteps;    // length  in steps (usually 1 or 2)
            int vel;         // 1..127
        };

        // Style policy - centralized bias control per style
        struct StylePolicy
        {
            float tripletMultiplier = 1.0f;    // multiplies UI tripletPct
            float dottedMultiplier = 1.0f;     // multiplies UI dottedPct
            float restAddPct = 0.0f;           // adds X% extra rest for this style (wxstie)
            float swingBias = 0.0f;            // extra swing bias per style (0..1)
            float fillFreq = 0.08f;            // chance to inject fill at phrase end (0..1)
            float noveltyDecay = 0.01f;        // how quickly novelty penalty decays per generation
        };

        // Template bank API:
        // Each template is represented as a vector<int> of length (kRows * stepsPerBar).
        // Velocities 0 => no-hit, >0 => velocity
        void registerTemplateBank(const juce::String& style, const std::vector<std::vector<int>>& templates);
        void clearTemplateBank(const juce::String& style);
        void registerDefaultTemplateBanks();
        void clearAllTemplateBanks();

        // Primary generator
        void generateByStyle(const juce::String& style,
            int bars,
            int restPct,
            int dottedPct,
            int tripletPct,
            int swingPct,
            int seed,
            boom::drums::DrumPattern& out,
            bool useTemplateBank = true,
            bool useMarkov = false);

        // Markov generator (exposed)
        void generateByMarkov(const juce::String& style,
            int bars,
            int restPct,
            int dottedPct,
            int tripletPct,
            int swingPct,
            int seed,
            boom::drums::DrumPattern& out);

        // Forward-only global seed
        uint32_t getNextGlobalSeed();

        // New pipeline helpers (exposed for testing if needed)
        void generatePhrasePlan(const juce::String& style,
            int bars,
            int restPct,
            int dottedPct,
            int tripletPct,
            int swingPct,
            int seed,
            std::vector<std::vector<int>>& outMatrix /*[kRows][bars*stepsPerBar]*/,
            bool useTemplateBank = true);

        void mutateBar(std::vector<std::vector<int>>& matrix,
            int barIndex,
            float flipProb,
            float shiftProb,
            std::mt19937& rng);

        void decorateBar(std::vector<std::vector<int>>& matrix,
            int barIndex,
            float ghostProb,
            std::mt19937& rng);

        void applyHumanize(boom::drums::DrumPattern& out,
            int humanizeAmt,    // 0..100
            int swingPct);      // 0..100

        // Novelty helpers
        uint32_t computeTemplateHash(const std::vector<int>& flat, int bars);
        float noveltyPenaltyForHash(uint32_t h);
        void noteGeneratedHash(uint32_t h);

    } // namespace drums
} // namespace boom

