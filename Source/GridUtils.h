#pragma once
#include <JuceHeader.h> 
#include <vector>
#include <algorithm>

namespace boom::grid
{
    // Return ticks-per-step (PPQ ticks for one grid step) given a PPQ and cells-per-beat
    inline int ticksPerStepFromPpq(int ppq, int cellsPerBeat)
    {
        return juce::jmax(1, ppq / juce::jmax(1, cellsPerBeat));
    }

    // Convert a step index -> PPQ ticks (start tick)
    inline int stepIndexToPpqTicks(int stepIndex, int ppq, int cellsPerBeat)
    {
        return stepIndex * ticksPerStepFromPpq(ppq, cellsPerBeat);
    }

    // Convert a length in steps -> PPQ ticks (at least 1)
    inline int stepsToPpqTicksLen(int lenSteps, int ppq, int cellsPerBeat)
    {
        return juce::jmax(1, lenSteps * ticksPerStepFromPpq(ppq, cellsPerBeat));
    }

    // Convert a start tick (in same tick unit as PPQ) -> nearest step index using given ticksPerStep
    inline int roundStartTickToStepIndex(int startTick, int ticksPerStep) noexcept
    {
        return (int)juce::roundToInt((double)startTick / (double)ticksPerStep);
    }

    // Convert ticks -> 16th index (integer). Equivalent to (ticks * 4) / ppq
    inline int ticksTo16thIndex(int ticks, int ppq) noexcept
    {
        if (ppq <= 0) return 0;
        return (ticks * 4) / ppq;
    }

    // Convert 16th index -> ticks (one 16th = ppq/4 ticks)
    inline int index16thToTicks(int idx16, int ppq) noexcept
    {
        return idx16 * (ppq / 4);
    }

    // --- Musical subdivision helpers ----------------------------------

    // Return ticks for a note given as a denominator relative to a whole note
    // e.g. denom=4 -> quarter note, denom=8 -> eighth note
    inline int ticksForDenominator(int ppq, int denom) noexcept
    {
        if (ppq <= 0 || denom <= 0) return 0;
        return juce::jmax(1, juce::roundToInt((4.0 * ppq) / (double)denom));
    }

    // Dotted duration = base * 3/2
    inline int dottedTicks(int baseTicks) noexcept
    {
        return juce::jmax(1, juce::roundToInt(baseTicks * 1.5));
    }

    // Triplet duration = base * 2/3
    inline int tripletTicks(int baseTicks) noexcept
    {
        return juce::jmax(1, juce::roundToInt(baseTicks * (2.0 / 3.0)));
    }

    // Return a sorted unique list of common subdivision tick-values
    inline std::vector<int> commonSubdivisionTicks(int ppq, bool includeDotted = true, bool includeTriplets = true)
    {
        std::vector<int> out;
        const int denoms[] = { 1, 2, 4, 8, 16, 32, 64 };

        for (auto d : denoms)
        {
            const int base = ticksForDenominator(ppq, d);
            if (base > 0) out.push_back(base);
            if (includeDotted) out.push_back(dottedTicks(base));
            if (includeTriplets) out.push_back(tripletTicks(base));
        }

        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // Snap an arbitrary tick length to the nearest common musical subdivision
    inline int snapTicksToNearestSubdivision(int ticks, int ppq, bool includeDotted = true, bool includeTriplets = true) noexcept
    {
        if (ppq <= 0) return ticks;
        auto subs = commonSubdivisionTicks(ppq, includeDotted, includeTriplets);
        if (subs.empty()) return ticks;

        int best = subs.front();
        int bestDiff = std::abs(ticks - best);

        for (auto s : subs)
        {
            int d = std::abs(ticks - s);
            if (d < bestDiff)
            {
                bestDiff = d;
                best = s;
            }
        }

        return best;
    }

    // Snap ticks to the editor's grid step (cellsPerBeat -> ticks per step)
    inline int snapTicksToGridStep(int ticks, int ppq, int cellsPerBeat) noexcept
    {
        const int step = ticksPerStepFromPpq(ppq, cellsPerBeat);
        if (step <= 0) return ticks;
        return juce::roundToInt((double)ticks / (double)step) * step;
    }

    // Detect dotted/triplet durations (within a small tolerance)
    inline bool isDottedTicks(int ticks, int ppq, int tolerance = 1) noexcept
    {
        if (ppq <= 0 || ticks <= 0) return false;
        const int denoms[] = { 1, 2, 4, 8, 16, 32, 64 };
        for (auto d : denoms)
        {
            const int base = ticksForDenominator(ppq, d);
            const int dotted = dottedTicks(base);
            if (std::abs(ticks - dotted) <= tolerance) return true;
        }
        return false;
    }

    inline bool isTripletTicks(int ticks, int ppq, int tolerance = 1) noexcept
    {
        if (ppq <= 0 || ticks <= 0) return false;
        const int denoms[] = { 1, 2, 4, 8, 16, 32, 64 };
        for (auto d : denoms)
        {
            const int base = ticksForDenominator(ppq, d);
            const int trip = tripletTicks(base);
            if (std::abs(ticks - trip) <= tolerance) return true;
        }
        return false;
    }
}