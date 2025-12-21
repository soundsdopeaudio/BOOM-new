#pragma once
#include <JuceHeader.h>
#include <vector>

namespace boom {
    namespace bass
    {
        // Rhythm-focused style spec for Bass (NOT the 808 engine).
        struct StyleSpec
        {
            const char* name;             // "trap", "wxstie", etc.

            // Subdivision weights (probabilities don't have to sum to 1; we’ll normalize)
            float div_1_4;     // quarter notes
            float div_1_8;     // eighth notes (on-beat)
            float div_off_1_8; // off-beat eighths ("&")
            float div_1_16;    // sixteenth notes
            float div_1_8T;    // eighth-note triplets
            float div_1_16T;   // sixteenth-note triplets

            // Groove feel / density
            float syncopationProb;   // chance to prefer off-beats / anticipations (0..1)
            float swingPct;          // 50 = straight; >50 leans later (e.g., 56 = mild swing)
            float restDensityMin;    // min % of empty grid per bar (0..1)
            float restDensityMax;    // max % of empty grid per bar (0..1)

            // Phrasing and variation
            int   smallVarEveryBars; // small mutations this often
            int   bigVarEveryBars;   // stronger mutations (fills, flips) this often
            int   maxHitsPerBar;     // hard cap to keep the style airy/dense as intended

            // Meter projection hints (how we map the style into non-4/4)
            // Use “cells” like 3+2+2 for 7/8, or triplet projection for x/8 meters.
            bool  prefersTripletMeters; // styles that love 6/8 / 12/8 projection (R&B, Drill sometimes)
            bool  prefersCellAccents;   // e.g., Reggaeton tresillo / dembow accent cells
            bool  enforceTresillo;      // force 3-3-2 accent blueprint inside 4/4 (scaled to bar)
        };

        // Accessors
        const std::vector<StyleSpec>& allStyles();                 // full list
        const StyleSpec& getStyle(juce::StringRef);  // by name (case-insensitive)
        const StyleSpec& defaultStyle();             // "trap" if not found

        // Convenience for combo boxes (exactly what you bind in the UI)
        juce::StringArray              styleChoices();             // names in a stable, user-facing order

        // Utility: returns normalized weights for the six subdivisions in this order:
        // [1/4, 1/8, off-1/8, 1/16, 1/8T, 1/16T]
        std::array<float, 6>           normalizedSubdivisionWeights(const StyleSpec& s);

        // Utility: return a (cells) accent plan for projecting into odd meters (e.g., 7/8 -> {3,2,2})
        // If meter isn’t odd or no special plan is needed, returns empty.
        std::vector<int>               defaultAccentCellsForMeter(int numerator, int denominator);

    }
} // namespace boom::bass

