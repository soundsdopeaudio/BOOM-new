#pragma once

#include <juce_core/juce_core.h>
#include "RollStyleRuleSet.h"

// Central resolver for roll style behavior
RollStyleRuleSet resolveRollRules(const juce::String& style);