#pragma once
#include <JuceHeader.h>

namespace boom {
    namespace drums
    {
        // We generate on a 16th-grid then convert to 96 PPQ (one 16th = 24 ticks).
        static constexpr int kStepsPerBar = 16;

        // Logical drum rows in your grid. Keep these aligned to what DrumGridComponent expects.
        enum Row
        {
            Kick = 0,
            Snare = 1,
            ClosedHat = 2,
            OpenHat = 3,
            Clap = 4,
            Perc = 5,
            NumRows
        };

        struct RowSpec
        {
            // Per-step probability (0..1) a hit may occur, before rests / gates.
            float p[kStepsPerBar]{};
            // Velocity ranges (MIDI 1..127).
            int velMin = 90, velMax = 120;
            // Probability of quick rolls on this row (e.g. hats).
            float rollProb = 0.0f;      // 0..1
            // Max roll rate in subdivisions of a 16th (e.g. 2 = 32nds, 3 = 16th triplets)
            int   maxRollSub = 1;       // 1=none, 2=32nds, 3=triplet 1/24 grid
            // Humanize windows (ticks @ 96 PPQ)
            int timingJitterTicks = 0;
            int lenTicks = 24;          // default 1x 16th
        };

        struct DrumStyleSpec
        {
            juce::String name;

            // Global feel controls
            float swingPct = 0.0f;          // 0..100; applied to 8th offbeats
            float tripletBias = 0.0f;        // 0..1 extra chance to favor triplet grid where appropriate
            float dottedBias = 0.0f;        // 0..1 favor dotted spacing patterns

            // Base tempo hints if you want later (not used in generator here).
            int   bpmMin = 70, bpmMax = 160;

            // Per-row specs
            RowSpec rows[NumRows];

            // Backbeat anchors (snare/clap typical hits in 4/4: steps 4,12 at 16ths)
            bool lockBackbeat = true;
        };

        // All supported names (for comboboxes, etc.)
        const juce::StringArray styleNames();

        // Lookup by canonical style name; guaranteed to return a valid spec (falls back to "hiphop").
        DrumStyleSpec getSpec(const juce::String& styleName);

        // Core generator that fills a pattern (row,startTick,lenTicks,velocity) for 'bars' bars.
        struct DrumNote { int row; int startTick; int lenTicks; int vel; };
        using DrumPattern = juce::Array<DrumNote>;

        void generate(const DrumStyleSpec& spec,
            int bars,
            int restPct,        // 0..100
            int dottedPct,      // 0..100
            int tripletPct,     // 0..100
            int swingPct,       // 0..100 (applies to hats/openhat/perc mostly)
            int seed,
            DrumPattern& out);
    }
} // namespace

