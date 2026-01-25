#pragma once
#include <JuceHeader.h>

enum class RollStyle
{
    Trap,
    Drill,
    HipHop,
    Wxstie,
    Rock,
    Pop,
    RnB,
    Reggaeton,
    EDM
};

const RollStyleRuleSet& getRollStyleRuleSet(RollStyle style);
