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
