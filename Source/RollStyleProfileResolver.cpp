#include "RollStyleProfileResolver.h"

RollStyleRuleSet resolveRollRules(const juce::String& style)
{
    RollStyleRuleSet r{}; // zero-init everything

    // base defaults (safe)
    r.steadyPulsePct = 55;
    r.gapPct = 35;
    r.offKilterPct = 20;

    r.rollStartPct = 30;
    r.rollWildPct = 15;
    r.maxRollSubdivision = 3;

    r.ascendPct = 33;
    r.descendPct = 33;
    r.stationaryPct = 34;

    r.favorTriplets = false;
    r.tripletBiasPct = 25;

    r.allowFullPatternMotion = true;
    r.endOfBarFillPct = 20;

    r.rollPctWild = 30;
    r.rollPctNormal = 70;
    r.rollChancePct = 35;

    r.fastRollPct = 25;
    r.flamPct = 10;
    r.innerDensityPct = 40;

    // style overrides
    if (style == "trap")
    {
        r.steadyPulsePct = 70;
        r.gapPct = 20;
        r.offKilterPct = 15;

        r.rollStartPct = 35;
        r.rollChancePct = 40;
        r.tripletBiasPct = 20;
        r.fastRollPct = 20;
        r.flamPct = 10;
        r.innerDensityPct = 35;
    }
    else if (style == "drill")
    {
        r.steadyPulsePct = 50;
        r.gapPct = 30;
        r.offKilterPct = 30;

        r.rollStartPct = 45;
        r.rollWildPct = 20;
        r.rollChancePct = 55;

        r.favorTriplets = true;
        r.tripletBiasPct = 75;
        r.fastRollPct = 45;
        r.flamPct = 15;
        r.endOfBarFillPct = 25;
        r.innerDensityPct = 55;
    }
    else if (style == "wxstie")
    {
        r.steadyPulsePct = 30;
        r.gapPct = 65;
        r.offKilterPct = 25;

        r.rollStartPct = 35;
        r.rollChancePct = 45;
        r.tripletBiasPct = 20;

        r.allowFullPatternMotion = false;
        r.innerDensityPct = 25;
    }
    else if (style == "hiphop")
    {
        r.steadyPulsePct = 55;
        r.gapPct = 45;
        r.offKilterPct = 20;

        r.rollStartPct = 25;
        r.rollChancePct = 35;

        r.maxRollSubdivision = 2; // keep it from going too tiny
        r.tripletBiasPct = 25;
        r.fastRollPct = 15;
        r.flamPct = 8;
        r.innerDensityPct = 30;
    }
    else if (style == "edm")
    {
        r.steadyPulsePct = 60;
        r.gapPct = 20;
        r.offKilterPct = 20;

        r.rollStartPct = 25;
        r.rollChancePct = 35;
        r.endOfBarFillPct = 35;

        r.tripletBiasPct = 10;
        r.fastRollPct = 20;
        r.innerDensityPct = 45;
    }
    else if (style == "reggaeton")
    {
        r.steadyPulsePct = 50;
        r.gapPct = 30;
        r.offKilterPct = 15;

        r.rollStartPct = 20;
        r.rollChancePct = 30;

        r.ascendPct = 20;
        r.descendPct = 20;
        r.stationaryPct = 60;

        r.tripletBiasPct = 15;
        r.fastRollPct = 10;
        r.innerDensityPct = 25;
    }

    return r;
}
