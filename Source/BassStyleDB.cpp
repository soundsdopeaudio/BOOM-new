#include "BassStyleDB.h"
#include <random>

namespace boom {
    namespace bass
    {
        // === Master list (Bass rhythms) ===
        // IMPORTANT: This is for BASS (synth/electric/sub), not the 808 engine.
        // Values are tuned for RHYTHM variety while preserving genre identity.

        static const std::vector<StyleSpec> kStyles = {
            // EDM (House / Pop-EDM): off-beat & 1/8 focus, short notes, moderate syncopation
            { "edm",
            /*divs*/ 0.05f, 0.55f, 0.25f, 0.15f, 0.00f, 0.00f,
            /*groove*/ 0.35f, 50.0f, 0.30f, 0.55f,
            /*phrase*/ 2, 4, 8,
            /*meter*/  false, false, false },

            // Trap (BASS version, not 808-only): mid density, occasional triplet gestures, repetitions
            { "trap",
              0.25f, 0.35f, 0.00f, 0.25f, 0.10f, 0.05f,
              0.40f, 50.0f, 0.25f, 0.55f,
              2, 4, 8,
              false, false, false },

              // Drill: choppier, triplet-leaning, leave space, then bursts
              { "drill",
                0.10f, 0.20f, 0.00f, 0.30f, 0.30f, 0.10f,
                0.45f, 50.0f, 0.30f, 0.60f,
                2, 4, 8,
                true,  false, false },

                // R&B / Neo-soul: swung 1/8s, syncopations, tasteful space
                { "r&b",
                  0.20f, 0.40f, 0.00f, 0.40f, 0.00f, 0.00f,
                  0.35f, 56.0f, 0.25f, 0.55f,
                  2, 4, 8,
                  true,  false, false },

                  // Rock: driving 1/8s, simple syncopation, fills at section edges
                  { "rock",
                    0.15f, 0.70f, 0.00f, 0.15f, 0.00f, 0.00f,
                    0.15f, 50.0f, 0.10f, 0.40f,
                    4, 8, 10,
                    false, false, false },

                    // Reggaeton: dembow/tresillo 3-3-2 feel, catchy motifs, medium syncopation
                    { "reggaeton",
                      0.10f, 0.55f, 0.15f, 0.20f, 0.00f, 0.00f,
                      0.45f, 50.0f, 0.25f, 0.55f,
                      2, 4, 8,
                      false, true,  true  },

                      // Hip-hop (non-trap): repetitive motifs, simple syncopation, occasional pickups
                      { "hip hop",
                        0.25f, 0.55f, 0.00f, 0.20f, 0.00f, 0.00f,
                        0.30f, 52.0f, 0.20f, 0.50f,
                        2, 4, 8,
                        false, false, false },

                        // === Wxstie (Modern West Coast) ===
                        // Sparse, mid-tempo bounce, off-beat awareness, bar-end pickups
                        { "wxstie",
                          0.10f, 0.55f, 0.00f, 0.25f, 0.10f, 0.00f,
                          0.40f, 50.0f, 0.35f, 0.50f,
                          2, 4, 8,
                          false, false, false },
        };

        // Case-insensitive compare helper
        static bool equalsIgnoreCase(juce::StringRef a, juce::StringRef b)
        {
            if (a.isEmpty() || b.isEmpty()) return false;
            return juce::String(a).equalsIgnoreCase(juce::String(b));
        }

        const std::vector<StyleSpec>& allStyles()
        {
            return kStyles;
        }

        const StyleSpec& defaultStyle()
        {
            // Default to "trap"
            for (const auto& s : kStyles) if (equalsIgnoreCase(s.name, "trap")) return s;
            return kStyles.front();
        }

        const StyleSpec& getStyle(juce::StringRef name)
        {
            for (const auto& s : kStyles)
                if (equalsIgnoreCase(s.name, name))
                    return s;
            return defaultStyle();
        }

        juce::StringArray styleChoices()
        {
            juce::StringArray out;
            out.ensureStorageAllocated(kStyles.size());
            for (const auto& s : kStyles) out.add(s.name);
            return out;
        }

        std::array<float, 6> normalizedSubdivisionWeights(const StyleSpec& s)
        {
            std::array<float, 6> w { s.div_1_4, s.div_1_8, s.div_off_1_8, s.div_1_16, s.div_1_8T, s.div_1_16T };
            float sum = 0.0f; for (float v : w) sum += v;
            if (sum <= 0.0f) { for (auto& v : w) v = (1.0f / 6.0f); return w; }
            for (auto& v : w) v = v / sum;
            return w;
        }

        // Simple, musical defaults for projecting styles into odd meters.
        // Examples:
        //  7/8 -> 3+2+2 (or 2+3+2). Here we return 3+2+2 as default.
        //  5/8 -> 3+2
        //  9/8 -> 3+3+3
        // 11/8 -> 3+3+3+2
        // For x/4 meters we return empty (you’ll just use straight accenting).
        std::vector<int> defaultAccentCellsForMeter(int num, int den)
        {
            std::vector<int> cells;
            if (den == 8)
            {
                switch (num)
                {
                case 5: cells = { 3,2 }; break;
                case 7: cells = { 3,2,2 }; break;
                case 9: cells = { 3,3,3 }; break;
                case 11: cells = { 3,3,3,2 }; break;
                case 13: cells = { 3,3,3,2,2 }; break;
                case 15: cells = { 3,3,3,3,3 }; break; // 5x3
                default: break;
                }
            }
            else if (den == 4)
            {
                // Leave empty: generator can accent beats {1, (weak 2), 3, (weak 4)}
            }
            else if (den == 16)
            {
                // Treat as fine-grained; caller can group into (4s) or (3+3+2) if wanted
            }
            return cells;
        }

    }
} // namespace boom::bass
