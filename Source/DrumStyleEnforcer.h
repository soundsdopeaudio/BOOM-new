#pragma once

#include <JuceHeader.h>
#include "DrumStyleRhythmProfile.h"
#include "PluginProcessor.h" // for BoomAudioProcessor::Note

namespace boom::drumstyle
{
    // Enforce style rules onto a generated drum pattern.
    // Call this after you have a base pattern (and any basic cleanup),
    // but before optional mode passes (GHXSTGRID, Scatter, etc.).
    void enforceStyle(const DrumStyleRhythmProfile& profile,
        juce::Array<BoomAudioProcessor::Note>& pattern,
        int bars,
        int ppq,
        int timeSigNum,
        int timeSigDen);
}

