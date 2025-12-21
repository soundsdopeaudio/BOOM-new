#include "RollStyleProfiles.h"

// --------------------------------------------
// Static rule definitions
// --------------------------------------------

static const RollStyleRuleSet trapRules{
    70, 20, 15,   // steady, gaps, off-kilter
    45,           // roll chance
    40, 40, 20,   // ascend / descend / stationary
    false,        // favorTriplets
    false         // forbidFastTriplets
};

static const RollStyleRuleSet drillRules{
    50, 40, 30,
    55,
    35, 35, 30,
    true,
    false
};

static const RollStyleRuleSet wxstieRules{
    30, 65, 25,
    45,
    33, 33, 34,
    false,
    false
};

static const RollStyleRuleSet hipHopRules{
    55, 45, 20,
    40,
    33, 33, 34,
    false,
    true     // NO sub-32nd triplet rolls
};

static const RollStyleRuleSet edmRules{
    60, 20, 20,
    35,
    45, 35, 20,
    false,
    false
};

static const RollStyleRuleSet reggaetonRules{
    50, 30, 15,
    30,
    30, 30, 40,
    false,
    false
};

static const RollStyleRuleSet defaultRules = trapRules;

// --------------------------------------------
// Resolver implementation
// --------------------------------------------
const RollStyleRuleSet& RollStyleProfileResolver::getRules(const juce::String& style)
{
    if (style == "trap")        return trapRules;
    if (style == "drill")       return drillRules;
    if (style == "wxstie")      return wxstieRules;
    if (style == "hiphop")      return hipHopRules;
    if (style == "edm")         return edmRules;
    if (style == "reggaeton")   return reggaetonRules;

    return defaultRules;
}
