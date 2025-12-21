#include "RollStyleRules.h"

// NOTE:
// Project PPQ = 96
// 32nd triplet = 96 / 12 = 8 ticks
// 64th triplet = 96 / 24 = 4 ticks

static const RollStyleRuleSet trapRules =
{
    0.80f,   // steadyPulseChance
    0.25f,   // gapChance
    0.30f,   // gapDominance

    0.30f,   // tripletDominance
    4,       // minSubdivisionTicks (allow fast rolls)

    0.45f,   // rollChance
    0.33f,   // ascending
    0.33f,   // descending
    0.34f,   // stationary

    0.70f,   // longFormMotionChance

    0.30f,   // offBeatEmphasis
    0.25f,   // riskChance
    7        // riskCooldownGenerations
};

static const RollStyleRuleSet drillRules =
{
    0.50f,
    0.50f,
    0.50f,

    0.85f,
    4,

    0.60f,
    0.33f,
    0.33f,
    0.34f,

    0.65f,

    0.60f,
    0.40f,
    4
};

static const RollStyleRuleSet wxstieRules =
{
    0.25f,
    0.70f,
    0.70f,

    0.30f,
    4,

    0.50f,
    0.33f,
    0.33f,
    0.34f,

    0.50f,

    0.50f,
    0.45f,
    3
};

static const RollStyleRuleSet hipHopRules =
{
    0.55f,
    0.45f,
    0.40f,

    0.25f,
    8,       // NO notes smaller than 32nd triplets

    0.30f,
    0.30f,
    0.30f,
    0.40f,

    0.20f,

    0.35f,
    0.15f,
    6
};

static const RollStyleRuleSet rockRules =
{
    0.90f,
    0.10f,
    0.10f,

    0.05f,
    12,      // very conservative

    0.15f,
    0.10f,
    0.10f,
    0.80f,

    0.05f,

    0.10f,
    0.05f,
    10
};

static const RollStyleRuleSet popRules =
{
    0.80f,
    0.20f,
    0.20f,

    0.10f,
    12,

    0.20f,
    0.20f,
    0.20f,
    0.60f,

    0.10f,

    0.15f,
    0.10f,
    8
};

static const RollStyleRuleSet rnbRules =
{
    0.60f,
    0.45f,
    0.45f,

    0.35f,
    8,

    0.35f,
    0.25f,
    0.45f,
    0.30f,

    0.40f,

    0.40f,
    0.25f,
    5
};

static const RollStyleRuleSet reggaetonRules =
{
    0.60f,
    0.35f,
    0.30f,

    0.05f,
    12,

    0.15f,
    0.20f,
    0.20f,
    0.60f,

    0.05f,

    0.20f,
    0.10f,
    7
};

static const RollStyleRuleSet edmRules =
{
    0.85f,
    0.15f,
    0.15f,

    0.35f,
    4,

    0.55f,
    0.60f,
    0.20f,
    0.20f,

    0.75f,

    0.45f,
    0.35f,
    4
};

const RollStyleRuleSet& getRollStyleRuleSet(RollStyle style)
{
    switch (style)
    {
        case RollStyle::Trap:        return trapRules;
        case RollStyle::Drill:       return drillRules;
        case RollStyle::HipHop:      return hipHopRules;
        case RollStyle::Wxstie:      return wxstieRules;
        case RollStyle::Rock:        return rockRules;
        case RollStyle::Pop:         return popRules;
        case RollStyle::RnB:         return rnbRules;
        case RollStyle::Reggaeton:   return reggaetonRules;
        case RollStyle::EDM:         return edmRules;
        default:                     return trapRules;
    }
}
