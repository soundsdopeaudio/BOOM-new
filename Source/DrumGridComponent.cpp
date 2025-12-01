#include "DrumGridComponent.h"
#include "Theme.h"

void DrumGridComponent::setRowLabelFontHeight(int px) noexcept
{
    rowLabelPx_ = juce::jlimit(8, 18, px);
    repaint();
}

void DrumGridComponent::setBarsToDisplay(int bars2) noexcept
{
    bars2 = juce::jlimit(1, 64, bars2);
    if (barsToDisplay_ != bars2)
    {
        barsToDisplay_ = bars2;
        resized();
        repaint();
    }
}

int DrumGridComponent::getHeaderHeight() const noexcept
{
    return headerH_;
}

uint32_t DrumGridComponent::getRowSelectionMask() const noexcept
{
    uint32_t mask = 0u;

    // Debug print selectedRows_ content
    {
        juce::String s = "DrumGridComponent::getRowSelectionMask selectedRows_=[";
        for (int i = 0; i < selectedRows_.size(); ++i) { if (i) s << ","; s << selectedRows_.getUnchecked(i); }
        s << "]";
        DBG(s);
    }

    // If explicit selection exists, build mask from it
    if (!selectedRows_.isEmpty())
    {
        for (int row : selectedRows_)
        {
            if (row >= 0 && row < 32)
                mask |= (1u << row);
        }

        DBG("DrumGridComponent::getRowSelectionMask -> using selectedRows mask = " + juce::String((int)mask));
        return mask;
    }

    // No explicit selection -> use rowEnabled as the mask
    {
        juce::String s = "DrumGridComponent::getRowSelectionMask rowEnabled=[";
        for (int r = 0; r < (int)rowEnabled.size(); ++r)
        {
            if (r) s << ",";
            s << (rowEnabled[(size_t)r] ? "1" : "0");
        }
        s << "]";
        DBG(s);
    }

    const int maxRows = juce::jmin((int)rowEnabled.size(), 32);
    for (int r = 0; r < maxRows; ++r)
    {
        if (rowEnabled[(size_t)r])
            mask |= (1u << r);
    }

    DBG("DrumGridComponent::getRowSelectionMask -> using rowEnabled mask = " + juce::String((int)mask));
    return mask;
}


juce::Array<int> DrumGridComponent::getSelectedRows() const
{
    // return a copy (safe)
    return selectedRows_;
}

bool DrumGridComponent::isAnyRowSelected() const
{
    return selectedRows_.size() > 0;
}

void DrumGridComponent::clearSelection()
{
    if (selectedRows_.isEmpty()) return;
    selectedRows_.clearQuick();
    lastSelectedRow_ = -1;
    repaint(); // update visual selection highlight if you draw one
}

void DrumGridComponent::setSelectedRows(const juce::Array<int>& rows)
{
    selectedRows_ = rows; // copy
    if (selectedRows_.size() > 0)
        lastSelectedRow_ = selectedRows_.getLast();
    else
        lastSelectedRow_ = -1;
    repaint();
}

juce::File DrumGridComponent::exportSelectedRowsToMidiTemp(uint32_t rowMask, const juce::String& baseFileName, int baseMidi) const
{
    using namespace juce;

    // Defensive: if mask == 0 -> nothing explicit selected; return empty File so caller can fallback.
    if (rowMask == 0u)
        return File();

    const int ppq = 96;
    MidiMessageSequence seq;

    // For each row listed in rowMask, iterate grid cells and add note on/off events
    const int R = (int)cells.size();
    for (int row = 0; row < R; ++row)
    {
        // mask supports rows 0..31. Skip rows not in mask.
        if (row < 32)
        {
            if (((rowMask >> row) & 1u) == 0u) continue;
        }
        else
        {
            // Mask cannot represent this row — skip it (safe fallback).
            continue;
        }

        for (int step = 0; step < totalSteps(); ++step)
        {
            if (!cells[(size_t)row][(size_t)step]) continue;

            // grid uses ticksPerStep (24) per step; convert to ppq space
            const int startTickBoom = step * ticksPerStep; // e.g., 24 * step
            const int startPpq = (startTickBoom * ppq) / ticksPerStep; // maps exactly
            const int lenPpq = (ticksPerStep * ppq) / ticksPerStep;   // equals ppq
            const int endPpq = startPpq + juce::jmax(1, lenPpq);

            const int midiNote = juce::jlimit(0, 127, baseMidi + row);
            const uint8 vel = (uint8)juce::jlimit(1, 127, 100);

            // Channel 10 for drums (GM) -> Midi channel number 10.
            seq.addEvent(MidiMessage::noteOn(10, midiNote, vel), startPpq);
            seq.addEvent(MidiMessage::noteOff(10, midiNote), endPpq);
        }
    }

    seq.updateMatchedPairs();

    MidiFile mf;
    mf.setTicksPerQuarterNote(ppq);
    mf.addTrack(seq);

    auto outFile = File::getSpecialLocation(File::tempDirectory).getChildFile(baseFileName + ".mid");
    if (outFile.existsAsFile()) outFile.deleteFile();

    FileOutputStream os(outFile);
    if (!os.openedOk()) return File();

    mf.writeTo(os);
    os.flush();

    return outFile;
}

