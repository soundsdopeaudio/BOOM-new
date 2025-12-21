#pragma once
#include <juce_core/juce_core.h>

// --------------------------------------------
// Hat Style Rule Set
// --------------------------------------------
// --------------------------------------------
// Resolver
// --------------------------------------------
class HatStyleProfileResolver
{
public:
    static const HatStyleRuleSet& getRules(const juce::String& style);
};

