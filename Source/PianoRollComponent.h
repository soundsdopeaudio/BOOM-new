#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "PluginProcessor.h"

class PianoRollComponent : public juce::Component
{
public:
    explicit PianoRollComponent(BoomAudioProcessor& p) : processor(p) {}

    void setPattern(const BoomAudioProcessor::Pattern& pat) { pattern = pat; repaint(); }

public:
    void setTimeSignature(int num, int den) noexcept;
    void setBarsToDisplay(int bars) noexcept;

    int  getTimeSigNumerator()   const noexcept { return timeSigNum_; }
    int  getTimeSigDenominator() const noexcept { return timeSigDen_; }

    int  getBarsToDisplay() const noexcept { return barsToDisplay_; }

    int getHeaderHeight() const noexcept;



    void paint(juce::Graphics& g) override
    {
        using namespace boomtheme;
        g.fillAll(GridBackground());


        const int bars = 4;                // grid visual only
        const int stepsPerBar = 16;
        const int cols = bars * stepsPerBar;
        int bars_ = 4;
        int tsNum_ = 4, tsDen_ = 4;

        const int rows = 48;               // 4 octaves view
        const int baseMidi = 36;           // C2 at bottom'

        // --- draw header / bar numbers (NOT clipped) ---
        const int headerH = headerH_;
        const int beatsPerBar = tsNum_;
        const int cellsPerBeat = cellsPerBeat_;
        const int headerTop = 0;
        int x = leftMargin_;

        // header bg
        g.setColour(boomtheme::HeaderBackground());
        g.fillRect(0, headerTop, getWidth(), headerH);

        // bar labels + bar boundary lines
        for (int bar = 0; bar < bars_; ++bar)
        {
            const int cellsThisBar = beatsPerBar * cellsPerBeat;
            const int w = cellsThisBar * cellPixelWidth_;
            g.setColour(boomtheme::LightAccent());
            g.drawFittedText(juce::String(bar + 1),
                x, headerTop, w, headerH,
                juce::Justification::centred, 1);

            g.setColour(boomtheme::GridBackground());
            g.drawLine((float)x, (float)headerH, (float)x, (float)getHeight());
            x += w;
        }
        // final right edge
        g.setColour(boomtheme::GridBackground());
        g.drawLine((float)x, (float)headerH, (float)x, (float)getHeight());

        // --- now clip and draw the grid so header stays visible ---
        g.saveState();
        g.reduceClipRegion(0, headerH, getWidth(), getHeight() - headerH);

        // draw left label band bg under the grid region if you have one
        g.setColour(boomtheme::PanelStroke());
        g.fillRect(0, headerH, leftMargin_, getHeight() - headerH);

        int x_ = leftMargin_;
        for (int bar = 0; bar < barsToDisplay_; ++bar)

        {
            const int cellsThisBar = beatsPerBar_ * cellsPerBeat_;
            const int w = cellsThisBar * cellPixelWidth_;

            g.setColour(boomtheme::NoteFill());
            g.drawFittedText(juce::String(bar + 1),
                { x_, 0, w, headerH_ },
                juce::Justification::centred, 1);

            g.setColour(boomtheme::GridLine());
            g.drawLine((float)x_, (float)headerH_, (float)x_, (float)getHeight());
            x_ += w;
        }


        auto r = getLocalBounds().toFloat();

        // --- keyboard gutter (left) ---
        const float kbW = juce::jmax(60.0f, r.getWidth() * 0.08f);
        const float gridX = r.getX() + kbW;
        const float gridW = r.getWidth() - kbW;

        g.setColour(juce::Colour::fromString("FF6e138b"));
        g.fillRect(juce::Rectangle<float>(r.getX(), r.getY(), kbW, r.getHeight()));

        // Draw white keys
        const float cellH = r.getHeight() / rows;
        for (int i = 0; i < rows; ++i)
        {
            const int midi = baseMidi + (rows - 1 - i);
            const int scaleDegree = midi % 12;
            const bool isBlack = (scaleDegree == 1 || scaleDegree == 3 || scaleDegree == 6 || scaleDegree == 8 || scaleDegree == 10);

            if (!isBlack)
            {
                g.setColour(juce::Colours::black);
                g.drawRect({ r.getX(), r.getY() + i * cellH, kbW, cellH }, 1.2f);
            }
        }

        // Draw black keys at 60% of white key depth
        const float blackDepth = kbW * 0.60f; // <<< shorter black keys
        for (int i = 0; i < rows; ++i)
        {
            const int midi = baseMidi + (rows - 1 - i);
            const int scaleDegree = midi % 12;
            const bool isBlack = (scaleDegree == 1 || scaleDegree == 3 || scaleDegree == 6 || scaleDegree == 8 || scaleDegree == 10);

            if (isBlack)
            {
                auto keyR = juce::Rectangle<float>(r.getX(), r.getY() + i * cellH, blackDepth, cellH);
                g.setColour(juce::Colours::black);
                g.fillRect(keyR);
                g.setColour(boomtheme::LightAccent().darker(0.6f));
                g.drawRect(keyR, 1.0f);
            }
        }



        // --- grid area ---
        g.setColour(GridBackground());
        g.fillRect(juce::Rectangle<float>(gridX, r.getY(), gridW, r.getHeight()));

        // vertical beat lines
        const float cellW = gridW / cols;
        g.setColour(GridLine());
        for (int c = 0; c <= cols; ++c)
        {
            const float x = gridX + c * cellW;
            const float thickness = (c % 4 == 0 ? 1.5f : 0.7f);
            g.drawLine(x, r.getY(), x, r.getBottom(), thickness);
        }
        // horizontal key lines
        for (int i = 0; i <= rows; ++i)
        {
            const float y = r.getY() + i * cellH;
            g.drawLine(gridX, y, gridX + gridW, y, 0.6f);
        }

        // --- notes ---
        g.setColour(NoteFill());
        for (const auto& n : pattern)
        {
            const int col = (n.startTick / 24) % cols;
            const int row = juce::jlimit(0, rows - 1, rows - 1 - ((n.pitch - baseMidi) % rows));
            const float w = cellW * juce::jmax(1, n.lengthTicks / 24) - 4.f;

            g.fillRoundedRectangle(juce::Rectangle<float>(gridX + col * cellW + 2.f,
                r.getY() + row * cellH + 2.f,
                w,
                cellH - 4.f), 4.f);

            g.restoreState();
        }
    }


private:
    BoomAudioProcessor& processor;
    BoomAudioProcessor::Pattern pattern;

    int timeSigNum_     { 4 };
    int timeSigDen_{ 4 };
    int barsToDisplay_{ 4 };
    int beatsPerBar_ = 4;   // default 4/4   // default 4 bars in header
    int headerH_ = 18;  // header band height (px)
    int leftMargin_ = 48;  // room for row labels / piano keys (px)
    int cellsPerBeat_ = 4;   // normal 16th grid = 4 cells per beat
    int cellPixelWidth_ = 16;  // whatever you use to draw columns

};
