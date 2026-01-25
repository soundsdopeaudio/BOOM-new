#include "HatsStyleProfiles.h"

HatStyleRuleSet resolveHatRules(const juce::String& style)
{
    HatStyleRuleSet r{}; // zero-init

    if (style == "trap")
    {
        r.steadyPulsePct = 75;
        r.gapPct = 15;
        r.offKilterPct = 10;

        r.rollChancePct = 30;
        r.rollWildPct = 10;
        r.maxSubdivision = 2;

        r.ascendPct = 33;
        r.descendPct = 33;
        r.stationaryPct = 34;

        r.tripletBiasPct = 20;
        r.innerDensityPct = 35;
        r.fastRollPct = 20;
    }
    else if (style == "drill")
    {
        r.steadyPulsePct = 60;
        r.gapPct = 30;
        r.offKilterPct = 25;

        r.rollChancePct = 45;
        r.rollWildPct = 20;
        r.maxSubdivision = 3;

        r.ascendPct = 34;
        r.descendPct = 33;
        r.stationaryPct = 33;

        r.tripletBiasPct = 75;
        r.innerDensityPct = 55;
        r.fastRollPct = 45;
    }
    else if (style == "hiphop")
    {
        r.steadyPulsePct = 65;
        r.gapPct = 35;
        r.offKilterPct = 20;

        r.rollChancePct = 20;
        r.rollWildPct = 5;
        r.maxSubdivision = 2;

        r.ascendPct = 33;
        r.descendPct = 33;
        r.stationaryPct = 34;

        r.tripletBiasPct = 25;
        r.innerDensityPct = 30;
        r.fastRollPct = 15;
    }
    else if (style == "wxstie")
    {
        r.steadyPulsePct = 12;
        r.gapPct = 70;
        r.offKilterPct = 35;

        r.rollChancePct = 30;
        r.rollWildPct = 15;
        r.maxSubdivision = 2;

        r.ascendPct = 33;
        r.descendPct = 33;
        r.stationaryPct = 34;

        r.tripletBiasPct = 20;
        r.innerDensityPct = 25;
        r.fastRollPct = 20;
    }
    else if (style == "reggaeton")
    {
        r.steadyPulsePct = 55;
        r.gapPct = 45;
        r.offKilterPct = 20;

        r.rollChancePct = 15;
        r.rollWildPct = 5;
        r.maxSubdivision = 2;

        r.ascendPct = 20;
        r.descendPct = 20;
        r.stationaryPct = 60;

        r.tripletBiasPct = 15;
        r.innerDensityPct = 25;
        r.fastRollPct = 10;
    }
    else if (style == "edm")
    {
        r.steadyPulsePct = 85;
        r.gapPct = 10;
        r.offKilterPct = 10;

        r.rollChancePct = 25;
        r.rollWildPct = 15;
        r.maxSubdivision = 2;

        r.ascendPct = 40;
        r.descendPct = 40;
        r.stationaryPct = 20;

        r.tripletBiasPct = 10;
        r.innerDensityPct = 45;
        r.fastRollPct = 20;
    }
    else
    {
        // fallback
        r.steadyPulsePct = 60;
        r.gapPct = 30;
        r.offKilterPct = 15;

        r.rollChancePct = 25;
        r.rollWildPct = 10;
        r.maxSubdivision = 2;

        r.ascendPct = 33;
        r.descendPct = 33;
        r.stationaryPct = 34;

        r.tripletBiasPct = 25;
        r.innerDensityPct = 35;
        r.fastRollPct = 20;
    }

    return r;
}