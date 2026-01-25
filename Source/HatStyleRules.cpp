#include "HatStyleRules.h"

static const HatStyleRuleSet trapRules =
{
    0.90f,   // steadyPulseChance
    true,    // allowPulseGaps
    0.15f,   // pulseGapChance

    0.25f,   // tripletDominance
    true,    // allowStraight
    true,    // allowHybrid

    0.35f,   // rollChance
    0.33f,   // ascending
    0.33f,   // descending
    0.34f,   // stationary

    0.40f,   // secondNoteChance
    0.30f,   // secondNoteOffsetRisk

    0.20f,   // riskChance
    5,       // riskCooldownGenerations

    false,   // allowRareSteadyPulse
    0
};

static const HatStyleRuleSet drillRules =
{
    0.60f,
    true,
    0.40f,

    0.85f,
    false,
    true,

    0.50f,
    0.33f,
    0.33f,
    0.34f,

    0.30f,
    0.40f,

    0.35f,
    3,

    false,
    0
};

static const HatStyleRuleSet hipHopRules =
{
    0.80f,
    true,
    0.25f,

    0.15f,
    true,
    false,

    0.20f,
    0.30f,
    0.30f,
    0.40f,

    0.25f,
    0.20f,

    0.15f,
    6,

    false,
    0
};

static const HatStyleRuleSet wxstieRules =
{
    0.05f,
    true,
    0.75f,

    0.25f,
    true,
    true,

    0.40f,
    0.33f,
    0.33f,
    0.34f,

    0.20f,
    0.50f,

    0.45f,
    2,

    true,
    8
};

static const HatStyleRuleSet rockRules =
{
    0.95f,
    false,
    0.05f,

    0.05f,
    true,
    false,

    0.10f,
    0.10f,
    0.10f,
    0.80f,

    0.05f,
    0.10f,

    0.05f,
    10,

    false,
    0
};

static const HatStyleRuleSet popRules =
{
    0.85f,
    true,
    0.15f,

    0.10f,
    true,
    false,

    0.15f,
    0.20f,
    0.20f,
    0.60f,

    0.20f,
    0.20f,

    0.10f,
    8,

    false,
    0
};

static const HatStyleRuleSet rnbRules =
{
    0.65f,
    true,
    0.40f,

    0.30f,
    true,
    true,

    0.25f,
    0.25f,
    0.25f,
    0.50f,

    0.30f,
    0.30f,

    0.20f,
    5,

    false,
    0
};

static const HatStyleRuleSet reggaetonRules =
{
    0.20f,
    true,
    0.60f,

    0.05f,
    true,
    false,

    0.10f,
    0.20f,
    0.20f,
    0.60f,

    0.10f,
    0.15f,

    0.10f,
    7,

    false,
    0
};

static const HatStyleRuleSet edmRules =
{
    0.85f,
    false,
    0.05f,

    0.30f,
    true,
    true,

    0.45f,
    0.60f,
    0.20f,
    0.20f,

    0.45f,
    0.40f,

    0.30f,
    4,

    false,
    0
};

const HatStyleRuleSet& getHatStyleRuleSet(HatStyle style)
{
    switch (style)
    {
        case HatStyle::Trap:        return trapRules;
        case HatStyle::Drill:       return drillRules;
        case HatStyle::HipHop:      return hipHopRules;
        case HatStyle::Wxstie:      return wxstieRules;
        case HatStyle::Rock:        return rockRules;
        case HatStyle::Pop:         return popRules;
        case HatStyle::RnB:         return rnbRules;
        case HatStyle::Reggaeton:   return reggaetonRules;
        case HatStyle::EDM:         return edmRules;
        default:                    return trapRules;
    }
}
