#pragma once
#include <juce_core/juce_core.h>

#pragma once

struct RollStyleRuleSet
{
    int steadyPulsePct;
    int gapPct;
    int offKilterPct;

    int rollStartPct;
    int rollWildPct;
    int maxRollSubdivision;

    int ascendPct;
    int descendPct;
    int stationaryPct;
    bool favorTriplets;
    int tripletBiasPct;
    bool allowFullPatternMotion;
    int endOfBarFillPct;
    int rollPctWild;
    int rollPctNormal;
    int rollChancePct;
    int fastRollPct;
    int flamPct;
    int innerDensityPct;
};
