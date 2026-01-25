#pragma once

#include <juce_core/juce_core.h>
#include "HatStyleRuleSet.h"

// Returns the rules for the given hat style string ("trap", "drill", etc.)
HatStyleRuleSet resolveHatRules(const juce::String& style);
