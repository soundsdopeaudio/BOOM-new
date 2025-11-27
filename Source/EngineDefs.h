#pragma once
#include <JuceHeader.h>

namespace boom
{

    // ===== Style definitions for combo boxes + generation rules =====
    inline const juce::StringArray& styleChoices()
    {
        static const juce::StringArray k =
        {
            "trap", "drill", "edm", "reggaeton", "r&b", "pop", "rock", "wxstie"
        };
        return k;
    }

    // We work on a 16-step grid per bar; your engine uses 24 ticks per 1/16 step.
    // 1 step  = 24 ticks
    // 2 steps = 48 ticks (1/8)
    // rolls use finer subdivs (e.g., 12 ticks = 1/32)
    struct StyleRules
    {
        juce::String name;

        // Snare backbeat positions (in 0..15 step indices) commonly used in this style (per bar)
        juce::Array<int> snareBeats;

        // Base hat density range (percent of 16 steps with hats before rolls/ornaments)
        int hatDensityMin = 50;
        int hatDensityMax = 80;

        // Candidate roll subdivision tick-lengths (BOOM ticks; 24=1/16, 12=1/32, 8≈1/16T, 6=1/64, 4≈1/32T)
        juce::Array<int> hatRollRates;

        // Probability weights (0..100)
        int tripletHatProb = 0;   // chances to use triplet-feel rolls
        int sparseHatProb = 0;   // chances to drop out hats for air
        int offbeatKickProb = 10;  // kick on offbeats
        int ghostSnareProb = 15;  // small grace notes around backbeat
        int tomFillProb = 10;  // end-of-bar tom run

        // Macro form
        bool fourOnFloor = false;   // EDM 4x kick
        bool dembow = false;   // Reggaeton backbone
        bool rockBackbeat = false;   // 1/3 kick, 2/4 snare archetype

        // Variation cadence (bars)
        int varyEveryBarsMin = 2;
        int varyEveryBarsMax = 8;

        // Hat placement flavor
        bool preferOffbeatHats = false; // EDM/offbeat open hats
    };

    inline const StyleRules& rulesForStyle(const juce::String& inName)
    {
        auto match = [](const juce::String& a, const juce::String& b) { return a.trim().equalsIgnoreCase(b); };

        // ---- TRAP ----
        static const StyleRules trap {
            "trap",
                /*snareBeats*/{ 4, 12 },
                /*hatDensity*/    70, 95,
                /*roll rates*/{ 24, 12, 8, 6, 4 },
                /*tripletProb*/   20,
                /*sparseProb*/    10,
                /*offKick*/       20,
                /*ghost*/         20,
                /*tomProb*/       8,
                /*4x*/            false,
                /*dembow*/        false,
                /*rock*/          false,
                /*vary*/          2, 4,
                /*offbeatHats*/   false
        };

        // ---- DRILL ---- (later snare options, more triplet hats, choppier kicks)
        static const StyleRules drill {
            "drill",
                /*snareBeats*/{ 4, 15 },    // allow late hit near end of bar
                /*hatDensity*/    55, 85,
                /*roll rates*/{ 12, 8, 4, 24 }, // prefer ternary/faster
                /*tripletProb*/   50,
                /*sparseProb*/    20,
                /*offKick*/       25,
                /*ghost*/         25,
                /*tomProb*/       6,
                /*4x*/            false,
                /*dembow*/        false,
                /*rock*/          false,
                /*vary*/          2, 4,
                /*offbeatHats*/   false
        };

        // ---- EDM ---- (4-on-floor, offbeat open hats, pre-drop rolls)
        static const StyleRules edm {
            "edm",
                /*snareBeats*/{ 4, 12 },
                /*hatDensity*/    45, 70,
                /*roll rates*/{ 24, 12, 8 },   // occasional short rolls
                /*tripletProb*/   10,
                /*sparseProb*/    10,
                /*offKick*/       5,
                /*ghost*/         5,
                /*tomProb*/       5,
                /*4x*/            true,
                /*dembow*/        false,
                /*rock*/          false,
                /*vary*/          4, 8,
                /*offbeatHats*/   true
        };

        // ---- REGGAETON ---- (dembow backbone + tasteful rolls)
        static const StyleRules reggaeton {
            "reggaeton",
                /*snareBeats*/{ 4, 12 },     // claps/snare on 2/4 but patterning differs
                /*hatDensity*/    40, 70,
                /*roll rates*/{ 24, 12, 8 },
                /*tripletProb*/   10,
                /*sparseProb*/    15,
                /*offKick*/       10,
                /*ghost*/         10,
                /*tomProb*/       12,
                /*4x*/            false,
                /*dembow*/        true,
                /*rock*/          false,
                /*vary*/          4, 8,
                /*offbeatHats*/   false
        };

        // ---- R&B ---- (laid back; ghosted snares; kick pickups)
        static const StyleRules rnb {
            "r&b",
                /*snareBeats*/{ 4, 12 },
                /*hatDensity*/    35, 65,
                /*roll rates*/{ 24, 12 },   // subtler rolls
                /*tripletProb*/   15,
                /*sparseProb*/    25,
                /*offKick*/       15,
                /*ghost*/         35,
                /*tomProb*/       8,
                /*4x*/            false,
                /*dembow*/        false,
                /*rock*/          false,
                /*vary*/          4, 8,
                /*offbeatHats*/   false
        };

        // ---- POP ---- (clean backbeat; tasteful end rolls)
        static const StyleRules pop {
            "pop",
                /*snareBeats*/{ 4, 12 },
                /*hatDensity*/    45, 75,
                /*roll rates*/{ 24, 12 },  // subtle
                /*tripletProb*/   10,
                /*sparseProb*/    10,
                /*offKick*/       10,
                /*ghost*/         10,
                /*tomProb**/      10,
                /*4x*/            false,
                /*dembow*/        false,
                /*rock*/          false,
                /*vary*/          4, 8,
                /*offbeatHats*/   false
        };

        // ---- ROCK ---- (1/3 kick, 2/4 snare; tom fills)
        static const StyleRules rock {
            "rock",
                /*snareBeats*/{ 4, 12 },
                /*hatDensity*/    40, 65,
                /*roll rates*/{ 24 },     // mainly straight, rolls via toms
                /*tripletProb*/   5,
                /*sparseProb*/    5,
                /*offKick*/       10,
                /*ghost*/         5,
                /*tomProb*/       35,         // end fills
                /*4x*/            false,
                /*dembow*/        false,
                /*rock*/          true,
                /*vary*/          4, 8,
                /*offbeatHats*/   false
        };

        // ---- WXSTIE (modern West Coast) ----
        static const StyleRules wxstie {
            "wxstie",
                /*snareBeats*/{ 4, 12 },      // can drift; we’ll vary around these
                /*hatDensity*/    25, 55,         // sparser hats than trap/drill
                /*roll rates*/{ 24, 12, 8 },  // still allows rolls
                /*tripletProb*/   15,
                /*sparseProb*/    35,
                /*offKick*/       35,             // more offbeat kicks
                /*ghost*/         15,
                /*tomProb*/       6,
                /*4x*/            false,
                /*dembow*/        false,
                /*rock*/          false,
                /*vary*/          2, 8,
                /*offbeatHats*/   false
        };

        if (match(inName, "trap"))       return trap;
        if (match(inName, "drill"))      return drill;
        if (match(inName, "edm"))        return edm;
        if (match(inName, "reggaeton"))  return reggaeton;
        if (match(inName, "r&b") || match(inName, "rnb")) return rnb;
        if (match(inName, "pop"))        return pop;
        if (match(inName, "rock"))       return rock;
        if (match(inName, "wxstie") || match(inName, "westcoast") || match(inName, "west coast")) return wxstie;

        // Default: pop-ish safe rules if style text is unknown
        return pop;
    }

    enum class Engine : int { e808 = 0, Bass = 1, Drums = 2 };

    inline const juce::StringArray& engineChoices()
    {
        static const juce::StringArray c { "808", "Bass", "Drums" };
        return c;
    }


    inline const juce::StringArray& keyChoices()
    {
        static const juce::StringArray c { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return c;
    }

    inline const juce::StringArray& scaleChoices()
    {
        // Full BANG list you provided
        static const juce::StringArray c {
            "Major", "Natural Minor", "Harmonic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian",
                "Locrian Nat6", "Ionian #5", "Dorian #4", "Phrygian Dom", "Lydian #2", "Super Locrian", "Dorian b2",
                "Lydian Aug", "Lydian Dom", "Mixo b6", "Locrian #2", "8 Tone Spanish", "Phrygian Nat3",
                "Blues", "Hungarian Min", "Harmonic Maj(Ethiopian)", "Dorian b5", "Phrygian b4", "Lydian b3", "Mixolydian b2",
                "Lydian Aug2", "Locrian bb7", "Pentatonic Maj", "Pentatonic Min", "Neopolitan Maj",
                "Neopolitan Min", "Spanish Gypsy", "Romanian Minor", "Chromatic", "Bebop Major", "Bebop Minor"
        };
        return c;
    }

    inline const juce::StringArray& timeSigChoices()
    {
        // Full BANG list you provided (including additive meters)
        static const juce::StringArray c {
            "4/4", "3/4", "6/8", "7/8", "5/4", "9/8", "12/8", "2/4", "7/4", "9/4",
                "5/8", "10/8", "11/8", "13/8", "15/8", "17/8", "19/8", "21/8",
                "5/16", "7/16", "9/16", "11/16", "13/16", "15/16", "17/16", "19/16",
                "3+2/8", "2+3/8",
                "2+2+3/8", "3+2+2/8", "2+3+2/8",
                "3+3+2/8", "3+2+3/8", "2+3+3/8",
                "4+3/8", "3+4/8",
                "3+2+2+3/8"
        };
        return c;
    }

    inline const juce::StringArray& barsChoices()
    {
        static const juce::StringArray c { "4", "8" };
        return c;
    }

    inline const juce::StringArray& defaultDrumRows()
    {
        static const juce::StringArray rows { "Kick", "Snare/Clap", "Hi-Hat", "Open Hat", "Perc 1", "Perc 2", "Perc 3" };
        return rows;
    }
}

namespace boomfix
{
    inline float readParamRaw(juce::AudioProcessorValueTreeState& apvts,
        const char* id,
        float fallback)
    {
        if (auto* v = apvts.getRawParameterValue(id))
            return v->load();
        return fallback;
    }
}