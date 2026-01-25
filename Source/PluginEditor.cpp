#include "PluginEditor.h"
#include "GridUtils.h"
#include "BoomLookAndFeel.h"
using boomui::loadSkin;
using boomui::setButtonImages;
using boomui::setToggleImages;
#include "EngineDefs.h"
#include "Theme.h"
#include "BassStyleDB.h"
#include "DrumStyles.h"
#include "DrumGridComponent.h"
#include "MidiUtils.h"
#include <memory>
#include "AudioInputManager.h"
#include "DrumGenerator.h"
#include <atomic>

namespace
{
    const juce::StringArray kKeys{ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    struct JuceStringLess
    {
        bool operator()(const juce::String& a, const juce::String& b) const noexcept
        {
            return a.compare(b) < 0;
        }
    };

    const std::map<juce::String, std::vector<int>, JuceStringLess> kScales = {
    {"Major",         {0, 2, 4, 5, 7, 9, 11}},
    {"Natural Minor", {0, 2, 3, 5, 7, 8, 10}},
    {"Harmonic Minor",{0, 2, 3, 5, 7, 8, 11}},
    {"Dorian",        {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian",      {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian",        {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian",    {0, 2, 4, 5, 7, 9, 10}},
    {"Aeolian",       {0, 2, 3, 5, 7, 8, 10}},
    {"Locrian",       {0, 1, 3, 5, 6, 8, 10}},
    {"Locrian Nat6",    {0, 1, 3, 5, 6, 9, 10}},
    {"Ionian #5",     {0, 2, 4, 6, 7, 9, 11}},
    {"Dorian #4",     {0, 2, 3, 6, 7, 9, 10}},
    {"Phrygian Dom",  {0, 1, 3, 5, 7, 9, 10}},
    {"Lydian #2",     {0, 3, 4, 6, 7, 9, 11}},
    {"Super Locrian", {0, 1, 3, 4, 6, 8, 10}},
    {"Dorian b2",     {0, 1, 3, 5, 7, 9, 10}},
    {"Lydian Aug",    {0, 2, 4, 6, 8, 9, 11}},
    {"Lydian Dom",    {0, 2, 4, 6, 7, 9, 10}},
    {"Mixo b6",       {0, 2, 4, 5, 7, 8, 10}},
    {"Locrian #2",    {0, 2, 3, 5, 6, 8, 10}},
    {"8 Tone Spanish", {0, 1, 3, 4, 5, 6, 8, 10}},
    {"Phyrgian Nat3",    {0, 1, 4, 5, 7, 8, 10}},
    {"Blues",         {0, 3, 5, 6, 7, 10}},
    {"Hungarian Min", {0, 3, 5, 8, 11}},
    {"Harmonic Maj(Ethopian)",  {0, 2, 4, 5, 7, 8, 11}},
    {"Dorian b5",     {0, 2, 3, 5, 6, 9, 10}},
    {"Phrygian b4",   {0, 1, 3, 4, 7, 8, 10}},
    {"Lydian b3",     {0, 2, 3, 6, 7, 9, 11}},
    {"Mixolydian b2", {0, 1, 4, 5, 7, 9, 10}},
    {"Lydian Aug2",   {0, 3, 4, 6, 8, 9, 11}},
    {"Locrian bb7",   {0, 1, 3, 5, 6, 8, 9}},
    {"Pentatonic Maj", {0, 2, 5, 7, 8}},
    {"Pentatonic Min", {0, 3, 5, 7, 10}},
    {"Neopolitan Maj", {0, 1, 3, 5, 7, 9, 11}},
    {"Neopolitan Min", {0, 1, 3, 5, 7, 8, 10}},
    {"Spanish Gypsy",  {0, 1, 4, 5, 7, 8, 10}},
    {"Romanian Minor", {0, 2, 3, 6, 7, 9, 10}},
    {"Chromatic",      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Bebop Major",  {0, 2, 4, 5, 7, 8, 9, 11}},
    {"Bebop Minor", {0, 2, 3, 5, 7, 8, 9, 10}},
    };

    struct KeyScale
    {
        int rootIndex = 0;
        const std::vector<int>* pcs = nullptr;
    };
}

namespace {
    enum PreviewOwner : int { Preview_None = 0, Preview_Rhythm = 1, Preview_Beatbox = 4 };
    static std::atomic<int> g_previewOwner{ Preview_None };
}

// AbbrevComboBox moved to PluginEditor.h


// ---------------------- SplashDialog & TransientMsgWindow ----------------------
// Small dialog that shows logo + copyright / info text.
// Use DialogWindow so it centers and behaves like a modal-ish info box.
class SplashDialog : public juce::Component
{
public:
    // bgColour: the editor's base background (we'll apply alpha to it).
    // alpha: 0.0..1.0 for translucency (1.0 = opaque). Default ~0.85.
    SplashDialog(juce::Colour bgColour = juce::Colour::fromString("FF092806"), float alpha = 0.85f)
        : backgroundColour(bgColour), bgAlpha(juce::jlimit(0.0f, 1.0f, alpha))
    {
        setSize(600, 700);

        logoImg.setImage(boomui::loadSkin("boomSplashUltd.png"));
        addAndMakeVisible(logoImg);

        infoLbl.setJustificationType(juce::Justification::centred);
        infoLbl.setColour(juce::Label::textColourId, juce::Colour::fromString("FF7cd400"));
        infoLbl.setText("COPYRIGHT " + juce::String(juce::Time::getCurrentTime().getYear()) +
            " SoundsDopeAudio  — All rights reserved. \n\n For more dope shit visit our website http:/www.soundsdope.net/. Created by WASNTMEVIELDIDIT & GHXSTLINE \n\n You are currently running\n\n BOOM Version: " + juce::String("1.0.0"),
            juce::dontSendNotification);
        infoLbl.setFont(juce::Font(12.0f));
        addAndMakeVisible(infoLbl);

        okBtn.setButtonText("Close");
        addAndMakeVisible(okBtn);
        okBtn.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState(0); };

        // We'll paint our own translucent background so make the component opaque.
        setOpaque(true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(18);
        auto logoR = r.removeFromTop(140);
        logoImg.setBounds(logoR.withSizeKeepingCentre(360, 90));
        infoLbl.setBounds(r.removeFromTop(120));
        okBtn.setBounds((getWidth() - 100) / 2, getHeight() - 48, 100, 28);
    }

    void paint(juce::Graphics& g) override
    {
        // Fill full component with the editor's background colour, using the requested alpha.
        g.fillAll(backgroundColour.withAlpha(bgAlpha));

        // Optionally add a subtle rounded panel effect (keeps it slick)
        auto boundsF = getLocalBounds().toFloat().reduced(6.0f);
        g.setColour(backgroundColour.contrasting(0.08f).withAlpha(juce::jmin(0.6f, bgAlpha)));
        g.fillRoundedRectangle(boundsF, 15.0f);
    }

private:
    juce::ImageComponent logoImg;
    juce::Label infoLbl;
    juce::TextButton okBtn;

    juce::Colour backgroundColour;
    float bgAlpha = 0.85f;
};

// Very small transient window that fades out after `lifeMs`.
// --------------------- transient message helper ---------------------
// Small self-closing dialog that shows a short message and closes itself
class TransientMsgComponent : public juce::Component, private juce::Timer
{
public:
    TransientMsgComponent(const juce::String& text, int timeoutMs = 1200)
        : msg(text), timeout(timeoutMs)
    {
        setSize(125, 10);
        startTimerHz(120); // animate fade
        startTimer(timeoutMs); // single-shot close
    }

    ~TransientMsgComponent() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour::fromString("FF092806"));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
        g.setColour(juce::Colour::fromString("FF7cd400"));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(msg, getLocalBounds().reduced(8, 2), juce::Justification::centred, true);
    }

    // Launch centered over 'parent' as a non-modal floating dialog
    static void launchCentered(juce::Component* parent, const juce::String& text, int timeoutMs = 1200)
    {
        auto* comp = new TransientMsgComponent(text, timeoutMs);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(comp);
        opts.dialogTitle = {};
        opts.useNativeTitleBar = false;
        opts.resizable = false;
        opts.escapeKeyTriggersCloseButton = false;
        opts.componentToCentreAround = parent;
        opts.launchAsync();
    }

private:
    void timerCallback() override
    {
        // If timer was started with a single-shot timeout, the DialogWindow will still exist;
        // attempt to close it by finding parent DialogWindow and calling exitModalState.
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
        stopTimer();
    }

    void timerCallback(int) {} // no-op for compatibility (won't be used)
    juce::String msg;
    int timeout = 1200;
};



// Local helper that does NOT touch headers; safe and scoped to this .cpp only.


class BpmPoller : private juce::Timer
{
public:
    BpmPoller(BoomAudioProcessor& p, std::function<void(double)> uiUpdate)
        : proc(p), onUiUpdate(std::move(uiUpdate))
    {
        startTimerHz(8);
    }
    ~BpmPoller() override { stopTimer(); }

private:
    void timerCallback() override
    {
        const double bpm = proc.getHostBpm();
        juce::MessageManager::callAsync([fn = onUiUpdate, bpm]
            {
                if (fn) fn(bpm);
            });
    }

    BoomAudioProcessor& proc;
    std::function<void(double)>    onUiUpdate;
};


namespace {
    // Toggle preview playback and update the play-button image.
    inline void toggleAIPreview(BoomAudioProcessor& proc, juce::ImageButton& playBtn)
    {
        // No capture -> nothing to preview
        if (!proc.aiHasCapture())
            return;

        if (proc.aiIsPreviewing())
        {
            proc.aiPreviewStop();
            boomui::setButtonImages(playBtn, "playBtn");
        }
        else
        {
            proc.aiPreviewStart();
            boomui::setButtonImages(playBtn, "playBtn_down");
        }
    }
}

// Now that BpmPoller is complete, define the editor dtor here:
BoomAudioProcessorEditor::~BoomAudioProcessorEditor()
{
    // stop timers / owned helpers first
    bpmPoller.reset();

    // restore any per-component LookAndFeel pointers we set to avoid dangling references.
    // purpleLNF is a member; clear sliders that were bound to it.
    dottedDensity.setLookAndFeel(nullptr);
    tripletDensity.setLookAndFeel(nullptr);
    // clear AppLookAndFeel-bound sliders (they were assigned a static raw pointer)

    proc.apvts.removeParameterListener("useTriplets", this);
    proc.apvts.removeParameterListener("useDotted", this);
    proc.apvts.removeParameterListener("tripletDensity", this);
    proc.apvts.removeParameterListener("dottedDensity", this);

    // Editor had a global L&F applied — restore default before destruction.
    setLookAndFeel(nullptr);
}

AIToolsWindow::~AIToolsWindow()
{
    // restore previous processor callback (if any) so main editor keeps receiving updates
    if (prevDrumPatternCallback)
        proc.drumPatternChangedCallback = prevDrumPatternCallback;
    else
        proc.drumPatternChangedCallback = {};

    rhythmSeekAtt.reset();
    beatboxSeekAtt.reset();

    // Restore look-and-feel on any components we applied it to to avoid dangling pointers.
    rhythmSeek.setLookAndFeel(nullptr);
    beatboxSeek.setLookAndFeel(nullptr);

    styleABox.setLookAndFeel(nullptr);
    styleBBox.setLookAndFeel(nullptr);

    if (audioInputManager)
    {
        if (auto* sel = audioInputManager->getDeviceSelectorComponent())
        {
            std::function<void(juce::Component*)> restore;
            restore = [&](juce::Component* c)
                {
                    for (int i = 0; i < c->getNumChildComponents(); ++i)
                    {
                        if (auto* ch = c->getChildComponent(i))
                        {
                            if (auto* cb = dynamic_cast<juce::ComboBox*>(ch))
                                cb->setLookAndFeel(nullptr);
                            restore(ch);
                        }
                    }
                };
            restore(sel);
        }
    }

    // Release our owned LookAndFeel last.
    aiToolsLnf.reset();
}

namespace {
    void updateEngineButtonSkins(boom::Engine e, juce::ImageButton& btn808, juce::ImageButton& btnBass, juce::ImageButton& btnDrums)
    {
        boomui::setButtonImages(btn808, "808Btn");
        boomui::setButtonImages(btnBass, "bassBtn");
        boomui::setButtonImages(btnDrums, "drumsBtn");

        if (e == boom::Engine::e808)  boomui::setButtonImagesSelected(btn808, "808Btn");
        if (e == boom::Engine::Bass)  boomui::setButtonImagesSelected(btnBass, "bassBtn");
        if (e == boom::Engine::Drums) boomui::setButtonImagesSelected(btnDrums, "drumsBtn");
    }
}



// Add near the top of this .cpp (after anonymous namespace/updateEngineButtonSkins or before AIToolsWindow ctor)
namespace {
    // Palette (ARGB)
    static const juce::Colour kPurple = juce::Colour(0xff3a1484);
    static const juce::Colour kDarkGreen = juce::Colour(0xff092806);
    static const juce::Colour kLime = juce::Colour(0xff7cd400);
    static const juce::Colour kMagenta = juce::Colour(0xff6e138b);
    static const juce::Colour kSlate = juce::Colour(0xff2d2e41);
    static const juce::Colour kOffWhite = juce::Colour(0xfff6f5ef);

    // Simple panel that paints a gradient rounded background and border
    struct DevicePanel : juce::Component
    {
        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
            juce::ColourGradient grad(kPurple.withAlpha(0.95f), bounds.getX(), bounds.getY(),
                kMagenta.withAlpha(0.95f), bounds.getRight(), bounds.getY(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(bounds, 8.0f);

            g.setColour(kSlate.withAlpha(0.6f));
            g.drawRoundedRectangle(bounds, 8.0f, 2.0f);
        }
    };

    // stylized meter drawer
    static void drawStyledMeter(juce::Graphics& g, juce::Rectangle<int> area, float level, float peakNorm)
    {
        // clamp
        level = juce::jlimit(0.0f, 1.0f, level);
        peakNorm = juce::jlimit(0.0f, 1.0f, peakNorm);

        const float radius = 4.0f;
        juce::Rectangle<float> r = area.toFloat().reduced(2.0f);

        // background
        g.setColour(kSlate.withAlpha(0.45f));
        g.fillRoundedRectangle(r, radius);

        // gradient fill for level
        juce::ColourGradient gfill(kLime, r.getX(), r.getY(), kMagenta, r.getRight(), r.getY(), false);
        g.setGradientFill(gfill);

        juce::Rectangle<float> filled = r.withWidth(r.getWidth() * level);
        g.fillRoundedRectangle(filled, radius);

        // subtle inner glow
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillRoundedRectangle(r.removeFromTop((int)juce::roundToInt(r.getHeight() * 0.12f)), radius);

        // peak marker (thin line)
        const float px = r.getX() + r.getWidth() * peakNorm;
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(px - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.setColour(kLime.withAlpha(0.9f));
        g.fillRect(px - 0.8f, r.getY() + 1.0f, 1.6f, r.getHeight() - 2.0f);
    }

    // AppLookAndFeel: small custom LNF for combo boxes and linear sliders (meters)
    struct AppLookAndFeel final : public juce::LookAndFeel_V4
    {
        AppLookAndFeel() = default;

        // ComboBox: rounded background, outline and a lime arrow
        void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
            int buttonX, int buttonY, int buttonW, int buttonH,
            juce::ComboBox& box) override
        {
            // Draw only the background, outline and arrow. Let the ComboBox's internal
            // child Label/TextEditor draw the selected text to avoid double-drawing.
            const auto bounds = box.getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(kSlate);
            g.fillRoundedRectangle(bounds, 6.0f);

            g.setColour(kPurple);
            g.drawRoundedRectangle(bounds, 6.0f, 2.0f);

            // arrow triangle
            juce::Path p;
            const float cx = (float)(buttonX + buttonW / 2);
            const float cy = (float)(buttonY + buttonH / 2);
            p.startNewSubPath(cx - 5.0f, cy - 1.0f);
            p.lineTo(cx, cy + 4.0f);
            p.lineTo(cx + 5.0f, cy - 1.0f);
            p.closeSubPath();
            g.setColour(kLime);
            g.fillPath(p);

            // Do NOT draw the text here — the ComboBox contains an internal Label/TextEditor
            // that will draw the text. Drawing text here plus the internal control causes
            // the overlapping/duplicated text you observed.
        }

        // Slider: used for seek bars / meters — draw a rounded track + gradient fill + small knob
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
            float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
            const juce::Slider::SliderStyle /*style*/, juce::Slider& slider) override
        {
            // make `area` non-const so we can call removeFromTop() safely
            juce::Rectangle<float> area((float)x, (float)y, (float)width, (float)height);
            const float radius = juce::jmin(6.0f, area.getHeight() * 0.3f);

            // background track
            g.setColour(kSlate.darker(0.05f));
            g.fillRoundedRectangle(area.reduced(1.0f), radius);

            // filled area (0..sliderPos)
            const float fillW = juce::jlimit(0.0f, area.getWidth(), sliderPos - (float)x);
            if (fillW > 0.0f)
            {
                juce::ColourGradient grad(kLime, area.getX(), area.getY(), kMagenta, area.getRight(), area.getY(), false);
                g.setGradientFill(grad);
                g.fillRoundedRectangle(area.withWidth(fillW).reduced(1.0f), radius);
            }

            // subtle inner highlight
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            const int highlightH = (int)juce::roundToInt(area.getHeight() * 0.18f);
            auto highlightRect = area.removeFromTop(highlightH).reduced(1.0f);
            g.fillRoundedRectangle(highlightRect, radius);

            // small knob (circle) so the user still sees a handle
            const float knobX = juce::jlimit(area.getX(), area.getRight(), sliderPos - 4.0f);
            const float knobRadius = juce::jmin(8.0f, area.getHeight() * 0.9f);
            g.setColour(kPurple.darker(0.3f));
            g.fillEllipse(knobX, area.getCentreY() - knobRadius * 0.5f, knobRadius, knobRadius);
            g.setColour(kLime.withAlpha(0.95f));
            g.drawEllipse(knobX, area.getCentreY() - knobRadius * 0.5f, knobRadius, knobRadius, 1.0f);
        }
    };
}

// ======= small helper =======

void BoomAudioProcessorEditor::performFileDrag(const juce::File& midiFile)
{
    if (!midiFile.existsAsFile())
    {
        DBG("performFileDrag: file does not exist: " << midiFile.getFullPathName());
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "File missing", "Temporary MIDI file not found:\n" + midiFile.getFullPathName());
        return;
    }

    juce::StringArray files;
    files.add(midiFile.getFullPathName());

    // 1) Best case: perform external drag directly from the editor (it inherits DragAndDropContainer).
    //    This is the correct path for the main window and avoids searching parents.
    try
    {
        performExternalDragDropOfFiles(files, true);
        DBG("performFileDrag: started external drag from editor for " << midiFile.getFullPathName());
        return;
    }
    catch (...)
    {
        DBG("performFileDrag: performExternalDragDropOfFiles threw - falling back to parent search");
    }

    // 2) Defensive: walk parent chain and ask the first DragAndDropContainer we find
    Component* comp = this;
    juce::DragAndDropContainer* dnd = nullptr;
    while (comp != nullptr && dnd == nullptr)
    {
        dnd = juce::DragAndDropContainer::findParentDragContainerFor(comp);
        comp = comp->getParentComponent();
    }

    if (dnd != nullptr)
    {
        dnd->performExternalDragDropOfFiles(files, true);
        DBG("performFileDrag: started external drag via parent container for " << midiFile.getFullPathName());
        return;
    }

    // 3) Host disallows external drags (common). Fallback: reveal file in OS file manager or copy to Desktop.
    DBG("performFileDrag: no DragAndDropContainer found for editor - falling back to reveal/copy. File=" << midiFile.getFullPathName());

    bool revealed = false;
    try
    {
        midiFile.revealToUser(); // revealToUser() returns void — call it and assume success if no exception
        revealed = true;
    }
    catch (...)
    {
        revealed = false;
    }

    if (revealed)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Drag not available",
            "Couldn't start an OS drag in this host. The temporary MIDI was revealed in your file manager:\n\n" + midiFile.getFullPathName());
        return;
    }

    // Last resort: copy to Desktop and reveal that copy
    auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    if (desktop.exists())
    {
        auto dest = desktop.getChildFile(midiFile.getFileName());
        if (dest.existsAsFile()) dest.deleteFile();
        const bool copied = midiFile.copyFileTo(dest);
        if (copied)
        {
            dest.revealToUser();
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                "Drag not available",
                "This host disallows starting an external drag.\nA temporary copy was placed on your Desktop:\n\n" + dest.getFullPathName());
            DBG("performFileDrag: copied temp -> desktop and revealed: " << dest.getFullPathName());
            return;
        }

        DBG("performFileDrag: failed to copy temp to Desktop: " << dest.getFullPathName());
    }

    // Final fallback: inform user where the temp file is
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
        "Drag not available",
        "Couldn't start a drag operation in this context.\nTemporary MIDI file saved to:\n\n" + midiFile.getFullPathName());
}


int BoomAudioProcessorEditor::barsFromBox(const juce::ComboBox& b) { return b.getSelectedId() == 2 ? 8 : 4; }

int BoomAudioProcessorEditor::getBarsFromUI() const
{
    return barsFromBox(barsBox); // you already have barsFromBox(...)
}

// -----------------------------------------------------------------------------
// buildBatchDrumMidi
// Creates a single MIDI file with `howMany` generated drum patterns concatenated sequentially.
// `rowFilterMask` - bitmask of rows to include in the export (1<<rowIndex). If 0 -> include all rows.
// ticksPerQuarter and ppq match your existing writer (we'll assume 96 PPQ like prior code).
//
// -----------------------------------------------------------------------------

int numerator = 4;
int denominator = 4;

// -----------------------------------------------------------------------------
// buildBatchDrumMidi - placed in PluginEditor.cpp near other temp-write helpers
// -----------------------------------------------------------------------------
juce::File buildBatchDrumMidi(const juce::String& baseName,
    const boom::drums::DrumStyleSpec& spec,
    int bars,
    int howMany,
    int restPct,
    int dottedPct,
    int tripletPct,
    int swingPct,
    int seed,
    uint32_t rowFilterMask,
    int numerator,
    int denominator,
    int baseMidiNote = 48)
{
    constexpr int PPQ = 96; // keep consistent with rest of project
    // compute ticks per bar robustly
    double ticksPerBarDouble = (double)PPQ * (double)numerator * (4.0 / (double)denominator);
    const int ticksPerBar = (int)std::round(ticksPerBarDouble);

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(PPQ);
    juce::MidiMessageSequence drumSeq;

    for (int i = 0; i < howMany; ++i)
    {
        boom::drums::DrumPattern pat;
        int useSeed = (seed < 0) ? -1 : seed + i;
        boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, useSeed, numerator, denominator, pat);

        // If your generator output is based on a fixed baseTicksPerBar (e.g., 4*PPQ),
        // uncomment and adjust the scale logic below:
        // double baseTicksPerBar = 4.0 * PPQ;
        // double scale = ticksPerBarDouble / baseTicksPerBar;

        const int patternOffset = (int)std::round(i * (double)bars * ticksPerBarDouble);

        // Generator produces ticks using its internal PPQ (boom::drums::kTicksPerQuarter).
        // Convert those tick values into the PPQ used by this writer (PPQ variable above)
        const double genPPQ = (double)boom::drums::kTicksPerQuarter;
        const double scale = (genPPQ > 0.0) ? ((double)PPQ / genPPQ) : 1.0;

        for (const auto& e : pat)
        {
            if (rowFilterMask != 0 && !(rowFilterMask & (1u << e.row)))
                continue;

            const double noteOnTick = (double)patternOffset + (double)e.startTick * scale;
            const double noteOffTick = noteOnTick + (double)e.lenTicks * scale;

            int midiNote = juce::jlimit(0, 127, baseMidiNote + (int)e.row);

            juce::MidiMessage onMsg = juce::MidiMessage::noteOn(9, (juce::uint8)midiNote, (juce::uint8)e.vel);
            onMsg.setTimeStamp(noteOnTick);
            juce::MidiMessage offMsg = juce::MidiMessage::noteOff(9, (juce::uint8)midiNote);
            offMsg.setTimeStamp(noteOffTick);

            drumSeq.addEvent(onMsg);
            drumSeq.addEvent(offMsg);
        }
    }

    drumSeq.updateMatchedPairs();
    midiFile.addTrack(drumSeq);

    juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile(baseName, ".mid");
    bool wroteOk = boom::midi::writeMidiToFile(midiFile, tmp);
    if (!wroteOk || !tmp.existsAsFile())
        DBG("buildBatchDrumMidi: failed to write temp midi " << tmp.getFullPathName());
    else
        DBG("buildBatchDrumMidi: wrote " << tmp.getFullPathName() << " size=" << tmp.getSize());

    return tmp;
}

juce::File writeAiCaptureToWav(BoomAudioProcessor& proc, const juce::String& baseName, BoomAudioProcessor::CaptureSource src)
{
    // Use public accessors instead of touching private members.
    const juce::AudioBuffer<float>& srcBuf = proc.getCaptureBuffer();
    const int bufSize = proc.getCaptureLengthSamples();

    if (bufSize <= 0 || srcBuf.getNumSamples() <= 0)
        return {};

    // Determine how many valid samples exist in the circular buffer.
    // We assume the editor wants the whole buffer contents up to the write index.
    const int channels = juce::jmax(1, srcBuf.getNumChannels());

    int samples = bufSize;

    if (samples <= 0)
        return {};

    // Snapshot into a local buffer (non-circular contiguous)
    juce::AudioBuffer<float> tmpBuf(channels, samples);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* srcPtr = srcBuf.getReadPointer(ch);
        tmpBuf.copyFrom(ch, 0, srcPtr, samples);
    }

    // Build timestamped temp filename
    auto tmpFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile(baseName + "_" + juce::String((int)juce::Time::getCurrentTime().toMilliseconds()), ".wav");

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> out(tmpFile.createOutputStream());
    if (!out)
        return {};

    double sampleRate = 44100.0;
    if (proc.getCaptureSampleRate() > 0.0)
        sampleRate = proc.getCaptureSampleRate();

    if (auto* writer = wavFormat.createWriterFor(out.release(), sampleRate,
        static_cast<unsigned int>(tmpBuf.getNumChannels()), 16, {}, 0))
    {
        std::unique_ptr<juce::AudioFormatWriter> w(writer);
        // Correct overload: (const float* const* arrays, int numChannels, int numSamples)
        w->writeFromFloatArrays(tmpBuf.getArrayOfReadPointers(), tmpBuf.getNumChannels(), tmpBuf.getNumSamples());
        return tmpFile;
    }

    return {};
}




// ================== Editor ==================
BoomAudioProcessorEditor::BoomAudioProcessorEditor(BoomAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
    drumGrid(p), pianoRoll(p)
{
    // Install app-wide Boom look-and-feel so all ComboBoxes and popups use the same styling.
    // Create a single shared BoomLookAndFeel instance and set it as the default L&F.
    static std::unique_ptr<BoomLookAndFeel> globalBoomLnf;
    if (!globalBoomLnf)
        globalBoomLnf = std::make_unique<BoomLookAndFeel>();
    juce::LookAndFeel::setDefaultLookAndFeel(globalBoomLnf.get());
    // create a single static AppLookAndFeel for slider styling
    static AppLookAndFeel* s_appLnf = nullptr;
    if (s_appLnf == nullptr)
        s_appLnf = new AppLookAndFeel();
    setResizable(true, true);
 

    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 1000);


    // Engine label + buttonskeVisi

    logoImg.setImage(loadSkin("logo.png")); addAndMakeVisible(logoImg);


    lockToBpmLbl.setImage(loadSkin("lockToBpmLbl.png")); addAndMakeVisible(lockToBpmLbl);
    bpmLbl.setImage(loadSkin("bpmLbl.png")); addAndMakeVisible(bpmLbl);
    bpmPoller = std::make_unique<BpmPoller>(proc, [this](double bpm)
        {
            // Replace 'bpmValueLbl' with your actual label variable name.
            // If you don’t have a label yet, use an empty lambda: [](double){}
            bpmValueLbl.setText(juce::String(juce::roundToInt(bpm)), juce::dontSendNotification);
        });

    // --- BPM Lock checkbox (uses existing helper + APVTS attachment) ---
    addAndMakeVisible(bpmLockChk);
    bpmLockChk.setClickingTogglesState(true);

    // IMPORTANT: we use your existing helper so there are NO new functions introduced.
    // This expects files: checkBoxOffBtn.png and checkBoxOnBtn.png in BinaryData.
    boomui::setToggleImages(bpmLockChk, "checkBoxOffBtn", "checkBoxOnBtn");

    // Bind to APVTS param "bpmLock"
    bpmLockAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "bpmLock", bpmLockChk);

    addAndMakeVisible(bpmSlider);
    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 35);
    bpmSlider.setRange(40.0, 240.0, 1.0);
    bpmSlider.setTextValueSuffix(" BPM");
    bpmSlider.setLookAndFeel(&boomui::AltLNF());

    // Attach slider to APVTS "bpm" parameter
    if (proc.apvts.getParameter("bpm") != nullptr)
        bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "bpm", bpmSlider);
    else
        juce::Logger::writeToLog("APVTS parameter missing: bpm - skipping bpm slider attachment");

    // A live BPM readout (uses getHostBpm which respects the lock)
    addAndMakeVisible(bpmValue);
    bpmValue.setJustificationType(juce::Justification::centredLeft);

    // Keep the slider enabled/disabled based on bpmLock
    auto refreshBpmEnabled = [this]
    {
        const bool locked = proc.apvts.getRawParameterValue("bpmLock")->load() > 0.5f;
        bpmSlider.setEnabled(!locked);
    };
    refreshBpmEnabled();

    // If you have a lock checkbox button wired, also make it refresh the slider:
    bpmLockChk.onClick = [this, refreshBpmEnabled] { refreshBpmEnabled(); };

    // Position – keep your existing coordinates if you already had them
    // (these numbers are just an example; do not copy if you already placed it)


    addAndMakeVisible(soundsDopeLbl);
    setButtonImages(soundsDopeLbl, "soundsDopeLbl");
    soundsDopeLbl.onClick = [this]
    {
        // Create a clickable image component that closes on mouse click
        class ClickableImageSplash : public juce::Component
        {
        public:
            ClickableImageSplash(const juce::Image& img)
            {
                splashImg.setImage(img);
                splashImg.setInterceptsMouseClicks(false, false);
                addAndMakeVisible(splashImg);
                
                const int w = img.getWidth();
                const int h = img.getHeight();
                setSize(w, h);
            }
            
            void resized() override
            {
                splashImg.setBounds(getLocalBounds());
            }
            
            void mouseDown(const juce::MouseEvent&) override
            {
                // Close the parent dialog window
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState(0);
            }
            
        private:
            juce::ImageComponent splashImg;
        };
        
        auto img = loadSkin("boomSplashUltd.png");
        if (!img.isValid())
        {
            DBG("soundsDopeLbl: failed to load boomSplashUltd.png");
            return;
        }
        
        auto* splashContent = new ClickableImageSplash(img);
        
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(splashContent);
        opts.dialogTitle = "";
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = false;
        opts.resizable = false;
        opts.componentToCentreAround = this;
        
        if (auto* dw = opts.launchAsync())
        {
            dw->centreAroundComponent(this, img.getWidth(), img.getHeight());
            dw->setVisible(true);
        }
    };
    engineLblImg.setImage(loadSkin("engineLbl.png")); addAndMakeVisible(engineLblImg);
    setButtonImages(btn808, "808Btn");    addAndMakeVisible(btn808);
    setButtonImages(btnBass, "bassBtn");   addAndMakeVisible(btnBass);
    setButtonImages(btnDrums, "drumsBtn");  addAndMakeVisible(btnDrums);
    btn808.onClick = [this] { setEngine(boom::Engine::e808);  };
    btnBass.onClick = [this] { setEngine(boom::Engine::Bass);  };
    btnDrums.onClick = [this] { setEngine(boom::Engine::Drums); };

    updateEngineButtonSkins((boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load(),
        btn808, btnBass, btnDrums);

    // Left labels
    scaleLblImg.setImage(loadSkin("scaleLbl.png"));          addAndMakeVisible(scaleLblImg);
    timeSigLblImg.setImage(loadSkin("timeSigLbl.png"));          addAndMakeVisible(timeSigLblImg);
    barsLblImg.setImage(loadSkin("barsLbl.png"));                addAndMakeVisible(barsLblImg);
    humanizeLblImg.setImage(loadSkin("humanizeLbl.png"));        addAndMakeVisible(humanizeLblImg);
    tripletsLblImg.setImage(loadSkin("tripletsLbl.png"));        addAndMakeVisible(tripletsLblImg);
    dottedNotesLblImg.setImage(loadSkin("dottedNotesLbl.png"));  addAndMakeVisible(dottedNotesLblImg);
    restDensityLblImg.setImage(loadSkin("restDensityLbl.png"));  addAndMakeVisible(restDensityLblImg);
    keyLblImg.setImage(loadSkin("keyLbl.png"));                  addAndMakeVisible(keyLblImg);
    octaveLblImg.setImage(loadSkin("octaveLbl.png"));            addAndMakeVisible(octaveLblImg);
    bassSelectorLblImg.setImage(loadSkin("bassSelectorLbl.png"));  addAndMakeVisible(bassSelectorLblImg);
    drumsSelectorLblImg.setImage(loadSkin("drumsSelectorLbl.png")); addAndMakeVisible(drumsSelectorLblImg);
    eightOhEightLblImg.setImage(loadSkin("808BassLbl.png"));     addAndMakeVisible(eightOhEightLblImg);
    styleLblImg.setImage(loadSkin("styleLbl.png"));     addAndMakeVisible(styleLblImg);


    // Left controls
    addAndMakeVisible(timeSigBox); timeSigBox.addItemList(boom::timeSigChoices(), 1);
    addAndMakeVisible(barsBox);    barsBox.addItemList(boom::barsChoices(), 1);

    addAndMakeVisible(humanizeTiming);   humanizeTiming.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeTiming.setRange(0, 100);
    humanizeTiming.setTooltip("Increase this slider to have more natural, human note/beat placeement!");
    addAndMakeVisible(humanizeVelocity); humanizeVelocity.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeVelocity.setRange(0, 100);
    humanizeVelocity.setTooltip("Increase this slider to have more dynamic range in velocity!");
    addAndMakeVisible(swing);            swing.setSliderStyle(juce::Slider::LinearHorizontal);
    swing.setRange(0, 100);
    swing.setTooltip("Increase this slider to create more swing in the MIDI patterns BOOM generates!");
    addAndMakeVisible(tripletDensity); tripletDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    tripletDensity.setRange(0, 100);
    addAndMakeVisible(dottedDensity);  dottedDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    dottedDensity.setRange(0, 100);
    dottedDensity.setLookAndFeel(&purpleLNF);
    tripletDensity.setLookAndFeel(&purpleLNF);
    boomui::makePercentSlider(dottedDensity);
    boomui::makePercentSlider(tripletDensity);
    dottedDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    tripletDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    humanizeTiming.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    humanizeVelocity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    swing.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    tripletsLblImg.setTooltip("Check the box to include triplets in the MIDI that BOOM generates. Use the slider below to determine how much!");
    dottedNotesLblImg.setTooltip("Check the box to include dotted notes in the MIDI that BOOM generates. Use the slider below to determine how much!");

    // Switches as ImageButtons
    addAndMakeVisible(useTriplets); setToggleImages(useTriplets, "checkBoxOffBtn", "checkBoxOnBtn");
    addAndMakeVisible(useDotted);   setToggleImages(useDotted, "checkBoxOffBtn", "checkBoxOnBtn");


  


    // APVTS attachments
    auto safeCreateCombo = [&](const char* id, std::unique_ptr<Attachment>& out, juce::ComboBox& cb)
        {
            if (proc.apvts.getParameter(id) != nullptr)
                out = std::make_unique<Attachment>(proc.apvts, id, cb);
            else
                juce::Logger::writeToLog(juce::String("APVTS parameter missing: ") + id);
        };

    auto safeCreateSlider = [&](const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& out, juce::Slider& s)
        {
            if (proc.apvts.getParameter(id) != nullptr)
                out = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, s);
            else
                juce::Logger::writeToLog(juce::String("APVTS parameter missing: ") + id);
        };

    auto safeCreateButton = [&](const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& out, juce::Button& b)
        {
            if (proc.apvts.getParameter(id) != nullptr)
                out = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, b);
            else
                juce::Logger::writeToLog(juce::String("APVTS parameter missing: ") + id);
        };

    // Create attachments using safe helpers
    safeCreateCombo("timeSig", timeSigAtt, timeSigBox);
    safeCreateCombo("bars", barsAtt, barsBox);
    safeCreateSlider("humanizeTiming", humanizeTimingAtt, humanizeTiming);
    safeCreateSlider("humanizeVelocity", humanizeVelocityAtt, humanizeVelocity);
    safeCreateSlider("swing", swingAtt, swing);
    safeCreateButton("useTriplets", useTripletsAtt, useTriplets);
    safeCreateSlider("tripletDensity", tripletDensityAtt, tripletDensity);
    safeCreateButton("useDotted", useDottedAtt, useDotted);
    safeCreateSlider("dottedDensity", dottedDensityAtt, dottedDensity);
    safeCreateCombo("key", keyAtt, keyBox);
    safeCreateCombo("scale", scaleAtt, scaleBox);
    safeCreateCombo("octave", octaveAtt, octaveBox);
    safeCreateSlider("restDensity808", rest808Att, rest808);
    safeCreateCombo("bassStyle", bassStyleAtt, bassStyleBox);
    safeCreateSlider("restDensityDrums", restDrumsAtt, restDrums);


    // Replace your current setApvtsBool with this robust version
    auto setApvtsBool = [this](const char* paramId, bool v)
        {
            const float normalized = v ? 1.0f : 0.0f;

            // Prefer setting through parameter object (keeps automation/host notified)
            if (auto* p = proc.apvts.getParameter(paramId))
            {
                p->setValueNotifyingHost(normalized);
                return;
            }

            // Fallback: write raw parameter value if available (some APVTS setups expose raw only)
            if (auto* raw = proc.apvts.getRawParameterValue(paramId))
            {
                raw->store(normalized);
                // Best effort: also notify host by calling setValueNotifyingHost on a parameter if it exists later.
                return;
            }

            // Last-resort: nothing to write (parameter id mismatch or missing). Log so you can see it without heavy debugging.
            DBG("setApvtsBool: WARNING - parameter not found for id='" << paramId << "' (could be an ID mismatch)");
        };

    // helper to read APVTS bool state
    auto getApvtsBool = [this](const char* paramId)->bool
        {
            if (auto* raw = proc.apvts.getRawParameterValue(paramId))
                return raw->load() > 0.5f;
            // If raw param not found, try parameter object (older JUCE APIs)
            if (auto* p = proc.apvts.getParameter(paramId))
            {
                return p->getValue() > 0.5f;
            }
            DBG("getApvtsBool: WARNING - parameter missing: " << paramId);
            return false;
        };

    // Ensure UI reflects APVTS initial state (preset recall / host automation safe)
    {
        const bool tripOn = getApvtsBool("useTriplets");
        const bool dotOn = getApvtsBool("useDotted");
        useTriplets.setToggleState(tripOn, juce::dontSendNotification);
        useDotted.setToggleState(dotOn, juce::dontSendNotification);
        tripletDensity.setEnabled(tripOn);
        dottedDensity.setEnabled(dotOn);
    }

    // When user toggles Triplets: update APVTS, force Dotted off if Triplets on
    useTriplets.onClick = [this, setApvtsBool]()
        {
            const bool newState = useTriplets.getToggleState();
            setApvtsBool("useTriplets", newState);

            if (newState)
            {
                // Turn the other checkbox OFF and update APVTS without sending extra clicks
                useDotted.setToggleState(false, juce::dontSendNotification);
                setApvtsBool("useDotted", false);

                // enable/disable sliders
                tripletDensity.setEnabled(true);
                dottedDensity.setEnabled(false);
            }
            else
            {
                // disable triplet slider only
                tripletDensity.setEnabled(false);
            }

            repaint();
        };

    // When user toggles Dotted: update APVTS, force Triplets off if Dotted on
    useDotted.onClick = [this, setApvtsBool]()
        {
            const bool newState = useDotted.getToggleState();
            setApvtsBool("useDotted", newState);

            if (newState)
            {
                useTriplets.setToggleState(false, juce::dontSendNotification);
                setApvtsBool("useTriplets", false);

                dottedDensity.setEnabled(true);
                tripletDensity.setEnabled(false);
            }
            else
            {
                dottedDensity.setEnabled(false);
            }

            repaint();
        };

    {
        const bool tripOn = getApvtsBool("useTriplets");
        const bool dotOn = getApvtsBool("useDotted");
        const bool ghxOn = getApvtsBool("mode_GHXSTGRID");
        useTriplets.setToggleState(tripOn, juce::dontSendNotification);
        useDotted.setToggleState(dotOn, juce::dontSendNotification);


        tripletDensity.setEnabled(tripOn);
        dottedDensity.setEnabled(dotOn);

    }

    // Parameter listener wiring for host/preset changes — ensure the editor updates
    proc.apvts.addParameterListener("useTriplets", this);
    proc.apvts.addParameterListener("useDotted", this);
    proc.apvts.addParameterListener("tripletDensity", this);
    proc.apvts.addParameterListener("dottedDensity", this);
    proc.apvts.addParameterListener("mode_GHXSTGRID", this);
    proc.apvts.addParameterListener("ghxst_intensity", this);
    proc.apvts.addParameterListener("mode_BounceSync", this);
    proc.apvts.addParameterListener("bouncesync_strength", this);
    proc.apvts.addParameterListener("mode_NegSpace", this);
    proc.apvts.addParameterListener("negspace_gapPct", this);
    proc.apvts.addParameterListener("mode_TripFlip", this);
    proc.apvts.addParameterListener("tripFlip_mode", this);
    proc.apvts.addParameterListener("tripflip_density", this);
    proc.apvts.addParameterListener("mode_PolyGod", this);
    proc.apvts.addParameterListener("polygod_ratio", this);
    proc.apvts.addParameterListener("mode_Scatter", this);


    DBG("APVTS useTriplets=" << proc.apvts.getRawParameterValue("useTriplets")->load()
        << " tripletDensity=" << proc.apvts.getRawParameterValue("tripletDensity")->load()
        << " useDotted=" << proc.apvts.getRawParameterValue("useDotted")->load()
        << " dottedDensity=" << proc.apvts.getRawParameterValue("dottedDensity")->load());

    timeSigBox.onChange = [this]
        {
            updateTimeSigAndBars();
        };

    barsBox.onChange = [this] { updateTimeSigAndBars(); };

    // 808/Bass
    addAndMakeVisible(keyBox);   keyBox.addItemList(boom::keyChoices(), 1);
    addAndMakeVisible(scaleBox); scaleBox.addItemList(boom::scaleChoices(), 1);
    addAndMakeVisible(octaveBox); octaveBox.addItemList(juce::StringArray("-2", "-1", "0", "+1", "+2"), 1);
    addAndMakeVisible(rest808);  rest808.setSliderStyle(juce::Slider::LinearHorizontal);
    rest808.setRange(0, 100); rest808.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rest808.setTooltip("Increase this slider for more gaps (rests) between notes/beats!");

    scaleBox.setTooltip("Choose scale.");
    keyBox.setTooltip("Choose scale.");
    timeSigBox.setTooltip("Choose time signature.");
    barsBox.setTooltip("Choose between 4 or 8 bars");
    octaveBox.setTooltip("Choose an octave.");
    bassStyleBox.setTooltip("Choose a genre of music you'd like to aim for when BOOM generates MIDI.");

    keyAtt = std::make_unique<Attachment>(proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<Attachment>(proc.apvts, "scale", scaleBox);
    octaveAtt = std::make_unique<Attachment>(proc.apvts, "octave", octaveBox);
    rest808Att = std::make_unique<SAttachment>(proc.apvts, "restDensity808", rest808);

    addAndMakeVisible(bassStyleBox); bassStyleBox.addItemList(boom::styleChoices(), 1);
    bassStyleAtt = std::make_unique<Attachment>(proc.apvts, "bassStyle", bassStyleBox);

    // Auto-enable triplets when "drill" style is selected
    bassStyleBox.onChange = [this, setApvtsBool]
    {
        if (bassStyleBox.getText().trim().equalsIgnoreCase("drill"))
        {
            useTriplets.setToggleState(true, juce::dontSendNotification);
            setApvtsBool("useTriplets", true);
            tripletDensity.setEnabled(true);
            useDotted.setToggleState(false, juce::dontSendNotification);
            setApvtsBool("useDotted", false);
            dottedDensity.setEnabled(false);
            repaint();
        }
    };

    // Drums
    addAndMakeVisible(drumStyleBox); drumStyleBox.addItemList(boom::styleChoices(), 1);
    safeCreateCombo("drumStyle", drumStyleAtt, drumStyleBox);
    drumStyleAtt.reset();
    drumStyleAtt = std::make_unique<Attachment>(proc.apvts, "drumStyle", drumStyleBox);

    // If the host/UI still comes up blank for any reason, force a valid visible selection.
    // This does NOT break automation because the attachment still controls the value.
    if (drumStyleBox.getSelectedItemIndex() < 0 && drumStyleBox.getNumItems() > 0)
        drumStyleBox.setSelectedItemIndex(0, juce::dontSendNotification);
    addAndMakeVisible(restDrums); restDrums.setSliderStyle(juce::Slider::LinearHorizontal);
    restDrums.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    restDrums.setRange(0, 100);
    restDrums.setTooltip("Increase this slider for more gaps (rests) between notes/beats!");

    // Auto-enable triplets when "drill" style is selected AND force checkbox to show as checked
    drumStyleBox.onChange = [this, setApvtsBool]
    {
        if (drumStyleBox.getText().trim().equalsIgnoreCase("drill"))
        {
            // Force UI checkbox to show as checked BEFORE setting APVTS
            useTriplets.setToggleState(true, juce::dontSendNotification);
            
            // Set APVTS value
            setApvtsBool("useTriplets", true);
            
            // Enable slider
            tripletDensity.setEnabled(true);
            
            // Set triplet density to minimum 25%
            const float currentDensity = proc.apvts.getRawParameterValue("tripletDensity")->load();
            if (currentDensity < 25.0f)
            {
                // Update both APVTS and UI slider
                if (auto* p = proc.apvts.getParameter("tripletDensity"))
                    p->setValueNotifyingHost(0.25f);  // 25% as 0..1 range
                tripletDensity.setValue(25.0, juce::dontSendNotification);
            }
            
            // Disable dotted
            useDotted.setToggleState(false, juce::dontSendNotification);
            setApvtsBool("useDotted", false);
            dottedDensity.setEnabled(false);
            
            repaint();
        }
    };

    // Center views
    drumGrid.setRows(proc.getDrumRows());
    drumGrid.onToggle = [this](int row, int tick) { toggleDrumCell(row, tick); };
    // DRUM GRID
    const int bars = getBarsFromUI();

    if (auto* g = dynamic_cast<DrumGridComponent*>(drumGridView.getViewedComponent()))
        g->setBarsToDisplay(bars);

    if (auto* pr = dynamic_cast<PianoRollComponent*>(pianoRollView.getViewedComponent()))
        pr->setBarsToDisplay(bars);


    addAndMakeVisible(drumGridView);
    drumGridView.setViewedComponent(&drumGrid, false);  // we own the child elsewhere
    drumGridView.setScrollBarsShown(true, true);        // horizontal + vertical

    // PIANO ROLL
    addAndMakeVisible(pianoRollView);
    pianoRollView.setViewedComponent(&pianoRoll, false);
    pianoRollView.setScrollBarsShown(true, true);

    drumGrid.setBarsToDisplay(bars);
    pianoRoll.setBarsToDisplay(bars);
    drumGrid.setTimeSignature(proc.getTimeSigNumerator(), proc.getTimeSigDenominator());
    pianoRoll.setTimeSignature(proc.getTimeSigNumerator(), proc.getTimeSigDenominator());

    // Right action buttons
    setButtonImages(btnAITools, "aiToolsBtn");  addAndMakeVisible(btnAITools);
    setButtonImages(btnRolls, "rollsBtn");    addAndMakeVisible(btnRolls);
    setButtonImages(btnBumppit, "bumppitBtn");  addAndMakeVisible(btnBumppit);
    setButtonImages(btnFlippit, "flippitBtn");  addAndMakeVisible(btnFlippit);
    setButtonImages(diceBtn, "diceBtn");      addAndMakeVisible(diceBtn);
    setButtonImages(hatsBtn, "hatsBtn");      addAndMakeVisible(hatsBtn);
    setButtonImages(moreBoomBtn, "moreBoomBtn");      addAndMakeVisible(moreBoomBtn);



    btnAITools.setTooltip("Opens the AI Tools Window.");
    btnRolls.setTooltip("Opens the Rolls Window.");
    btnBumppit.setTooltip("Opens the BUMPPIT Window.");
    btnFlippit.setTooltip("Opens the FLIPPIT Window.");
    hatsBtn.setTooltip("Opens the HATS Window.");
    moreBoomBtn.setTooltip("Opens the MORE BOOM Window.");
    diceBtn.setTooltip("Randomizes the parameters in the selection boxes on the left and the humanization sliders on the right. Then just press GENERATE, and BOOM, random fun!");

    diceBtn.onClick = [this]
    {
        int bars = 4;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(proc.apvts.getParameter("bars")))
            bars = p->get();

        juce::Random r;

        // time signature
        if (timeSigBox.getNumItems() > 0)
            timeSigBox.setSelectedId(1 + r.nextInt({ timeSigBox.getNumItems() }), juce::sendNotification);

        // key & scale (if visible for current engine)
        if (keyBox.isVisible() && keyBox.getNumItems() > 0)
            keyBox.setSelectedId(1 + r.nextInt({ keyBox.getNumItems() }), juce::sendNotification);

        if (scaleBox.isVisible() && scaleBox.getNumItems() > 0)
            scaleBox.setSelectedId(1 + r.nextInt({ scaleBox.getNumItems() }), juce::sendNotification);

        // style (leave Bars & Octave alone)
        if (bassStyleBox.getNumItems() > 0)
            bassStyleBox.setSelectedId(1 + r.nextInt({ bassStyleBox.getNumItems() }), juce::sendNotification);

        proc.randomizeCurrentEngine(bars);

        // Update both editors; only one might be visible but this is cheap
        drumGrid.setPattern(proc.getDrumPattern());
        drumGrid.repaint();

        pianoRoll.setPattern(proc.getMelodicPattern());
        pianoRoll.repaint();

        repaint();
    };

    timeSigAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "timeSig", timeSigBox);


    barsAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "bars", barsBox);

    proc.apvts.addParameterListener("timeSig", this);
    proc.apvts.addParameterListener("bars", this);

    // Immediately push current values into both components
    {
        const int num = proc.getTimeSigNumerator();
        const int den = proc.getTimeSigDenominator();
        const int bars5 = getBarsFromUI();

        drumGrid.setTimeSignature(num, den);
        drumGrid.setBarsToDisplay(bars5);

        pianoRoll.setTimeSignature(num, den);
        pianoRoll.setBarsToDisplay(bars5);

        drumGridView.setViewPosition(0, 0);
        pianoRollView.setViewPosition(0, 0);
    }


    btnFlippit.onClick = [this]
    {
        auto engine = (boom::Engine)proc.apvts.getRawParameterValue("engine")->load();

        flippit.reset(new FlippitWindow(
            proc,
            [this] { flippit.reset(); },
            [this](int density)
            {
                const auto eng = proc.getEngineSafe();
                if (eng == boom::Engine::Drums) proc.flipDrums(density, 10, 10);
                else                             proc.flipMelodic(density, 10, 10);
                regenerate();
            },
            engine));

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(flippit.release());
        o.dialogTitle = "FLIPPIT";
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.componentToCentreAround = this;
        o.launchAsync();
    };

    btnBumppit.onClick = [this]
    {
        auto engine = (boom::Engine)proc.apvts.getRawParameterValue("engine")->load();

        bumppit.reset(new BumppitWindow(
            proc,
            [this] { bumppit.reset(); },
            [this] { proc.bumpDrumRowsUp(); regenerate(); },
            engine));

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(bumppit.release());
        o.dialogTitle = "BUMPPIT";
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.componentToCentreAround = this;
        o.launchAsync();
    };

    hatsBtn.onClick = [this]
    {
        auto engine = (boom::Engine)proc.apvts.getRawParameterValue("engine")->load();
        if (engine == boom::Engine::Drums)
        {
            hats.reset(new HatsWindow(proc, [this] { hats.reset(); },
                [this](juce::String style, int bars, int density)
                {
                    juce::ignoreUnused(style, density);
                    proc.setDrumPattern(makeDemoPatternDrums(bars));
                    regenerate();
                }));
            juce::DialogWindow::LaunchOptions o;
            o.content.setOwned(hats.release());
            o.dialogTitle = "HATS";
            o.useNativeTitleBar = true;
            o.resizable = false;
            o.componentToCentreAround = this;
            o.launchAsync();
        }
    };

    moreBoomBtn.onClick = [this]
    {
        moreBoom.reset(new moreBoomWindow(proc, [this] { moreBoom.reset(); }));
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(moreBoom.release());
        o.dialogTitle = "MORE BOOM";
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.componentToCentreAround = this;
        o.launchAsync();
    };

    btnRolls.onClick = [this]
    {
        rolls.reset(new RollsWindow(proc, [this] { rolls.reset(); },
            [this](juce::String style, int bars, int density)
            { juce::ignoreUnused(style, density); proc.setDrumPattern(makeDemoPatternDrums(bars)); regenerate(); }));
        juce::DialogWindow::LaunchOptions o; o.content.setOwned(rolls.release());
        o.dialogTitle = "ROLLS"; o.useNativeTitleBar = true; o.resizable = false; o.componentToCentreAround = this; o.launchAsync();
    };


    btnAITools.onClick = [this]
    {
        juce::DialogWindow::LaunchOptions opts;
        opts.dialogTitle = "AI Tools";
        opts.componentToCentreAround = this;
        opts.useNativeTitleBar = true;
        opts.escapeKeyTriggersCloseButton = true;
        opts.resizable = true;

        opts.content.setOwned(new AIToolsWindow(proc));  // uses default {} onClose
        if (auto* dw = opts.launchAsync())
        {
            dw->setResizable(true, true);
            dw->centreAroundComponent(this, 800, 950);
            dw->setVisible(true);
        }
    };



    // Bottom bar: Generate + Drag (ImageButtons)
    setButtonImages(btnGenerate, "generateBtn"); addAndMakeVisible(btnGenerate);
    setButtonImages(btnDragMidi, "dragBtn");     addAndMakeVisible(btnDragMidi);

    // make selected a latc
    btnGenerate.setTooltip("Generates MIDI patterns according to the ENGINE selected at the top, the choices in the boxes on the left, and the humanization sliders on the right!");
    btnDragMidi.setTooltip("Allows you to drag and drop the MIDI you have generated into your DAW! Hold shift to drag stems out into separate tracks!");
    btnDragMidi.addMouseListener(this, true); // start drag on mouseDown



    // === Correct Generate wiring that matches our existing Processor APIs ===
    btnGenerate.onClick = [this]
        {
            auto readRaw = [&](const char* id, float fallback) -> float
                {
                    if (auto* v = proc.apvts.getRawParameterValue(id)) return v->load();
                    return fallback;
                };
            const auto eng = proc.getEngineSafe(); // your helper that returns boom::Engine
            // Keep UI / APVTS coherent
            setEngine((boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load());

            if (eng == boom::Engine::e808)
            {
                // ---- Bars from APVTS (AudioParameterChoice "bars"), fallback 4 ----
                int uiBars = 4;
                if (auto* barsParam = proc.apvts.getParameter("bars"))
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(barsParam))
                        uiBars = choice->getCurrentChoiceName().getIntValue();

                int uiOctave = 0; // -2..+2 from your "octave" choice
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("octave")))
                    uiOctave = p->getIndex() - 2;

                // NOTE: you had an inverted mapping here previously (100 - ...) — keep as you had it if intentional
                int restPct808 = 10; // fallback
                if (auto* v = proc.apvts.getRawParameterValue("restDensity808"))
                    restPct808 = juce::jlimit(0, 100, (int)juce::roundToInt(v->load()));

                // you intentionally invert it (density = 100 - rest%)
                const int uiDensity = 100 - restPct808;

                // read triplet/dotted toggles (booleans)
                const bool uiTriplets = (boomfix::readParamRaw(proc.apvts, "useTriplets", 0.0f) > 0.5f);
                const bool uiDotted = (boomfix::readParamRaw(proc.apvts, "useDotted", 0.0f) > 0.5f);

                const int seed = -1; // or a fixed int for reproducible results

                // pass the boolean flags (allowTriplets/allowDotted) into the 808 generator
                proc.generate808(uiBars, uiOctave, uiDensity, uiTriplets, uiDotted, seed);

                // ---- Refresh piano roll
                const auto bounds = proc.getMelodicPitchBounds();
                pianoRoll.setPitchRange(bounds.first, bounds.second);

                // Push the notes into the UI
                pianoRoll.setPattern(proc.getMelodicPattern());

                // Finally repaint
                pianoRoll.repaint();
                return;
            }

            if (eng == boom::Engine::Bass)
            {
                // ---- STYLE from APVTS (AudioParameterChoice "style") ----
                juce::String style = "trap";
                if (auto* styleParam = proc.apvts.getParameter("style"))
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(styleParam))
                    {
                        const int idx = choice->getIndex();
                        auto styles = boom::styleChoices();
                        if (styles.size() > 0)
                            style = styles[juce::jlimit(0, styles.size() - 1, idx)];
                    }

                // ---- BARS from APVTS (AudioParameterChoice "bars"), fallback 4 ----
                int bars = 4;
                if (auto* barsParam = proc.apvts.getParameter("bars"))
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(barsParam))
                        bars = choice->getCurrentChoiceName().getIntValue();

                // ---- OCTAVE from APVTS (AudioParameterChoice "octave"), fallback 0 ----
                int octave = 0;
                if (auto* octParam = proc.apvts.getParameter("octave"))
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(octParam))
                        octave = choice->getIndex() - 2;

                // ---- DENSITIES from APVTS raw values (support both 0..1 and 0..100 representations) ----
                // Some hosts/storefronts expose slider values as 0..1 floats, others as 0..100.
                // Handle both: if value > 1.5 assume 0..100, otherwise treat as 0..1 and scale.
                auto clampPct = [](float v) -> int {
                    if (v > 1.5f)
                        return juce::jlimit(0, 100, (int)juce::roundToInt(v));
                    return juce::jlimit(0, 100, (int)juce::roundToInt(v * 100.0f));
                };

                int restPct = 0;
                if (auto* rp = proc.apvts.getRawParameterValue("restDensity"))
                    restPct = clampPct(rp->load());

                int dottedPct = 0;
                if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity"))
                    dottedPct = clampPct(dp->load());

                int tripletPct = 0;
                if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity"))
                    tripletPct = clampPct(tp->load());

                int swingPct = 0;
                if (auto* sp = proc.apvts.getRawParameterValue("swing"))
                    swingPct = clampPct(sp->load());

                // ---- Read checkboxes: gate the sliders (if checkbox OFF, density forced 0)
                bool useTriplets = false;
                bool useDotted = false;
                if (auto* b = proc.apvts.getRawParameterValue("useTriplets")) useTriplets = (b->load() > 0.5f);
                if (auto* b2 = proc.apvts.getRawParameterValue("useDotted"))  useDotted = (b2->load() > 0.5f);

                const int effectiveTripletPct = useTriplets ? tripletPct : 0;
                const int effectiveDottedPct = useDotted ? dottedPct : 0;

                // ---- Generate + refresh UI ----
                // PASS the effective (gated) values into the generator!!
                proc.generateBassFromSpec(style, bars, octave, restPct, effectiveDottedPct, effectiveTripletPct, swingPct, /*seed*/ -1);

                // ---- Refresh piano roll pitch range (CRITICAL FIX: makes BASS notes visible) ----
                const auto bounds = proc.getMelodicPitchBounds();
                pianoRoll.setPitchRange(bounds.first, bounds.second);

                // Push the notes into the UI
                pianoRoll.setPattern(proc.getMelodicPattern());

                // Finally repaint
                pianoRoll.repaint();
                repaint();
                return;
            }

            if (eng == boom::Engine::Drums)
            {
                // ---- STYLE from APVTS
// ---- STYLE from APVTS (MUST be "drumStyle")
                juce::String style = "trap";
                if (auto* styleParam = proc.apvts.getParameter("drumStyle"))
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(styleParam))
                        style = choice->getCurrentChoiceName().trim();

                // ---- BARS from APVTS (AudioParameterChoice "bars"), fallback 4
                int bars = 4;
                if (auto* barsParam = proc.apvts.getParameter("bars"))
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(barsParam))
                        bars = choice->getCurrentChoiceName().getIntValue();

                // ---- DENSITIES from APVTS (support both 0..1 and 0..100 representations)
                auto clampPct = [](float v) -> int {
                    if (v > 1.5f)
                        return juce::jlimit(0, 100, (int)juce::roundToInt(v));
                    return juce::jlimit(0, 100, (int)juce::roundToInt(v * 100.0f));
                };

                int restPct = 0;
                if (auto* rp = proc.apvts.getRawParameterValue("restDensityDrums"))
                    restPct = clampPct(rp->load());

                int dottedPct = 0;
                if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity"))
                    dottedPct = clampPct(dp->load());

                int tripletPct = 0;
                if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity"))
                    tripletPct = clampPct(tp->load());

                int swingPct = 0;
                if (auto* sp = proc.apvts.getRawParameterValue("swing"))
                    swingPct = clampPct(sp->load());

                // ---- Read checkboxes: gate the sliders (if checkbox OFF, density forced 0)
                bool useTriplets = false;
                bool useDotted = false;
                if (auto* b = proc.apvts.getRawParameterValue("useTriplets")) useTriplets = (b->load() > 0.5f);
                if (auto* b2 = proc.apvts.getRawParameterValue("useDotted"))  useDotted = (b2->load() > 0.5f);

                const int effectiveTripletPct = useTriplets ? tripletPct : 0;
                const int effectiveDottedPct = useDotted ? dottedPct : 0;

                // ---- TIME SIGNATURE from UI (READ FROM PROCESSOR, NOT HARD-CODED GLOBALS!)
                const int numerator = proc.getTimeSigNumerator();
                const int denominator = proc.getTimeSigDenominator();

                // ---- Call database generator (PASS effective gated values + ACTUAL time signature)
                boom::drums::DrumStyleSpec spec = boom::drums::getSpecForTimeSig(style, numerator, denominator, /*seed*/ -1);

                boom::drums::DrumPattern pat;
                boom::drums::generate(spec, bars, restPct, effectiveDottedPct, effectiveTripletPct, swingPct, /*seed*/ -1, numerator, denominator, pat);

                // Diagnostics: log how many events we got from the DB generator and effective densities
                DBG("Generate (Drums) -> DB pat size = " << (int)pat.size()
                    << " rest=" << restPct
                    << " dotted=" << effectiveDottedPct
                    << " triplet=" << effectiveTripletPct
                    << " swing=" << swingPct);

                // ---- Convert DB pattern -> processor Pattern explicitly ----
                BoomAudioProcessor::Pattern out;
                out.ensureStorageAllocated((int)pat.size());
                for (const auto& e : pat)
                {
                    BoomAudioProcessor::Note n;
                    n.pitch = 0; // drums don't use pitch in the grid
                    n.row = e.row;
                    n.startTick = e.startTick;
                    n.lengthTicks = e.lenTicks;
                    n.velocity = juce::jlimit<int>(1, 127, (int)e.vel);
                    out.add(n);
                }

                // ============================================================
                // INSERT 2 IN DROP OUT + GLITCHSWITCH MODS HERE
                // (they must modify `out` before it gets stored in processor)
                // ============================================================
                // ============================================================
// moreBoom DRUM MODS (runs here because Drums generation happens in PluginEditor.cpp)
// ============================================================
                {
                    auto removeRow = [&](int row)
                        {
                            for (int i = out.size() - 1; i >= 0; --i)
                                if (out.getReference(i).row == row)
                                    out.remove(i);
                        };

                    // Helper: build a fresh pattern with a real random seed
                    auto buildFreshOutPattern = [&](BoomAudioProcessor::Pattern& freshOut)
                        {
                            juce::Random rr;
                            const int seed = rr.nextInt();

                            auto spec2 = boom::drums::getSpecForTimeSig(style, numerator, denominator, seed);

                            boom::drums::DrumPattern pat2;
                            boom::drums::generate(spec2, bars, restPct, effectiveDottedPct, effectiveTripletPct, swingPct,
                                seed, numerator, denominator, pat2);

                            freshOut.clearQuick();
                            freshOut.ensureStorageAllocated((int)pat2.size());

                            for (const auto& e2 : pat2)
                            {
                                BoomAudioProcessor::Note n2;
                                n2.pitch = 0;
                                n2.row = e2.row;
                                n2.startTick = e2.startTick;
                                n2.lengthTicks = e2.lenTicks;
                                n2.velocity = juce::jlimit<int>(1, 127, (int)e2.vel);
                                freshOut.add(n2);
                            }
                        };

                    // Read params
                    const int glitchMode =
                        (int)proc.apvts.getRawParameterValue("glitchswitch_mode")->load(); // 0 OFF, 1 ON, 2 ON+REGEN

                    const bool twoInDropOutOn =
                        (proc.apvts.getRawParameterValue("mode_TwoInDropOut") != nullptr)
                        ? (proc.apvts.getRawParameterValue("mode_TwoInDropOut")->load() > 0.5f)
                        : false;

                    // ---- ON preserves existing (we're already generating fresh here, so preserve behavior is handled elsewhere)
                    // For ON+REGEN, we simply regenerate AGAIN right here before applying rolls/drops.
                    if (glitchMode == 2)
                    {
                        BoomAudioProcessor::Pattern regen;
                        buildFreshOutPattern(regen);
                        out = regen;
                    }

                    // ------------------------------------------------------------
                    // 2 IN, DROP OUT
                    // Drops 1-2 random rows completely, and regenerates 1-2 OTHER rows completely
                    // ------------------------------------------------------------
                    if (twoInDropOutOn)
                    {
                        const int dropCount = (juce::Random::getSystemRandom().nextInt(100) < 50) ? 1 : 2;
                        const int regenCount = (juce::Random::getSystemRandom().nextInt(100) < 50) ? 1 : 2;

                        // determine available rows from the actual UI row list
                        const auto rowNames = proc.getDrumRows();
                        const int numRows = juce::jlimit(1, 32, rowNames.size());

                        juce::Array<int> rows;
                        for (int r = 0; r < numRows; ++r) rows.add(r);

                        // shuffle
                        for (int i = rows.size() - 1; i > 0; --i)
                        {
                            const int j = juce::Random::getSystemRandom().nextInt(i + 1);
                            std::swap(rows.getReference(i), rows.getReference(j));
                        }

                        juce::Array<int> dropRows;
                        juce::Array<int> regenRows;

                        for (int i = 0; i < rows.size() && dropRows.size() < dropCount; ++i)
                            dropRows.add(rows[i]);

                        for (int i = dropRows.size(); i < rows.size() && regenRows.size() < regenCount; ++i)
                            regenRows.add(rows[i]);

                        // drop completely
                        for (int i = 0; i < dropRows.size(); ++i)
                            removeRow(dropRows[i]);

                        // regen rows completely by stealing those rows from a newly-generated pattern
                        BoomAudioProcessor::Pattern fresh;
                        buildFreshOutPattern(fresh);

                        for (int i = 0; i < regenRows.size(); ++i)
                        {
                            const int rr = regenRows[i];
                            removeRow(rr);

                            for (const auto& n2 : fresh)
                                if (n2.row == rr)
                                    out.add(n2);
                        }
                    }

                    // ------------------------------------------------------------
                    // GLITCHSWITCH
                    // Adds glitch bursts (rolls) to up to 2 rows, favor hats + percs
                    // ------------------------------------------------------------
                    if (glitchMode != 0)
                    {
                        const auto rowNames = proc.getDrumRows();
                        const int numRows = juce::jlimit(1, 32, rowNames.size());

                        auto findFirstRowContains = [&](const juce::String& needle) -> int
                            {
                                for (int i = 0; i < numRows; ++i)
                                    if (rowNames[i].toLowerCase().contains(needle))
                                        return i;
                                return -1;
                            };

                        const int rowHat = findFirstRowContains("hat");
                        const int rowPerc = findFirstRowContains("perc");

                        auto pickWeightedRow = [&]() -> int
                            {
                                const int v = juce::Random::getSystemRandom().nextInt(100);
                                if (rowHat >= 0 && v < 55) return rowHat;
                                if (rowPerc >= 0 && v < 90) return rowPerc;
                                // otherwise random from available
                                return juce::Random::getSystemRandom().nextInt(numRows);
                            };

                        const int ticksPerQuarter = BoomAudioProcessor::PPQ; // 96 in your project
                        const int ticksPerBeat = (int)std::round((double)ticksPerQuarter * (4.0 / (double)denominator));
                        const int ticksPerBar = ticksPerBeat * numerator;

                        auto pickGrid = [&]() -> int
                            {
                                // 1/16T .. 1/64 (favor smaller than 1/16T)
                                const int v = juce::Random::getSystemRandom().nextInt(100);
                                if (v < 10) return juce::jmax(1, ticksPerBeat / 6);   // 1/16T
                                if (v < 30) return juce::jmax(1, ticksPerBeat / 8);   // 1/32
                                if (v < 70) return juce::jmax(1, ticksPerBeat / 12);  // 1/32T
                                return juce::jmax(1, ticksPerBeat / 16);              // 1/64
                            };

                        const int totalTicks = bars * ticksPerBar;
                        const int rowCount = (juce::Random::getSystemRandom().nextInt(100) < 60) ? 1 : 2;

                        juce::Array<int> chosenRows;
                        while (chosenRows.size() < rowCount)
                        {
                            const int r = pickWeightedRow();
                            if (!chosenRows.contains(r))
                                chosenRows.add(r);
                        }

                        for (int ri = 0; ri < chosenRows.size(); ++ri)
                        {
                            const int row = chosenRows[ri];
                            const int segments = 1 + juce::Random::getSystemRandom().nextInt(3); // 1..3 bursts

                            for (int s = 0; s < segments; ++s)
                            {
                                const int bar = juce::Random::getSystemRandom().nextInt(bars);
                                const int grid = pickGrid();

                                // burst starts aligned to 1/16
                                const int align = juce::jmax(1, ticksPerBeat / 4);
                                const int startInBar = juce::Random::getSystemRandom().nextInt(juce::jmax(1, ticksPerBar / align)) * align;

                                const int hits = 4 + juce::Random::getSystemRandom().nextInt(11); // 4..14
                                const int startTickAbs = bar * ticksPerBar + startInBar;
                                const int endTickAbs = juce::jmin(totalTicks, startTickAbs + hits * grid);

                                const int baseVel = 70;
                                const int velRand = 30;
                                const int noteLen = juce::jmax(3, grid / 2);

                                for (int t = startTickAbs; t < endTickAbs; t += grid)
                                {
                                    BoomAudioProcessor::Note n;
                                    n.pitch = 0;
                                    n.row = row;
                                    n.startTick = t;
                                    n.lengthTicks = noteLen;
                                    n.velocity = juce::jlimit(1, 127, baseVel + juce::Random::getSystemRandom().nextInt(velRand));
                                    out.add(n);
                                }
                            }
                        }
                    }
                }

                proc.setDrumPattern(out);
                DBG("Generate (Drums) -> processor drumPattern size = " << (int)proc.getDrumPattern().size());

                // Force UI to show the drum-grid (defensive: ensures editor matches state)
                setEngine(boom::Engine::Drums);
                syncVisibility();

                // Ensure grid shows the requested bars and the newly-set pattern, then repaint + scroll to start
                drumGrid.setBarsToDisplay(bars);
                drumGrid.setPattern(proc.getDrumPattern());
                drumGridView.setViewPosition(0, 0);
                drumGrid.repaint();
                repaint();
                return;
            }
        };


    // --------------------- REPLACE the existing btnDragMidi.onClick body with this ---------------------
    btnDragMidi.onClick = [this]()
        {
            const auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
            juce::File tmp;

            if (engine == boom::Engine::Drums)
            {
                const uint32_t rowMask = drumGrid.getRowSelectionMask();
                if (rowMask != 0u)
                {
                    // Export only selected rows (respects time sig / densities)
                    tmp = buildTempMidiForSelectedRows(rowMask, "BOOM_SelectedRows");
                }
                else
                {
                    // No selection -> export full drum pattern
                    tmp = writeTempMidiFile();
                }
            }
            else
            {
                // Non-drums -> export melodic/full pattern
                tmp = writeTempMidiFile();
            }

            if (!tmp.existsAsFile())
            {
                DBG("btnDragMidi: temp MIDI not found: " << tmp.getFullPathName());
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "MIDI missing",
                    "Temporary MIDI file not found:\n" + tmp.getFullPathName());
                return;
            }

            // Use the editor helper that searches for a DragAndDropContainer and falls back gracefully.
            performFileDrag(tmp);
        };



    // Replace the whole previous dragSelected.onClick = [...] with this block:
  

    // Init engine & layout
    syncVisibility();
    regenerate();


    // Ensure UI updates when processor patterns change:
    // Drum updates (used by various modal windows / processors)
    proc.drumPatternChangedCallback = [this]()
        {
            juce::MessageManager::callAsync([this]()
                {
                    drumGrid.setPattern(proc.getDrumPattern());
                    drumGrid.repaint();
                });
        };

    // Melodic updates (piano roll) — this fixes cases where a modal (BUMPPIT/AI)
    // changes the processor melodic pattern but the main editor wasn't pulling it.
    proc.melodicPatternChangedCallback = [this]()
        {
            juce::MessageManager::callAsync([this]()
                {
                    pianoRoll.setPattern(proc.getMelodicPattern());
                    pianoRoll.repaint();
                });
        };

    setSize(800, 730);

    proc.apvts.addParameterListener("timeSig", this);
    proc.apvts.addParameterListener("bars", this);
}

void BoomAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // parameterChanged may be called off the message thread; marshal UI work back to the message thread.
    juce::MessageManager::callAsync([this, parameterID, newValue]()
        {
            if (parameterID == "timeSig" || parameterID == "bars")
            {
                // preserve your existing behaviour for time signature & bars
                updateTimeSigAndBars();
                return;
            }

            if (parameterID == "useTriplets")
            {
                const bool v = proc.apvts.getRawParameterValue("useTriplets")->load() > 0.5f;
                useTriplets.setToggleState(v, juce::dontSendNotification);
                tripletDensity.setEnabled(v);

                if (v)
                {
                    useDotted.setToggleState(false, juce::dontSendNotification);
                    dottedDensity.setEnabled(false);
                }
                return;
            }

            if (parameterID == "useDotted")
            {
                const bool v = proc.apvts.getRawParameterValue("useDotted")->load() > 0.5f;
                useDotted.setToggleState(v, juce::dontSendNotification);
                dottedDensity.setEnabled(v);

                if (v)
                {
                    useTriplets.setToggleState(false, juce::dontSendNotification);
                    tripletDensity.setEnabled(false);
                }
                return;
            }

            // Keep density sliders in sync for preset recall / automation
            if (parameterID == "tripletDensity")
            {
                if (proc.apvts.getRawParameterValue("tripletDensity") != nullptr)
                {
                    const float v = proc.apvts.getRawParameterValue("tripletDensity")->load();
                    tripletDensity.setValue(v, juce::dontSendNotification);
                }
                return;
            }

            if (parameterID == "dottedDensity")
            {
                if (proc.apvts.getRawParameterValue("dottedDensity") != nullptr)
                {
                    const float v = proc.apvts.getRawParameterValue("dottedDensity")->load();
                    dottedDensity.setValue(v, juce::dontSendNotification);
                }
                return;
            }
        });
}


void BoomAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void BoomAudioProcessorEditor::resized()
{
    const float W = 783.f, H = 714.f;
    auto bounds = getLocalBounds();

    auto sx = bounds.getWidth() / W;
    auto sy = bounds.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx),
            juce::roundToInt(y * sy),
            juce::roundToInt(w * sx),
            juce::roundToInt(h * sy));
    };




    // Header
    engineLblImg.setBounds(S(241, 10, 300, 40));
    btn808.setBounds(S(232, 50, 100, 52));
    btnBass.setBounds(S(341, 50, 100, 52));
    btnDrums.setBounds(S(451, 50, 100, 52));

    logoImg.setBounds(S(255, 95, 290, 290));


    // Right column
    diceBtn.setBounds(S(723, 15, 50, 50));
    tripletsLblImg.setBounds(S(610, 10, 73, 26));
    useTriplets.setBounds(S(690, 18, 20, 20));
    tripletDensity.setBounds(S(583, 30, 100, 20));
    dottedNotesLblImg.setBounds(S(565, 45, 114, 26));
    dottedDensity.setBounds(S(568, 65, 100, 20));
    useDotted.setBounds(S(685, 50, 20, 20));



    soundsDopeLbl.setBounds(S(15, 15, 100, 49));
    lockToBpmLbl.setBounds(S(95, 65, 100, 20));
    bpmLockChk.setBounds(S(200, 60, 24, 24));
    bpmLbl.setBounds(S(105, 85, 100, 20));

    // Row 2: "BPM" label + slider + value label



    // Left Column
    int y = 130;
    const int x = 10;
    const int lblWidth = 100;
    const int ctlWidth = 125;
    const int height = 26;
    const int spacing = 30;
    bpmSlider.setBounds(S(x + lblWidth + 5, y - 20, ctlWidth, height + 20));
    bpmValueLbl.setBounds(S(x + lblWidth + 5 + ctlWidth - 55, y, 150, height));
    y += spacing;

    keyLblImg.setBounds(S(x, y, lblWidth, height));
    keyBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    scaleLblImg.setBounds(S(x, y, lblWidth, height));
    scaleBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    octaveLblImg.setBounds(S(x, y, lblWidth, height));
    octaveBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    timeSigLblImg.setBounds(S(x, y, lblWidth, height));
    timeSigBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    barsLblImg.setBounds(S(x, y, lblWidth, height));
    barsBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    restDensityLblImg.setBounds(S(x, y, lblWidth, height));
    rest808.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    restDrums.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    styleLblImg.setBounds(S(x, y, lblWidth, height));
    bassStyleBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    drumStyleBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    // Right Column
    int right_x = 550;
    y = 150;
    humanizeLblImg.setBounds(S(right_x, y, 200, 26));
    y += spacing;
    humanizeTiming.setBounds(S(right_x, y, 200, 50));
    y += spacing;
    humanizeVelocity.setBounds(S(right_x, y, 200, 50));
    y += spacing;
    swing.setBounds(S(right_x, y, 200, 50));


    // Buttons
    btnBumppit.setBounds(S(580, 280, 200, 60));
    hatsBtn.setBounds(S(15, 180, 200, 60));
    
    // Bottom row buttons - moreBoomBtn large, other buttons bigger
    moreBoomBtn.setBounds(S(40, 370, 190, 55));
    btnRolls.setBounds(S(245, 370, 155, 48));
    btnAITools.setBounds(S(410, 370, 155, 48));
    btnFlippit.setBounds(S(575, 370, 155, 48));

    // DRUM GRID (main window) — larger with reduced gap above
    {
        auto gridArea = S(40, 425, 700, 200);
        drumGridView.setBounds(gridArea);
        drumGrid.setTopLeftPosition(0, 0);
        drumGrid.setSize(gridArea.getWidth() * 2, gridArea.getHeight());
    }

    // PIANO ROLL (main window) — larger with reduced gap above
    {
        auto rollArea = S(40, 425, 700, 200);
        pianoRollView.setBounds(rollArea);
        pianoRoll.setTopLeftPosition(0, 0);
        pianoRoll.setSize(pianoRoll.contentWidth(), pianoRoll.contentHeight());
    }

    // Make sure they’re visible (constructor must setViewedComponent already)
    pianoRollView.toFront(false);
    drumGridView.toFront(false);

    // Bottom bar
    btnGenerate.setBounds(S(40, 640, 300, 70));
    btnDragMidi.setBounds(S(443, 640, 300, 70));

    // compute two-button rect but only apply when overlay is NOT visible
    syncVisibility();
}

void AIToolsWindow::timerCallback()
{
    // Update seekbars from processor (position/length)
    updateSeekFromProcessor();

    // Update input meters (smoothed)
    const float l = proc.getInputRMSL();
    const float r = proc.getInputRMSR();

    levelL = 0.9f * levelL + 0.1f * l;
    levelR = 0.9f * levelR + 0.1f * r;

    // Playback / capture playhead info for display
    const int playS = proc.getCapturePlayheadSamples();
    const int lenS = proc.getCaptureLengthSamples();
    const double sr = proc.getCaptureSampleRate();

    playbackSeconds = (sr > 0.0 ? (double)playS / sr : 0.0);
    lengthSeconds = (sr > 0.0 ? (double)lenS / sr : 0.0);

    // Ensure seekbars are enabled only when capture exists
    const bool hasCap = proc.aiHasCapture();
    rhythmSeek.setEnabled(hasCap && activeTool_ == Tool::Rhythmimick);
    beatboxSeek.setEnabled(hasCap && activeTool_ == Tool::Beatbox);

    // Update record button visual blinking when recording
    const bool blink = ((juce::Time::getMillisecondCounter() / 400) % 2) == 0;
    if (proc.ai_isRhRecording())
        setButtonImages(btnRec1, blink ? "recordBtn_down" : "recordBtn");
    else
        setButtonImages(btnRec1, "recordBtn");

    if (proc.ai_isBxRecording())
        setButtonImages(btnRec4, blink ? "recordBtn_down" : "recordBtn");
    else
        setButtonImages(btnRec4, "recordBtn");

    // Update play button visuals based on processor previewing state (covers external changes)
    if (proc.aiIsPreviewing())
    {
        // Only highlight the UI control that owns the preview. The window remembers which button
        // last started preview (g_previewOwner). If unknown, stay conservative and show neither.
        const int owner = g_previewOwner.load();
        if (owner == Preview_Rhythm)
        {
            setButtonImages(btnPlay1, "playBtn_down");
            setButtonImages(btnPlay4, "playBtn");
        }
        else if (owner == Preview_Beatbox)
        {
            setButtonImages(btnPlay4, "playBtn_down");
            setButtonImages(btnPlay1, "playBtn");
        }
        else
        {
            // Unknown owner -> show neither as "down" to avoid confusing the user
            setButtonImages(btnPlay1, "playBtn");
            setButtonImages(btnPlay4, "playBtn");
        }
    }
    else
    {
        // Preview stopped — reset both and clear owner
        setButtonImages(btnPlay1, "playBtn");
        setButtonImages(btnPlay4, "playBtn");
        g_previewOwner.store(Preview_None);
    }

    // --- Diagnostic logging (low-rate) --------------------------------
    static int tick = 0;
    ++tick;
    if ((tick % 8) == 0)
    {
        juce::String devName = "(no manager)";
        bool onBuf = false;
        bool recFile = false;

        if (audioInputManager)
        {
            devName = audioInputManager->getCurrentInputDeviceName();
            onBuf = static_cast<bool>(audioInputManager->onBufferReady);
            recFile = audioInputManager->isRecordingToFile();
        }

        juce::String msg = juce::String("AIToolsWindow: STATUS dev='") + devName
            + "' onBufferReady=" + juce::String((int)onBuf)
            + " procCapturing=" + juce::String((int)proc.aiIsCapturing())
            + " ai_isRhRecording=" + juce::String((int)proc.ai_isRhRecording())
            + " isRecToFile=" + juce::String((int)recFile)
            + " capLenSamples=" + juce::String(proc.getCaptureLengthSamples());

        DBG(msg);
    }

    repaint();
}

void BoomAudioProcessorEditor::updateTimeSigAndBars()
{
    const int num = proc.getTimeSigNumerator();
    const int den = proc.getTimeSigDenominator();
    const int bars = proc.getBars();

    // Push into your components FIRST (before clearing patterns)
    drumGrid.setTimeSignature(num, den);
    pianoRoll.setTimeSignature(num, den);
    drumGrid.setBarsToDisplay(bars);
    pianoRoll.setBarsToDisplay(bars);
    
    // CRITICAL: Force drum grid to recalculate its size so mouse interaction works
    drumGrid.resized();

    // Clear pattern when time signature changes (AFTER setting time sig in components)
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    
    // Create empty patterns first
    BoomAudioProcessor::Pattern emptyDrumPattern;
    BoomAudioProcessor::MelPattern emptyMelPattern;
    
    if (engine == boom::Engine::Drums)
    {
        proc.setDrumPattern(emptyDrumPattern);
        drumGrid.setPattern(emptyDrumPattern);
    }
    else
    {
        proc.setMelodicPattern(emptyMelPattern);
        pianoRoll.setPattern(emptyMelPattern);
    }

    // Reset scroll so users see bar 1 when TS/bars change
    drumGridView.setViewPosition(0, 0);
    pianoRollView.setViewPosition(0, 0);

    // Force a repaint
    drumGrid.repaint();
    pianoRoll.repaint();
}

void BoomAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &btnDragMidi || e.originalComponent == &btnDragMidi)
    {
        // show overlay for drums (existing logic depends on this flag)
        drumDragChoicesVisible = true;

        const bool multiTrackStems = e.mods.isShiftDown(); // SHIFT = stems
        startExternalMidiDrag(multiTrackStems);
    }


    // otherwise ignore (soundsDopeLbl now handles its own onClick)
}

void BoomAudioProcessorEditor::setEngine(boom::Engine e)
{
    proc.apvts.getParameter("engine")->beginChangeGesture();
    dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("engine"))->operator=((int)e);
    proc.apvts.getParameter("engine")->endChangeGesture();
    syncVisibility();
    resized();
    updateEngineButtonSkins(e, btn808, btnBass, btnDrums);
}

void BoomAudioProcessorEditor::syncVisibility()
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    const bool is808 = engine == boom::Engine::e808;
    const bool isBass = engine == boom::Engine::Bass;
    const bool isDrums = engine == boom::Engine::Drums;

    drumGridView.setVisible(isDrums);
    pianoRollView.setVisible(!isDrums);

    // --- Left Column based on user request ---

    // 808: KEY, SCALE, BARS, OCTAVE, Rest Density
    // BASS: KEY, SCALE, BARS, OCTAVE, STYLE, Rest Density
    // DRUMS: TIME SIGNATURE, BARS, STYLE

    keyLblImg.setVisible(is808 || isBass);
    keyBox.setVisible(is808 || isBass);

    scaleLblImg.setVisible(is808 || isBass);
    scaleBox.setVisible(is808 || isBass);

    barsLblImg.setVisible(true); // all engines
    barsBox.setVisible(true); // all engines

    octaveLblImg.setVisible(is808 || isBass);
    octaveBox.setVisible(is808 || isBass);

    styleLblImg.setVisible(isBass || isDrums);
    bassStyleBox.setVisible(isBass);
    drumStyleBox.setVisible(isDrums);

    restDensityLblImg.setVisible(true);
    rest808.setVisible(is808 || isBass);
    restDrums.setVisible(isDrums);

    timeSigLblImg.setVisible(true);
    timeSigBox.setVisible(true);

    // --- Other UI elements from original implementation ---
    // These seem to be independent of the combo box changes.
    tripletDensity.setVisible(is808 || isDrums || isBass);
    dottedDensity.setVisible(is808 || isDrums || isBass);
    eightOhEightLblImg.setVisible(is808);

    // Hide these as they are replaced by the generic "style" label.
    bassSelectorLblImg.setVisible(false);
    drumsSelectorLblImg.setVisible(false);

    // --- Buttons ---
    // Keep ROLLS, AI Tools, and moreBoom visible for all engines
    // For 808/BASS: gray tint to indicate disabled
    btnRolls.setVisible(true);
    btnRolls.setEnabled(isDrums);
    btnRolls.setAlpha(1.0f);
    
    btnAITools.setVisible(true);
    btnAITools.setEnabled(isDrums);
    btnAITools.setAlpha(1.0f);
    
    moreBoomBtn.setVisible(true);
    moreBoomBtn.setEnabled(isDrums);
    moreBoomBtn.setAlpha(1.0f);
    
    hatsBtn.setVisible(isDrums);

}

void BoomAudioProcessorEditor::regenerate()
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    const int bars = barsFromBox(barsBox);

    if (engine == boom::Engine::Drums)
    {
        if (proc.getDrumPattern().isEmpty())
            proc.setDrumPattern(makeDemoPatternDrums(bars));
        drumGrid.setPattern(proc.getDrumPattern());
    }
    else
    {
        if (proc.getMelodicPattern().isEmpty())
            proc.setMelodicPattern(makeDemoPatternMelodic(bars));
        pianoRoll.setPattern(proc.getMelodicPattern());
    }

    repaint();
}

void BoomAudioProcessorEditor::toggleDrumCell(int row, int tick)
{
    auto pat = proc.getDrumPattern();
    for (int i = 0; i < pat.size(); ++i)
        if (pat[i].row == row && pat[i].startTick == tick)
        {
            pat.remove(i); proc.setDrumPattern(pat); drumGrid.setPattern(pat); repaint(); return;
        }
    BoomAudioProcessor::Note n; n.row = row; n.startTick = tick; n.lengthTicks = 24; n.velocity = 100; n.pitch = 0;
    pat.add(n); proc.setDrumPattern(pat); drumGrid.setPattern(pat); repaint();
}

BoomAudioProcessor::DrumPattern BoomAudioProcessorEditor::makeDemoPatternDrums(int bars) const
{
    BoomAudioProcessor::DrumPattern pat;
    const int stepsPerBar = 16; const int ticksPerStep = 24;
    const int totalSteps = stepsPerBar * juce::jmax(1, bars);
    for (int c = 0; c < totalSteps; ++c)
    {
        if (c % stepsPerBar == 0) { pat.add({ 0, 0, c * ticksPerStep, 24, 110 }); }
        if (c % stepsPerBar == 8) { pat.add({ 0, 0, c * ticksPerStep, 24, 105 }); }
        if (c % 4 == 0) { pat.add({ 0, 2, c * ticksPerStep, 12,  80 }); }
        if (c % stepsPerBar == 4) { pat.add({ 0, 1, c * ticksPerStep, 24, 110 }); }
        if (c % stepsPerBar == 12) { pat.add({ 0, 1, c * ticksPerStep, 24, 110 }); }
    }
    return pat;
}

BoomAudioProcessor::MelPattern BoomAudioProcessorEditor::makeDemoPatternMelodic(int bars) const
{
    BoomAudioProcessor::MelPattern pat;
    const int ticks = 24;
    const int base = 36; // C2
    for (int b = 0; b < juce::jmax(1, bars); ++b)
    {
        pat.add({ base + 0, (b * 16 + 0) * ticks, 8 * ticks, 100 });
        pat.add({ base + 7, (b * 16 + 8) * ticks, 8 * ticks, 100 });
    }
    return pat;
}

// --- replace the existing function in PluginEditor.cpp with this ---
// --- replace the existing function in PluginEditor.cpp with this ---
juce::File BoomAudioProcessorEditor::buildTempMidiForSelectedRows(const juce::String& baseName) const
{
    // Read bars from UI
    const int bars = getBarsFromUI();

    // Read style from UI (drumStyleBox exists in the editor ctor earlier)
    juce::String style = "trap";
    if (drumStyleBox.getNumItems() > 0)
        style = drumStyleBox.getText().trim();

    // read percent-style params (defensive guards)
    auto clampPct = [](float v) -> int { return juce::jlimit(0, 100, (int)juce::roundToInt(v)); };

    int restPct = 0, dottedPct = 0, tripletPct = 0, swingPct = 0;
    if (auto* rp = proc.apvts.getRawParameterValue("restDensityDrums")) restPct = clampPct(rp->load());
    if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity")) dottedPct = clampPct(dp->load());
    if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity")) tripletPct = clampPct(tp->load());
    if (auto* sp = proc.apvts.getRawParameterValue("swing")) swingPct = clampPct(sp->load());

    // get the selection mask from the grid
    uint32_t rowMask = drumGrid.getRowSelectionMask();

    // If no rows selected (rowMask == 0), preserve existing behavior: use buildBatchDrumMidi
    if (rowMask == 0u)
    {
        // buildBatchDrumMidi expects 0 => include all rows (per its comment)
        // keep calling it exactly as before
        auto tmp = buildBatchDrumMidi(baseName,
            boom::drums::getSpecForTimeSig(style,
                proc.getTimeSigNumerator(),
                proc.getTimeSigDenominator(),
                /*seed*/ -1),
            bars,
            1,               // howMany
            restPct,
            dottedPct,
            tripletPct,
            swingPct,
            -1,              // seed
            rowMask,
            proc.getTimeSigNumerator(),
            proc.getTimeSigDenominator());

        return tmp;
    }

    // --- Selected-rows path: build from current processor pattern so sub-16th notes survive ---

    // Get the processor's stored drum pattern (uses the exact startTick/lengthTicks values)
    const auto& procPattern = proc.getDrumPattern();

    // Convert processor pattern -> midi DrumPattern, filtering by rowMask
    boom::midi::DrumPattern mp;

    int keptNotes = 0;
    for (const auto& n : procPattern)
    {
        // If a rowMask is supplied, skip rows not selected.
        if (!(rowMask & (1u << n.row)))
            continue;

        // Preserve the exact ticks/length/velocity from the processor
        mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
        ++keptNotes;
    }

    DBG("buildTempMidiForSelectedRows: procPattern total=" << procPattern.size()
        << " kept=" << keptNotes << " rowMask=0x" << juce::String::toHexString(rowMask));

return buildTempMidiForSelectedRows(rowMask, baseName);
}




// Add / replace in PluginEditor.cpp

// Replace existing BoomAudioProcessorEditor::buildTempMidiForSelectedRows(...) with this:
juce::File BoomAudioProcessorEditor::buildTempMidiForSelectedRows(uint32_t rowMask, const juce::String& baseName) const
{
    // If rowMask == 0 -> preserve existing batch generation behavior
    if (rowMask == 0u)
    {
        const juce::String style = drumStyleBox.getText().trim();
        const int bars = getBarsFromUI();
        const int num = proc.getTimeSigNumerator();
        const int den = proc.getTimeSigDenominator();

        return buildBatchDrumMidi(baseName,
            boom::drums::getSpecForTimeSig(style, num, den, /*seed*/ -1),
            bars,
            1,   // howMany
            (int)juce::roundToInt(proc.apvts.getRawParameterValue("restDensityDrums")->load()),
            (int)juce::roundToInt(proc.apvts.getRawParameterValue("dottedDensity")->load()),
            (int)juce::roundToInt(proc.apvts.getRawParameterValue("tripletDensity")->load()),
            (int)juce::roundToInt(proc.apvts.getRawParameterValue("swing")->load()),
            -1, // seed
            rowMask,
            num,
            den);
    }

    // Build from the processor pattern (preserve exact tick resolution)
    boom::midi::DrumPattern mp;
    const auto& procPattern = proc.getDrumPattern();

    int keptNotes = 0;
    for (const auto& n : procPattern)
    {
        if (!(rowMask & (1u << n.row)))
            continue;

        // Use exact stored ticks/length/velocity
        mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
        ++keptNotes;
    }

    DBG("buildTempMidiForSelectedRows: procPattern total=" << procPattern.size()
        << " kept=" << keptNotes << " rowMask=0x" << juce::String::toHexString(rowMask));

    // Build a JUCE MidiFile explicitly and write it ourselves to avoid any hidden quantization.
    juce::MidiFile mf;
    const int ppq = 96;
    mf.setTicksPerQuarterNote(ppq);

    // Create one MidiMessageSequence track for all drum notes
    juce::MidiMessageSequence track;

    const int baseMidi = 60; // C3

    for (const auto& e : mp)
    {
        const int pitch = juce::jlimit(0, 127, baseMidi + e.row);
        const int ch = 10; // drums channel
        const juce::uint8 vel = (juce::uint8)juce::jlimit(1, 127, (int)e.velocity);

        const juce::int32 onTick = (juce::int32)e.startTick;
        const juce::int32 offTick = (juce::int32)(e.startTick + juce::jmax(1, (int)e.lengthTicks));

        track.addEvent(juce::MidiMessage::noteOn(ch, pitch, vel), onTick);
        track.addEvent(juce::MidiMessage::noteOff(ch, pitch), offTick);
    }


    track.updateMatchedPairs();
    mf.addTrack(track);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile(baseName, ".mid");

    if (tmp.existsAsFile())
        tmp.deleteFile();

    juce::FileOutputStream out(tmp);
    if (!out.openedOk())
    {
        DBG("buildTempMidiForSelectedRows: failed to open temp file for writing: " << tmp.getFullPathName());
        return tmp;
    }

    mf.writeTo(out);
    out.flush();
    // DO NOT call out.close(); FileOutputStream has no close() member — it closes on destruction / scope exit.

    if (!tmp.existsAsFile())
    {
        DBG("buildTempMidiForSelectedRows: write failed for " << tmp.getFullPathName());
    }
    else
    {
        int minLen = INT_MAX;
        for (auto& e : mp) minLen = juce::jmin(minLen, (int)e.lengthTicks);
        DBG("buildTempMidiForSelectedRows: wrote " << tmp.getFullPathName() << " notes=" << mp.size() << " minLenTicks=" << (minLen == INT_MAX ? 0 : minLen));
    }

    return tmp;
}

juce::File BoomAudioProcessorEditor::buildTempMidiForSelectedRowsMultiTrack(uint32_t rowMask,
    const juce::String& baseFileName) const
{
    using namespace juce;

    // Pull the processor pattern (exact ticks/lengths preserved)
    const auto& pat = proc.getDrumPattern();

    // Row names (Kick, Snare, HiHat...)
    const auto& rowNames = proc.getDrumRows();

    // Same mapping logic you already use in DrumGridComponent::exportSelectionToMidiTemp()
    auto noteForRow = [&rowNames](int row) -> int
        {
            if ((unsigned)row < (unsigned)rowNames.size())
            {
                auto name = rowNames[(int)row].toLowerCase();

                if (name.contains("kick"))  return 36;
                if (name.contains("snare")) return 38;
                if (name.contains("clap"))  return 39;
                if (name.contains("rim"))   return 37;
                if (name.contains("open") && name.contains("hat"))   return 46;
                if (name.contains("closed") && name.contains("hat")) return 42;
                if (name.contains("hat"))   return 42;
                if (name.contains("low") && name.contains("tom"))    return 45;
                if (name.contains("mid") && name.contains("tom"))    return 47;
                if (name.contains("high") && name.contains("tom"))   return 50;
                if (name.contains("perc"))  return 48;
                if (name.contains("crash")) return 49;
                if (name.contains("ride"))  return 51;
            }

            // fallback (matches your DrumGrid fallback style)
            switch (row)
            {
            case 0: return 36; case 1: return 38; case 2: return 42; case 3: return 46; default: return 45 + (row % 5);
            }
        };

    MidiFile mf;
    const int ppq = 96;
    mf.setTicksPerQuarterNote(ppq);

    // For each row, make a separate track
    for (int row = 0; row < rowNames.size(); ++row)
    {
        if (rowMask != 0u && ((rowMask & (1u << (uint32_t)row)) == 0u))
            continue;

        MidiMessageSequence track;

        // Track name at tick 0 so DAWs label tracks nicely
        track.addEvent(MidiMessage::textMetaEvent(3, rowNames[row]), 0);

        const int midiNote = noteForRow(row);
        const int ch = 10; // drums

        for (const auto& n : pat)
        {
            if (n.row != row)
                continue;

            const int startTick = jmax(0, n.startTick);
            const int lenTick = jmax(1, n.lengthTicks);
            const int endTick = startTick + lenTick;

            const uint8 vel = (uint8)jlimit(1, 127, n.velocity);

            track.addEvent(MidiMessage::noteOn(ch, midiNote, vel), startTick);
            track.addEvent(MidiMessage::noteOff(ch, midiNote), endTick);
        }

        track.updateMatchedPairs();

        // Only add non-empty tracks (prevents “blank tracks” in DAWs)
        if (track.getNumEvents() > 1) // track-name + at least one note event
            mf.addTrack(track);
    }

    // Write to temp
    auto tmp = File::getSpecialLocation(File::tempDirectory)
        .getNonexistentChildFile(baseFileName, ".mid");

    if (tmp.existsAsFile())
        tmp.deleteFile();

    FileOutputStream out(tmp);
    if (!out.openedOk())
    {
        DBG("buildTempMidiForSelectedRowsMultiTrack: failed to open temp file: " << tmp.getFullPathName());
        return tmp;
    }

    mf.writeTo(out);
    out.flush();

    DBG("buildTempMidiForSelectedRowsMultiTrack: wrote " << tmp.getFullPathName()
        << " tracks=" << mf.getNumTracks());

    return tmp;
}


// ---- replace existing BoomAudioProcessorEditor::writeTempMidiFile() ----
juce::File BoomAudioProcessorEditor::writeTempMidiFile() const
{
    const int ppq = 96;

    // Determine engine
    boom::Engine eng = boom::Engine::Drums;
    eng = proc.getEngineSafe();

    // Use different temp files so melodic exports don't overwrite drum exports
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::File outFile = (eng == boom::Engine::Drums)
        ? tempDir.getChildFile("boom_temp_drum_export.mid")
        : tempDir.getChildFile("boom_temp_melodic_export.mid");

    if (outFile.existsAsFile())
        outFile.deleteFile();

    juce::MidiMessageSequence seq;

    // ============================================================
    // DRUMS EXPORT (keeps your current behavior)
    // ============================================================
    if (eng == boom::Engine::Drums)
    {
        const int baseMidi = 60; // C3
        const auto pattern = proc.getDrumPattern();

        juce::Array<int> selectedRows;
        bool selectionAvailable = false;

        if (&drumGrid != nullptr)
        {
            if (drumGrid.isAnyRowSelected())
            {
                selectedRows = drumGrid.getSelectedRows();
                selectionAvailable = true;
            }
        }

        // Sanitize pattern for export according to APVTS toggles
        auto sanitizePatternForExport = [this](const BoomAudioProcessor::Pattern& src)->BoomAudioProcessor::Pattern
            {
                BoomAudioProcessor::Pattern out;
                out.ensureStorageAllocated(src.size());

                const int ppqLocal = 96;
                const bool showTriplets = (proc.apvts.getRawParameterValue("useTriplets") != nullptr)
                    ? (proc.apvts.getRawParameterValue("useTriplets")->load() > 0.5f)
                    : false;
                const bool showDotted = (proc.apvts.getRawParameterValue("useDotted") != nullptr)
                    ? (proc.apvts.getRawParameterValue("useDotted")->load() > 0.5f)
                    : false;

                auto normalizePct01or100 = [](float v) -> int
                    {
                        if (v <= 1.0f)
                            return juce::jlimit(0, 100, (int)juce::roundToInt(v * 100.0f));
                        return juce::jlimit(0, 100, (int)juce::roundToInt(v));
                    };

                int tripletPct = 0;
                int dottedPct = 0;

                if (auto* r = proc.apvts.getRawParameterValue("tripletDensity"))
                    tripletPct = normalizePct01or100(r->load());
                if (auto* d = proc.apvts.getRawParameterValue("dottedDensity"))
                    dottedPct = normalizePct01or100(d->load());

                if (!showTriplets) tripletPct = 0;
                if (!showDotted)  dottedPct = 0;

                const int cellsPerBeat = drumGrid.getCellsPerBeat();
                juce::Random rnd((int)juce::Time::getMillisecondCounter());

                auto findNearestBaseTicks = [&](int lenTicks)->int
                    {
                        const int denoms[] = { 1,2,4,8,16,32,64 };
                        int best = boom::grid::ticksForDenominator(ppqLocal, 16);
                        int bestDiff = INT_MAX;

                        for (auto d : denoms)
                        {
                            int base = boom::grid::ticksForDenominator(ppqLocal, d);
                            int diff = std::abs(lenTicks - base);
                            if (diff < bestDiff) { bestDiff = diff; best = base; }
                        }
                        return best;
                    };

                for (const auto& n : src)
                {
                    BoomAudioProcessor::Note nn = n;

                    if (tripletPct == 0 && dottedPct == 0)
                    {
                        nn.lengthTicks = boom::grid::snapTicksToNearestSubdivision(nn.lengthTicks, ppqLocal, false, false);
                        nn.startTick = boom::grid::snapTicksToGridStep(nn.startTick, ppqLocal, cellsPerBeat);
                        out.add(nn);
                        continue;
                    }

                    const int totalPct = tripletPct + dottedPct;
                    const int roll = (totalPct > 0) ? rnd.nextInt(100) : 100;

                    if (totalPct > 0 && roll < tripletPct)
                    {
                        int base = findNearestBaseTicks(nn.lengthTicks);
                        nn.lengthTicks = boom::grid::tripletTicks(base);
                    }
                    else if (totalPct > 0 && roll < tripletPct + dottedPct)
                    {
                        int base = findNearestBaseTicks(nn.lengthTicks);
                        nn.lengthTicks = boom::grid::dottedTicks(base);
                    }
                    else
                    {
                        if (tripletPct == 0)
                            nn.lengthTicks = boom::grid::snapTicksToNearestSubdivision(nn.lengthTicks, ppqLocal, true, false);
                        else if (dottedPct == 0)
                            nn.lengthTicks = boom::grid::snapTicksToNearestSubdivision(nn.lengthTicks, ppqLocal, false, true);
                        else
                            nn.lengthTicks = juce::jmax(1, nn.lengthTicks);
                    }

                    if (tripletPct == 0)
                        nn.startTick = boom::grid::snapTicksToGridStep(nn.startTick, ppqLocal, cellsPerBeat);

                    out.add(nn);
                }

                return out;
            };

        BoomAudioProcessor::Pattern exportPattern;
        if (!selectionAvailable)
            exportPattern = sanitizePatternForExport(pattern);
        else
        {
            BoomAudioProcessor::Pattern filtered;
            for (const auto& n : pattern)
                if (selectedRows.contains(n.row)) filtered.add(n);
            exportPattern = sanitizePatternForExport(filtered);
        }

        for (const auto& n : exportPattern)
        {
            const int pitch = juce::jlimit(0, 127, baseMidi + n.row);
            const int channel = 10;

            const juce::MidiMessage on = juce::MidiMessage::noteOn(channel, pitch, (juce::uint8)n.velocity);
            const juce::MidiMessage off = juce::MidiMessage::noteOff(channel, pitch);

            seq.addEvent(on, n.startTick);
            seq.addEvent(off, n.startTick + juce::jmax(1, n.lengthTicks));
        }
    }
    // ============================================================
    // MELODIC EXPORT (808 / Bass engines)
    // ============================================================
    else
    {
        const auto mp = proc.getMelodicPattern();

        for (const auto& n : mp)
        {
            const int pitch = juce::jlimit(0, 127, n.pitch);
            const int ch = juce::jlimit(1, 16, n.channel);
            const juce::uint8 vel = (juce::uint8)juce::jlimit(1, 127, (int)n.velocity);

            const juce::MidiMessage on = juce::MidiMessage::noteOn(ch, pitch, vel);
            const juce::MidiMessage off = juce::MidiMessage::noteOff(ch, pitch);

            seq.addEvent(on, n.startTick);
            seq.addEvent(off, n.startTick + juce::jmax(1, n.lengthTicks));
        }
    }

    // Finalize MIDI
    seq.updateMatchedPairs();
    seq.sort();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ppq);
    midiFile.addTrack(seq);

    juce::FileOutputStream out(outFile);
    if (!out.openedOk())
    {
        DBG("writeTempMidiFile: failed to open temp file for writing: " + outFile.getFullPathName());
        return {};
    }

    midiFile.writeTo(out);
    out.flush();

    DBG("writeTempMidiFile: wrote to " + outFile.getFullPathName());
    return outFile;
}



void BoomAudioProcessorEditor::startExternalMidiDrag(bool multiTrackStems)
{
    juce::File tmp;

    // Determine current engine (use your safe getter if available)
    // If proc.getEngineSafe() exists prefer it; otherwise read APVTS as fallback.
    boom::Engine eng = boom::Engine::Drums;
    if (/* proc.getEngineSafe exists */ true)
    {
        // use getEngineSafe when present - change this line if your code uses a different getter
        eng = proc.getEngineSafe();
    }
    else
    {
        eng = static_cast<boom::Engine>(static_cast<int>(proc.apvts.getRawParameterValue("engine")->load()));
    }

    if (eng == boom::Engine::Drums)
    {
        const uint32_t rowMask = drumGrid.getRowSelectionMask();

        // If nothing is selected and user wants stems, export ALL rows as stems.
        uint32_t exportMask = rowMask;
        if (exportMask == 0u && multiTrackStems)
        {
            const int numRows = proc.getDrumRows().size();
            exportMask = (numRows >= 32) ? 0xFFFFFFFFu : ((1u << (uint32_t)numRows) - 1u);
        }

        if (exportMask != 0u)
        {
            if (multiTrackStems)
            {
                // STEMS (one MIDI track per row) — requires your multi-track builder function
                tmp = buildTempMidiForSelectedRowsMultiTrack(exportMask, "BOOM_RowStems");
                TransientMsgComponent::launchCentered(this, "DRAG: STEMS (SHIFT)");
                DBG("startExternalMidiDrag: drums - STEMS rowMask=0x" << juce::String::toHexString(exportMask));
            }
            else
            {
                // SINGLE CLIP (selected rows merged into one MIDI track)
                tmp = buildTempMidiForSelectedRows(exportMask, "BOOM_SelectedRows");
                TransientMsgComponent::launchCentered(this, "DRAG: CLIP");
                DBG("startExternalMidiDrag: drums - CLIP rowMask=0x" << juce::String::toHexString(exportMask));
            }
        }
        else
        {
            // No selection, not stems => your normal full-pattern export
            tmp = writeTempMidiFile();
            TransientMsgComponent::launchCentered(this, "DRAG: CLIP");
            DBG("startExternalMidiDrag: drums - no row selection, using writeTempMidiFile()");
        }
    }
    else
    {
        // Non-drums engines: preserve existing behaviour by using the generic writer
        tmp = writeTempMidiFile();
        DBG("startExternalMidiDrag: non-drums - using writeTempMidiFile()");
    }

    if (!tmp.existsAsFile())
    {
        DBG("startExternalMidiDrag: temp MIDI not found: " << tmp.getFullPathName());
        return;
    }

    juce::StringArray files;
    files.add(tmp.getFullPathName());

    // Try to perform the OS drag directly (editor inherits DragAndDropContainer)
    DBG("startExternalMidiDrag: attempting performExternalDragDropOfFiles for " << tmp.getFullPathName());
    performExternalDragDropOfFiles(files, false);
}


juce::File AIToolsWindow::buildTempMidi(const juce::String& base) const
{
    const int ppq = 96;
    const int baseMidi = 60; // C3

    juce::MidiMessageSequence seq;

    for (const auto& n : proc.getDrumPattern())
    {
        const int pitch = juce::jlimit(0, 127, baseMidi + n.row);
        const int channel = 10;

        const juce::uint8 vel = (juce::uint8)juce::jlimit(1, 127, (int)n.velocity);

        seq.addEvent(juce::MidiMessage::noteOn(channel, pitch, vel), n.startTick);
        seq.addEvent(juce::MidiMessage::noteOff(channel, pitch), n.startTick + juce::jmax(1, n.lengthTicks));
    }

    seq.updateMatchedPairs();
    seq.sort();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ppq);
    midiFile.addTrack(seq);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile(base + ".mid");

    if (tmp.existsAsFile())
        tmp.deleteFile();

    juce::FileOutputStream out(tmp);
    if (!out.openedOk())
        return {};

    midiFile.writeTo(out);
    out.flush();
    return tmp;
}



AIToolsWindow::AIToolsWindow(BoomAudioProcessor& p, std::function<void()> onClose)
    : proc(p), onCloseFn(std::move(onClose))
{
    // Use the Boom look-and-feel for the AI tools window as well so its combos match.
    aiToolsLnf.reset(new BoomLookAndFeel());
    setSize(800, 950);


        // ---- Non-interactive labels from your artwork (no mouse) ----
    auto addLbl = [this](juce::ImageComponent& ic, const juce::String& png)
    {
        ic.setImage(loadSkin(png));
        ic.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(ic);
    };
    addLbl(titleLbl, "aiToolsLbl.png");
    addLbl(selectAToolLbl, "selectAToolLbl.png");
    addLbl(rhythmimickLbl, "rhythmimickLbl.png");
    addLbl(lockToBpmLbl, "lockToBpmLbl.png");
    addLbl(bpmLbl, "bpmLbl.png");
    addLbl(styleBlenderLbl, "styleBlenderLbl.png");
    addLbl(beatboxLbl, "beatboxLbl.png");

    addLbl(recordUpTo60LblTop, "recordUpTo60SecLbl.png");
    addLbl(recordUpTo60LblBottom, "recordUpTo60SecLbl.png");
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 1000);
    addAndMakeVisible(bpmLockChk);
    bpmLockChk.setClickingTogglesState(true);
    // BPM slider UI

    // If you already have a timer in the editor, great—otherwise start one:
    startTimerHz(8); // update 8x per second

    // create audio input manager used for recording WAVs from the device
    // create audio input manager used for recording WAVs from the device
    audioInputManager = std::make_unique<AudioInputManager>();

    // Forward buffers to the processor capture implementation so AI functions can use them.
    // This ensures the editor-managed device actually populates the processor capture buffer.
    audioInputManager->onBufferReady = [this](const juce::AudioBuffer<float>& buf, double sampleRate)
        {
            proc.appendCaptureFrom(buf);
        };

    // choose a reasonable callback block size for recorder deliveries (matches deliverBuffer usage)
    audioInputManager->setCallbackBlockSize(512);
    // start the device manager callbacks (will quietly return if already started)
    audioInputManager->start();

    devicePanel = std::make_unique<DevicePanel>();
    addChildComponent(*devicePanel); // added as background (not intercepting mouse)
    devicePanel->toBack();


    // Add device selector UI (if available) so user can choose input device / loopback
    if (audioInputManager)
    {
        if (auto* sel = audioInputManager->getDeviceSelectorComponent())
        {
            addAndMakeVisible(sel);
            sel->setInterceptsMouseClicks(true, true);

            // Style internal combo-boxes / labels inside the selector to match the app palette.
            // Recursively traverse children to find combo boxes and labels.
            std::function<void(juce::Component*)> styleChildren;
            styleChildren = [&](juce::Component* comp)
                {
                    for (int i = 0; i < comp->getNumChildComponents(); ++i)
                    {
                        auto* ch = comp->getChildComponent(i);

                        if (auto* cb = dynamic_cast<juce::ComboBox*>(ch))
                        {
                            // ensure the selector's child ComboBoxes use our window LNF and colours
                            cb->setLookAndFeel(aiToolsLnf.get());
                            cb->setColour(juce::ComboBox::backgroundColourId, kSlate.darker(0.15f));
                            cb->setColour(juce::ComboBox::textColourId, kOffWhite);
                            cb->setColour(juce::ComboBox::outlineColourId, kPurple.darker(0.25f));
                            cb->setColour(juce::ComboBox::arrowColourId, kOffWhite);
                            // don't call setFont() on ComboBox - use LookAndFeel or style its child Label if necessary
                        }

                        if (auto* lab = dynamic_cast<juce::Label*>(ch))
                        {
                            lab->setColour(juce::Label::textColourId, juce::Colours::white);
                            lab->setFont(juce::Font(11.0f, juce::Font::bold));
                        }

                        // Recurse
                        styleChildren(ch);
                    }
                };

            styleChildren(sel);
        }
    }

    addAndMakeVisible(deviceNameLbl);
    deviceNameLbl.setColour(juce::Label::textColourId, juce::Colours::white);
    deviceNameLbl.setFont(juce::Font(14.0f, juce::Font::bold));
    deviceNameLbl.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(deviceStatusLbl);
    deviceStatusLbl.setColour(juce::Label::textColourId, kLime.darker());
    deviceStatusLbl.setFont(juce::Font(11.0f));
    deviceStatusLbl.setJustificationType(juce::Justification::centredRight);

    // Add "Open Device" button that attempts to open currently-selected device
    addAndMakeVisible(openDeviceBtn);
    openDeviceBtn.setButtonText("Open Device");
    openDeviceBtn.onClick = [this]()
        {
            if (!audioInputManager) return;
            const juce::String name = audioInputManager->getCurrentInputDeviceName();
            if (name.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "No device",
                    "No input device is currently selected in the selector.");
                return;
            }
            audioInputManager->openInputDeviceByName(name);
        };


    // IMPORTANT: we use your existing helper so there are NO new functions introduced.
// This expects files: checkBoxOffBtn.png and checkBoxOnBtn.png in BinaryData.
    boomui::setToggleImages(bpmLockChk, "checkBoxOffBtn", "checkBoxOnBtn");

    // ---- Toggles (right side) ----
    addAndMakeVisible(toggleRhythm);
    addAndMakeVisible(toggleBlend);
    addAndMakeVisible(toggleBeat);

    auto addToGroup = [](juce::Array<juce::Component*>& dst,
        std::initializer_list<juce::Component*> items)
    {
        for (auto* c : items) dst.add(c);
    };

    // --- group wiring (add AFTER you’ve created the controls) ---



    // Ensure only one tool is ON at a time
    toggleRhythm.onClick = [this] { makeToolActive(Tool::Rhythmimick); };
    toggleBlend.onClick = [this] { makeToolActive(Tool::StyleBlender); };
    toggleBeat.onClick = [this] { makeToolActive(Tool::Beatbox);     };

    // Default active tool (pick one; Rhythmimick here)
    makeToolActive(Tool::Rhythmimick);


    rhythmimickLbl.setTooltip("Record up to 60sec with your soundcard and have Rhythmimick make a MIDI pattern from what it hears. Works with all engines.");
    styleBlenderLbl.setTooltip("Choose two styles from the combination boxes above and have StyleBlender generate unique MIDI patterns from the blend. Works with all engines.");
    beatboxLbl.setTooltip("Record up to 60sec with your microphone and let Beatbox generate MIDI patterns from what it hears! Works with all engines.");

    // IMPORTANT: we use your existing helper so there are NO new functions introduced.
    // This expects files: checkBoxOffBtn.png and checkBoxOnBtn.png in BinaryData.
    boomui::setToggleImages(bpmLockChk, "checkBoxOffBtn", "checkBoxOnBtn");

    // ---- Rhythmimick row (recording block) ----
    addAndMakeVisible(btnRec1);   setButtonImages(btnRec1, "recordBtn");
    addAndMakeVisible(btnPlay1);   setButtonImages(btnPlay1, "playBtn");
    addAndMakeVisible(btnStop1);  setButtonImages(btnStop1, "stopBtn");
    addAndMakeVisible(btnGen1);   setButtonImages(btnGen1, "generateBtn");
    addAndMakeVisible(btnSave1);  setButtonImages(btnSave1, "saveMidiBtn");
    addAndMakeVisible(btnDrag1);  setButtonImages(btnDrag1, "dragBtn");



    btnGen1.onClick = [this]
        {
            // Rhythmimick: stop loopback capture and analyze into drums
            int bars = 4;
            if (auto* p = dynamic_cast<juce::AudioParameterInt*>(proc.apvts.getParameter("bars")))
                bars = p->get();

            int bpm = 120;
            if (auto* bp = proc.apvts.getRawParameterValue("bpm"))
                bpm = juce::roundToInt(bp->load());

            proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Loopback);
            proc.aiAnalyzeCapturedToDrums(bars, bpm);

            TransientMsgComponent::launchCentered(this, "MIDI GENERATED!", 1400);
        };

    btnSave1.onClick = [this]
        {
            auto tmp = writeAiCaptureToWav(proc, "BOOM_Rhythmimick_Capture", BoomAudioProcessor::CaptureSource::Loopback);
            if (!tmp.existsAsFile()) return;

            auto* fc = new juce::FileChooser("Save recorded capture as...", tmp, "*");
            fc->launchAsync(juce::FileBrowserComponent::saveMode,
                [tmp, fc](const juce::FileChooser& chooser)
                {
                    const juce::File dest = chooser.getResult();
                    if (dest.getFullPathName().isNotEmpty())
                    {
                        // Ensure parent exists
                        dest.getParentDirectory().createDirectory();
                        tmp.copyFileTo(dest);
                    }
                    delete fc;
                });
        };

    btnGen1.setTooltip("Generates MIDI patterns from audio you have recorded from your soundcard, depending on which engine you have selected at the top of the main window!");
    btnGen2.setTooltip("Designed to work almost as if you have a collaborator in the room. Upon pressing GENERATE, SlapSmith then uses AI to alter your input on the mini-grid above into a more complete pattern. Almost like the main window, except with more smarts! ");
    btnSave1.setTooltip("Click to save MIDI to a folder on your device of your choice!");
    btnSave2.setTooltip("Click to save MIDI to a folder on your device of your choice!");
    btnDrag1.setTooltip("Allows you to drag and drop the MIDI you have generated into your DAW!");
    btnDrag2.setTooltip("Allows you to drag and drop the MIDI you have generated into your DAW!");

    auto addArrow = [this](juce::ImageComponent& ic)
    {
        addAndMakeVisible(ic);
        ic.setInterceptsMouseClicks(false, false);
        ic.setImage(loadSkin("arrowLbl.png"));
        ic.setImagePlacement(juce::RectanglePlacement::centred);
    };
    addArrow(arrowRhythm);
    addArrow(arrowSlap);
    addArrow(arrowBlend);
    addArrow(arrowBeat);

    // ---- Style Blender blend slider (0..100%, no textbox)
    addAndMakeVisible(blendAB);
    blendAB.setRange(0.0, 100.0, 1.0);
    blendAB.setSliderStyle(juce::Slider::LinearHorizontal);
    blendAB.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    blendAB.setValue(50.0);
    addAndMakeVisible(blendPctLbl);
    blendPctLbl.setJustificationType(juce::Justification::centredRight);
    blendPctLbl.setText("50%", juce::dontSendNotification);
    blendAB.onValueChange = [this]() {
        const int pct = (int)blendAB.getValue();
        blendPctLbl.setText(juce::String(pct) + "%", juce::dontSendNotification);
        repaint();
        };
    blendABAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "styleBlend", blendAB);

    blendAB.setTooltip("Blends two styles together to make one unique MIDI pattern!");


    addAndMakeVisible(rhythmSeek);
    // keep existing LNF use in your project
    rhythmSeek.setSliderStyle(juce::Slider::LinearHorizontal);
    rhythmSeek.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    // small non-zero step to avoid any pathological behaviour
    rhythmSeek.setRange(0.0, 1.0, 0.0001);
    rhythmSeek.onDragStart = [this] { if (proc.aiIsPreviewing()) proc.aiPreviewStop(); };
    rhythmSeek.onValueChange = [this]
        {
            // If there's a capture, convert normalized slider value to seconds and seek.
            if (!proc.aiHasCapture()) return;
            const double capLen = proc.getCaptureLengthSeconds();
            if (capLen <= 0.0) return;
            const double sec = rhythmSeek.getValue() * capLen;
            proc.aiSeekToSeconds(sec);
        };

    // Start disabled until capture exists
    rhythmSeek.setEnabled(false);

    addAndMakeVisible(styleABox);
    styleABox.addItemList(boom::drums::styleNames(), 1);
    if (styleABox.getNumItems() > 0)
        styleABox.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(styleBBox);
    styleBBox.addItemList(boom::drums::styleNames(), 1);
    if (styleBBox.getNumItems() > 1)
        styleBBox.setSelectedId(2, juce::dontSendNotification);
    else if (styleBBox.getNumItems() > 0)
        styleBBox.setSelectedId(1, juce::dontSendNotification);

    // Play: toggle preview start/stop and update visual
    btnPlay1.onClick = [this]
        {
            DBG("AIToolsWindow: btnPlay1 clicked (Rhythmimick)");
            if (!proc.aiHasCapture()) { DBG("AIToolsWindow: no capture available"); return; }
            if (proc.aiIsPreviewing())
            {
                proc.aiPreviewStop();
                DBG("AIToolsWindow: requested aiPreviewStop() from btnPlay1");
            }
            else
            {
                proc.aiPreviewStart();
                DBG("AIToolsWindow: requested aiPreviewStart() from btnPlay1");
            }
        };

    // Replace the existing btnPlay4.onClick lambda with this


    // Record: toggle capture state and update UI image
    btnRec1.onClick = [this]()
        {
            // Log current device + ensure manager started
            if (audioInputManager)
            {
                audioInputManager->start(); // idempotent
                const juce::String devName = audioInputManager->getCurrentInputDeviceName();

                // Quick diagnostic: try to start a temporary WAV recorder to see if onBufferReady/deliverBuffer is getting data.
                auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("AITestCapture", ".wav");
                const bool recOk = audioInputManager->startRecordingToFile(tmp);
            }

            if (!proc.ai_isRhRecording())
            {
                // Start processor capture (Loopback)
                proc.aiStartCapture(BoomAudioProcessor::CaptureSource::Loopback);
                setButtonImages(btnRec1, "recordBtn_down");
                startTimerHz(8);
            }
            else
            {

                proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Loopback);
                setButtonImages(btnRec1, "recordBtn");
            }
        };

    // Stop: stop preview and stop capture; reset UI
    btnStop1.onClick = [this]()
        {
            DBG("UI: btnStop1 clicked. Stopping preview + capture.");
            proc.aiPreviewStop();
            proc.aiStopCapture();
               g_previewOwner.store(Preview_None);
        };


        addAndMakeVisible(beatboxSeek);
    beatboxSeek.setSliderStyle(juce::Slider::LinearHorizontal);
    beatboxSeek.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    // small non-zero step to avoid any pathological behaviour
    beatboxSeek.setRange(0.0, 1.0, 0.0001);
    beatboxSeek.onDragStart = [this] { if (proc.aiIsPreviewing()) proc.aiPreviewStop(); };
    beatboxSeek.onValueChange = [this]
        {
            if (!proc.aiHasCapture()) return;
            const double capLen = proc.getCaptureLengthSeconds();
            if (capLen <= 0.0) return;
            const double sec = beatboxSeek.getValue() * capLen;
            proc.aiSeekToSeconds(sec);
        };

    // Initially disabled
    beatboxSeek.setEnabled(false);

    // Beatbox play / record / stop mirrors Rhythmimick logic but uses Microphone source
    btnPlay4.onClick = [this]
        {
            DBG("AIToolsWindow: btnPlay4 clicked (Beatbox)");
            if (!proc.aiHasCapture()) { DBG("AIToolsWindow: no capture available"); return; }
            if (proc.aiIsPreviewing())
            {
                proc.aiPreviewStop();
                DBG("AIToolsWindow: requested aiPreviewStop() from btnPlay4");
            }
            else
            {
                proc.aiPreviewStart();
                DBG("AIToolsWindow: requested aiPreviewStart() from btnPlay4");
            }
        };


    btnRec4.onClick = [this]()
        {
            if (!proc.ai_isBxRecording())
            {
                proc.aiStartCapture(BoomAudioProcessor::CaptureSource::Microphone);
                startTimerHz(8);
                setButtonImages(btnRec4, "recordBtn_down");
            }
            else
            {
                proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Microphone);
                setButtonImages(btnRec4, "recordBtn");
            }
        };

    btnStop4.onClick = [this]()
        {
            DBG("UI: btnStop4 clicked. Stopping preview + capture.");
            proc.aiPreviewStop();
            proc.aiStopCapture();
            g_previewOwner.store(Preview_None);
        };

    addAndMakeVisible(btnGen3);   setButtonImages(btnGen3, "generateBtn");
    addAndMakeVisible(btnSave3);  setButtonImages(btnSave3, "saveMidiBtn");
    addAndMakeVisible(btnDrag3);  setButtonImages(btnDrag3, "dragBtn");

    // ---- Beatbox row (recording block) ----
    addAndMakeVisible(btnRec4);   setButtonImages(btnRec4, "recordBtn");
    addAndMakeVisible(btnPlay4);  setButtonImages(btnPlay4, "playBtn");
    addAndMakeVisible(btnStop4);  setButtonImages(btnStop4, "stopBtn");
    addAndMakeVisible(btnGen4);   setButtonImages(btnGen4, "generateBtn");
    addAndMakeVisible(btnSave4);  setButtonImages(btnSave4, "saveMidiBtn");
    addAndMakeVisible(btnDrag4);  setButtonImages(btnDrag4, "dragBtn");

    recImgNormal_ = loadSkin("recordBtn.png");
    recImgHover_ = loadSkin("recordBtnHover.png");
    recImgDown_ = loadSkin("recordBtnDown.png");


    auto hookupRow = [this](juce::ImageButton& save, juce::ImageButton& drag, const juce::String& baseFile)
    {
                save.onClick = [this]
                {
                    juce::File src = buildTempMidi("BOOM_Slapsmith.mid");
                    launchSaveMidiChooserAsync("Save MIDI...", src, "BOOM_MIDI.mid");
                };

        drag.onClick = [this, baseFile]
        {
            juce::File f = buildTempMidi(baseFile);
            if (!f.existsAsFile()) return;
            if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                juce::StringArray files; files.add(f.getFullPathName());
                dnd->performExternalDragDropOfFiles(files, true);
            }
        };
    };

    hookupRow(btnSave1, btnDrag1, "BOOM_Rhythmimick");
    // --- Slapsmith drag choices overlay (two buttons that pop up over the main drag) ---

    // Override the default drag handler: when user clicks Slapsmith's drag, show the two choices.
    auto writeSplitTracksAndDrag = [this](const BoomAudioProcessor::Pattern& pat, const juce::String& tempName)
        {
            const int ppq = 96;
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote(ppq);

            // one sequence per drum row
            juce::HashMap<int, std::shared_ptr<juce::MidiMessageSequence>> perRow;

            auto noteForRow = [this](int row) -> int
                {
                    const auto& names = proc.getDrumRows();
                    if ((unsigned)row < (unsigned)names.size())
                    {
                        auto name = names[(int)row].toLowerCase();
                        if (name.contains("kick"))  return 36;
                        if (name.contains("snare")) return 38;
                        if (name.contains("clap"))  return 39;
                        if (name.contains("rim"))   return 37;
                        if (name.contains("open") && name.contains("hat"))   return 46;
                        if (name.contains("closed") && name.contains("hat")) return 42;
                        if (name.contains("hat"))   return 42;
                        if (name.contains("low") && name.contains("tom"))    return 45;
                        if (name.contains("mid") && name.contains("tom"))    return 47;
                        if (name.contains("high") && name.contains("tom"))   return 50;
                        if (name.contains("perc"))  return 48;
                        if (name.contains("crash")) return 49;
                        if (name.contains("ride"))  return 51;
                    }
        switch (row) { case 0: return 36; case 1: return 38; case 2: return 42; case 3: return 46; default: return 45 + (row % 5); }
                };

            for (const auto& n : pat)
            {
                const int row = n.row;
                const int pitch = noteForRow(row);
                const int onPPQ = (n.startTick * ppq) / 24;
                const int len = juce::jmax(1, (n.lengthTicks * ppq) / 24);
                const int offPPQ = onPPQ + len;
                const int vel = juce::jlimit(1, 127, n.velocity);

                if (!perRow.contains(row))
                    perRow.set(row, std::make_unique<juce::MidiMessageSequence>());

                auto& seq = *perRow.getReference(row);
                seq.addEvent(juce::MidiMessage::noteOn(10, pitch, (juce::uint8)vel), onPPQ); // ch 10 for drums
                seq.addEvent(juce::MidiMessage::noteOff(10, pitch), offPPQ);
            }

            // add tracks in row order
// Collect keys with JUCE HashMap iterator, sort, then add tracks
            juce::Array<int> rows;
            for (juce::HashMap<int, std::shared_ptr<juce::MidiMessageSequence>>::Iterator it(perRow); it.next(); )
                rows.add(it.getKey());

            rows.sort();

            for (int i = 0; i < rows.size(); ++i)
                mf.addTrack(*perRow.getReference(rows.getUnchecked(i)));


            auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(tempName + ".mid");
            tmp.deleteFile();
            { juce::FileOutputStream os(tmp); mf.writeTo(os); }

            performFileDrag(tmp);

            dragChoices2Visible = false;
            dragSelected2.setVisible(false);
            dragAll2.setVisible(false);
        };


    hookupRow(btnSave3, btnDrag3, "BOOM_StyleBlender");
    hookupRow(btnSave4, btnDrag4, "BOOM_Beatbox");

    blendAB.onValueChange = [this]()
        {
            // live UI feedback (you can use this to update a small label if you add one)
            // For now just repaint so any visual indicators refresh
            repaint();
        };

    // --- StyleBlender Generate (replace existing btnGen3.onClick with this) ---
    btnGen3.onClick = [this]
        {
            // Read style names from the two style combo boxes
            const juce::String styleA = styleABox.getText().trim();
            const juce::String styleB = styleBBox.getText().trim();

            // Validate against style DB (safe fallback)
            auto styles = boom::drums::styleNames();
            juce::String a = styleA;
            juce::String b = styleB;
            if (!styles.contains(a) && styles.size() > 0) a = styles[0];
            if (!styles.contains(b) && styles.size() > 1) b = styles[1];
            else if (!styles.contains(b) && styles.size() > 0) b = styles[0];

            // BARS: read number of bars (fallback 4)
            int bars = 4;
            if (auto* barsParam = proc.apvts.getParameter("bars"))
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(barsParam))
                    bars = choice->getCurrentChoiceName().getIntValue();

            // Compute weights from blendAB (0..100 -> 0.0..1.0)
            const float wA = juce::jlimit(0.0f, 1.0f, (float)blendAB.getValue() / 100.0f);
            const float wB = 1.0f - wA;

            // Call the processor blend routine (you already have this API in the project)
            // Signature used: aiStyleBlendDrums(juce::String a, juce::String b, int bars, float wA, float wB)
            proc.aiStyleBlendDrums(a, b, bars, wA, wB);

            // quick sanity-check / debug: log produced pattern size and force editor refresh
            DBG("AI StyleBlend requested: " << a << " + " << b << " -> weights " << wA << "," << wB
                << " ; proc.getDrumPattern().size() = " << (int)proc.getDrumPattern().size());

            auto tmp = buildTempMidi("AI_StyleBlend.mid");
            lastGeneratedTempFile = tmp;

            if (auto* ed = findParentComponentOfClass<BoomAudioProcessorEditor>())
            {
                ed->refreshFromAI();
            }
            else
            {
                repaint();
            }


            // Redraw the AI window (meters / seek bars, etc.)
            repaint();
        };


    btnGen4.onClick = [this]
        {
            // Beatbox: stop mic capture and analyze into drums
            int bars = 4;
            if (auto* p = dynamic_cast<juce::AudioParameterInt*>(proc.apvts.getParameter("bars")))
                bars = p->get();

            int bpm = 120;
            if (auto* bp = proc.apvts.getRawParameterValue("bpm"))
                bpm = juce::roundToInt(bp->load());

            proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Microphone);
            proc.aiAnalyzeCapturedToDrums(bars, bpm);

            TransientMsgComponent::launchCentered(this, "MIDI GENERATED!", 1400);
        };

    btnSave4.onClick = [this]
        {
            auto tmp = writeAiCaptureToWav(proc, "BOOM_Beatbox_Capture", BoomAudioProcessor::CaptureSource::Microphone);
            if (!tmp.existsAsFile()) return;

            auto* fc = new juce::FileChooser("Save recorded capture as...", tmp, "*");
            fc->launchAsync(juce::FileBrowserComponent::saveMode,
                [tmp, fc](const juce::FileChooser& chooser)
                {
                    const juce::File dest = chooser.getResult();
                    if (dest.getFullPathName().isNotEmpty())
                    {
                        dest.getParentDirectory().createDirectory();
                        tmp.copyFileTo(dest);
                    }
                    delete fc;
                });
        };

    btnGen3.setTooltip("Generates MIDI patterns based on the choices you have made in the style dropboxes!");
    btnGen4.setTooltip("Generates MIDI patterns from audio you have recorded with your microphone according to the engine you have selected in the main window at the top!");
    btnSave3.setTooltip("Click to save MIDI to a folder on your device of your choice!");
    btnSave4.setTooltip("Click to save MIDI to a folder on your device of your choice!");
    btnDrag3.setTooltip("Allows you to drag and drop the MIDI you have generated into your DAW!");
    btnDrag4.setTooltip("Allows you to drag and drop the MIDI you have generated into your DAW!");

    // ---- Home ----
    addAndMakeVisible(btnHome);   setButtonImages(btnHome, "homeBtn");
    btnHome.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };


    // Single, message-thread-safe callback so the processor can notify the UI when the pattern changes.

    // Ensure the mini-grid accepts mouse input and is z-ordered correctly.

    startTimerHz(20); // update 8x per second
    // ---- Shared actions (Save/Drag) use real MIDI from processor ----

}

void AIToolsWindow::updateSeekFromProcessor()
{
    if (!proc.aiHasCapture())
    {
        if (!rhythmSeek.isMouseButtonDown())
            rhythmSeek.setValue(0.0, juce::dontSendNotification);
        if (!beatboxSeek.isMouseButtonDown())
            beatboxSeek.setValue(0.0, juce::dontSendNotification);
        return;
    }

    const double len = juce::jmax(0.000001, proc.getCaptureLengthSeconds());
    const double pos = proc.getCapturePositionSeconds();
    const double norm = juce::jlimit(0.0, 1.0, pos / len);

    // Update only the seekbar(s) that are not being actively dragged.
    // Also prefer updating only the active tool's seekbar so the other doesn't jump.
    switch (activeTool_)
    {
    case Tool::Rhythmimick:
        if (!rhythmSeek.isMouseButtonDown())
            rhythmSeek.setValue(norm, juce::dontSendNotification);
        // keep beatbox visually in sync only when nobody is interacting
        if (!beatboxSeek.isMouseButtonDown() && !rhythmSeek.isMouseButtonDown())
            beatboxSeek.setValue(norm, juce::dontSendNotification);
        break;

    case Tool::Beatbox:
        if (!beatboxSeek.isMouseButtonDown())
            beatboxSeek.setValue(norm, juce::dontSendNotification);
        if (!rhythmSeek.isMouseButtonDown() && !beatboxSeek.isMouseButtonDown())
            rhythmSeek.setValue(norm, juce::dontSendNotification);
        break;

    default:
        // Style blender / neutral: update both unless user is dragging one
        if (!rhythmSeek.isMouseButtonDown())
            rhythmSeek.setValue(norm, juce::dontSendNotification);
        if (!beatboxSeek.isMouseButtonDown())
            beatboxSeek.setValue(norm, juce::dontSendNotification);
        break;
    }
}

void AIToolsWindow::uncheckAllToggles()
{
    toggleRhythm.setToggleState(false, juce::dontSendNotification);
    toggleSlap.setToggleState(false, juce::dontSendNotification);
    toggleBlend.setToggleState(false, juce::dontSendNotification);
    toggleBeat.setToggleState(false, juce::dontSendNotification);
}

void AIToolsWindow::setGroupEnabled(const juce::Array<juce::Component*>& group, bool enabled, float dimAlpha)
{
    const float a = enabled ? 1.0f : dimAlpha;
    for (auto* c : group)
    {
        if (c == nullptr) continue;

        // Default behaviour for other components
        c->setEnabled(enabled);
        c->setAlpha(a);
    }
}

void AIToolsWindow::setActiveTool(Tool t)
{
    activeTool_ = t;

    const bool r = (t == Tool::Rhythmimick);
    const bool b = (t == Tool::Beatbox);
    const bool y = (t == Tool::StyleBlender);

    // Dim when disabled (alpha 0.35) + block input via setEnabled(false)
    setGroupEnabled(rhythmimickGroup, r);
    setGroupEnabled(beatboxGroup, b);
    setGroupEnabled(styleBlendGroup, y);

    // If you have any arrowLbl.png between toggle and labels, include them
    // in the corresponding groups above so they dim too.
}

void AIToolsWindow::makeToolActive(Tool t)
{
    activeTool_ = t;

    auto setActive = [](juce::ImageButton& b, bool on) {
        b.setToggleState(on, juce::dontSendNotification);
        if (on)
        {
            setButtonImages(b, "toggleBtnOn");
        }
        else
        {
            auto offImage = loadSkin("toggleBtnOff.png");
            b.setImages(false, true, true,
                offImage, 1.0f, juce::Colour(),
                offImage, 1.0f, juce::Colour(),
                offImage, 1.0f, juce::Colour());
        }
        };

    setActive(toggleRhythm, t == Tool::Rhythmimick);
    setActive(toggleBlend, t == Tool::StyleBlender);
    setActive(toggleBeat, t == Tool::Beatbox);

    // Optional: enable/disable row controls based on active tool
    const bool r = (t == Tool::Rhythmimick);
    const bool b = (t == Tool::Beatbox);
    const bool y = (t == Tool::StyleBlender);

    btnRec1.setEnabled(r);   btnPlay1.setEnabled(r); btnStop1.setEnabled(r);   btnGen1.setEnabled(r);   btnSave1.setEnabled(r);   btnDrag1.setEnabled(r);
    btnGen4.setEnabled(b);   btnRec4.setEnabled(b);    btnPlay4.setEnabled(b); btnStop4.setEnabled(b);  btnSave4.setEnabled(b);   btnDrag4.setEnabled(b);
    btnGen3.setEnabled(y);   btnSave3.setEnabled(y);   btnDrag3.setEnabled(y);  blendAB.setEnabled(y); styleABox.setEnabled(y); styleBBox.setEnabled(y);

    repaint();
}

void AIToolsWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());

    static float rhPeak = 0.0f;
    static float bxPeak = 0.0f;

    // basic peak smoothing
    rhPeak = juce::jmax(rhPeak * 0.92f, rhL_);
    bxPeak = juce::jmax(bxPeak * 0.92f, bxL_);

    drawStyledMeter(g, { rightPanelX + 36, rhY + 52, 120, 12 }, rhL_, rhPeak);
    drawStyledMeter(g, { rightPanelX + 36, rhY + 72, 120, 12 }, rhR_, rhPeak);
    drawStyledMeter(g, { rightPanelX + 36, bxY + 800, 120, 12 }, bxL_, bxPeak);
    drawStyledMeter(g, { rightPanelX + 36, bxY + 108, 120, 12 }, bxR_, bxPeak);

}

void AIToolsWindow::resized()
{
    const float W = 800.f, H = 950.f; // Increased window size
    auto r = getLocalBounds();
    auto sx = r.getWidth() / W, sy = r.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };

    // --- Device selector (show the AudioDeviceSelectorComponent provided by AudioInputManager)
    if (audioInputManager)
    {
        if (auto* sel = audioInputManager->getDeviceSelectorComponent())
        {
            // Panel slightly larger than selector for padding
            auto panelRect = S(1, 56, 300, 85);
            devicePanel->setBounds(panelRect);

            // selector inside panel (slightly inset)
            sel->setBounds(S(1, 60, 300, 75));

            // device name left, status right above selector
            deviceNameLbl.setBounds(panelRect.removeFromLeft(panelRect.getWidth() - 140).withHeight(22).translated(4, -22));
            deviceStatusLbl.setBounds(S(18, 56, 40, 20));
        }
    }

    // --- Top section ---
    titleLbl.setBounds(S(300, 24, 200, 44));
    selectAToolLbl.setBounds(S(600, 10, 160, 60));
    lockToBpmLbl.setBounds(S(10, 15, 100, 20));
    bpmLbl.setBounds(S(10, 35, 100, 20));
    bpmLockChk.setBounds(S(115, 10, 24, 24));

    int y = 120; // Initial y position for the first tool
    const int vertical_spacing = 220; // Increased space between tools
    const int label_height = 60; // Larger labels

    // --- Rhythm-Mimmick ---
    rhythmimickLbl.setBounds(S(300, y, 220, label_height));
    toggleRhythm.setBounds(S(600, y, 120, 40));
    arrowRhythm.setBounds(S(530, y, 60, 40));
    recordUpTo60LblTop.setBounds(S(320, y + 65, 180, 20));
    btnRec1.setBounds(S(320, y + 85, 30, 30));
    btnPlay1.setBounds(S(360, y + 85, 30, 30));
    rhythmSeek.setBounds(S(400, y + 85, 140, 30));
    btnStop1.setBounds(S(550, y + 85, 30, 30));
    btnGen1.setBounds(S(320, y + 120, 90, 30));
    btnSave1.setBounds(S(420, y + 120, 90, 30));
    btnDrag1.setBounds(S(520, y + 120, 90, 30));
    y += vertical_spacing;

    // --- Style-Blender ---
    styleBlenderLbl.setBounds(S(300, y, 220, label_height));
    toggleBlend.setBounds(S(600, y, 120, 40));
    arrowBlend.setBounds(S(530, y, 60, 40));
    styleABox.setBounds(S(320, y + 65, 120, 28));
    styleBBox.setBounds(S(450, y + 65, 120, 28));
    blendAB.setBounds(S(320, y + 100, 250, 20));
    btnGen3.setBounds(S(320, y + 130, 90, 30));
    btnSave3.setBounds(S(420, y + 130, 90, 30));
    btnDrag3.setBounds(S(520, y + 130, 90, 30));
    y += vertical_spacing;

    // --- Beatbox ---
    beatboxLbl.setBounds(S(300, y, 220, label_height));
    toggleBeat.setBounds(S(600, y, 120, 40));
    arrowBeat.setBounds(S(530, y, 60, 40));
    recordUpTo60LblBottom.setBounds(S(320, y + 65, 180, 20));
    btnRec4.setBounds(S(320, y + 85, 30, 30));
    btnPlay4.setBounds(S(360, y + 85, 30, 30));
    beatboxSeek.setBounds(S(400, y + 85, 140, 30));
    btnStop4.setBounds(S(550, y + 85, 30, 30));
    btnGen4.setBounds(S(320, y + 120, 90, 30));
    btnSave4.setBounds(S(420, y + 120, 90, 30));
    btnDrag4.setBounds(S(520, y + 120, 90, 30));
    // --- Home button ---
    btnHome.setBounds(S(680, 850, 80, 80));
}

void AIToolsWindow::performFileDrag(const juce::File& f)
{
    if (!f.existsAsFile()) return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        juce::StringArray files; files.add(f.getFullPathName());
        dnd->performExternalDragDropOfFiles(files, true);
    }
}


// ================== Modals: ALL ImageButtons ==================
// --- FlippitWindow ---
FlippitWindow::FlippitWindow(BoomAudioProcessor& p,
    std::function<void()> onClose,
    std::function<void(int density)> onFlip,
    boom::Engine engine)
    : proc(p), onCloseFn(std::move(onClose)), onFlipFn(std::move(onFlip))
{
    setSize(700, 450);
    
    // Create tooltip window for this modal
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);

    const bool isDrums = (engine == boom::Engine::Drums);

    {
        const juce::String lblFile = isDrums ? "flippitDrumsLbl.png" : "flippitLbl.png";
        titleLbl.setImage(boomui::loadSkin(lblFile));
        titleLbl.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(titleLbl);
    }
    // Engine-specific button bases
    const juce::String flipArtBase = isDrums ? "flippitBtnDrums" : "flippitBtn808Bass";
    const juce::String saveArtBase = isDrums ? "saveMidiFlippitDrums" : "saveMidiFlippit808Bass";
    const juce::String dragArtBase = isDrums ? "dragBtnFlippitDrums" : "dragBtnFlippit808Bass";

    // Controls
    addAndMakeVisible(variation);
    variation.setRange(0, 100, 1);
    variation.setValue(35);
    variation.setSliderStyle(juce::Slider::LinearHorizontal);

    addAndMakeVisible(btnFlip);     setButtonImages(btnFlip, flipArtBase);
    addAndMakeVisible(btnSaveMidi); setButtonImages(btnSaveMidi, saveArtBase);
    addAndMakeVisible(btnDragMidi); setButtonImages(btnDragMidi, dragArtBase);
    addAndMakeVisible(btnHome);     setButtonImages(btnHome, "homeBtn");

    variation.setTooltip("Control how much you want FLIPPIT to variate the MIDI you have currently!");
    btnHome.setTooltip("Return to Main Window.");
    btnSaveMidi.setTooltip("Click to save MIDI to a folder on your device of your choice!");
    btnDragMidi.setTooltip("Allows you to drag and drop the MIDI you have generated into your DAW!");
    btnFlip.setTooltip("FLIPPIT! FLIPPIT GOOD!");

    btnHome.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    btnFlip.onClick = [this]
    {
        if (onFlipFn) onFlipFn((int)juce::jlimit(0.0, 100.0, variation.getValue()));
    };

    btnSaveMidi.onClick = [this]
        {
            juce::File src = buildTempMidi();
            launchSaveMidiChooserAsync("Save MIDI...", src, "BOOM_Rolls.mid");
        };


    btnDragMidi.onClick = [this]
    {
        juce::File f = buildTempMidi();
        performFileDrag(f);
    };
}

void FlippitWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void FlippitWindow::resized()
{
    auto r = getLocalBounds();

    // 700x450 reference
    const float W = 700.f, H = 450.f;
    auto sx = r.getWidth() / W, sy = r.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };

    // Center the title at the top using the image's natural size scaled by sx/sy.
    {
        auto img = titleLbl.getImage();
        const int iw = juce::roundToInt(img.getWidth() * sx);
        const int ih = juce::roundToInt(img.getHeight() * sy);
        const int x = (r.getWidth() - iw) / 2;
        const int y = juce::roundToInt(24 * sy);
        titleLbl.setBounds(x, y, iw, ih);
    }

    // Positions tuned to the provided flippit mockups
    btnFlip.setBounds(S(270, 150, 160, 72));
    variation.setBounds(S(40, 250, 620, 24));
    btnSaveMidi.setBounds(S(40, 350, 120, 40));
    btnDragMidi.setBounds(S(220, 340, 260, 50));
    btnHome.setBounds(S(600, 350, 60, 60));
}

// --- Flippit helpers ---
juce::File FlippitWindow::buildTempMidi() const
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    juce::MidiFile mf;

    if (engine == boom::Engine::Drums)
    {
        const int ppq = 96;
        const int baseMidi = 60; // C3

        juce::MidiMessageSequence seq;

        for (const auto& n : proc.getDrumPattern())
        {
            const int pitch = juce::jlimit(0, 127, baseMidi + n.row);
            const int channel = 10;

            const juce::uint8 vel = (juce::uint8)juce::jlimit(1, 127, (int)n.velocity);

            seq.addEvent(juce::MidiMessage::noteOn(channel, pitch, vel), n.startTick);
            seq.addEvent(juce::MidiMessage::noteOff(channel, pitch), n.startTick + juce::jmax(1, n.lengthTicks));
        }

        seq.updateMatchedPairs();
        seq.sort();

        mf.setTicksPerQuarterNote(ppq);
        mf.addTrack(seq);
    }

    else
    {
        boom::midi::MelodicPattern mp;
        for (const auto& n : proc.getMelodicPattern())
            mp.add({ n.pitch, n.startTick, n.lengthTicks, n.velocity, 1 });
        mf = boom::midi::buildMidiFromMelodic(mp, 96);
    }

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("BOOM_Flippit.mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}

void FlippitWindow::performFileDrag(const juce::File& f)
{
    if (!f.existsAsFile()) return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        juce::StringArray files; files.add(f.getFullPathName());
        dnd->performExternalDragDropOfFiles(files, true);
    }
}

// --- BumppitWindow ---
BumppitWindow::BumppitWindow(BoomAudioProcessor& p,
    std::function<void()> onClose,
    std::function<void()> onBump,
    boom::Engine engine)
    : proc(p), onCloseFn(std::move(onClose)), onBumpFn(std::move(onBump))
{
    setSize(700, 462);
    
    // Create tooltip window for this modal
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);

    // Background per engine
    const bool isDrums = (engine == boom::Engine::Drums);
    // Top label depending on engine
    {
        const juce::String lblFile = isDrums ? "bumppitDrumsLbl.png" : "bumppitLbl.png";
        titleLbl.setImage(boomui::loadSkin(lblFile));
        titleLbl.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(titleLbl);
    }

    // Engine-specific Bumppit button art
    const juce::String bumpArtBase = isDrums ? "bumppitBtnDrums" : "bumppitBtn808Bass";

    addAndMakeVisible(btnBump);  setButtonImages(btnBump, bumpArtBase);
    addAndMakeVisible(btnHome);  setButtonImages(btnHome, "homeBtn");
    btnBump.setTooltip("For DRUMS, BUMP each row in the drum grid's MIDI pattern DOWN *1* row. Bottom row moves up to the top row. For 808/BASS, keep or BUMP *discard* settings!");
    btnHome.setTooltip("Return to Main Window.");

    btnBump.onClick = [this]()
        {
            if (showMelodicOptions)
            {
                // Melodic path (808 / Bass): read UI choices and call melodic transpose on processor.
                int targetKeyIndex = 0;
                if (keyBox.getSelectedId() > 0)
                    targetKeyIndex = keyBox.getSelectedId() - 1; // combo box IDs are 1-based

                int octaveDelta = 0;
                if (octaveBox.getSelectedId() > 0)
                    octaveDelta = octaveBox.getText().getIntValue();

                // Call processor API (two-arg signature) to transpose melodic notes
                proc.bumppitTranspose(targetKeyIndex, octaveDelta);

                // Robustly find the active editor via the processor and request a UI refresh.
                if (auto* ae = proc.getActiveEditor())
                    if (auto* ed = dynamic_cast<BoomAudioProcessorEditor*>(ae))
                        ed->refreshFromAI();
            }
            else
            {
                // Drums path: call the callback provided by the caller (preserves original behavior)
                if (onBumpFn) onBumpFn();
            }
        };
    btnHome.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    // Melodic options only for 808/Bass layout
    showMelodicOptions = !isDrums;
    if (showMelodicOptions)
    {
        // Add labels with proper images
        addAndMakeVisible(keyLbl);
        keyLbl.setImage(boomui::loadSkin("keyLbl.png"));
        keyLbl.setInterceptsMouseClicks(false, false);
        
        addAndMakeVisible(scaleLbl);
        scaleLbl.setImage(boomui::loadSkin("scaleLbl.png"));
        scaleLbl.setInterceptsMouseClicks(false, false);
        
        addAndMakeVisible(octaveLbl);
        octaveLbl.setImage(boomui::loadSkin("octaveLbl.png"));
        octaveLbl.setInterceptsMouseClicks(false, false);
        
        // Add combo boxes
        addAndMakeVisible(keyBox);    
        keyBox.addItemList(boom::keyChoices(), 1);     
        keyBox.setSelectedId(1);
        
        addAndMakeVisible(scaleBox);  
        scaleBox.addItemList(boom::scaleChoices(), 1); 
        scaleBox.setSelectedId(1);
        
        addAndMakeVisible(octaveBox); 
        octaveBox.addItemList(juce::StringArray("-2", "-1", "0", "+1", "+2"), 1); 
        octaveBox.setSelectedId(3);
        
        // Tooltips
        keyBox.setTooltip("Choose a new key to transpose to.");
        scaleBox.setTooltip("Choose a new scale to transpose to.");
        octaveBox.setTooltip("Choose a new octave to transpose to.");
    }
}

void BumppitWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void BumppitWindow::resized()
{
    auto r = getLocalBounds();

    // scale helper for 700x462
    const float W = 700.f, H = 462.f;
    auto sx = r.getWidth() / W, sy = r.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };

    // Center the title at the top using the image's natural size scaled by sx/sy.
    {
        auto img = titleLbl.getImage();
        const int iw = juce::roundToInt(img.getWidth() * sx);
        const int ih = juce::roundToInt(img.getHeight() * sy);
        const int x = (r.getWidth() - iw) / 2;
        const int y = juce::roundToInt(24 * sy);
        titleLbl.setBounds(x, y, iw, ih);
    }

    // layout per mockups:
    if (showMelodicOptions)
    {
        // 808/Bass mockup: labels on left, combo boxes on right
        const int lblX = 130;      // label X position
        const int lblWidth = 80;   // label width
        const int boxX = 220;      // combo box X position
        const int boxWidth = 270;  // combo box width
        const int rowHeight = 50;  // vertical spacing between rows
        const int startY = 130;    // starting Y position
        
        // Key row
        keyLbl.setBounds(S(lblX, startY, lblWidth, 26));
        keyBox.setBounds(S(boxX, startY, boxWidth, 46));
        
        // Scale row
        scaleLbl.setBounds(S(lblX, startY + rowHeight, lblWidth, 26));
        scaleBox.setBounds(S(boxX, startY + rowHeight, boxWidth, 46));
        
        // Octave row
        octaveLbl.setBounds(S(lblX, startY + rowHeight * 2, lblWidth, 26));
        octaveBox.setBounds(S(boxX, startY + rowHeight * 2, boxWidth, 46));

        // place the Bumppit button beneath those controls (centered)
        btnBump.setBounds(S(175, 340, 350, 74));
    }
    else
    {
        // Drums mockup (second image): one big Bumppit button in the middle
        btnBump.setBounds(S(130, 171, 440, 120));
    }

    // Home button bottom-right (as in mockups)
    btnHome.setBounds(S(620, 382, 60, 60));
}

HatsWindow::HatsWindow(BoomAudioProcessor& p,
    std::function<void()> onClose,
    std::function<void(juce::String, int, int)> /*onGen*/)
    : proc(p), onCloseFn(std::move(onClose))
{
    setSize(690, 690);

    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 1000);

    // --- static bottom art (centered near bottom like your mock) ---
    hatsLbl.setImage(loadSkin("hatsLbl.png"));
    hatsLbl.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(hatsLbl);

    hatsDescriptionLbl.setImage(loadSkin("hatsDescriptionLbl.png"));
    hatsDescriptionLbl.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(hatsDescriptionLbl);

    // --- left/middle labels above their boxes ---
    styleLbl.setImage(loadSkin("styleLbl.png")); styleLbl.setInterceptsMouseClicks(false, false);
    timeSigLbl.setImage(loadSkin("timeSigLbl.png")); timeSigLbl.setInterceptsMouseClicks(false, false);
    barsLbl.setImage(loadSkin("timeSigLbl.png")); // you gave a BARS label image earlier; if it’s "barsLbl.png", swap here
    barsLbl.setImage(loadSkin("barsLbl.png"));
    howManyLbl.setImage(loadSkin("howManyLbl.png")); howManyLbl.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(howManyLbl);
    keyLbl.setImage(loadSkin("keyLbl.png"));
    scaleLbl.setImage(loadSkin("scaleLbl.png"));
    addAndMakeVisible(keyLbl);
    addAndMakeVisible(scaleLbl);\
    addAndMakeVisible(keyBox);
    addAndMakeVisible(scaleBox);
    addAndMakeVisible(keyBox);   keyBox.addItemList(boom::keyChoices(), 1);
    addAndMakeVisible(scaleBox); scaleBox.addItemList(boom::scaleChoices(), 1);
    keyLbl.setVisible(false);
    scaleLbl.setVisible(false);
    keyBox.setVisible(false);
    scaleBox.setVisible(false);
    tunedLbl.setImage(loadSkin("tunedLbl.png")); howManyLbl.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(tunedChk);
    boomui::setToggleImages(tunedChk, "checkboxOffBtn", "checkboxOnBtn");
    tunedChk.setClickingTogglesState(true);
    tunedChk.onClick = [this]()
        {
            const bool enabled = tunedChk.getToggleState();
            keyLbl.setVisible(enabled);
            scaleLbl.setVisible(enabled);
            keyBox.setVisible(enabled);
            scaleBox.setVisible(enabled);
            resized(); // re-layout the UI
        };
    keyBox.setVisible(tunedChk.getToggleState());
    scaleBox.setVisible(tunedChk.getToggleState());

    addAndMakeVisible(styleLbl);
    addAndMakeVisible(timeSigLbl);
    addAndMakeVisible(barsLbl);
    addAndMakeVisible(howManyLbl);
    addAndMakeVisible(tunedLbl);

    // --- combo boxes ---
    addAndMakeVisible(styleBox);
    styleBox.addItemList(boom::drums::styleNames(), 1);
    styleBox.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(timeSigBox);
    timeSigBox.addItemList(boom::timeSigChoices(), 1);
    timeSigBox.setSelectedId(1, juce::dontSendNotification); // "4/4" in your list

    addAndMakeVisible(barsBox);
    barsBox.addItem("4", 1);
    barsBox.addItem("8", 2);
    barsBox.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(howManyBox);
    howManyBox.addItem("5", 5);
    howManyBox.addItem("25", 25);
    howManyBox.addItem("50", 50);
    howManyBox.addItem("100", 100);
    howManyBox.setSelectedId(5, juce::dontSendNotification);

    // --- triplets / dotted with your checkbox art ---

    // --- Save + Home ---
    addAndMakeVisible(btnSaveMidi); setButtonImages(btnSaveMidi, "saveMidiBtn");
    addAndMakeVisible(btnGenerate); setButtonImages(btnGenerate, "generateBtn");
    setButtonImages(btnDragMidi, "dragBtn");
    btnDragMidi.setTooltip("Drag your hi-hat MIDI to your DAW.");

    btnDragMidi.onClick = [this]
        {
            juce::File f = buildTempMidi();
            performFileDrag(f);
        };
    addAndMakeVisible(btnHome); setButtonImages(btnHome, "homeBtn");

    btnHome.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
            if (onCloseFn) onCloseFn();
        };

    // === HATS: Generate and Save batch handler ===
    btnGenerate.onClick = [this]
        {
            auto* fc = new juce::FileChooser("Select destination folder...",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
                "*", true);

            fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this, fc](const juce::FileChooser& chooser)
                {
                    juce::File destFolder = chooser.getResult();
                    if (!destFolder.isDirectory())
                    {
                        delete fc;
                        return;
                    }

                    // -----------------------------
                    // BARS (Hats window: 4 or 8)
                    // -----------------------------
                    int bars = 4;
                    if (barsBox.getSelectedId() > 0)
                        bars = barsBox.getText().getIntValue();
                    bars = (bars == 8 ? 8 : 4);

                    // -----------------------------
                    // STYLE
                    // -----------------------------
                    const juce::String style = styleBox.getText().trim();

                    // -----------------------------
                    // HOW MANY  (IMPORTANT FIX: don't use getSelectedId() here)
                    // -----------------------------
                    int howMany = 5;
                    if (howManyBox.getSelectedId() > 0)
                        howMany = howManyBox.getText().getIntValue();
                    if (howMany != 5 && howMany != 25 && howMany != 50 && howMany != 100)
                        howMany = 5;

                    // -----------------------------
                    // TIME SIGNATURE
                    // -----------------------------
                    int numerator = 4, denominator = 4;
                    {
                        juce::String ts = timeSigBox.getText().trim();
                        auto parts = juce::StringArray::fromTokens(ts, "/", "");
                        if (parts.size() == 2)
                        {
                            numerator = parts[0].getIntValue();
                            denominator = parts[1].getIntValue();
                            if (numerator <= 0) numerator = 4;
                            if (denominator <= 0) denominator = 4;
                        }
                    }

                    // -----------------------------
                    // Gate triplets/dotted using the MAIN checkboxes (APVTS)
                    // (Hats generator should use these flags)
                    // -----------------------------
                    bool allowTriplets = false;
                    bool allowDotted = false;
                    if (auto* b = proc.apvts.getRawParameterValue("useTriplets")) allowTriplets = (b->load() > 0.5f);
                    if (auto* b = proc.apvts.getRawParameterValue("useDotted"))   allowDotted = (b->load() > 0.5f);

                    // -----------------------------
                    // GENERATE + WRITE FILES
                    // -----------------------------
                    for (int i = 0; i < howMany; ++i)
                    {
                        juce::String fileName = "BOOM_Hats_" + style + "_" + juce::String(i + 1) + ".mid";
                        juce::File destFile = destFolder.getChildFile(fileName);

                        // ✅ Use the REAL hats generator (NOT drum generator + mask)
                        juce::MidiMessageSequence seq =
                            proc.makeHiHatPattern(style, numerator, denominator, bars, allowTriplets, allowDotted, /*seed*/ -1);

                        juce::MidiFile mf;
                        mf.setTicksPerQuarterNote(96);
                        mf.addTrack(seq);

                        boom::midi::writeMidiToFile(mf, destFile);

                        DBG("HatsWindow: created " << destFile.getFullPathName());
                    }

                    delete fc;
                    TransientMsgComponent::launchCentered(this, "MIDI GENERATED!", 1400);
                });
        };


    btnSaveMidi.onClick = [this]
        {
            // Re-route to the Generate button's handler, which now does the saving
            btnGenerate.onClick();
        };

    // tooltips (matches your other windows style)
    styleBox.setTooltip("Choose your hihat pattern style.");
    timeSigBox.setTooltip("Choose a time signature for the patterns.");
    barsBox.setTooltip("Choose 4 or 8 bars.");
    howManyBox.setTooltip("How many distinct MIDI files to create.");
    btnSaveMidi.setTooltip("Save a batch of closed-hat MIDI files.");
    btnHome.setTooltip("Close this window.");
}

void HatsWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void HatsWindow::resized()
{
    auto r = getLocalBounds();
    const int W = r.getWidth();
    const int H = r.getHeight();

    // Bottom art (like your mock)
    hatsLbl.setBounds((W - 510) / 2, H - 300, 500, 105);
    hatsDescriptionLbl.setBounds((W - 540) / 2, H - 170, 540, 72);

    // Top/center controls
    const int colW = 150;
    const int gapX = 32;
    const int rowTop = 110; // top Y of label row
    const int boxTop = rowTop + 32;

    int x = (W - (colW * 3 + gapX * 2)) / 2;

    styleLbl.setBounds(x, rowTop + 20, colW, 26);
    styleBox.setBounds(x, boxTop + 20, colW, 26);
    x += colW + gapX;

    timeSigLbl.setBounds(x, rowTop + 20, colW, 26);
    timeSigBox.setBounds(x, boxTop + 20, colW, 26);
    x += colW + gapX;

    barsLbl.setBounds(x, rowTop + 20, colW, 26);
    barsBox.setBounds(x, boxTop + 20, colW, 26);

    // “How many?” row centered under the three selectors
   // Centered horizontal layout for "How Many" and "Tuned" controls
    const int howManyGroupWidth = 150;
    const int tunedGroupWidth = 150;
    const int groupSpacing = 30;
    const int totalCenteredWidth = howManyGroupWidth + groupSpacing + tunedGroupWidth;

    int currentX = (W - totalCenteredWidth) / 2;
    int yPos = boxTop + 60;

    // How Many group
    howManyLbl.setBounds(currentX, yPos, 150, 26);
    howManyBox.setBounds(currentX + (150 - 90) / 2, yPos + 26 + 5, 90, 26);

    currentX += howManyGroupWidth + groupSpacing;

    // Tuned group
    tunedLbl.setBounds(currentX, yPos, 150, 26);
    tunedChk.setBounds(currentX + (150 - 28) / 2, yPos + 26 + 5, 28, 28);

    int controlsBottomY = juce::jmax(howManyBox.getBottom(), tunedChk.getBottom());

    // Place key and scale boxes below the "Tuned" group when visible
    if (tunedChk.getToggleState())
    {
        const int keyScaleY = tunedChk.getBottom() + 10;
        const int labelWidth = 50;
        const int boxWidth = 120;
        const int labelBoxSpacing = 5;
        const int totalGroupWidth = labelWidth + labelBoxSpacing + boxWidth;
        const int groupX = (W - totalGroupWidth) / 2;
        
        // Key label + box
        keyLbl.setBounds(groupX, keyScaleY, labelWidth, 26);
        keyBox.setBounds(groupX + labelWidth + labelBoxSpacing, keyScaleY, boxWidth, 26);
        
        // Scale label + box
        scaleLbl.setBounds(groupX, keyScaleY + 26 + 5, labelWidth, 26);
        scaleBox.setBounds(groupX + labelWidth + labelBoxSpacing, keyScaleY + 26 + 5, boxWidth, 26);
        
        controlsBottomY = scaleBox.getBottom();
    }

    // Triplets / Dotted on right side (like mock)
    const int rightX = W - 240;
    tripletsLblImg.setBounds(rightX + 50, rowTop - 70, 160, 24);
    tripletsChk.setBounds(rightX + 200, rowTop - 72, 28, 28);
    dottedLblImg.setBounds(rightX + 50, rowTop - 25, 160, 24);
    dottedChk.setBounds(rightX + 200, rowTop - 27, 28, 28);
    tripletDensity.setBounds(583, 65, 100, 20);
    dottedDensity.setBounds(568, 110, 100, 20);

    // Save + Home
    btnGenerate.setBounds((W - 150) / 2, controlsBottomY + 30, 150, 40);
    btnDragMidi.setBounds((W - 150) / 2, H - 100, 150, 50);
    btnHome.setBounds(W - 84 - 18, H - 84 + 2, 84, 84);
}

juce::File HatsWindow::buildTempMidi() const
{
    // -----------------------------
    // BARS (Hats window: 4 or 8)
    // -----------------------------
    int bars = 4;
    if (barsBox.getSelectedId() > 0)
        bars = barsBox.getText().getIntValue();
    bars = (bars == 8 ? 8 : 4);

    // -----------------------------
    // STYLE
    // -----------------------------
    const juce::String style = styleBox.getText().trim();

    // -----------------------------
    // TIME SIGNATURE
    // -----------------------------
    int numerator = 4, denominator = 4;
    {
        juce::String ts = timeSigBox.getText().trim();
        auto parts = juce::StringArray::fromTokens(ts, "/", "");
        if (parts.size() == 2)
        {
            numerator = parts[0].getIntValue();
            denominator = parts[1].getIntValue();
            if (numerator <= 0) numerator = 4;
            if (denominator <= 0) denominator = 4;
        }
    }

    // -----------------------------
    // Gate triplets/dotted using MAIN checkboxes (APVTS)
    // -----------------------------
    bool allowTriplets = false;
    bool allowDotted = false;
    if (auto* b = proc.apvts.getRawParameterValue("useTriplets")) allowTriplets = (b->load() > 0.5f);
    if (auto* b = proc.apvts.getRawParameterValue("useDotted"))   allowDotted = (b->load() > 0.5f);

    // -----------------------------
    // Generate hats sequence (same engine as batch button)
    // -----------------------------
    juce::MidiMessageSequence seq =
        proc.makeHiHatPattern(style, numerator, denominator, bars, allowTriplets, allowDotted, /*seed*/ -1);

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(96);
    mf.addTrack(seq);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("BOOM_Hats.mid");

    if (tmp.existsAsFile())
        tmp.deleteFile();

    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}


void HatsWindow::performFileDrag(const juce::File& f)
{
    if (!f.existsAsFile()) return;

    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        juce::StringArray files;
        files.add(f.getFullPathName());
        dnd->performExternalDragDropOfFiles(files, true);
    }
}




RollsWindow::RollsWindow(BoomAudioProcessor& p, std::function<void()> onClose, std::function<void(juce::String, int, int)> onGen)
    : proc(p), onCloseFn(std::move(onClose)), onGenerateFn(std::move(onGen))
{
    setSize(700, 447);
    
    // Create tooltip window for this modal
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);

    // STYLE box (same list as main)
    setButtonImages(diceBtn, "diceBtn");      addAndMakeVisible(diceBtn);
    barsLbl.setImage(loadSkin("barsLbl.png"));
    addAndMakeVisible(barsLbl);
    styleLbl.setImage(loadSkin("styleLbl.png"));
    addAndMakeVisible(styleLbl);
    timeSigLbl.setImage(loadSkin("timeSigLbl.png"));
    addAndMakeVisible(timeSigLbl);
    styleBox.addItemList(boom::styleChoices(), 1);
    styleBox.setSelectedId(1, juce::dontSendNotification);
    howManyLbl.setImage(loadSkin("howManyLbl.png"));
    addAndMakeVisible(howManyLbl);
    keyLbl.setImage(loadSkin("keyLbl.png"));
    scaleLbl.setImage(loadSkin("scaleLbl.png"));
    addAndMakeVisible(keyLbl);
    addAndMakeVisible(scaleLbl);
    keyLbl.setVisible(false);
    scaleLbl.setVisible(false);
    addAndMakeVisible(keyBox);   keyBox.addItemList(boom::keyChoices(), 1);
    addAndMakeVisible(scaleBox); scaleBox.addItemList(boom::scaleChoices(), 1);
    keyBox.setVisible(false);
    scaleBox.setVisible(false);
    tunedLbl.setImage(loadSkin("tunedLbl.png")); howManyLbl.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(tunedLbl);
    addAndMakeVisible(tunedChk);
    boomui::setToggleImages(tunedChk, "checkboxOffBtn", "checkboxOnBtn");
    tunedChk.setClickingTogglesState(true);
    tunedChk.onClick = [this]()
        {
            const bool enabled = tunedChk.getToggleState();
            keyLbl.setVisible(enabled);
            scaleLbl.setVisible(enabled);
            keyBox.setVisible(enabled);
            scaleBox.setVisible(enabled);
            resized(); // re-layout the UI
        };
    keyBox.setVisible(tunedChk.getToggleState());
    scaleBox.setVisible(tunedChk.getToggleState());


    // BARS box (Rolls-specific: 1,2,4,8)
    barsBox.clear();
    barsBox.addItem("1", 1);
    barsBox.addItem("2", 2);
    barsBox.addItem("4", 3);
    barsBox.addItem("8", 4);
    barsBox.setSelectedId(3, juce::dontSendNotification); // default to "4"

    addAndMakeVisible(timeSigBox);
    timeSigBox.addItemList(boom::timeSigChoices(), 1);

    addAndMakeVisible(howManyBox);
    howManyBox.addItem("5", 5);
    howManyBox.addItem("25", 25);
    howManyBox.addItem("50", 50);
    howManyBox.addItem("100", 100);
    howManyBox.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(styleBox);
    addAndMakeVisible(barsBox);
    addAndMakeVisible(rollsTitleImg);
    rollsTitleImg.setInterceptsMouseClicks(false, false);
    rollsTitleImg.setImage(loadSkin("rollGerneratorLbl.png")); // exact filename from your asset list
    rollsTitleImg.setImagePlacement(juce::RectanglePlacement::centred); // keep aspect when we size the box
    addAndMakeVisible(variation); variation.setRange(0, 100, 1); variation.setValue(35); variation.setSliderStyle(juce::Slider::LinearHorizontal);


    addAndMakeVisible(btnGenerate); setButtonImages(btnGenerate, "generateBtn");
    addAndMakeVisible(btnHome);    setButtonImages(btnHome, "homeBtn");
    btnHome.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    diceBtn.setTooltip("Randomizes the parameteres in the boxes on the left and the humanization sliders on the right. Then just press GENERATE, and BOOM, random fun!");
    barsBox.setTooltip("Choose how long you want your drumroll midi to be.");
    styleBox.setTooltip("Choose your drumroll style.");
    timeSigBox.setTooltip("Choose your drumroll's time signature.");
    howManyBox.setTooltip("How many distinct MIDI files to create.");
    btnGenerate.setTooltip("Generate your midi drumroll.");
    btnHome.setTooltip("Close this window.");


    diceBtn.onClick = [this]
    {
        // random style in the box
        const int n = styleBox.getNumItems();
        if (n > 0) styleBox.setSelectedId(1 + juce::Random::getSystemRandom().nextInt(n));

        // random bars choice
        barsBox.setSelectedId(1 + juce::Random::getSystemRandom().nextInt(4));

        // trigger a generate
        btnGenerate.triggerClick();
    };


    // === ROLLS: Generate and Save batch of snare-roll patterns ===
    btnGenerate.onClick = [this]
        {
            auto* fc = new juce::FileChooser("Select destination folder...",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
                "*", true);

            fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this, fc](const juce::FileChooser& chooser)
                {
                    juce::File destFolder = chooser.getResult();
                    if (!destFolder.isDirectory())
                    {
                        delete fc;
                        return;
                    }

                    // -----------------------------
                    // BARS (Rolls window: 1,2,4,8)
                    // -----------------------------
                    int bars = 4;
                    if (barsBox.getSelectedId() > 0)
                        bars = barsBox.getText().getIntValue();

                    // -----------------------------
                    // STYLE (Rolls styles are separate from drums styles)
                    // -----------------------------
                    juce::String style = styleBox.getText().trim();

                    // -----------------------------
                    // HOW MANY (5/25/50/100)
                    // -----------------------------
                    int howMany = 5;
                    if (howManyBox.getSelectedId() > 0)
                        howMany = howManyBox.getText().getIntValue();

                    // -----------------------------
                    // TIME SIGNATURE (from this window's timeSigBox)
                    // -----------------------------
                    int numerator = 4, denominator = 4;
                    {
                        juce::String ts = timeSigBox.getText().trim(); // e.g. "4/4"
                        auto parts = juce::StringArray::fromTokens(ts, "/", "");
                        if (parts.size() == 2)
                        {
                            numerator = parts[0].getIntValue();
                            denominator = parts[1].getIntValue();
                        }
                        if (numerator <= 0) numerator = 4;
                        if (denominator <= 0) denominator = 4;
                    }

                    // -----------------------------
                    // Use Triplets / Dotted (APVTS toggles)
                    // -----------------------------
                    const bool allowTriplets = (proc.apvts.getRawParameterValue("useTriplets")->load() > 0.5f);
                    const bool allowDotted = (proc.apvts.getRawParameterValue("useDotted")->load() > 0.5f);

                    // -----------------------------
                    // If Rolls Tuned is ON, push this window's key/scale into APVTS
                    // (generateRolls reads APVTS "key" + "scale")
                    // -----------------------------
                    const bool rollsTuned = (proc.apvts.getRawParameterValue("rollsTuned")->load() > 0.5f);

                    auto findChoiceIndexIgnoreCase = [](const juce::StringArray& choices, const juce::String& name) -> int
                        {
                            for (int i = 0; i < choices.size(); ++i)
                                if (choices[i].trim().equalsIgnoreCase(name.trim()))
                                    return i;
                            return -1;
                        };

                    if (rollsTuned)
                    {
                        const juce::String keyName = keyBox.getText().trim();
                        const juce::String scaleName = scaleBox.getText().trim();

                        if (auto* keyParam = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("key")))
                        {
                            const int idx = findChoiceIndexIgnoreCase(boom::keyChoices(), keyName);
                            if (idx >= 0)
                            {
                                const int n = keyParam->choices.size();
                                const float norm = (n <= 1) ? 0.0f : (float)idx / (float)(n - 1);
                                keyParam->setValueNotifyingHost(norm);
                            }
                        }

                        if (auto* scaleParam = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("scale")))
                        {
                            const int idx = findChoiceIndexIgnoreCase(boom::scaleChoices(), scaleName);
                            if (idx >= 0)
                            {
                                const int n = scaleParam->choices.size();
                                const float norm = (n <= 1) ? 0.0f : (float)idx / (float)(n - 1);
                                scaleParam->setValueNotifyingHost(norm);
                            }
                        }
                    }

                    // -----------------------------
                    // NEW: export batch using the NEW rolls engine
                    // -----------------------------
                    proc.generateRollBatch(style, numerator, denominator, bars, howMany, destFolder, allowTriplets, allowDotted);

                    delete fc;
                    TransientMsgComponent::launchCentered(this, "MIDI GENERATED!", 1400);
                });
        };


    btnGenerate.setTooltip("Generates snare roll midi patterns. depending on the time signature, style, bars, and how many you choose, and lets you pick a folder to save them in.");
    timeSigBox.setTooltip("Pick a time signature for your roll patterns.");
    barsBox.setTooltip("Pick a length for your roll patterns.");
    styleBox.setTooltip("Pick a style for your roll patterns.");

}
void RollsWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void RollsWindow::resized()
{
    auto bounds = getLocalBounds();
    const int W = bounds.getWidth();
    const int H = bounds.getHeight();

    // 1. Title image
    const int titleImageWidth = 258;
    const int titleImageHeight = 131;
    rollsTitleImg.setBounds((W - titleImageWidth) / 2, 15, titleImageWidth, titleImageHeight);

    // 2. Combo boxes and labels
    // Order: Time Sig, Bars, Style
    const int itemWidth = 150;
    const int labelHeight = 26;
    const int comboBoxHeight = 24;
    const int horizontalSpacing = 20;
    int yPos = titleImageHeight + 40;



    const int numItems = 3;
    const int totalLayoutWidth = (numItems * itemWidth) + ((numItems - 1) * horizontalSpacing);

    int currentX = (W - totalLayoutWidth) / 2;
    int labelY = titleImageHeight + 100;
    const int verticalSpacing = 5;
    int boxY = labelY + labelHeight + verticalSpacing;

    // Time Sig
    auto area = getLocalBounds().reduced(12);
    const int topPad = 150;
    area.removeFromTop(topPad);
    auto row = area.removeFromTop(32);
    timeSigLbl.setBounds(currentX, yPos, itemWidth, labelHeight);
    timeSigBox.setBounds(currentX, yPos + labelHeight + 5, itemWidth, comboBoxHeight);
    currentX += itemWidth + horizontalSpacing;

    barsLbl.setBounds(currentX, yPos, itemWidth, labelHeight);
    barsBox.setBounds(currentX, yPos + labelHeight + 5, itemWidth, comboBoxHeight);
    currentX += itemWidth + horizontalSpacing;

    styleLbl.setBounds(currentX, yPos, itemWidth, labelHeight);
    styleBox.setBounds(currentX, yPos + labelHeight + 5, itemWidth, comboBoxHeight);

    yPos = styleBox.getBottom() + 20;

    // Centered horizontal layout for "How Many" and "Tuned" controls
    const int howManyGroupWidth = 150;
    const int tunedGroupWidth = 150;
    const int groupSpacing = 30;
    const int totalCenteredWidth = howManyGroupWidth + groupSpacing + tunedGroupWidth;

    currentX = (W - totalCenteredWidth) / 2;

    // How Many group
    howManyLbl.setBounds(currentX, yPos, 150, 26);
    howManyBox.setBounds(currentX + (150 - 90) / 2, yPos + 26 + 5, 90, 26);

    currentX += howManyGroupWidth + groupSpacing;

    // Tuned group
    tunedLbl.setBounds(currentX, yPos, 150, 26);
    tunedChk.setBounds(currentX + (150 - 28) / 2, yPos + 26 + 5, 28, 28);

    int controlsBottomY = juce::jmax(howManyBox.getBottom(), tunedChk.getBottom());

    if (tunedChk.getToggleState())
    {
        const int labelsWidth = 100;
        const int boxWidth = 150;
        const int spacing = 5;
        const int totalWidth = labelsWidth + spacing + boxWidth;
        const int keyScaleY = tunedChk.getBottom() + 10;
        const int startX = tunedChk.getX() - (totalWidth - tunedChk.getWidth()) / 2;

        keyLbl.setBounds(startX, keyScaleY, labelsWidth, 26);
        keyBox.setBounds(startX + labelsWidth + spacing, keyScaleY, boxWidth, 26);
        scaleLbl.setBounds(startX, keyScaleY + 26 + 5, labelsWidth, 26);
        scaleBox.setBounds(startX + labelsWidth + spacing, keyScaleY + 26 + 5, boxWidth, 26);
        controlsBottomY = scaleBox.getBottom();
    }
    const int rowTop = 110; // top Y of label row
    const int rightX = W - 240;

    // 3. Buttons row
    const int generateButtonWidth = 190;
    const int buttonHeight = 50;
    btnGenerate.setBounds((W - generateButtonWidth) / 2, controlsBottomY + 20, generateButtonWidth, buttonHeight);
   btnHome.setBounds(W - 80, H - 80, 60, 60);
}

juce::File RollsWindow::buildTempMidi() const
{
    // -----------------------------
    // BARS (Rolls window: 1,2,4,8)
    // -----------------------------
    int bars = 4;
    if (barsBox.getSelectedId() > 0)
        bars = barsBox.getText().getIntValue();
    bars = juce::jlimit(1, 8, bars);

    // -----------------------------
    // STYLE
    // -----------------------------
    const juce::String style = styleBox.getText().trim();

    // -----------------------------
    // TIME SIGNATURE
    // -----------------------------
    int numerator = 4, denominator = 4;
    {
        juce::String ts = timeSigBox.getText().trim();
        auto parts = juce::StringArray::fromTokens(ts, "/", "");
        if (parts.size() == 2)
        {
            numerator = parts[0].getIntValue();
            denominator = parts[1].getIntValue();
        }
        if (numerator <= 0) numerator = 4;
        if (denominator <= 0) denominator = 4;
    }

    // -----------------------------
    // Use Triplets / Dotted (APVTS toggles)
    // -----------------------------
    bool allowTriplets = false;
    bool allowDotted = false;
    if (auto* b = proc.apvts.getRawParameterValue("useTriplets")) allowTriplets = (b->load() > 0.5f);
    if (auto* b = proc.apvts.getRawParameterValue("useDotted"))   allowDotted = (b->load() > 0.5f);

    // Generate one pattern using the actual engine
    const int seed = (int)juce::Time::getMillisecondCounter();
    juce::MidiMessageSequence seq = proc.generateRolls(style, numerator, denominator, bars, allowTriplets, allowDotted, seed);

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(96);
    seq.updateMatchedPairs();
    mf.addTrack(seq);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("BOOM_Roll.mid");

    if (tmp.existsAsFile())
        tmp.deleteFile();

    if (auto out = tmp.createOutputStream())
    {
        mf.writeTo(*out);
        out->flush();
        return tmp;
    }

    return {};
}


void RollsWindow::performFileDrag(const juce::File& f)
{
    if (!f.existsAsFile()) return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        juce::StringArray files; files.add(f.getFullPathName());
        dnd->performExternalDragDropOfFiles(files, true);
    }
}
