#include "DrumStyleEnforcer.h"

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
namespace
{

    static DrumRole roleForRow(int row)
    {
        // Drum grid rows (authoritative per DrumStyleRhythmProfile.h):
        // 0 Kick, 1 Snare, 2 HiHat, 3 OpenHat, 4 Perc1, 5 Perc2, 6 Perc3
        switch (row)
        {
        case 0: return DrumRole::Kick;
        case 1: return DrumRole::Snare;
        case 2: return DrumRole::HiHat;
        case 3: return DrumRole::OpenHat;
        default: return DrumRole::Perc;
        }
    }

    static const DrumRoleRules& rulesForRole(const DrumStyleRhythmProfile& profile, DrumRole role)
    {
        switch (role)
        {
        case DrumRole::Kick:     return profile.kick;
        case DrumRole::Snare:    return profile.snare;
        case DrumRole::HiHat:    return profile.hiHat;
        case DrumRole::OpenHat:  return profile.openHat;
        case DrumRole::Perc:     return profile.perc;
        }
        return profile.perc;
    }

    static bool hasNoteAt(const juce::Array<BoomAudioProcessor::Note>& pattern, int row, int startTick, int tolTicks)
    {
        for (const auto& n : pattern)
            if (n.row == row && std::abs(n.startTick - startTick) <= tolTicks)
                return true;
        return false;
    }
}

namespace boom::drumstyle
{
    void boom::drumstyle::enforceStyle(const DrumStyleRhythmProfile& profile,
        juce::Array<BoomAudioProcessor::Note>& pattern,
        int bars,
        int ppq,
        int timeSigNum,
        int timeSigDen)
    {
        // 1 step = 1/16 note (BOOM uses 96 PPQ => 1/16 = 24 ticks when ppq=96)
        const int ticksPerStep = (ppq / 4);

        // Steps-per-bar depends on time signature when the step is fixed at 1/16.
        // stepsPerBeat = number of 1/16 notes inside the beat unit (1/den)
        const int stepsPerBeat = juce::jmax(1, 16 / juce::jmax(1, timeSigDen));
        const int stepsPerBar = juce::jmax(1, timeSigNum * stepsPerBeat);

        const int ticksPerBar = ticksPerStep * stepsPerBar;
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
        DBG("[Enforcer] bars=" << bars
            << " ts=" << timeSigNum << "/" << timeSigDen
            << " stepsPerBar=" << stepsPerBar
            << " ticksPerBar=" << ticksPerBar
            << " patternIn=" << pattern.size());
        // --------------------------------------------------------
        // 2) FORBIDDEN STEPS REMOVAL (per role)
        // --------------------------------------------------------
        int removedCount = 0;
        for (int i = pattern.size() - 1; i >= 0; --i)
        {
            const auto& n = pattern.getReference(i);
            const DrumRole role = roleForRow(n.row);
            const auto& rules = rulesForRole(profile, role);

            const int stepInBar = (n.startTick / ticksPerStep) % stepsPerBar;

            for (int forbidden : rules.forbiddenSteps)
            {
                if (stepInBar == forbidden)
                {
                    DBG("[Enforcer] REMOVING row=" << n.row << " at step=" << stepInBar 
                        << " (forbidden by profile)");
                    pattern.remove(i);
                    removedCount++;
                    break;
                }
            }
        }
        DBG("[Enforcer] Removed " << removedCount << " forbidden notes");

        // --------------------------------------------------------
        // 3) MANDATORY STEPS INSERTION (authoritative)
        // --------------------------------------------------------
        int addedCount = 0;
        auto ensureNoteAtExact = [&](int row, int startTick, int velocity)
            {
                if (hasNoteAt(pattern, row, startTick, /*tolTicks*/ 0))
                    return;

                DBG("[Enforcer] ADDING row=" << row << " at tick=" << startTick 
                    << " (mandatory by profile)");
                
                BoomAudioProcessor::Note nn;
                nn.row = row;
                nn.startTick = startTick;
                nn.lengthTicks = ticksPerStep;
                nn.velocity = juce::jlimit(1, 127, velocity);
                pattern.add(nn);
                addedCount++;
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
                    if (step < 0 || step >= stepsPerBar) continue;
                    ensureNoteAtExact(row, barStart + (step * ticksPerStep), 110);
                }
            }
        }
        DBG("[Enforcer] Added " << addedCount << " mandatory notes");

        // --------------------------------------------------------
        // 4) PREFERRED STEPS (soft bias: add gentle probability)
        // --------------------------------------------------------
        {
            juce::Random rng;

            auto preferredAddChanceForRole = [&](const DrumRoleRules& rules) -> int
                {
                    // Base 25%, then +/- densityBias * 0.25
                    const int base = 25;
                    const int tweak = juce::roundToInt((float)rules.densityBias * 0.25f);
                    return juce::jlimit(0, 100, base + tweak);
                };

            for (int bar = 0; bar < bars; ++bar)
            {
                const int barStart = bar * ticksPerBar;

                for (int row = 0; row < 7; ++row)
                {
                    const DrumRole role = roleForRow(row);
                    const auto& rules = rulesForRole(profile, role);
                    if (rules.preferredSteps.empty())
                        continue;

                    const int addChance = preferredAddChanceForRole(rules);

                    for (int step : rules.preferredSteps)
                    {
                        if (step < 0 || step >= stepsPerBar) continue;

                        const int t = barStart + (step * ticksPerStep);
                        if (hasNoteAt(pattern, row, t, /*tolTicks*/ 2))
                            continue;

                        if (rng.nextInt(100) < addChance)
                        {
                            BoomAudioProcessor::Note nn;
                            nn.row = row;
                            nn.startTick = t;
                            nn.lengthTicks = ticksPerStep;
                            nn.velocity = juce::jlimit(1, 127, 85 + rng.nextInt(25));
                            pattern.add(nn);
                        }
                    }
                }
            }
        }

        // --------------------------------------------------------
        // 5) DENSITY BIAS (thin notes)
        // --------------------------------------------------------
        {
            juce::Random rng;

            for (int row = 0; row < 7; ++row)
            {
                const DrumRole role = roleForRow(row);
                const auto& rules = rulesForRole(profile, role);

                if (rules.densityBias >= 0)
                    continue; // thinning only

                const int removeChance = juce::jlimit(0, 100, -rules.densityBias);

                for (int i = pattern.size() - 1; i >= 0; --i)
                {
                    const auto& n = pattern.getReference(i);
                    if (n.row != row)
                        continue;

                    // Never remove mandatory hits
                    const int stepInBar = (n.startTick / ticksPerStep) % stepsPerBar;
                    bool isMandatory = false;
                    for (int s : rules.mandatorySteps)
                        if (s == stepInBar) { isMandatory = true; break; }
                    if (isMandatory)
                        continue;

                    if (rng.nextInt(100) < removeChance)
                        pattern.remove(i);
                }
            }
        }

        // --------------------------------------------------------
        // 6) VELOCITY BIAS
        // --------------------------------------------------------
        for (auto& n : pattern)
        {
            const DrumRole role = roleForRow(n.row);
            const auto& rules = rulesForRole(profile, role);

            if (rules.velocityBias != 0)
            {
                const int v = n.velocity + juce::roundToInt((float)rules.velocityBias * 0.25f);
                n.velocity = juce::jlimit(1, 127, v);
            }
        }

        // --------------------------------------------------------
        // 7) FINAL SORT (UI + export safety)
        // --------------------------------------------------------
        std::sort(pattern.begin(), pattern.end(),
            [](const BoomAudioProcessor::Note& a, const BoomAudioProcessor::Note& b)
            {
                if (a.startTick != b.startTick)
                    return a.startTick < b.startTick;
                return a.row < b.row;
            });
    }
}
