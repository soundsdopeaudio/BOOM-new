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
    void setTimeSignature(int num, int den = 4) noexcept;
    void setBarsToDisplay(int bars) noexcept;

    int  getTimeSigNumerator()   const noexcept { return timeSigNum_; }
    int  getTimeSigDenominator() const noexcept { return timeSigDen_; }

    int  getBarsToDisplay() const noexcept { return barsToDisplay_; }


    void paint(juce::Graphics& g) override
    {
        using namespace boomtheme;
        g.fillAll(GridBackground());


        const int bars = 4;                // grid visual only
        const int stepsPerBar = 16;
        const int cols = bars * stepsPerBar;

        const int rows = 48;               // 4 octaves view
        const int baseMidi = 36;           // C2 at bottom

        int x = leftMargin_;
        for (int bar = 0; bar < barsToDisplay_; ++bar)
        {
            const int cellsThisBar = beatsPerBar_ * cellsPerBeat_;
            const int w = cellsThisBar * cellPixelWidth_;

            g.setColour(boomtheme::NoteFill());
            g.drawFittedText(juce::String(bar + 1),
                { x, 0, w, headerH_ },
                juce::Justification::centred, 1);

            g.setColour(boomtheme::GridLine());
            g.drawLine((float)x, (float)headerH_, (float)x, (float)getHeight());
            x += w;
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
