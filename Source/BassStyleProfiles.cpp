#include "BassStyleProfiles.h"

static BassStyleRuleSet makeDefaultBass()
{
    BassStyleRuleSet r;
    r.baseOctave = 3;
    r.sustainMinSteps = 1;
    r.sustainMaxSteps = 3;
    r.burstPct = 20;
    r.wStay = 55;
    r.wFifth = 25;
    r.wSmallStep = 15;
    r.wDownStep = 5;
    r.wOctLeap = 0;
    r.downStepSize = 2;
    r.forceDownbeatPct = 35;
    r.downbeatRootPct = 75;
    return r;
}

static BassStyleRuleSet makeTrapBass()
{
    auto r = makeDefaultBass();
    r.baseOctave = 2;
    r.stepHitBiasOddPct = 8;
    r.sustainMinSteps = 1;
    r.sustainMaxSteps = 2;
    r.burstPct = 55;
    r.wStay = 40;
    r.wFifth = 25;
    r.wSmallStep = 10;
    r.wDownStep = 15;
    r.wOctLeap = 10;
    r.downStepSize = 3;
    r.forceDownbeatPct = 45;
    r.downbeatRootPct = 80;
    return r;
}

static BassStyleRuleSet makeDrillBass()
{
    auto r = makeDefaultBass();
    r.baseOctave = 2;
    r.stepHitBiasOddPct = 10;
    r.sustainMinSteps = 1;
    r.sustainMaxSteps = 2;
    r.burstPct = 60;
    r.wStay = 35;
    r.wFifth = 25;
    r.wSmallStep = 10;
    r.wDownStep = 15;
    r.wOctLeap = 15;
    r.downStepSize = 2;
    r.forceDownbeatPct = 50;
    r.downbeatRootPct = 75;
    return r;
}

static BassStyleRuleSet makeWxstieBass()
{
    auto r = makeDefaultBass();
    r.baseOctave = 2;
    r.stepHitBiasOddPct = 6;
    r.sustainMinSteps = 1;
    r.sustainMaxSteps = 3;
    r.burstPct = 35;
    r.wStay = 45;
    r.wFifth = 25;
    r.wSmallStep = 20;
    r.wDownStep = 10;
    r.wOctLeap = 0;
    r.downStepSize = 2;
    r.forceDownbeatPct = 40;
    r.downbeatRootPct = 75;
    return r;
}

static BassStyleRuleSet makeReggaetonBass()
{
    auto r = makeDefaultBass();
    r.baseOctave = 3;
    r.stepHitBiasEvenPct = 6;
    r.sustainMinSteps = 2;
    r.sustainMaxSteps = 4;
    r.burstPct = 15;
    r.wStay = 60;
    r.wFifth = 25;
    r.wSmallStep = 10;
    r.wDownStep = 5;
    r.wOctLeap = 0;
    r.downStepSize = 2;
    r.forceDownbeatPct = 55;
    r.downbeatRootPct = 85;
    return r;
}

BassStyleRuleSet resolveBassRules(const juce::String& styleIn)
{
    const auto style = styleIn.trim().toLowerCase();

    if (style == "trap")      return makeTrapBass();
    if (style == "drill")     return makeDrillBass();
    if (style == "wxstie")    return makeWxstieBass();
    if (style == "reggaeton") return makeReggaetonBass();

    // optional aliases / safety nets
    if (style == "hiphop" || style == "hip-hop") return makeDefaultBass();
    if (style == "rnb" || style == "r&b")        return makeDefaultBass();
    if (style == "edm")                          return makeDefaultBass();
    if (style == "pop")                          return makeDefaultBass();
    if (style == "rock")                         return makeDefaultBass();

    return makeDefaultBass();
}
