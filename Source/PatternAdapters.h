#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FlipUtils.h"
#include "MidiUtils.h"

namespace boom::adapters
{
    inline boom::flip::DrumPattern toFlip(const BoomAudioProcessor::Pattern& src)
    {
        boom::flip::DrumPattern out; out.ensureStorageAllocated(src.size());
        for (const auto& n : src) out.add({ n.row, n.startTick, juce::jmax(12,n.lengthTicks), n.velocity });
        return out;
    }
    inline BoomAudioProcessor::Pattern fromFlip(const boom::flip::DrumPattern& src)
    {
        BoomAudioProcessor::Pattern out; out.ensureStorageAllocated(src.size());
        for (const auto& e : src) out.add({ 0, e.row, e.startTick, e.lengthTicks, e.velocity });
        return out;
    }

    inline boom::flip::MelodicPattern toFlipMelodic(const BoomAudioProcessor::Pattern& src)
    {
        boom::flip::MelodicPattern out; out.ensureStorageAllocated(src.size());
        for (const auto& n : src) out.add({ n.pitch, n.startTick, juce::jmax(12,n.lengthTicks), n.velocity, 1 });
        return out;
    }
    inline BoomAudioProcessor::Pattern fromFlipMelodic(const boom::flip::MelodicPattern& src)
    {
        BoomAudioProcessor::Pattern out; out.ensureStorageAllocated(src.size());
        for (const auto& m : src) out.add({ m.pitch, 0, m.startTick, m.lengthTicks, m.velocity });
        return out;
    }

    inline boom::midi::DrumPattern toMidi(const BoomAudioProcessor::Pattern& src)
    {
        boom::midi::DrumPattern out; out.ensureStorageAllocated(src.size());
        for (const auto& n : src) out.add({ n.row, n.startTick, juce::jmax(12,n.lengthTicks), n.velocity });
        return out;
    }

    inline boom::midi::MelodicPattern toMidiMelodic(const BoomAudioProcessor::Pattern& src)
    {
        boom::midi::MelodicPattern out; out.ensureStorageAllocated(src.size());
        for (const auto& n : src) out.add({ n.pitch, n.startTick, juce::jmax(12,n.lengthTicks), n.velocity, 1 });
        return out;
    }
}
