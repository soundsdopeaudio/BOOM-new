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

    // Horizontal zoom: >1.0 = zoomed in (fewer steps shown, bigger cells)
    // Use setViewStartStep(...) to pan the visible window.
    void setZoomX(float z) noexcept
    {
        zoomX_ = juce::jmax(1.0f, z);
        // clamp view start so it remains valid
        const int maxStart = juce::jmax(0, totalSteps() - visibleSteps());
        viewStartStep_ = juce::jlimit(0, maxStart, viewStartStep_);
        repaint();
    }
    float getZoomX() const noexcept { return zoomX_; }

    // Choose which step index becomes the left-most visible column
    void setViewStartStep(int s) noexcept
    {
        const int maxStart = juce::jmax(0, totalSteps() - visibleSteps());
        viewStartStep_ = juce::jlimit(0, maxStart, s);
        repaint();
    }
    int getViewStartStep() const noexcept { return viewStartStep_; }

    // How many actual steps are visible horizontally given current zoom
    int visibleSteps() const noexcept
    {
        const int tot = totalSteps();
        if (tot <= 0) return 1;
        const int v = (int)std::round((double)tot / (double)zoomX_);
        return juce::jlimit(1, tot, v);
    }

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
        const int total = totalSteps();
        if (total <= 0) return; // defensive: avoid modulo-by-zero and out-of-range access

        for (const auto& n : pat)
        {
            if (n.row < 0 || n.row >= (int)cells.size()) continue;
            const int step = (n.startTick / ticksPerStep) % total;
            if (step >= 0 && step < total)
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
        if (C <= 0) return; // nothing to draw safely

        const float labelWf = labelWidth();
        const float gridX = (float)labelWf;
        const float gridW = (float)(getWidth() - labelWf);

        // number of visual columns (may be fewer than totalSteps when zoomed)
        const int visC = visibleSteps();
        const int startStep = juce::jlimit(0, juce::jmax(0, totalSteps() - visC), viewStartStep_);

        // defensive: if there is no horizontal room, skip detailed grid drawing
        if (gridW <= 8.0f)
        {
            // still draw left labels so user can toggle rows, but avoid division by zero later
            g.setColour(boomtheme::PanelStroke());
            g.fillRect(juce::Rectangle<float>(0.0f, (float)headerH, labelWf, (float)(getHeight() - headerH)));
            for (int row = 0; row < R; ++row)
            {
                const float rowY = (float)headerH + row * ((float)(getHeight() - headerH) / (float)R);
                g.setColour(juce::Colour::fromString("FF3a1484"));
                g.fillRect(juce::Rectangle<float>(0.0f, rowY, labelWf, (float)((getHeight() - headerH) / R)));
            }
            return;
        }

        const float cellH = (float)(getHeight() - headerH) / (float)R;
        const float cellW = (gridW > 0.0f && visC > 0) ? (gridW / (float)visC) : 0.0f;

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

        // grid lines (use visible columns and map to actual step indices)
        g.setColour(GridLine());
        for (int vc = 0; vc <= visC; ++vc)
        {
            const int actualStep = startStep + vc;
            const float gx = gridX + vc * cellW;
            const float thickness = (actualStep % stepsPerBar == 0 ? 1.6f : (actualStep % 4 == 0 ? 1.1f : 0.6f));
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
            for (int vc = 0; vc < visC; ++vc)
            {
                const float x__ = gridX + vc * cellW;
                const float y = (float)headerH + row * cellH;

                // map visible column -> actual pattern step index
                const int actualStep = startStep + vc;
                // Make sure we draw a valid, non-negative rectangle even for tiny cellW/cellH.
                float w = cellW - 4.0f;
                float h = cellH - 4.0f;
                float rx = x__ + 2.0f;
                float ry = y + 2.0f;

                if (w < 1.5f || h < 1.5f)
                {
                    // compact fallback drawing for cramped layouts:
                    // draw a 1x1/2 px pixel or a thin line so the note is still visible.
                    if (actualStep >= 0 && actualStep < totalSteps() && cells[(size_t)row][(size_t)actualStep])
                    {
                        g.setColour(juce::Colour::fromString("FF6e138b"));
                        g.fillRect((int)std::round(rx), (int)std::round(ry), juce::jmax(1, (int)std::round(w)), juce::jmax(1, (int)std::round(h)));
                    }
                    else if (!enabled)
                    {
                        g.setColour(PanelStroke().withAlpha(0.15f));
                        g.fillRect((int)std::round(rx), (int)std::round(ry), juce::jmax(1, (int)std::round(w)), juce::jmax(1, (int)std::round(h)));
                    }
                    // small debug index (optional)
                    continue;
                }

                juce::Rectangle<float> cellR(rx, ry, w, h);
                const float corner = juce::jmin(3.5f, juce::jmin(w, h) * 0.25f);

                if (actualStep >= 0 && actualStep < totalSteps() && cells[(size_t)row][(size_t)actualStep])
                {
                    g.setColour(juce::Colour::fromString("FF6e138b"));
                    g.fillRoundedRectangle(cellR, corner);
                    g.setColour(juce::Colours::black);
                    g.drawRoundedRectangle(cellR, corner, 1.0f);
                }
                else
                {
                    if (!enabled)
                    {
                        g.setColour(PanelStroke().withAlpha(0.15f));
                        g.fillRoundedRectangle(cellR, corner);
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
        if (totalSteps() <= 0) return; // defensive
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

    // keep label width constrained so it cannot consume entire component width
    float labelWidth() const
    {
        const int gw = getWidth();
        float lw = juce::jmax(120.0f, gw * 0.12f);
        // ensure we leave at least some room for the grid
        const float maxAllowed = (float)juce::jmax(16, gw - 64);
        return juce::jmin(lw, maxAllowed);
    }

    float labelWidth() { return const_cast<const MiniDrumGridComponent*>(this)->labelWidth(); }

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
    int rowHpx_ = 24;

    bool showBarHeader = true;

    struct Hit { bool valid = false; bool onLabel = false; int row = -1; int step = -1; };
    // Horizontal zoom state (1.0 = full-view, 2.0 = 2x zoom meaning half of steps shown)
    float zoomX_ = 1.0f;
    // left-most visible step index when zoomed
    int viewStartStep_ = 0;

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
        const float cellW = (gridW > 0.0f && totalSteps() > 0) ? (gridW / (float)totalSteps()) : 0.0f;
        const int visC = visibleSteps();
        const int startStep = juce::jlimit(0, juce::jmax(0, totalSteps() - visC), viewStartStep_);
        h.row = juce::jlimit(0, R - 1, (int)((p.y - r.getY()) / cellH));
        if (p.x < gridX)
        {
            h.onLabel = true; h.valid = true; return h;
        }
        h.onLabel = false;
        if (cellW <= 0.0f) { h.valid = false; return h; }
        int visCol = (int)((p.x - gridX) / cellW);
        visCol = juce::jlimit(0, visC - 1, visCol);
        int step = startStep + visCol;
        h.step = juce::jlimit(0, totalSteps() - 1, step);
        h.valid = true; return h;
    }
};