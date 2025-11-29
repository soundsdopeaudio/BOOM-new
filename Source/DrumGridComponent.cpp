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

    // Defensive: if no cells, return 0 (means "no filter" / include all)
    if (cells.empty())
        return 0u;

    // For each row, mark it if any cell in that row is non-zero/active.
    // This assumes `cells` is a vector-like container where each row is indexable and
    // non-zero value == active/hit. This matches how the grid draws/uses `cells` elsewhere.
    const size_t maxRows = juce::jmin((size_t)32, cells.size()); // limit to 32 rows for mask safety
    for (size_t r = 0; r < maxRows; ++r)
    {
        bool anyActive = false;

        // row might be something like std::vector<int> or std::vector<uint8_t>
        // we iterate generically with range-for
        for (const auto& v : cells[r])
        {
            if (v != 0) { anyActive = true; break; }
        }

        if (anyActive)
            mask |= (1u << (unsigned)r);
    }

    // If mask==0 here you can interpret it as "no rows chosen" (calling code may treat 0 as "include all").
    return mask;
}

