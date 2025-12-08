#pragma once
#include <JuceHeader.h>
#include "GridUtils.h"

namespace boom::midi
{
    // Drum map for 7 lanes
    static inline const int kDrumMap[7] = { 36, 38, 42, 46, 39, 75, 81 }; // Kick, Snare, CHH, OHH, Perc1, Perc2, Perc3

    struct DrumNote { int row{}; int startTick{}; int lengthTicks{24}; int velocity{100}; };
    using DrumPattern = juce::Array<DrumNote>;

    struct MelodicNote { int pitch{60}; int startTick{}; int lengthTicks{24}; int velocity{100}; int channel{1}; };
    using MelodicPattern = juce::Array<MelodicNote>;

    inline juce::MidiFile buildMidiFromDrums(const DrumPattern& pat, int ppq = 96)
    {
        juce::MidiMessageSequence seq;
        const int ch = 10; // drums
        for (const auto& n : pat)
        {
            const int lane = juce::jlimit(0, 6, n.row);
            const int pitch = kDrumMap[lane];
            const int on  = n.startTick;
            const int off = n.startTick + juce::jmax(12, n.lengthTicks);
            seq.addEvent(juce::MidiMessage::noteOn (ch, pitch, (juce::uint8)juce::jlimit(1,127,n.velocity)), on);
            seq.addEvent(juce::MidiMessage::noteOff(ch, pitch), off);
        }
        seq.updateMatchedPairs();
        juce::MidiFile mf; mf.setTicksPerQuarterNote(ppq); mf.addTrack(seq); return mf;
    }

    inline juce::MidiFile buildMidiFromMelodic(const MelodicPattern& pat, int ppq = 96)
    {
        juce::MidiMessageSequence seq;
        for (const auto& n : pat)
        {
            const int on  = n.startTick;
            const int off = n.startTick + juce::jmax(12, n.lengthTicks);
            seq.addEvent(juce::MidiMessage::noteOn (juce::jlimit(1,16,n.channel),
                                                    juce::jlimit(0,127,n.pitch),
                                                    (juce::uint8)juce::jlimit(1,127,n.velocity)), on);
            seq.addEvent(juce::MidiMessage::noteOff(juce::jlimit(1,16,n.channel),
                                                    juce::jlimit(0,127,n.pitch)), off);
        }
        seq.updateMatchedPairs();
        juce::MidiFile mf; mf.setTicksPerQuarterNote(ppq); mf.addTrack(seq); return mf;
    }

    inline bool writeMidiToFile(const juce::MidiFile& mf, const juce::File& f)
    {
        juce::FileOutputStream fos(f);
        if (!fos.openedOk()) return false;
        fos.setPosition(0);
        return mf.writeTo(fos);
    }
}
