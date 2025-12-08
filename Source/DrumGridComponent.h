#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "GridUtils.h"

class DrumGridComponent : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit DrumGridComponent(BoomAudioProcessor& p, int barsToShow = 4, int stepsPerBar_ = 16)
        : proc(p), bars(barsToShow), stepsPerBar(stepsPerBar_)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setInterceptsMouseClicks(true, true);

        setRows(proc.getDrumRows());  // default names from processor
        clearGrid();
    }

    static constexpr int kProjectPPQ = 96;

    juce::Array<int> getSelectedRows() const;

    // Quick test whether any rows are selected.
    bool isAnyRowSelected() const;

    // Clear current selection
    void clearSelection();

    void setCellsPerBeat(int cpb) { cellsPerBeat_ = juce::jmax(1, cpb); repaint(); }
    int getCellsPerBeat() const noexcept { return cellsPerBeat_; }

    // Optional: programmatically set selection to list
    void setSelectedRows(const juce::Array<int>& rows);

    void setTimeSignature(int num, int den)
    {
        timeSigNum_ = juce::jmax(1, num);
        timeSigDen_ = juce::jmax(1, den);
        resized();
        repaint();
    }

    uint32_t getRowSelectionMask() const noexcept;
    int getHeaderHeight() const noexcept;
    int  getTimeSigNumerator()   const noexcept { return timeSigNum_; }
    int  getTimeSigDenominator() const noexcept { return timeSigDen_; }

    void setBarsToDisplay(int bars) noexcept;
    int  getBarsToDisplay() const noexcept { return barsToDisplay_; }
    bool showBarHeader = true;
    void setShowBarHeader(bool b) { showBarHeader = b; repaint(); }

    int rowHpx_ = 16;

    void setRowLabelFontHeight(int px) noexcept;

    int tsNum_ = 4, tsDen_ = 4;


    void setRowHeightPixels(int px) noexcept
    {
        rowHpx_ = juce::jlimit(10, 28, px);
        repaint();
    }


    // Set the visible row names (e.g., Kick, Snare, Hat, Tom…)
    void setRows(const juce::StringArray& names)
    {
        rowNames = names;
        const int r = juce::jmax(1, rowNames.size());
        cells.resize((size_t)r);
        rowEnabled.resize((size_t)r, true);
        for (auto& row : cells) row.assign(totalSteps(), false);
        repaint();
    }

    // Push an existing drum pattern into the grid (marks cells true where notes exist).
    // Assumes: row field of Note is the drum row index; startTick quantized at 16th (24 ticks).
    // NOTE: we round startTick/ticksPerStep so triplet/dotted/sub-16th notes map to the nearest cell
    void setPattern(const BoomAudioProcessor::Pattern& pat)
    {
        clearGrid();

        // project PPQ constant (keep consistent across files)
        const int ppq = 96;

        // compute ticks-per-grid-step using the helper in gridutils.h
        const int ticksPerStepLocal = boom::grid::ticksPerStepFromPpq(ppq, cellsPerBeat_);

        // total available steps in the grid
        const int TS = totalSteps();
        if (TS <= 0)
        {
            repaint();
            return;
        }

        for (const auto& n : pat)
        {
            if (n.row < 0 || n.row >= (int)cells.size()) continue;

            // Round startTick to the nearest grid step index using helper
            const int rawStep = boom::grid::roundStartTickToStepIndex(n.startTick, ticksPerStepLocal);

            // ensure in-range and wrap modulo TS (keep behaviour similar to earlier code)
            const int step = juce::jlimit<int>(0, TS - 1, rawStep % TS);

            cells[(size_t)n.row][(size_t)step] = true;
        }

        repaint();
    }


    // Read out the grid into a Pattern (all rows), for internal use.
    BoomAudioProcessor::Pattern getPatternAllRows() const
    {
        BoomAudioProcessor::Pattern p;
        for (int r = 0; r < (int)cells.size(); ++r)
            for (int s = 0; s < totalSteps(); ++s)
                if (cells[(size_t)r][(size_t)s])
                    p.add({ 0, r, s * ticksPerStep(), ticksPerStep(), 100 });
        return p;
    }

    // Read out only **enabled** rows (for filtered export).
    BoomAudioProcessor::Pattern getPatternEnabledRows() const
    {
        BoomAudioProcessor::Pattern p;
        for (int r = 0; r < (int)cells.size(); ++r)
        {
            if (!rowEnabled[(size_t)r]) continue;
            for (int s = 0; s < totalSteps(); ++s)
                if (cells[(size_t)r][(size_t)s])
                    p.add({ 0, r, s * ticksPerStep(), ticksPerStep(), 100 });
        }
        return p;
    }

    juce::File exportSelectionToMidiTemp(const juce::String& baseFileName) const
    {
        using namespace juce;

        MidiMessageSequence seq;
        const int ppq = 96;

        auto noteForRow = [this](int row) -> int
            {
                if ((unsigned)row < (unsigned)rowNames.size())
                {
                    auto name = rowNames[(int)row].toLowerCase();
                    if (name.contains("kick"))      return 36;
                    if (name.contains("snare"))     return 38;
                    if (name.contains("clap"))      return 39;
                    if (name.contains("rim"))       return 37;
                    if (name.contains("hat") && name.contains("open"))   return 46;
                    if (name.contains("hat") && name.contains("closed")) return 42;
                    if (name.contains("hat"))       return 42;
                    if (name.contains("tom") && name.contains("low"))    return 45;
                    if (name.contains("tom") && name.contains("mid"))    return 47;
                    if (name.contains("tom") && name.contains("high"))   return 50;
                    if (name.contains("perc"))      return 48;
                    if (name.contains("crash"))     return 49;
                    if (name.contains("ride"))      return 51;
                }
                // fallback
                switch (row)
                {
                case 0: return 36;
                case 1: return 38;
                case 2: return 42;
                case 3: return 46;
                default: return 45 + (row % 5);
                }
            };

        // Build pattern from enabled rows only
        auto pat = getPatternEnabledRows();

        // Conversion: step -> ppq ticks
        // step length in ppq ticks = ppq / cellsPerBeat_  (e.g. 96 / 4 = 24)
        const int ticksPerStepPpq = boom::grid::ticksPerStepFromPpq(ppq, cellsPerBeat_);

        for (const auto& n : pat)
        {
            // Pattern stores ticks already in project PPQ units. Use them directly
            // to preserve triplet/dotted/subdivisions when exporting.
            const int startPpq = juce::jmax(0, n.startTick);
            const int lenPpq = juce::jmax(1, n.lengthTicks);
            const int endPpq = startPpq + lenPpq;

            const int midiNote = noteForRow(n.row);
            const int vel = juce::jlimit(1, 127, n.velocity);

            // Use MIDI channel 10 for drums
            seq.addEvent(MidiMessage::noteOn(10, midiNote, (juce::uint8)vel), startPpq);
            seq.addEvent(MidiMessage::noteOff(10, midiNote), endPpq);
        }

        seq.updateMatchedPairs();

        MidiFile mf;
        mf.setTicksPerQuarterNote(ppq);
        mf.addTrack(seq);

        auto tmp = File::getSpecialLocation(File::tempDirectory).getChildFile(baseFileName + ".mid");
        if (tmp.existsAsFile()) tmp.deleteFile();

        FileOutputStream os(tmp);
        if (os.openedOk()) mf.writeTo(os);

        return tmp;
    }

    // Quick export of the **enabled rows only** to a temp MIDI file (pure JUCE)
    // External hook (optional): editor can observe toggles
    std::function<void(int row, int tick)> onToggle;
    std::function<void(int row, int step, bool value)> onCellEdited;

    // ================= Component =================
    void paint(juce::Graphics& g) override
    {
        if (!isEnabled())
        {
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.fillRect(getLocalBounds());
        }
        using namespace boomtheme;
        g.fillAll(GridBackground());


        // --- HEADER + bar/beat drawing (uses your existing variables) ---
        const int headerH = headerH_;
        const int beatsPerBar = timeSigNum_;
        const int cellsPerBeat = cellsPerBeat_;
        const int headerTop = 0;
        int x = leftMargin_;
        auto r = juce::Rectangle<float>(
            0.0f,
            (float)headerH_,                      // start below the header
            (float)getWidth(),
            (float)(getHeight() - headerH_)       // only the body height
        );
        const float labelWf = labelWidth();
        const float gridX = r.getX() + labelWf;
        const float gridW = r.getWidth() - labelWf;

        const int R = juce::jmax(1, (int)cells.size());


        const int C = totalSteps();
        const float cellH = r.getHeight() / (float)R;
        const float cellW = (C > 0) ? (gridW / (float)C) : 1.0f;
        const int stepsPerBarDyn = cellsPerBeat_ * timeSigNum_;
        const int stepsPerSubbeat = cellsPerBeat_;
        const float thickness = (C % stepsPerBarDyn == 0 ? 1.6f : (C % stepsPerSubbeat == 0 ? 1.1f : 0.6f));



        // ===== 1) HEADER (NOT CLIPPED) =====
        g.setColour(HeaderBackground());
        g.fillRect(0, headerTop, getWidth(), headerH);

        // Header bottom divider
        g.setColour(PanelStroke().withAlpha(0.25f));
        g.fillRect(0, headerTop + headerH - 1, getWidth(), 1);

        // Bar labels, bar lines, and beat numbers
        float currentX = gridX;
        for (int bar = 0; bar < barsToDisplay_; ++bar)
        {
            // pixel width for this bar (float)
            const float barPixelWidthF = (float)beatsPerBar * (float)cellsPerBeat * cellW;
            // Bar start vertical line (drawn from header bottom down through grid)
            g.setColour(PanelStroke().withAlpha(0.80f));
            g.drawLine(currentX, (float)headerH, currentX, (float)getHeight(), 2.0f);

            // Draw beat numbers within this bar
            g.setColour(LightAccent().withAlpha(0.90f));
            g.setFont(juce::Font(11.0f, juce::Font::plain));
            for (int beatIdx = 0; beatIdx < beatsPerBar; ++beatIdx)
            {
                const float beatStartXF = currentX + (float)beatIdx * (float)cellsPerBeat * cellW;
                const float beatWidthF = (float)cellsPerBeat * cellW;

                const int bx = juce::roundToInt(beatStartXF);
                const int bw = juce::roundToInt(beatWidthF);
                if (bw <= 0) continue;

                juce::Rectangle<int> beatArea(bx, headerTop, bw, headerH);
                g.drawFittedText(juce::String(beatIdx + 1), beatArea, juce::Justification::left, 1);
            }

            currentX += barPixelWidthF;
        }

        // final right edge grid line (from header down)
        g.setColour(GridLine());
        g.drawLine((float)gridX, (float)headerH, currentX, (float)getHeight());

        // --- now clip and draw the grid so header stays visible ---
        g.saveState();
        g.reduceClipRegion(0, headerH, getWidth(), getHeight() - headerH);

        // draw left label band bg under the grid region if you have one
        g.setColour(boomtheme::PanelStroke());
        g.fillRect(0, headerH, leftMargin_, getHeight() - headerH);
        for (int row = 0; row < R; ++row)
        {
            auto rowY = r.getY() + row * cellH;

            // background strip for label
            g.setColour(juce::Colour::fromString("FF3a1484"));
            g.fillRect(juce::Rectangle<float>(r.getX(), rowY, labelWf, cellH));
            g.setColour(juce::Colours::black);
            g.drawRect(juce::Rectangle<float>(r.getX(), rowY, labelWf, cellH), 1.2f);

            // label text/image: we’ll render text (you can change to images if you have per-row art)
            const auto name = rowNames[row];
            g.setColour(rowEnabled[(size_t)row] ? juce::Colour::fromString("FF7cd400") : juce::Colours::grey);
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawFittedText(name, juce::Rectangle<int>((int)r.getX() + 6, (int)rowY, (int)labelWf - 12, (int)cellH), juce::Justification::centredLeft, 1);
        }


        g.restoreState();

        if (showBarHeader)
        {

            int x_ = leftMargin_; // use your existing left margin
            g.setFont(juce::Font(12.0f));

            for (int bar_ = 0; bar_ < bars; ++bar_)
            {
                const int cellsThisBar = beatsPerBar * cellsPerBeat;
                const int w = cellsThisBar * cellW; // your existing cell width
                g.setColour(boomtheme::HeaderBackground());
                g.drawFittedText(juce::String(bar_ + 1), { x_, 0, w, headerH }, juce::Justification::centred, 1);

                g.setColour(boomtheme::GridLine());
                g.drawLine((float)x_, (float)headerH, (float)x_, (float)getHeight());
                x_ += w;
            }
            g.drawLine((float)x, (float)headerH, (float)x_, (float)getHeight());
        }
        // Grid background
        g.setColour(GridBackground());
        g.fillRect(juce::Rectangle<float>(gridX, r.getY(), gridW, r.getHeight()));

        // Grid lines
        g.setColour(GridLine());
        for (int c = 0; c <= C; ++c)
        {
            const float gr = gridX + c * cellW;
            const float thickness = (c % stepsPerBarDyn == 0 ? 1.6f : (c % stepsPerSubbeat == 0 ? 1.1f : 0.6f));
            g.drawLine(gr, r.getY(), gr, r.getBottom(), thickness);
        }
        for (int row = 0; row <= R; ++row)
        {
            const float y = r.getY() + row * cellH;
            g.drawLine(gridX, y, gridX + gridW, y, 0.6f);
        }

        // Cells
        for (int row = 0; row < R; ++row)
        {
            const bool enabled = rowEnabled[(size_t)row];
            for (int c = 0; c < C; ++c)
            {
                const auto x__ = gridX + c * cellW;
                const auto y = r.getY() + row * cellH;
                auto cellR = juce::Rectangle<float>(x__ + 2.f, y + 2.f, cellW - 4.f, cellH - 4.f);

                if (!cells[(size_t)row][(size_t)c] && !enabled)
                {
                    g.setColour(PanelStroke().withAlpha(0.15f));
                    g.fillRoundedRectangle(cellR, 3.5f);
                }
            }
        }

        // Draw actual drum notes from pattern
        const auto& pattern = proc.getDrumPattern();
        const float cellWidthPerTick = cellW / (float)ticksPerStep(); // width of 1 tick

        // Respect editor APVTS settings: only show triplets/dotted if enabled
        const bool showTriplets = (proc.apvts.getRawParameterValue("useTriplets") != nullptr)
            ? (proc.apvts.getRawParameterValue("useTriplets")->load() > 0.5f)
            : false;
        const bool showDotted = (proc.apvts.getRawParameterValue("useDotted") != nullptr)
            ? (proc.apvts.getRawParameterValue("useDotted")->load() > 0.5f)
            : false;

        for (const auto& note : pattern)
        {
            const int row = note.row;

            // Determine displayed start/length depending on toggles
            const int ppq = kProjectPPQ;
            int dispStart = note.startTick;
            int dispLen = note.lengthTicks;

            // Read density sliders (0..1 floats stored in APVTS) and convert to 0..100
            int tripletPct = 0;
            int dottedPct = 0;
            if (auto* r = proc.apvts.getRawParameterValue("tripletDensity")) tripletPct = juce::jlimit(0, 100, (int)juce::roundToInt(r->load() * 100.0f));
            if (auto* d = proc.apvts.getRawParameterValue("dottedDensity"))  dottedPct  = juce::jlimit(0, 100, (int)juce::roundToInt(d->load() * 100.0f));

            if (!showTriplets) tripletPct = 0;
            if (!showDotted)  dottedPct = 0;

            // If both densities are zero, snap both start and length to grid/subdivisions
            if (tripletPct == 0 && dottedPct == 0)
            {
                dispLen = boom::grid::snapTicksToNearestSubdivision(note.lengthTicks, ppq, false, false);
                dispStart = boom::grid::snapTicksToGridStep(note.startTick, ppq, cellsPerBeat_);
            }
            else
            {
                // Deterministic per-note pseudo-random decision so visuals are stable per note.
                // Use a hashed combination of startTick/row/length so nearby notes and different rows
                // get varied outcomes instead of all notes on one row matching.
                auto findNearestBaseTicks = [&](int lenTicks)->int
                {
                    const int denoms[] = { 1,2,4,8,16,32,64 };
                    int best = boom::grid::ticksForDenominator(ppq, 16);
                    int bestDiff = INT_MAX;
                    for (auto d : denoms)
                    {
                        int base = boom::grid::ticksForDenominator(ppq, d);
                        int diff = std::abs(lenTicks - base);
                        if (diff < bestDiff) { bestDiff = diff; best = base; }
                    }
                    return best;
                };

                uint64_t key = (((uint64_t)(uint32_t)note.startTick) << 32) ^ (((uint64_t)(uint32_t)note.row) << 16) ^ (uint64_t)(uint32_t)note.lengthTicks;
                const int roll = (int)(std::hash<uint64_t>{}(key) % 100);

                if (roll < tripletPct)
                {
                    const int base = findNearestBaseTicks(note.lengthTicks);
                    dispLen = boom::grid::tripletTicks(base);
                }
                else if (roll < tripletPct + dottedPct)
                {
                    const int base = findNearestBaseTicks(note.lengthTicks);
                    dispLen = boom::grid::dottedTicks(base);
                }
                else
                {
                    // no special ornament chosen; snap to nearest subdivision including both types
                    dispLen = boom::grid::snapTicksToNearestSubdivision(note.lengthTicks, ppq, true, true);
                }

                // Snap start to grid step for consistent visual alignment
                dispStart = boom::grid::snapTicksToGridStep(note.startTick, ppq, cellsPerBeat_);
            }

            const float cellX = gridX + dispStart * cellWidthPerTick;
            const float noteWidth = juce::jmax(2.0f, dispLen * cellWidthPerTick);
            const float cellY = r.getY() + row * cellH;
            const float cellHeightAdj = cellH - 4.f;

            juce::Rectangle<float> noteRect(cellX, cellY + 2.f, noteWidth, cellHeightAdj);

            g.setColour(juce::Colour::fromString("FF6e138b"));
            g.fillRoundedRectangle(noteRect, 3.5f);
            g.setColour(juce::Colours::black);
            g.drawRoundedRectangle(noteRect, 3.5f, 1.2f);

            // Draw markers when showing special durations (based on displayed length)
            const bool isDotted = boom::grid::isDottedTicks(dispLen, ppq);
            const bool isTrip = boom::grid::isTripletTicks(dispLen, ppq);

            if ((showDotted && isDotted) || (showTriplets && isTrip))
            {
                juce::Colour accent = isTrip ? juce::Colour::fromString("FF6e138b") : juce::Colour::fromString("FF7cd400");

                // left accent stripe
                const float stripeW = juce::jmin(5.0f, noteRect.getHeight() * 0.25f);
                g.setColour(accent);
                g.fillRoundedRectangle(noteRect.withX(noteRect.getX() + 2.0f).withWidth(stripeW), 2.0f);

                // glow
                g.setColour(accent.withAlpha(0.14f));
                g.fillRoundedRectangle(noteRect.reduced(-2.0f, -2.0f), 6.0f);

                if (isDotted && showDotted)
                {
                    const float cx = noteRect.getRight() - juce::jmin(12.0f, noteRect.getWidth() * 0.18f);
                    const float cy = noteRect.getCentreY();
                    const float rdot = juce::jmin(6.0f, noteRect.getHeight() * 0.28f);
                    g.setColour(juce::Colours::black.withAlpha(0.6f));
                    g.fillEllipse(cx - rdot * 0.6f, cy - rdot * 0.6f, rdot * 1.2f, rdot * 1.2f);
                    g.setColour(accent);
                    g.fillEllipse(cx - rdot * 0.45f, cy - rdot * 0.45f, rdot * 0.9f, rdot * 0.9f);
                }
                else if (isTrip && showTriplets)
                {
                    const float badgeW = juce::jmin(20.0f, noteRect.getWidth() * 0.22f);
                    const float bx = noteRect.getRight() - badgeW - 6.0f;
                    const float by = noteRect.getY() + 4.0f;
                    g.setColour(accent);
                    g.fillRoundedRectangle(juce::Rectangle<float>(bx, by, badgeW, badgeW * 0.6f), 4.0f);
                    g.setColour(juce::Colours::black);
                    g.setFont(juce::Font((float)juce::jmax(10, (int)std::round(badgeW * 0.45f)), juce::Font::bold));
                    g.drawText("3", (int)bx, (int)by, (int)badgeW, (int)(badgeW * 0.6f), juce::Justification::centred, false);
                }
            }
        }

        // Live numeric readout while resizing
        if (resizing && resizingPatternIndex >= 0)
        {
            const auto pat = proc.getDrumPattern();
            if (resizingPatternIndex < pat.size())
            {
                const int len = pat[resizingPatternIndex].lengthTicks;
                // Convert len (ticks) into a human-readable musical label (e.g., 1/4, 1/8 triplet, 1/16 dotted)
                juce::String labelTxt;
                
                // Try to match common denominations first
                const int denoms[] = {1,2,4,8,16,32,64};
                bool matched = false;
                for (auto d : denoms)
                {
                    const int base = boom::grid::ticksForDenominator(kProjectPPQ, d);
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
                g.setColour(juce::Colours::black.withAlpha(0.85f));
                const int sx = lastMouseX + 8;
                const int sy = juce::jmax(headerH_ + 4, lastMouseY - 12);
                g.fillRoundedRectangle((float)sx, (float)sy, 140.0f, 20.0f, 4.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(12.0f, juce::Font::bold));
                g.drawText(txt, sx + 6, sy, 132, 20, juce::Justification::centredLeft, false);
            }
        }
    }

    void resized() override {}

    // ================= Interaction =================

    void mouseDown(const juce::MouseEvent& e) override
    {
        lastMouseX = (int)e.x; lastMouseY = (int)e.y;
        const Hit h = hitTest(e.position);
        DBG("DrumGridComponent::mouseDown valid=" << (int)h.valid << " onLabel=" << (int)h.onLabel
            << " row=" << h.row << " step=" << h.step);
        if (!h.valid) return;

        if (h.onLabel)
        {
            // Toggle row enabled/disabled
            const bool now = !(rowEnabled[(size_t)h.row]);
            rowEnabled[(size_t)h.row] = now;
            repaint();
            return;
        }

        // If user clicked on an existing filled cell, check for resize handle first, else start a drag operation
        if ((size_t)h.row < cells.size() && (size_t)h.step < cells[(size_t)h.row].size() && cells[(size_t)h.row][(size_t)h.step])
        {
            // detect resize handle on notes in the processor pattern
            const auto pat = proc.getDrumPattern();
            const int ppq = kProjectPPQ;
            // compute geometry similar to paint
            auto r = juce::Rectangle<float>(
                0.0f,
                (float)headerH_,
                (float)getWidth(),
                (float)(getHeight() - headerH_)
            );
            const float labelWf = labelWidth();
            const float gridX = r.getX() + labelWf;
            const float gridW = r.getWidth() - labelWf;
            const int R = juce::jmax(1, (int)cells.size());
            const int C = totalSteps();
            const float cellH = r.getHeight() / (float)R;
            const float cellW = (C > 0) ? (gridW / (float)C) : 1.0f;

            for (int idx = 0; idx < pat.size(); ++idx)
            {
                const auto& note = pat[idx];
                const int row = note.row;
                if (row < 0 || row >= R) continue;
                const float noteX = gridX + note.startTick * (cellW / (float)ticksPerStep());
                const float noteW = juce::jmax(2.0f, note.lengthTicks * (cellW / (float)ticksPerStep()));
                const float noteY = r.getY() + row * cellH;
                const float noteH = cellH - 4.f;
                const float right = noteX + noteW;
                if ((float)e.x >= right - kResizeHandlePx && (float)e.x <= right + 2.0f && (float)e.y >= noteY && (float)e.y <= noteY + noteH)
                {
                    // begin resizing this pattern index
                    resizing = true;
                    resizingPatternIndex = idx;
                    resizeInitialMouseX = (int)e.x;
                    resizeOriginalLenTicks = note.lengthTicks;
                    return;
                }
            }

            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            {
                // Build a DynamicObject description with basic note info
                auto* obj = new juce::DynamicObject();
                obj->setProperty("type", "drum");
                obj->setProperty("row", h.row);
                const int startTick = h.step * ticksPerStep();
                obj->setProperty("startTick", startTick);
                obj->setProperty("lengthTicks", ticksPerStep());

                juce::var desc (obj);

                // Simple drag image: a small rounded rectangle representing the note
                const int imgW = juce::jmax(24, cellPixelWidth_);
                const int imgH = juce::jmax(12, rowHpx_ - 2);
                juce::Image im (juce::Image::ARGB, imgW, imgH, true);
                juce::Graphics g (im);
                g.fillAll (juce::Colours::transparentBlack);
                g.setColour (juce::Colour::fromString("FF6e138b"));
                g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, 2.0f, (float)imgW - 4.0f, (float)imgH - 4.0f), 3.0f);

                juce::ScaledImage simg (im, 1.0f);

                container->startDragging (desc, this, simg, true, nullptr, nullptr);
                return;
            }
        }

        // Start paint sweep on this row (normal edit)
        dragging = true;
        dragRow = h.row;
        dragValue = !cells[(size_t)h.row][(size_t)h.step]; // invert current cell and paint that value
        setCell(h.row, h.step, dragValue);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        lastMouseX = (int)e.x; lastMouseY = (int)e.y;

        if (resizing && resizingPatternIndex >= 0)
        {
            const auto pat = proc.getDrumPattern();
            if (resizingPatternIndex >= pat.size()) return;
            const auto note = pat[resizingPatternIndex];

            // compute cell width per tick as in paint
            auto r = juce::Rectangle<float>(0.0f, (float)headerH_, (float)getWidth(), (float)(getHeight() - headerH_));
            const float labelWf = labelWidth();
            const float gridX = r.getX() + labelWf;
            const float gridW = r.getWidth() - labelWf;
            const int C = totalSteps();
            const float cellW = (C > 0) ? (gridW / (float)C) : 1.0f;
            const float cellWidthPerTick = cellW / (float)ticksPerStep();

            const int dx = (int)e.x - resizeInitialMouseX;
            const int deltaTicks = juce::roundToInt((float)dx / juce::jmax(0.0001f, cellWidthPerTick));
            int newLen = juce::jmax(1, resizeOriginalLenTicks + deltaTicks);


            const int ppq = kProjectPPQ;
            const bool showTriplets = (proc.apvts.getRawParameterValue("useTriplets") != nullptr) ? (proc.apvts.getRawParameterValue("useTriplets")->load() > 0.5f) : false;
            const bool showDotted = (proc.apvts.getRawParameterValue("useDotted") != nullptr) ? (proc.apvts.getRawParameterValue("useDotted")->load() > 0.5f) : false;

            // If neither ornament type is enabled, snap to grid step. Otherwise, allow snapping
            // to nearest subdivision including the enabled ornament types. To ensure we can create
            // sub-16th durations when densities are non-zero, allow both types when snapping here.
            if (!showTriplets && !showDotted)
                newLen = boom::grid::snapTicksToGridStep(newLen, ppq, cellsPerBeat_);
            else
                newLen = boom::grid::snapTicksToNearestSubdivision(newLen, ppq, true, true);

            newLen = juce::jmax(1, newLen);

            auto newPat = pat;
            newPat.getReference(resizingPatternIndex).lengthTicks = newLen;
            proc.setDrumPattern(newPat);
            // keep grid visual in sync
            setPattern(newPat);
            repaint();
            return;
        }

        if (!dragging) return;

        const Hit h = hitTest(e.position);
        DBG("DrumGridComponent::mouseDrag valid=" << (int)h.valid << " row=" << h.row << " step=" << h.step);
        if (!h.valid) return;

        // Constrain sweep to the same row where mouseDown happened
        if (h.row != dragRow) return;

        setCell(h.row, h.step, dragValue);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        DBG("DrumGridComponent::mouseUp dragging=" << (int)dragging << " dragRow=" << dragRow << " resizing=" << (int)resizing);
        dragging = false;
        dragRow = -1;
        if (resizing)
        {
            resizing = false;
            resizingPatternIndex = -1;
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        lastMouseX = (int)e.x; lastMouseY = (int)e.y;
        // change cursor when near a note edge
        const auto pat = proc.getDrumPattern();
        auto r = juce::Rectangle<float>(0.0f, (float)headerH_, (float)getWidth(), (float)(getHeight() - headerH_));
        const float labelWf = labelWidth();
        const float gridX = r.getX() + labelWf;
        const float gridW = r.getWidth() - labelWf;
        const int R = juce::jmax(1, (int)cells.size());
        const int C = totalSteps();
        const float cellH = r.getHeight() / (float)R;
        const float cellW = (C > 0) ? (gridW / (float)C) : 1.0f;
        const float cellWidthPerTick = cellW / (float)ticksPerStep();

        bool near = false;
        for (int idx = 0; idx < pat.size(); ++idx)
        {
            const auto& note = pat[idx];
            const int row = note.row;
            if (row < 0 || row >= R) continue;
            const float noteX = gridX + note.startTick * cellWidthPerTick;
            const float noteW = juce::jmax(2.0f, note.lengthTicks * cellWidthPerTick);
            const float noteY = r.getY() + row * cellH;
            const float noteH = cellH - 4.f;
            const float right = noteX + noteW;
            if ((float)e.x >= right - kResizeHandlePx && (float)e.x <= right + 2.0f && (float)e.y >= noteY && (float)e.y <= noteY + noteH)
            {
                near = true; break;
            }
        }
        if (near) setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    // Drag-and-drop target API (accept drum note descriptions)
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        if (! details.description.isObject()) return false;
        if (auto* o = details.description.getDynamicObject())
        {
            auto t = o->getProperty("type");
            return t.isString() && t.toString() == "drum";
        }
        return false;
    }

    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& /*details*/) override {}
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& /*details*/) override {}
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& /*details*/) override {}

    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        if (! details.description.isObject()) return;
        auto* o = details.description.getDynamicObject();
        if (o == nullptr) return;

        if (auto t = o->getProperty("type"); t.isString() && t.toString() == "drum")
        {
            const int row = (int) o->getProperty("row");
            int startTick = (int) o->getProperty("startTick");
            int lengthTicks = (int) o->getProperty("lengthTicks");

            // Snap length to musical subdivisions and to grid step
            const int ppq = kProjectPPQ;
            const int snapped = boom::grid::snapTicksToNearestSubdivision(lengthTicks, ppq, true, true);
            const int snappedGrid = boom::grid::snapTicksToGridStep(snapped, ppq, cellsPerBeat_);

            BoomAudioProcessor::Pattern pat = proc.getDrumPattern();
            BoomAudioProcessor::Note n;
            n.row = row;
            n.startTick = juce::jmax(0, startTick);
            n.lengthTicks = juce::jmax(1, snappedGrid);
            n.velocity = 100;
            n.pitch = 0;

            pat.add(n);
            proc.setDrumPattern(pat);
            setPattern(pat);
            repaint();
        }
    }

    void setCell(int row, int step, bool v)
    {
        if (row < 0 || row >= (int)cells.size()) return;
        if (step < 0 || step >= totalSteps())    return;
        if (!rowEnabled[(size_t)row])           return; // ignore edits when row disabled

        if (cells[(size_t)row][(size_t)step] == v) return;

        DBG("DrumGridComponent::setCell row=" << row << " step=" << step << " value=" << (int)v);

        cells[(size_t)row][(size_t)step] = v;

        if (onCellEdited) onCellEdited(row, step, v);
        if (onToggle) onToggle(row, step * ticksPerStep());
        repaint();
    }

    // Export only rows in rowMask (bit N -> row N).
    // baseMidi: base note for row 0 (default 60 => C3). Returns the temp File.
    // Export only rows in rowMask (bit N -> row N). baseMidi defaults to 60 => C3.
    juce::File exportSelectedRowsToMidiTemp(uint32_t rowMask, const juce::String& baseFileName, int baseMidi = 60) const;


private:
    BoomAudioProcessor& proc;

    juce::StringArray rowNames;
    std::vector<std::vector<bool>> cells;   // [row][step]
    std::vector<bool> rowEnabled;

    juce::Array<int> selectedRows_;
    int lastSelectedRow_ = -1; // used for shift-select behavior
    const int stepsPerBar = 16;

    // helper: compute ticks-per-grid-step from project PPQ + current cellsPerBeat_
    inline int ticksPerStep() const noexcept { return boom::grid::ticksPerStepFromPpq(kProjectPPQ, cellsPerBeat_); }

    const int bars;
    bool dragging = false;
    int dragRow = -1;
    bool dragValue = false;

    // ----- resize state for notes -----
    bool resizing = false;
    int resizingPatternIndex = -1; // index into proc.getDrumPattern()
    int resizeInitialMouseX = 0;
    int resizeOriginalLenTicks = 0;
    int lastMouseX = 0, lastMouseY = 0;
    const int kResizeHandlePx = 8;

    int timeSigNum_{ 4 };
    int timeSigDen_{ 4 };
    int barsToDisplay_{ 8 };
    int bars_ = 4;
    int beatsPerBar_ = 4;   // default 4/4
    // default 4 bars in header
    int headerH_ = 18;  // header band height (px)
    int leftMargin_ = 48;  // room for row labels / piano keys (px)
    int cellsPerBeat_ = 4;   // normal 16th grid = 4 cells per beat
    int cellPixelWidth_ = 16;  // whatever you use to draw columns

    int rowLabelPx_ = 10;

    struct Hit
    {
        bool valid = false;
        bool onLabel = false;
        int row = -1;
        int step = -1;
    };

    // total steps = bars * beatsPerBar * cellsPerBeat_
    // this keeps steps consistent with the cell/beat variables used elsewhere
    int totalSteps() const { return bars * timeSigNum_ * cellsPerBeat_; }
    float labelWidth() const noexcept
    {
        // Increase the minimum width and percentage so longer row names fit.
        // Tweak 160.0 and 0.18 to taste.
        return juce::jmax(100.0f, getWidth() * 0.10f);
    }

    void clearGrid()
    {
        const int R = juce::jmax(1, rowNames.size());
        cells.resize((size_t)R);
        for (auto& row : cells) row.assign(totalSteps(), false);
    }

    void updateContentSize()
    {
        const int rows = 7;
        const int rowH = 18;
        const int beatsPerBar = timeSigNum_;
        const int totalCells = barsToDisplay_ * beatsPerBar * cellsPerBeat_;
        const int w = leftMargin_ + totalCells * 16;
        const int h = getHeaderHeight() + rows * rowH; // add header
        setSize(w, h);
    }

    Hit hitTest(juce::Point<float> p) const
    {
        Hit h;
        auto r = juce::Rectangle<float>(
            0.0f,
            (float)headerH_,
            (float)getWidth(),
            (float)(getHeight() - headerH_)
        );
        if (!r.contains(p)) return h;

        const int R = (int)cells.size();
        if (R <= 0) return h;

        const float labelWf = labelWidth();
        const float gridX = r.getX() + labelWf;
        const float gridW = r.getWidth() - labelWf;

        const float cellH = r.getHeight() / (float)R;
        const float cellW_ = gridW / (float)totalSteps();

        h.row = juce::jlimit(0, R - 1, (int)((p.y - r.getY()) / cellH));

        if (p.x < gridX)
        {
            h.onLabel = true;
            h.valid = true;
            return h;
        }

        h.onLabel = false;
        int step = (int)((p.x - gridX) / cellW_);
        h.step = juce::jlimit(0, totalSteps() - 1, step);
        h.valid = true;
        return h;
    }
};
