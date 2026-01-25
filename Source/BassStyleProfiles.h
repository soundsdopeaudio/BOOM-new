#pragma once

#include <juce_core/juce_core.h>
#include "BassStyleRuleSet.h"

// Returns the rules for the given bass style string ("trap", "drill", etc.)
BassStyleRuleSet resolveBassRules(const juce::String& style);
#pragma once
