#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "PluginProcessor.h"
#include "GridUtils.h"

// Draws a scrollable piano-roll view of BoomAudioProcessor::MelPattern
class PianoRollComponent : public juce::Component
{
public:
    explicit PianoRollComponent(BoomAudioProcessor& p) : processor(p) {}

    // You already call this from the editor after generating:
    void setPattern(const BoomAudioProcessor::MelPattern& pat)
    {
        pattern = pat;
        updateContentSize();
        repaint();
    }

    // You already call these from the editor:
    void setTimeSignature(int num, int den)
    {
        timeSigNum_ = juce::jmax(1, num);
        timeSigDen_ = juce::jmax(1, den);

        // keep the grid logic in-sync too
        beatsPerBar_ = timeSigNum_;

        repaint();
    }

    void setBarsToDisplay(int bars)
    {
        barsToDisplay_ = (bars > 0 ? bars : 4);
        updateContentSize();
        repaint();
    }

    // Optional helpers (you had partial stubs for these already):
    int  getTimeSigNumerator()   const noexcept { return timeSigNum_; }
    int  getTimeSigDenominator() const noexcept { return timeSigDen_; }

    // Coordinate helpers (used internally and handy for future features)
    int pitchToY(int p) const
    {
        const int rows = (pitchMax_ - pitchMin_) + 1;
        if (rows <= 0) return headerH_;
        const float rowH = (float)(contentHeightNoHeader()) / (float)rows;
        // y grows down; top row = highest pitch
        return (int)std::floor((float)headerH_ + (float)(pitchMax_ - p) * rowH);
    }

    int tickToX(int tick) const
    {
        return (int)std::floor((float)leftMargin_ + pixelsPerTick_ * (float)tick);
    }

    void setPitchRange(int minPitch, int maxPitch)
    {
        if (minPitch > maxPitch) std::swap(minPitch, maxPitch);
        pitchMin_ = juce::jlimit(0, 127, minPitch);
        pitchMax_ = juce::jlimit(0, 127, maxPitch);
        updateContentSize();
        repaint();
    }

    int contentWidth() const
    {
        const int totalBeats = beatsPerBar_ * barsToDisplay_;
        const int gridPixels = totalBeats * cellsPerBeat_ * cellPixelWidth_;
        return leftMargin_ + gridPixels;
    }

    int contentHeight() const
    {
        return headerH_ + contentHeightNoHeader();
    }

    void setSemitonePixelHeight(int px)
    {
        semitonePixelHeight_ = juce::jlimit(6, 32, px); // clamp: 6–32 px per semitone
        updateContentSize();                             // ensure the viewport updates
        repaint();
    }


    // JUCE paint/resize
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    // === sizing/metrics ===
    // Derived pixelsPerTick_ depends on your generator’s tick resolution.
    // Your melodic generator uses tps=24 (ticks per 1/16), so one beat = 96 ticks.
    void updateDerivedScaling()
    {
        const float pixelsPerBeat = (float)cellsPerBeat_ * (float)cellPixelWidth_;

        // PPQ in this project is 96 ticks per quarter note (same value you're using elsewhere).
        const float ppqQuarter = 96.0f;

        // ticks per beat depends on denominator:
        // beat = 1/den whole note, so relative to quarter: ticksPerBeat = ppqQuarter * (4 / den)
        const float ticksPerBeat = ppqQuarter * (4.0f / (float)timeSigDen_);

        pixelsPerTick_ = pixelsPerBeat / juce::jmax(1.0f, ticksPerBeat);
    }

    int  semitonePixelHeight_{ 12 }; // was hardcoded to 20 before; now adjustable

    int contentHeightNoHeader() const
    {
        const int rows = (pitchMax_ - pitchMin_) + 1;
        return juce::jmax(1, rows * semitonePixelHeight_);
    }

    void updateContentSize()
    {
        // Make this component bigger than the viewport so scrollbars appear
        setSize(contentWidth(), contentHeight());
    }

    // === drawing helpers ===
    void paintHeader(juce::Graphics& g);
    void paintGrid(juce::Graphics& g);
    void paintNotes(juce::Graphics& g);

private:
    BoomAudioProcessor& processor;
    BoomAudioProcessor::MelPattern pattern;
    void paintPianoKeys(juce::Graphics& g);
    // State you already had (keeping names intact):
    int timeSigNum_{ 4 };
    int timeSigDen_{ 4 };
    int barsToDisplay_{ 8 };
    int beatsPerBar_{ 4 };   // 4/4 default
    int headerH_{ 18 };  // header band height (px)
    int leftMargin_{ 96 };   // wider keybed so it looks like a piano
    int cellPixelWidth_{ 16 };  // zoom out horizontally (more time on screen)
    int cellsPerBeat_{ 4 };   // 16th grid = 4 cells per beat
    int pitchMin_{ 36 };  // C2
    int pitchMax_{ 84 };  // C6

    float pixelsPerTick_{ 0.8f }; // recomputed in updateDerivedScaling()

    // ----- note-resize interaction state -----
    bool resizing_ = false;
    int resizingNoteIndex_ = -1; // index in `pattern`
    int resizeInitialMouseX_ = 0;
    int resizeOriginalLenTicks_ = 0;
    const int kResizeHandlePx = 8; // hit region around right edge to start resize
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
};
