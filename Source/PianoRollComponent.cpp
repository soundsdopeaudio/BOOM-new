#pragma once
#include "PianoRollComponent.h"

void PianoRollComponent::setTimeSignature(int num, int den) noexcept
{
    num = juce::jlimit(1, 32, num);
    den = juce::jlimit(1, 32, den);

    if (timeSigNum_ != num || timeSigDen_ != den)
    {
        timeSigNum_ = num;
        timeSigDen_ = den;
        beatsPerBar_ = num;
        cellsPerBeat_ = (den == 8) ? 3 : 4;
        resized();
        repaint();
    }
}

void PianoRollComponent::setBarsToDisplay(int bars) noexcept
{
    bars = juce::jlimit(1, 64, bars);
    if (barsToDisplay_ != bars)
    {
        barsToDisplay_ = bars;
        resized();
        repaint();
    }
}

int PianoRollComponent::getHeaderHeight() const noexcept
{
    const int rows = 7;
    const int rowH = 18;
    return headerH_ + rows * rowH;
}
