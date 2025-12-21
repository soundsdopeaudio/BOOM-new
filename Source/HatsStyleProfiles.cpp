#include "HatStyleProfiles.h"
#include "HatStyleRuleSet.h"

// --------------------------------------------
// Static rule definitions
// --------------------------------------------

static const HatStyleRuleSet trapRules{
    75, 15, 15,   // steady, gaps, off-kilter
    40,           // roll chance
    35, 35, 30,   // ascend / descend / stationary
    false         // favorTriplets
};

static const HatStyleRuleSet drillRules{
    60, 35, 25,
    45,
    33, 33, 34,
    true          // heavy triplet bias
};

static const HatStyleRuleSet hipHopRules{
    70, 25, 20,
    35,
    34, 33, 33,
    false
};

static const HatStyleRuleSet wxstieRules{
    15, 70, 30,
    40,
    33, 33, 34,
    false
};

static const HatStyleRuleSet edmRules{
    65, 15, 20,
    30,
    45, 35, 20,
    false
};

static const HatStyleRuleSet reggaetonRules{
    55, 30, 15,
    25,
    30, 30, 40,
    false
};

static const HatStyleRuleSet defaultRules = trapRules;

// --------------------------------------------
// Resolver implementation
// --------------------------------------------
const HatStyleRuleSet& HatStyleProfileResolver::getRules(const juce::String& style)
{
    if (style == "trap")        return trapRules;
    if (style == "drill")       return drillRules;
    if (style == "hiphop")      return hipHopRules;
    if (style == "wxstie")      return wxstieRules;
    if (style == "edm")         return edmRules;
    if (style == "reggaeton")   return reggaetonRules;

    return defaultRules;
}
