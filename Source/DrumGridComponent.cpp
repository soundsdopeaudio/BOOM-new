#include "DrumGridComponent.h"
#include "Theme.h"
#include "PluginEditor.h"

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

    // If no rows are selected, the behaviour is to include all rows, so we return 0.
    if (selectedRows_.isEmpty())
        return 0u;

    // For each selected row, add it to the bitmask.
    for (int row : selectedRows_)
    {
        if (row >= 0 && row < 32) // Basic range check for a 32-bit mask
        {
            mask |= (1u << row);
        }
    }

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

static int yToRowIndex(const juce::Component& c, int y, int numRows)
{
    if (numRows <= 0) return 0;
    const float rowH = (float)c.getHeight() / (float)numRows;
    int r = (int)std::floor((float)y / rowH);
    return juce::jlimit(0, numRows - 1, r);
}

// ===== Unified mouseDown handler (handles single, shift-select, and double-click) =====
void DrumGridComponent::mouseDown(const juce::MouseEvent& e)
{

    const Hit h = hitTest(e.position);
    DBG("DrumGridComponent::mouseDown valid=" << (int)h.valid << " onLabel=" << (int)h.onLabel
        << " row=" << h.row << " step=" << h.step);
    if (!h.valid) return;

    if (h.onLabel)
    {
        // Toggle row enabled/disabled
        const bool now = !(rowEnabled[(size_t)h.row]);
        rowEnabled[(size_t)h.row] = now;
        repaint();
        return;
    }

    // Start paint sweep on this row
    dragging = true;
    dragRow = h.row;
    dragValue = !cells[(size_t)h.row][(size_t)h.step]; // invert current cell and paint that value
    setCell(h.row, h.step, dragValue);

    // convert y -> row index (replace numRows_ with your actual member if different)
    const int row = yToRowIndex(*this, e.y, numRows_);

    const bool shift = e.mods.isShiftDown();
    const bool ctrlOrCmd = e.mods.isCommandDown() || e.mods.isCtrlDown();
    const int clicks = e.getNumberOfClicks();

    if (clicks > 1)
    {
        // double-click: treat as toggle + ensure it's the only selection (common UX)
        if (selectedRows_.contains(row))
        {
            // already selected: clear all others, keep this one
            selectedRows_.clearQuick();
            selectedRows_.add(row);
        }
        else
        {
            // make this the only selected row
            selectedRows_.clearQuick();
            selectedRows_.add(row);
        }
        lastSelectedRow_ = row;
    }
    else
    {
        // single-click behavior
        if (shift && lastSelectedRow_ >= 0)
        {
            // contiguous selection between lastSelectedRow_ and clicked row
            int a = juce::jmin(lastSelectedRow_, row);
            int b = juce::jmax(lastSelectedRow_, row);
            for (int r = a; r <= b; ++r)
                if (!selectedRows_.contains(r))
                    selectedRows_.add(r);
        }
        else if (ctrlOrCmd)
        {
            // ctrl/cmd-click toggles single row without affecting others
            if (selectedRows_.contains(row))
                selectedRows_.removeAllInstancesOf(row);
            else
                selectedRows_.add(row);

            lastSelectedRow_ = row;
        }
        else
        {
            // plain click: toggle selection of that row but make it the primary selection
            if (selectedRows_.contains(row))
            {
                // if already the only selection, unselect it
                if (selectedRows_.size() == 1 && selectedRows_.contains(row))
                {
                    selectedRows_.clearQuick();
                    lastSelectedRow_ = -1;
                }
                else
                {
                    // make this the only selection
                    selectedRows_.clearQuick();
                    selectedRows_.add(row);
                    lastSelectedRow_ = row;
                }
            }
            else
            {
                // select only this row
                selectedRows_.clearQuick();
                selectedRows_.add(row);
                lastSelectedRow_ = row;
            }
        }
    }

    // UI feedback
    repaint();

}