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

        // Beat numbers inside this bar (ADAPTS TO TIME SIGNATURE)
        // beatsPerBar is derived from timeSigNum_, so this automatically shows correct beat count
        // e.g., 1-5 for 5/4, 1-7 for 7/4, etc.
        g.setColour(LightAccent().withAlpha(0.90f));
        g.setFont(juce::Font(11.0f, juce::Font::plain));
        for (int beatIdx = 0; beatIdx < beatsPerBar; ++beatIdx)
        {
            const int beatStartX = x + beatIdx * cellsPB * cellW;
            const int beatWidthPx = cellsPB * cellW;
            const int textX = beatStartX;

            if (beatIdx == 0)
            {
                // BAR NUMBER: brighter + bold + slight outline for readability
                g.setFont(juce::Font(11.5f, juce::Font::bold));
                g.setColour(LightAccent().withAlpha(1.0f));

                // Draw a subtle shadow/outline effect so it stands out on any header color
                g.drawFittedText(juce::String(bar + 1),
                    textX + 1, headerTop + 1, beatWidthPx, headerHeight,
                    juce::Justification::centredLeft, 1);

                g.setColour(juce::Colours::black.withAlpha(0.25f));
                g.drawFittedText(juce::String(bar + 1),
                    textX, headerTop, beatWidthPx, headerHeight,
                    juce::Justification::centredLeft, 1);

                // Restore main colour on top
                g.setColour(LightAccent().withAlpha(1.0f));
                g.drawFittedText(juce::String(bar + 1),
                    textX, headerTop, beatWidthPx, headerHeight,
                    juce::Justification::centredLeft, 1);
            }
            else
            {
                g.setFont(juce::Font(11.0f, juce::Font::plain));
                g.setColour(LightAccent().withAlpha(0.85f));
                g.drawFittedText(juce::String(beatIdx + 1),
                    textX, headerTop, beatWidthPx, headerHeight,
                    juce::Justification::centredLeft, 1);
            }
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

    // Draw live musical readout when resizing (show human-friendly denom like 1/8, 1/16 triplet, dotted)
    if (resizing_ && resizingNoteIndex_ >= 0 && resizingNoteIndex_ < pattern.size())
    {
        const auto& n = pattern.getReference(resizingNoteIndex_);
        const int len = n.lengthTicks;

        // Try to match common denominators first
        juce::String labelTxt;
        const int ppq = 96;
        const int denoms[] = { 1,2,4,8,16,32,64 };
        bool matched = false;
        for (auto d : denoms)
        {
            const int base = boom::grid::ticksForDenominator(ppq, d);
            if (std::abs(len - base) <= 1)
            {
                labelTxt = "1/" + juce::String(d);
                matched = true; break;
            }
            if (std::abs(len - boom::grid::dottedTicks(base)) <= 1)
            {
                labelTxt = "1/" + juce::String(d) + " dotted"; matched = true; break;
            }
            if (std::abs(len - boom::grid::tripletTicks(base)) <= 1)
            {
                labelTxt = "1/" + juce::String(d) + " triplet"; matched = true; break;
            }
        }

        if (!matched)
        {
            // fallback to showing ticks if nothing musical matched
            labelTxt = juce::String(len) + " ticks";
        }

        juce::String txt = labelTxt;
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        const int sx = lastMouseX_ + 8;
        const int sy = juce::jmax(headerH_ + 4, lastMouseY_ - 12);
        const int boxW = 160;
        g.fillRoundedRectangle((float)sx, (float)sy, (float)boxW, 20.0f, 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(txt, sx + 6, sy, boxW - 12, 20, juce::Justification::centredLeft, false);
    }
    }

    const int beatsPerBar = juce::jmax(1, timeSigNum_);
    const int totalBeats = beatsPerBar * barsToDisplay_;
    const int cellW = cellPixelWidth_;

    for (int beat = 0; beat <= totalBeats; ++beat)
    {
        const int beatX = leftMargin_ + beat * cellsPerBeat_ * cellW;
        const bool isBarLine = (beat % beatsPerBar) == 0;

        // Stronger visual for bar lines, medium for beat lines
        if (isBarLine)
            g.setColour(juce::Colours::white.withAlpha(0.12f));
        else
            g.setColour(boomtheme::LightAccent().withAlpha(0.28f));

        const float thickness = isBarLine ? 2.0f : 1.0f;
        g.drawLine((float)beatX, (float)headerH_, (float)beatX, (float)getHeight(), thickness);

        // 16th subdivisions within the beat (lighter)
        for (int c = 1; c < cellsPerBeat_; ++c)
        {
            const int x = beatX + c * cellW;
            g.setColour(boomtheme::GridLine().withAlpha(0.28f));
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
            // Slightly stronger octave separators (still subtle)
            g.setColour(boomtheme::PanelStroke().withAlpha(0.22f));
            g.drawLine((float)leftMargin_, (float)y, (float)getWidth(), (float)y, 1.6f);
        }
        else
        {
            // Light semitone separators
            g.setColour(boomtheme::PanelStroke().withAlpha(0.12f));
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

    // Black keys are shorter on the RIGHT (like a real piano) - 65% of white key width
    const int blackW = juce::jlimit(6, laneW - 6, (int)std::round(laneW * 0.65f));
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
            // Black keys: slightly shorter in HEIGHT (centered vertically), like a real keyboard
            const int blackH = juce::jmax(2, (int)std::round((float)rowH * 0.72f)); // 72% height
            const int blackY = rowTop + (rowH - blackH) / 2;

            g.setColour(blackKey);
            g.fillRect(blackX, blackY, blackW, blackH);

            // Subtle “termination” edge where the black key ends (match the shorter height)
            g.setColour(boomtheme::PanelStroke().withAlpha(0.65f));
            g.drawLine((float)(blackX + blackW), (float)blackY,
                (float)(blackX + blackW), (float)(blackY + blackH), 1.0f);
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
        const int ppq = 96; // match project PPQ
        // Query editor APVTS via a global or external proc is not available here; we assume pattern contains raw ticks
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

        // Draw dotted/triplet markers on piano roll.
        // Determine if the note's length is an explicit dotted/triplet duration.
        const bool isDotted = boom::grid::isDottedTicks(n.lengthTicks, ppq);
        const bool isTrip = boom::grid::isTripletTicks(n.lengthTicks, ppq);

        // Read UI toggles & densities so we can optionally DECORATE notes probabilistically even when
        // their stored length isn't a dotted/triplet variant. This visually reflects the user's density sliders.
        bool allowDotted = false, allowTrip = false;
        int dottedPct = 0, tripletPct = 0;
        if (processor.apvts.getRawParameterValue("useDotted") != nullptr)
            allowDotted = processor.apvts.getRawParameterValue("useDotted")->load() > 0.5f;
        if (processor.apvts.getRawParameterValue("useTriplets") != nullptr)
            allowTrip = processor.apvts.getRawParameterValue("useTriplets")->load() > 0.5f;

        if (processor.apvts.getRawParameterValue("dottedDensity") != nullptr)
        {
            auto v = processor.apvts.getRawParameterValue("dottedDensity")->load();
            dottedPct = (v > 1.5f) ? (int)juce::roundToInt(v) : (int)juce::roundToInt(v * 100.0f);
        }
        if (processor.apvts.getRawParameterValue("tripletDensity") != nullptr)
        {
            auto v = processor.apvts.getRawParameterValue("tripletDensity")->load();
            tripletPct = (v > 1.5f) ? (int)juce::roundToInt(v) : (int)juce::roundToInt(v * 100.0f);
        }

        // Decide whether to decorate this note as dotted/triplet based on deterministic pseudo-random per-note seed.
        bool decorateAsDotted = isDotted;
        bool decorateAsTrip = isTrip;
        if (!decorateAsDotted && !decorateAsTrip)
        {
            juce::Random r((int)(n.startTick * 31 + n.pitch * 97 + n.lengthTicks * 13));
            const int roll = r.nextInt(100);
            if (allowTrip && tripletPct > 0 && roll < tripletPct)
                decorateAsTrip = true;
            else if (allowDotted && dottedPct > 0 && roll < dottedPct)
                decorateAsDotted = true;
        }

        if (decorateAsDotted || decorateAsTrip)
        {
            // accent stripe on left of note — subtle, not garish
            const float stripeW = juce::jmin(6.0f, (float)h * 0.18f);
            const juce::Colour tripCol = juce::Colour::fromString("FF2D0050"); // dark indigo (subtle)
            const juce::Colour dotCol = juce::Colour::fromString("FF8A5DBE");  // subtle lighter purple
            juce::Colour accent = decorateAsTrip ? tripCol : dotCol;

            g.setColour(accent.withAlpha(0.80f));
            g.fillRoundedRectangle((float)x + 2.0f, (float)y + 2.0f, stripeW, (float)h - 4.0f, 2.0f);

            // very subtle glow under note for emphasis (low alpha)
            g.setColour(accent.withAlpha(0.08f));
            g.fillRoundedRectangle((float)x - 2.0f, (float)y - 2.0f, (float)clampedW + 4.0f, (float)h + 4.0f, 6.0f);

            if (decorateAsDotted)
            {
                // small ring-style dot near right edge with muted outline
                const float cx = (float)x + (float)clampedW - juce::jmin(10.0f, (float)clampedW * 0.12f);
                const float cy = (float)y + (float)h * 0.5f;
                const float rdot = juce::jmin(5.0f, (float)h * 0.22f);
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillEllipse(cx - rdot * 0.5f, cy - rdot * 0.5f, rdot, rdot);
                g.setColour(accent.withAlpha(0.95f));
                g.fillEllipse(cx - rdot * 0.35f, cy - rdot * 0.35f, rdot * 0.7f, rdot * 0.7f);
            }
            else if (decorateAsTrip)
            {
                // small subtle badge for triplet near right edge
                const float badgeW = juce::jmin(18.0f, (float)clampedW * 0.18f);
                const float bx = (float)x + (float)clampedW - badgeW - 6.0f;
                const float by = (float)y + 4.0f;
                g.setColour(accent.withAlpha(0.85f));
                g.fillRoundedRectangle(juce::Rectangle<float>(bx, by, badgeW, badgeW * 0.55f), 3.0f);
                g.setColour(juce::Colours::black.withAlpha(0.85f));
                g.setFont(juce::Font((float)juce::jmax(9, (int)std::round(badgeW * 0.45f)), juce::Font::bold));
                g.drawText("3", (int)bx, (int)by, (int)badgeW, (int)juce::roundToInt(badgeW * 0.55f), juce::Justification::centred, false);
            }
        }
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

void PianoRollComponent::mouseMove(const juce::MouseEvent& e)
{
    lastMouseX_ = e.x;
    lastMouseY_ = e.y;
    // Change cursor if hovering near a note's right edge
    const int mx = e.x;
    const int my = e.y;
    bool near = false;

    for (int i = 0; i < pattern.size(); ++i)
    {
        const auto& n = pattern.getReference(i);
        const int x = tickToX(n.startTick);
        const int w = juce::jmax(1, (int)std::round(pixelsPerTick_ * (float)n.lengthTicks));
        const int yT = pitchToY(n.pitch);
        const int yB = pitchToY(n.pitch - 1);

        if (my >= yT && my <= yB)
        {
            const int right = x + w;
            if (mx >= right - kResizeHandlePx && mx <= right + 2)
            {
                near = true; break;
            }
        }
    }

    if (near)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    const int mx = e.x;
    const int my = e.y;

    // detect handle hit and begin resizing
    for (int i = 0; i < pattern.size(); ++i)
    {
        const auto& n = pattern.getReference(i);
        const int x = tickToX(n.startTick);
        const int w = juce::jmax(1, (int)std::round(pixelsPerTick_ * (float)n.lengthTicks));
        const int yT = pitchToY(n.pitch);
        const int yB = pitchToY(n.pitch - 1);

        if (my >= yT && my <= yB)
        {
            const int right = x + w;
            if (mx >= right - kResizeHandlePx && mx <= right + 2)
            {
                resizing_ = true;
                resizingNoteIndex_ = i;
                resizeInitialMouseX_ = mx;
                resizeOriginalLenTicks_ = n.lengthTicks;
                return;
            }
        }
    }

    // not a resize — clear state
    resizing_ = false;
    resizingNoteIndex_ = -1;

    // Right-click on piano roll: delete note under mouse if any
    if (e.mods.isRightButtonDown())
    {
        // find pattern index whose visual rect contains the click (approx)
        for (int i = 0; i < pattern.size(); ++i)
        {
            const auto& n = pattern.getReference(i);
            const int x = tickToX(n.startTick);
            const int w = juce::jmax(1, (int)std::round(pixelsPerTick_ * (float)n.lengthTicks));
            const int yT = pitchToY(n.pitch);
            const int yB = pitchToY(n.pitch - 1);
            const int h = juce::jmax(6, yB - yT);

            if (mx >= x && mx <= x + w && my >= yT && my <= yT + h)
            {
                // remove note i from pattern and update processor
                BoomAudioProcessor::MelPattern newPat = pattern;
                newPat.remove(i);
                processor.setMelodicPattern(newPat);
                setPattern(newPat);
                repaint();
                return;
            }
        }
    }
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    lastMouseX_ = e.x;
    lastMouseY_ = e.y;

    if (!resizing_ || resizingNoteIndex_ < 0 || resizingNoteIndex_ >= pattern.size()) return;

    const int mx = e.x;
    const int dx = mx - resizeInitialMouseX_;
    const int deltaTicks = juce::roundToInt((float)dx / juce::jmax(0.0001f, pixelsPerTick_));

    int newLen = juce::jmax(1, resizeOriginalLenTicks_ + deltaTicks);

    // snapping rules: consult APVTS toggles on processor
    const int ppq = 96;
    const bool showTriplets = (processor.apvts.getRawParameterValue("useTriplets") != nullptr) ? (processor.apvts.getRawParameterValue("useTriplets")->load() > 0.5f) : false;
    const bool showDotted = (processor.apvts.getRawParameterValue("useDotted") != nullptr) ? (processor.apvts.getRawParameterValue("useDotted")->load() > 0.5f) : false;

    if (!showTriplets && !showDotted)
    {
        // snap to grid step when neither special type is allowed
        newLen = boom::grid::snapTicksToGridStep(newLen, ppq, cellsPerBeat_);
    }
    else
    {
        // snap to nearest allowed musical subdivision (respecting which types are enabled)
        newLen = boom::grid::snapTicksToNearestSubdivision(newLen, ppq, showDotted, showTriplets);
    }

    newLen = juce::jmax(1, newLen);

    // apply change
    pattern.getReference(resizingNoteIndex_).lengthTicks = newLen;

    // push to processor so other UI/tools see it
    processor.setMelodicPattern(pattern);
    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&)
{
    if (resizing_)
    {
        resizing_ = false;
        resizingNoteIndex_ = -1;
    }
}

