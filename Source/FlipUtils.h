#pragma once
#include <JuceHeader.h>

namespace boom::flip
{
    struct DrumEvent { int row{}, startTick{}, lengthTicks{24}, velocity{100}; };
    using DrumPattern = juce::Array<DrumEvent>;

    struct MelodicNote { int pitch{}, startTick{}, lengthTicks{24}, velocity{100}, channel{1}; };
    using MelodicPattern = juce::Array<MelodicNote>;

    inline void microFlipDrums(DrumPattern& pat, int seed, int density, int bars = 4)
    {
        juce::Random rng(seed);
        const int ops = juce::jlimit(1, 16, density / 6);
        const int ticksPerStep = 24;
        const int cols = bars * 16;

        for (int i = 0; i < ops && !pat.isEmpty(); ++i)
        {
            auto& e = pat.getReference(rng.nextInt(pat.size()));
            const int kind = rng.nextInt({0,3}); // 0 shift step, 1 change len, 2 swap row
            if (kind == 0)
            {
                int col = (e.startTick / ticksPerStep);
                col = juce::jlimit(0, cols - 1, col + (rng.nextBool()? 1 : -1));
                e.startTick = col * ticksPerStep;
            }
            else if (kind == 1)
            {
                e.lengthTicks = juce::jlimit(12, 6*ticksPerStep, e.lengthTicks + (rng.nextBool()? 12 : -12));
            }
            else
            {
                e.row = juce::jlimit(0, 6, e.row + (rng.nextBool()? 1 : -1));
            }
            e.velocity = juce::jlimit(30, 127, e.velocity + (rng.nextBool()? 8 : -8));
        }
    }

    inline void microFlipMelodic(MelodicPattern& pat, int seed, int density, int bars = 4)
    {
        juce::Random rng(seed);
        const int ops = juce::jlimit(1, 20, juce::roundToInt(density / 5.0));
        const int ticksPerStep = 24;
        const int cols = bars * 16;

        for (int i = 0; i < ops && !pat.isEmpty(); ++i)
        {
            auto& n = pat.getReference(rng.nextInt(pat.size()));
            const int kind = rng.nextInt({0,3}); // 0 nudge start, 1 len, 2 gap
            if (kind == 0)
            {
                int col = (n.startTick / ticksPerStep);
                col = juce::jlimit(0, cols - 1, col + (rng.nextBool()? 1 : -1));
                n.startTick = col * ticksPerStep;
            }
            else if (kind == 1)
            {
                n.lengthTicks = juce::jlimit(12, 6*ticksPerStep, n.lengthTicks + (rng.nextBool()? 12 : -12));
            }
            else
            {
                if ((n.startTick / ticksPerStep) < cols - 1)
                    n.startTick += 12;
            }
            n.velocity = juce::jlimit(30, 127, n.velocity + (rng.nextBool()? 6 : -6));
        }
    }
}
