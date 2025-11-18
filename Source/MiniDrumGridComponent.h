#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "PluginProcessor.h"

// A lightweight, self-contained mini drum grid used by Slapsmith/AI windows.
// Visual appearance and interaction match DrumGridComponent but this class
// does NOT reference the processor and stores its own pattern state so it
// can live independently.
class MiniDrumGridComponent : public juce::Component
{
public:
    explicit MiniDrumGridComponent(int barsToShow = 4, int stepsPerBar_ = 16)
        : bars(barsToShow), stepsPerBar(stepsPerBar_)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setInterceptsMouseClicks(true, true);

        // default labels (same order as main grid expects)
        setRows({ "Kick", "Snare", "ClosedHat", "OpenHat", "Clap", "Perc" });
        clearGrid();
    }

    // basic setters that match the main DrumGridComponent API used by the editor
    void setTimeSignature(int num, int den)
    {
        timeSigNum_ = juce::jmax(1, num);
        timeSigDen_ = juce::jmax(1, den);
        resized();
        repaint();
    }

    void setBarsToDisplay(int b) noexcept { barsToDisplay_ = juce::jlimit(1, 16, b); repaint(); }
    int getBarsToDisplay() const noexcept { return barsToDisplay_; }
    void setShowBarHeader(bool s) { showBarHeader = s; repaint(); }

    void setRowHeightPixels(int px) noexcept { rowHpx_ = juce::jlimit(8, 32, px); repaint(); }

    void setRows(const juce::StringArray& names)
    {
        rowNames = names;
        const int r = juce::jmax(1, rowNames.size());
        cells.resize((size_t)r);
        rowEnabled.resize((size_t)r, true);
        for (auto& row : cells) row.assign(totalSteps(), false);
        repaint();
    }

    // Accept your processor-style Pattern directly (same shape used elsewhere)
    template<typename PatternT>
    void setPattern(const PatternT& pat)
    {
        clearGrid();
        for (const auto& n : pat)
        {
            if (n.row < 0 || n.row >= (int)cells.size()) continue;
            const int step = (n.startTick / ticksPerStep) % totalSteps();
            if (step >= 0 && step < totalSteps())
                cells[(size_t)n.row][(size_t)step] = true;
        }
        repaint();
    }

    // Read out only enabled rows (same contract as DrumGridComponent)
    template<typename PatternOut>
    void getPatternEnabledRows(PatternOut& out) const
    {
        for (int r = 0; r < (int)cells.size(); ++r)
        {
            if (!rowEnabled[(size_t)r]) continue;
            for (int s = 0; s < totalSteps(); ++s)
                if (cells[(size_t)r][(size_t)s])
                    out.add({ 0, r, s * ticksPerStep, ticksPerStep, 100 });
        }
    }

    // External hook (editor assigns)
    std::function<void(int row, int step, bool value)> onCellEdited;
    std::function<void(int row, int tick)> onToggle;

    void paint(juce::Graphics& g) override
    {
        using namespace boomtheme;
        g.fillAll(GridBackground());

        // header
        const int headerH = headerH_;
        g.setColour(HeaderBackground());
        g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, (float)getWidth(), (float)headerH));
        g.setColour(PanelStroke().withAlpha(0.25f));
        g.fillRect(juce::Rectangle<float>(0.0f, (float)headerH - 1.0f, (float)getWidth(), 1.0f));

        // compute geometry
        const int R = juce::jmax(1, (int)cells.size());
        const int C = totalSteps();
        const float labelWf = labelWidth();
        const float gridX = (float)labelWf;
        const float gridW = (float)(getWidth() - labelWf);
        const float cellH = (float)(getHeight() - headerH) / (float)R;
        const float cellW = gridW / (float)C;

        // header bar numbers
        int x = (int)gridX;
        for (int bar = 0; bar < barsToDisplay_; ++bar)
        {
            const int barPixelWidth = (int)std::round(timeSigNum_ * cellsPerBeat_ * cellW);
            g.setColour(LightAccent().withAlpha(0.95f));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawFittedText(juce::String(bar + 1), x, 0, barPixelWidth, headerH, juce::Justification::centred, 1);
            g.setColour(PanelStroke().withAlpha(0.80f));
            g.drawLine((float)x, (float)headerH, (float)x, (float)getHeight(), 2.0f);
            x += barPixelWidth;
        }

        // Clip and draw grid body
        g.saveState();
        g.reduceClipRegion(0, headerH, getWidth(), getHeight() - headerH);

        // left label band (explicit Rectangle<float> to avoid overload ambiguity)
        g.setColour(boomtheme::PanelStroke());
        g.fillRect(juce::Rectangle<float>(0.0f, (float)headerH, labelWf, (float)(getHeight() - headerH)));

        // Row labels
        for (int row = 0; row < R; ++row)
        {
            const float rowY = (float)headerH + row * cellH;
            g.setColour(juce::Colour::fromString("FF3a1484"));
            g.fillRect(juce::Rectangle<float>(0.0f, rowY, labelWf, cellH));
            g.setColour(juce::Colours::black);
            // use Rectangle<float> overload of drawRect to avoid mixed-type overload ambiguity
            g.drawRect(juce::Rectangle<float>(0.0f, rowY, labelWf, cellH), 1.2f);

            const auto name = rowNames[row];
            g.setColour(rowEnabled[(size_t)row] ? juce::Colour::fromString("FF7cd400") : juce::Colours::grey);
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawFittedText(name, juce::Rectangle<int>((int)0 + 6, (int)rowY, (int)labelWf - 12, (int)cellH),
                juce::Justification::centredLeft, 1);
        }

        // grid background
        g.setColour(GridBackground());
        g.fillRect(juce::Rectangle<float>(gridX, (float)headerH, gridW, (float)(getHeight() - headerH)));

        // grid lines
        g.setColour(GridLine());
        for (int c = 0; c <= C; ++c)
        {
            const float gx = gridX + c * cellW;
            const float thickness = (c % stepsPerBar == 0 ? 1.6f : (c % 4 == 0 ? 1.1f : 0.6f));
            g.drawLine(gx, (float)headerH, gx, (float)getHeight(), thickness);
        }
        for (int row = 0; row <= R; ++row)
        {
            const float y = (float)headerH + row * cellH;
            g.drawLine(gridX, y, gridX + gridW, y, 0.6f);
        }

        // cells
        for (int row = 0; row < R; ++row)
        {
            const bool enabled = rowEnabled[(size_t)row];
            for (int c = 0; c < C; ++c)
            {
                const float x__ = gridX + c * cellW;
                const float y = (float)headerH + row * cellH;
                juce::Rectangle<float> cellR(x__ + 2.f, y + 2.f, cellW - 4.f, cellH - 4.f);

                if (cells[(size_t)row][(size_t)c])
                {
                    g.setColour(juce::Colour::fromString("FF6e138b"));
                    g.fillRoundedRectangle(cellR, 3.5f);
                    g.setColour(juce::Colours::black);
                    g.drawRoundedRectangle(cellR, 3.5f, 1.2f);
                }
                else
                {
                    if (!enabled)
                    {
                        g.setColour(PanelStroke().withAlpha(0.15f));
                        g.fillRoundedRectangle(cellR, 3.5f);
                    }
                }
            }
        }

        g.restoreState();
    }

    void resized() override {}

    void mouseDown(const juce::MouseEvent& e) override
    {
        const Hit h = hitTest(e.position);
        if (!h.valid) return;

        if (h.onLabel)
        {
            const bool now = !(rowEnabled[(size_t)h.row]);
            rowEnabled[(size_t)h.row] = now;
            repaint();
            if (onToggle) onToggle(h.row, 0); // tick value not used by label toggles here
            return;
        }

        dragging = true;
        dragRow = h.row;
        dragValue = !cells[(size_t)h.row][(size_t)h.step];
        setCell(h.row, h.step, dragValue);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging) return;
        const Hit h = hitTest(e.position);
        if (!h.valid) return;
        if (h.row != dragRow) return;
        setCell(h.row, h.step, dragValue);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragging = false;
        dragRow = -1;
    }

    void setCell(int row, int step, bool v)
    {
        if (row < 0 || row >= (int)cells.size()) return;
        if (step < 0 || step >= totalSteps()) return;
        if (!rowEnabled[(size_t)row]) return;

        if (cells[(size_t)row][(size_t)step] == v) return;

        cells[(size_t)row][(size_t)step] = v;
        if (onCellEdited) onCellEdited(row, step, v);
        if (onToggle) onToggle(row, step * ticksPerStep);
        repaint();
    }

    // helpers
    void clearGrid()
    {
        const int R = juce::jmax(1, rowNames.size());
        cells.resize((size_t)R);
        for (auto& row : cells) row.assign(totalSteps(), false);
    }

    int totalSteps() const { return bars * stepsPerBar; }
    float labelWidth() const { return juce::jmax(120.0f, getWidth() * 0.12f); }

private:
    juce::StringArray rowNames;
    std::vector<std::vector<bool>> cells;
    std::vector<bool> rowEnabled;

    const int stepsPerBar = 16;
    const int ticksPerStep = 24;
    const int bars;
    bool dragging = false;
    int dragRow = -1;
    bool dragValue = false;

    int timeSigNum_{ 4 };
    int timeSigDen_{ 4 };
    int barsToDisplay_{ 4 };
    int headerH_ = 18;
    int leftMargin_ = 48;
    int cellsPerBeat_ = 4;
    int rowHpx_ = 16;

    bool showBarHeader = true;

    struct Hit { bool valid = false; bool onLabel = false; int row = -1; int step = -1; };

    Hit hitTest(juce::Point<float> p) const
    {
        Hit h;
        auto r = juce::Rectangle<float>(0.0f, (float)headerH_, (float)getWidth(), (float)(getHeight() - headerH_));
        if (!r.contains(p)) return h;
        const int R = (int)cells.size();
        if (R <= 0) return h;
        const float labelWf = labelWidth();
        const float gridX = r.getX() + labelWf;
        const float gridW = r.getWidth() - labelWf;
        const float cellH = r.getHeight() / (float)R;
        const float cellW = gridW / (float)totalSteps();
        h.row = juce::jlimit(0, R - 1, (int)((p.y - r.getY()) / cellH));
        if (p.x < gridX)
        {
            h.onLabel = true; h.valid = true; return h;
        }
        h.onLabel = false;
        int step = (int)((p.x - gridX) / cellW);
        h.step = juce::jlimit(0, totalSteps() - 1, step);
        h.valid = true; return h;
    }
};