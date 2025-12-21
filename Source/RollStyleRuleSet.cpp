#include "RollStyleProfiles.h"

RollStyleRuleSet resolveRollRules(const juce::String& style)
{
    RollStyleRuleSet r;

    if (style == "trap")
    {
        r.steadyPulsePct = 65;
        r.gapPct = 20;
        r.offKilterPct = 15;
        r.rollStartPct = 30;
        r.rollWildPct = 10;
        r.maxRollSubdivision = 2;
        r.tripletBiasPct = 20;
        r.allowFullPatternMotion = true;
    }
    else if (style == "drill")
    {
        r.steadyPulsePct = 50;
        r.gapPct = 30;
        r.offKilterPct = 30;
        r.rollStartPct = 40;
        r.rollWildPct = 20;
        r.maxRollSubdivision = 3;
        r.tripletBiasPct = 70;
        r.allowFullPatternMotion = true;
    }
    else if (style == "wxstie")
    {
        r.steadyPulsePct = 20;
        r.gapPct = 65;
        r.offKilterPct = 25;
        r.rollStartPct = 35;
        r.rollWildPct = 15;
        r.maxRollSubdivision = 2;
        r.tripletBiasPct = 20;
    }
    else if (style == "hiphop")
    {
        r.steadyPulsePct = 55;
        r.gapPct = 45;
        r.offKilterPct = 20;
        r.rollStartPct = 25;
        r.maxRollSubdivision = 2; // NO smaller than 32nd triplets
        r.tripletBiasPct = 25;
    }

    return r;
}
