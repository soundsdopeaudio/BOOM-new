#pragma once

#include <juce_core/juce_core.h>
#include "DrumStyleRhythmProfile.h"
#include "BoomAudioProcessor.h"

namespace boom::drumstyle
{
    void enforceStyle(
        const DrumStyleRhythmProfile& profile,
        juce::Array<BoomAudioProcessor::Note>& pattern,
        int bars,
        int ppq,
        int timeSigNum,
        int timeSigDen
    );
}


    // ------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------

    // grid is always treated as 16 steps per bar
    static constexpr int kStepsPerBar = 16;

    // map drum grid row -> logical role
    static DrumRole roleForRow(int row)
    {
        switch (row)
        {
        case 0: return DrumRole::Kick;
        case 1: return DrumRole::Snare;
        case 2: return DrumRole::HiHat;
        case 3: return DrumRole::OpenHat;
        default: return DrumRole::Perc; // rows 4–6
        }
    }

    static const DrumRoleRules& rulesForRole(
        const DrumStyleRhythmProfile& profile,
        DrumRole role
    )
    {
        switch (role)
        {
        case DrumRole::Kick:     return profile.kick;
        case DrumRole::Snare:    return profile.snare;
        case DrumRole::HiHat:    return profile.hiHat;
        case DrumRole::OpenHat:  return profile.openHat;
        case DrumRole::Perc:     return profile.perc;
        }

        // never reached
        return profile.perc;
    }

    // ------------------------------------------------------------
    // Core enforcement
    // ------------------------------------------------------------
    void enforceStyle(
        const DrumStyleRhythmProfile& profile,
        juce::Array<BoomAudioProcessor::Note>& pattern,
        int bars,
        int ppq,
        int timeSigNum,
        int timeSigDen
    )
    {
        const int ticksPerStep = (ppq * 4) / kStepsPerBar;
        const int ticksPerBar = ticksPerStep * kStepsPerBar;

        // --------------------------------------------------------
        // 1) HARD GRID CLEANUP (rigid styles: Reggaeton / EDM)
        // --------------------------------------------------------
        if (profile.forceRigidGrid)
        {
            for (int i = pattern.size() - 1; i >= 0; --i)
            {
                const auto& n = pattern.getReference(i);
                if ((n.startTick % ticksPerStep) != 0)
                    pattern.remove(i);
            }
        }

        // --------------------------------------------------------
        // 2) FORBIDDEN STEPS REMOVAL (per role)
        // --------------------------------------------------------
        for (int i = pattern.size() - 1; i >= 0; --i)
        {
            const auto& n = pattern.getReference(i);
            const DrumRole role = roleForRow(n.row);
            const auto& rules = rulesForRole(profile, role);

            const int stepInBar = (n.startTick / ticksPerStep) % kStepsPerBar;

            for (int forbidden : rules.forbiddenSteps)
            {
                if (stepInBar == forbidden)
                {
                    pattern.remove(i);
                    break;
                }
            }
        }

        // --------------------------------------------------------
        // 3) MANDATORY STEPS INSERTION (authoritative)
        // --------------------------------------------------------
        auto ensureNoteAt = [&](int row, int startTick, int velocity)
            {
                for (const auto& n : pattern)
                    if (n.row == row && n.startTick == startTick)
                        return;

                BoomAudioProcessor::Note nn;
                nn.row = row;
                nn.startTick = startTick;
                nn.lengthTicks = ticksPerStep;
                nn.velocity = velocity;
                pattern.add(nn);
            };

        for (int bar = 0; bar < bars; ++bar)
        {
            const int barStart = bar * ticksPerBar;

            for (int row = 0; row < 7; ++row)
            {
                const DrumRole role = roleForRow(row);
                const auto& rules = rulesForRole(profile, role);

                for (int step : rules.mandatorySteps)
                {
                    ensureNoteAt(
                        row,
                        barStart + (step * ticksPerStep),
                        110
                    );
                }
            }
        }

        // --------------------------------------------------------
        // 4) DENSITY BIAS (thin or boost notes)
        // --------------------------------------------------------
        juce::Random rng;

        for (int row = 0; row < 7; ++row)
        {
            const DrumRole role = roleForRow(row);
            const auto& rules = rulesForRole(profile, role);

            if (rules.densityBias == 0)
                continue;

            for (int i = pattern.size() - 1; i >= 0; --i)
            {
                const auto& n = pattern.getReference(i);
                if (n.row != row)
                    continue;

                if (rules.densityBias < 0)
                {
                    const int removeChance = juce::jlimit(0, 100, -rules.densityBias);
                    if (rng.nextInt(100) < removeChance)
                        pattern.remove(i);
                }
            }
        }

        // --------------------------------------------------------
        // 5) VELOCITY BIAS
        // --------------------------------------------------------
        for (auto& n : pattern)
        {
            const DrumRole role = roleForRow(n.row);
            const auto& rules = rulesForRole(profile, role);

            if (rules.velocityBias != 0)
            {
                const int v =
                    n.velocity +
                    juce::roundToInt(rules.velocityBias * 0.25f);

                n.velocity = juce::jlimit(1, 127, v);
            }
        }

        // --------------------------------------------------------
        // 6) FINAL SORT (UI + export safety)
        // --------------------------------------------------------
        std::sort(pattern.begin(), pattern.end(),
            [](const BoomAudioProcessor::Note& a,
                const BoomAudioProcessor::Note& b)
            {
                if (a.startTick != b.startTick)
                    return a.startTick < b.startTick;
                return a.row < b.row;
            });
    }

} // namespace boom::drums
