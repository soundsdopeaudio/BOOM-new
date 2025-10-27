#include "DrumGridComponent.h"
#include "Theme.h"

void DrumGridComponent::setRowLabelFontHeight(int px) noexcept
{
    rowLabelPx_ = juce::jlimit(8, 18, px);
    repaint();
}

void DrumGridComponent::setTimeSignature(int num, int den) noexcept
{
    num = juce::jlimit(1, 32, num);
    den = juce::jlimit(1, 32, den);

    if (timeSigNum_ != num || timeSigDen_ != den)
    {
        timeSigNum_ = num;
        timeSigDen_ = den;
        beatsPerBar_ = num;
        cellsPerBeat_ = (den == 8) ? 3 : 4;
        resized();     // if layout depends on header width per bar
        repaint();
    }
}

void DrumGridComponent::setBarsToDisplay(int bars) noexcept
{
    bars = juce::jlimit(1, 64, bars);
    if (barsToDisplay_ != bars)
    {
        barsToDisplay_ = bars;
        resized();
        repaint();
    }
}

int DrumGridComponent::getHeaderHeight() const noexcept
{
    const int rows = 7;
    const int rowH = 18;
    return headerH_ + rows * rowH;
}

