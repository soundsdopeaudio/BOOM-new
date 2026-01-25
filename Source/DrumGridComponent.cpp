#include "DrumGridComponent.h"
#include "Theme.h"
#include "GridUtils.h"

// Drum grid is implemented as a single self-contained component class in this file.
// It maintains a boolean cell matrix for quick editing and also mirrors a precise
// Pattern stored in the processor for accurate export / resizing behavior.

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
        resizeCellsToTotalSteps();
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

    // If explicit selection exists, build mask from it
    if (!selectedRows_.isEmpty())
    {
        for (int row : selectedRows_)
        {
            if (row >= 0 && row < 32)
                mask |= (1u << row);
        }
        return mask;
    }

    // No explicit selection -> use rowEnabled as the mask
    const int maxRows = juce::jmin((int)rowEnabled.size(), 32);
    for (int r = 0; r < maxRows; ++r)
    {
        if (rowEnabled[(size_t)r])
            mask |= (1u << r);
    }

    return mask;
}


juce::Array<int> DrumGridComponent::getSelectedRows() const
{
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
    repaint();
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

    if (rowMask == 0u)
        return File();

    const int ppq = 96;
    MidiMessageSequence seq;

    // Use the same noteForRow mapping as exportSelectionToMidiTemp for consistency
    auto noteForRow = [this](int row) -> int
        {
            if ((unsigned)row < (unsigned)rowNames.size())
            {
                auto name = rowNames[(int)row].toLowerCase();
                if (name.contains("kick"))  return 36;
                if (name.contains("snare")) return 38;
                if (name.contains("clap"))  return 39;
                if (name.contains("rim"))   return 37;
                if (name.contains("open") && name.contains("hat"))   return 46;
                if (name.contains("closed") && name.contains("hat")) return 42;
                if (name.contains("hat"))   return 42;
                if (name.contains("low") && name.contains("tom"))    return 45;
                if (name.contains("mid") && name.contains("tom"))    return 47;
                if (name.contains("high") && name.contains("tom"))   return 50;
                if (name.contains("perc"))  return 48;
                if (name.contains("crash")) return 49;
                if (name.contains("ride"))  return 51;
            }
            // fallback
            switch (row)
            {
            case 0: return 36; case 1: return 38; case 2: return 42; case 3: return 46; default: return 45 + (row % 5);
            }
        };

    const auto& procPat = proc.getDrumPattern();
    for (const auto& n : procPat)
    {
        if (rowMask != 0u)
        {
            if (n.row < 0 || n.row >= 32) continue;
            if (((rowMask >> n.row) & 1u) == 0u) continue;
        }

        const int startPpq = juce::jmax(0, n.startTick);
        const int lenPpq = juce::jmax(1, n.lengthTicks);
        const int endPpq = startPpq + lenPpq;

        const int midiNote = noteForRow(n.row);
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
    os.flush();

    return outFile;
}

juce::File DrumGridComponent::exportSelectedRowsToMidiTempMultiTrack(uint32_t rowMask, const juce::String& baseFileName, int baseMidi) const
{
    using namespace juce;

    if (rowMask == 0u)
        return File();

    const int ppq = 96;
    MidiFile mf;
    mf.setTicksPerQuarterNote(ppq);

    const auto& procPat = proc.getDrumPattern();

    // Create one track per row (up to 32 rows max)
    for (int rowIdx = 0; rowIdx < 32; ++rowIdx)
    {
        // Check if this row is in the mask
        if (((rowMask >> rowIdx) & 1u) == 0u)
            continue;

        // Create a sequence for this row
        MidiMessageSequence rowSeq;

        // Add all notes from this row
        for (const auto& n : procPat)
        {
            if (n.row != rowIdx)
                continue;

            const int startPpq = juce::jmax(0, n.startTick);
            const int lenPpq = juce::jmax(1, n.lengthTicks);
            const int endPpq = startPpq + lenPpq;

            const int midiNote = juce::jlimit(0, 127, baseMidi + n.row);
            const uint8 vel = (uint8)juce::jlimit(1, 127, n.velocity);

            rowSeq.addEvent(MidiMessage::noteOn(1, midiNote, vel), (double)startPpq);
            rowSeq.addEvent(MidiMessage::noteOff(1, midiNote), (double)endPpq);
        }

        // Only add the track if it has notes
        if (rowSeq.getNumEvents() > 0)
        {
            rowSeq.updateMatchedPairs();
            mf.addTrack(rowSeq);
        }
    }

    // Write the file
    auto outFile = File::getSpecialLocation(File::tempDirectory).getChildFile(baseFileName + ".mid");
    if (outFile.existsAsFile()) outFile.deleteFile();

    FileOutputStream os(outFile);
    if (!os.openedOk()) return File();

    mf.writeTo(os);
    os.flush();

    return outFile;
}


