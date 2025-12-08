#include "DrumGridComponent.h"
#include "Theme.h"
#include "GridUtils.h"

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

    // If mask == 0 -> caller should fallback; return empty file to indicate failure/no-selection.
    if (rowMask == 0u)
        return File();

    const int ppq = 96;
    MidiMessageSequence seq;

    // Use grid helper for ticks-per-step (PPQ ticks per cell/grid-step)
    const int ticksPerStepPpq = boom::grid::ticksPerStepFromPpq(ppq, cellsPerBeat_);

    // Instead of exporting from the boolean cell grid (which quantizes to step indices and
    // collapses triplet/dotted durations), export directly from the processor's stored pattern
    // which keeps exact PPQ tick values (preserving triplets/dotted/sub-16th notes).
    const auto& procPat = proc.getDrumPattern();
    for (const auto& n : procPat)
    {
        // only include rows matching mask
        if (rowMask != 0u)
        {
            if (n.row < 0 || n.row >= 32) continue;
            if (((rowMask >> n.row) & 1u) == 0u) continue;
        }

        const int startPpq = juce::jmax(0, n.startTick);
        const int lenPpq = juce::jmax(1, n.lengthTicks);
        const int endPpq = startPpq + lenPpq;

        const int midiNote = juce::jlimit(0, 127, baseMidi + n.row);
        const uint8 vel = (uint8)juce::jlimit(1, 127, n.velocity);

        seq.addEvent(MidiMessage::noteOn(10, midiNote, vel), (double)startPpq);
        seq.addEvent(MidiMessage::noteOff(10, midiNote), (double)endPpq);
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
    os.flush(); // optional

    return outFile;
}


