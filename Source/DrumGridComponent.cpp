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

