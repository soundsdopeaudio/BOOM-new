#include "PianoRollComponent.h"

// ===== Header band with bar/beat markers =====
void PianoRollComponent::paintHeader(juce::Graphics& g)
{
    using namespace boomtheme;

    const int W = getWidth();
    const int H = getHeight();
    const int headerTop = 0;
    const int headerHeight = headerH_;
    const int gridStartX = leftMargin_;
    const int beatsPerBar = juce::jmax(1, timeSigNum_);
    const int totalBars = juce::jmax(1, barsToDisplay_);
    const int cellsPB = cellsPerBeat_;
    const int cellW = cellPixelWidth_;

    // Header background
    g.setColour(HeaderBackground());
    g.fillRect(0, headerTop, W, headerHeight);

    // Bottom divider
    g.setColour(PanelStroke().withAlpha(0.25f));
    g.fillRect(0, headerTop + headerHeight - 1, W, 1);

    // Draw per-bar label, bar lines, and beat numbers
    int x = gridStartX;
    for (int bar = 0; bar < totalBars; ++bar)
    {
        const int barPixelWidth = beatsPerBar * cellsPB * cellW;

        // Bar start line (thicker)
        g.setColour(PanelStroke().withAlpha(0.80f));
        g.drawLine((float)x, (float)headerHeight, (float)x, (float)H, 2.0f);

        // Beat numbers inside this bar
        g.setColour(LightAccent().withAlpha(0.90f));
        g.setFont(juce::Font(11.0f, juce::Font::plain));
        for (int beatIdx = 0; beatIdx < beatsPerBar; ++beatIdx)
        {
            const int beatStartX = x + beatIdx * cellsPB * cellW;
            const int beatWidthPx = cellsPB * cellW;
            const int textX = beatStartX;
            g.drawFittedText(juce::String(beatIdx + 1),
                textX, headerTop, beatWidthPx, headerHeight,
                juce::Justification::centred, 1);
        }

        x += barPixelWidth;
    }

    // Final right edge
    g.setColour(GridLine());
    g.drawLine((float)x, (float)headerHeight, (float)x, (float)H);
}

// ===== Grid body (pitches x time) =====
void PianoRollComponent::paintGrid(juce::Graphics& g)
{
    const int gridX = leftMargin_;
    const int gridY = headerH_;
    const int gridW = juce::jmax(0, getWidth() - leftMargin_);
    const int gridH = juce::jmax(0, getHeight() - headerH_);
    const juce::Rectangle<int> area(gridX, gridY, gridW, gridH);

    // Background
    g.setColour(boomtheme::GridBackground());
    g.fillRect(area);

    g.saveState();
    g.reduceClipRegion(area);

    // --- alternating row shading that tracks white/black keys ---
    {
        const int rows = (pitchMax_ - pitchMin_) + 1;
        if (rows > 0)
        {
            // Two close shades; black-key rows a bit brighter to pop subtly
            const juce::Colour whiteRow = boomtheme::GridBackground().brighter(0.06f);
            const juce::Colour blackRow = boomtheme::GridBackground().brighter(0.12f);

            auto isBlackKey = [](int midiNote) noexcept
                {
                    switch (midiNote % 12)
                    {
                    case 1: case 3: case 6: case 8: case 10: return true;
                    default: return false;
                    }
                };

            for (int p = pitchMax_; p >= pitchMin_; --p)
            {
                const int yTop = pitchToY(p);
                const int yBot = (p > pitchMin_) ? pitchToY(p - 1) : getHeight();
                const int rowTop = juce::jlimit(headerH_, getHeight(), yTop);
                const int rowBot = juce::jlimit(headerH_, getHeight(), yBot);
                const int rowH = juce::jmax(1, rowBot - rowTop);

                g.setColour(isBlackKey(p) ? blackRow : whiteRow);
                g.fillRect(area.getX(), rowTop, area.getWidth(), rowH);
            }
        }
    }

    const int totalBeats = beatsPerBar_ * barsToDisplay_;
    const int cellW = cellPixelWidth_;

    for (int beat = 0; beat <= totalBeats; ++beat)
    {
        const int beatX = leftMargin_ + beat * cellsPerBeat_ * cellW;
        const bool isBarLine = (beat % beatsPerBar_) == 0;

        g.setColour(isBarLine ? juce::Colours::black.withAlpha(0.65f)
            : boomtheme::LightAccent().withAlpha(0.55f));
        g.drawLine((float)beatX, (float)headerH_, (float)beatX, (float)getHeight(), isBarLine ? 1.0f : 1.0f);

        // 16th subdivisions within the beat
        for (int c = 1; c < cellsPerBeat_; ++c)
        {
            const int x = beatX + c * cellW;
            g.setColour(boomtheme::GridLine().withAlpha(0.40f));
            g.drawVerticalLine(x, headerH_, (float)getHeight());
        }
    }

    // Horizontal rows (one per semitone)
    const int rows = (pitchMax_ - pitchMin_) + 1;
    const float rowH = (float)contentHeightNoHeader() / (float)rows;

    for (int i = 0; i <= rows; ++i)
    {
        const int y = headerH_ + (int)std::round((float)i * rowH);

        const bool isOctaveLine = (i % 12 == 0);
        if (isOctaveLine)
        {
            // Use your green background color for octave markers
            g.setColour(juce::Colour::fromString("FF2D2E41")); // BOOM green
            g.drawLine((float)leftMargin_, (float)y, (float)getWidth(), (float)y, 2.0f);
        }
        else
        {
            g.setColour(boomtheme::GridLine().withAlpha(0.35f));
            g.drawLine((float)leftMargin_, (float)y, (float)getWidth(), (float)y, 1.0f);
        }
    }


    // Vertical grid lines (beats & 16ths)

    g.restoreState();
}

void PianoRollComponent::paintPianoKeys(juce::Graphics& g)
{
    using namespace boomtheme;

    const int laneX = 0;
    const int laneW = leftMargin_;
    const int laneTop = headerH_;
    const int laneBottom = getHeight();
    const int rows = (pitchMax_ - pitchMin_) + 1;
    if (rows <= 0 || laneW <= 0) return;

    // White key color EXACTLY as requested
    const juce::Colour whiteKey = juce::Colour::fromString("FF3A1484");
    // Black keys: darker variant of the same hue
    const juce::Colour blackKey = whiteKey.darker(0.65f);

    // Black keys are shorter on the RIGHT (like a real piano)
    const int rightNotch = juce::jmax(12, laneW / 5);          // how much shorter than white keys
    const int blackW = juce::jlimit(6, laneW - 6, laneW - rightNotch);
    const int blackX = laneX;
    // start at left edge

    auto isBlackKey = [](int midiNote) noexcept
        {
            switch (midiNote % 12)
            {
            case 1: case 3: case 6: case 8: case 10: return true; // C#,D#,F#,G#,A#
            default: return false;
            }
        };

    for (int p = pitchMax_; p >= pitchMin_; --p)
    {
        const int yTop = pitchToY(p);
        const int yBot = (p > pitchMin_) ? pitchToY(p - 1) : laneBottom;

        const int rowTop = juce::jlimit(laneTop, laneBottom, yTop);
        const int rowBot = juce::jlimit(laneTop, laneBottom, yBot);
        const int rowH = juce::jmax(1, rowBot - rowTop);

        // Base WHITE key: full width
        g.setColour(whiteKey);
        g.fillRect(laneX, rowTop, laneW, rowH);

        // BLACK key overlay: shorter on the right
        if (isBlackKey(p))
        {
            // Black key body (shorter than the lane width)
            g.setColour(blackKey);
            g.fillRect(blackX, rowTop, blackW, rowH);

            // A subtle “termination” edge where the black key ends
            g.setColour(boomtheme::PanelStroke().withAlpha(0.65f));
            g.drawLine((float)(blackX + blackW), (float)rowTop, (float)(blackX + blackW), (float)rowBot, 1.0f);
        }

        // Subtle row divider
        g.setColour(PanelStroke().withAlpha(0.25f));
        g.fillRect(laneX, rowBot - 1, laneW, 1);

        // C octave labels
        if ((p % 12) == 0)
        {
            g.setColour(LightAccent().withAlpha(0.90f));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            const int octave = p / 12 - 1;
            g.drawText("C" + juce::String(octave),
                laneX + 6, rowTop, laneW - 8, juce::jmax(12, rowH),
                juce::Justification::centredLeft, false);
        }
    }

    // Right border of the key lane
    g.setColour(PanelStroke().withAlpha(0.85f));
    g.fillRect(laneX + laneW - 1, laneTop, 1, laneBottom - laneTop);
}

// ===== Note blocks =====
void PianoRollComponent::paintNotes(juce::Graphics& g)
{
    // Fill for notes
    juce::Colour noteFill;
    try { noteFill = boomtheme::NoteFill(); }
    catch (...) {}

    const int rows = (pitchMax_ - pitchMin_) + 1;
    const float rowH = rows > 0 ? (float)contentHeightNoHeader() / (float)rows : 16.0f;

    for (const auto& n : pattern)
    {
        const int x = tickToX(n.startTick);
        const int w = juce::jmax(1, (int)std::round(pixelsPerTick_ * (float)n.lengthTicks));
        const int yT = pitchToY(n.pitch);
        const int yB = pitchToY(n.pitch - 1);
        const int h = juce::jmax(6, yB - yT); // pad min height

        // Clamp to component bounds
        const int y = juce::jlimit(headerH_, getHeight(), yT + 1);
        const int maxX = getWidth();
        const int clampedW = juce::jlimit(1, juce::jmax(1, maxX - x), w);

        // Body
        g.setColour(noteFill.withAlpha(0.95f));
        g.fillRoundedRectangle(juce::Rectangle<float>((float)x + 1.0f,
            (float)y + 1.0f,
            (float)clampedW - 2.0f,
            (float)h - 2.0f), 4.0f);

        // Outline
        g.setColour(juce::Colours::black.withAlpha(0.70f));
        g.drawRoundedRectangle(juce::Rectangle<float>((float)x + 1.0f,
            (float)y + 1.0f,
            (float)clampedW - 2.0f,
            (float)h - 2.0f), 4.0f, 1.6f);
    }
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
    paintHeader(g);
    paintPianoKeys(g);
    paintGrid(g);
    paintNotes(g);

}

void PianoRollComponent::resized()
{

}
