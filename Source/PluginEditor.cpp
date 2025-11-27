#include "PluginEditor.h"
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
#include "MiniDrumGridComponent.h" 
#include "DrumGenerator.h"
#include <atomic>

namespace {
    enum PreviewOwner : int { Preview_None = 0, Preview_Rhythm = 1, Preview_Beatbox = 4 };
    static std::atomic<int> g_previewOwner{ Preview_None };
}

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

        logoImg.setImage(boomui::loadSkin("logo.png"));
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

    // Editor had a global L&F applied — restore default before destruction.
    setLookAndFeel(nullptr);
}

AIToolsWindow::~AIToolsWindow()
{
    // prevent processor from calling back into a destroyed window
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
        boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, useSeed, pat);

        // If your generator output is based on a fixed baseTicksPerBar (e.g., 4*PPQ),
        // uncomment and adjust the scale logic below:
        // double baseTicksPerBar = 4.0 * PPQ;
        // double scale = ticksPerBarDouble / baseTicksPerBar;

        const int patternOffset = (int)std::round(i * (double)bars * ticksPerBarDouble);

        for (const auto& e : pat)
        {
            if (rowFilterMask != 0 && !(rowFilterMask & (1u << e.row)))
                continue;

            const double noteOnTick = (double)patternOffset + (double)e.startTick; // * scale if using scale
            const double noteOffTick = noteOnTick + (double)e.lenTicks;            // * scale if using scale

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
    setLookAndFeel(&boomui::LNF());
    setResizable(true, true);
    setSize(783, 714);

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


    soundsDopeLbl.setImage(loadSkin("soundsDopeLbl.png")); addAndMakeVisible(soundsDopeLbl);
    soundsDopeLbl.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    soundsDopeLbl.addMouseListener(this, false);
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
    safeCreateCombo("drumStyle", drumStyleAtt, drumStyleBox);
    safeCreateSlider("restDensityDrums", restDrumsAtt, restDrums);

    auto setApvtsBool = [this](const char* paramId, bool v)
        {
            if (auto* p = proc.apvts.getParameter(paramId))
                p->setValueNotifyingHost(v ? 1.0f : 0.0f);
        };

    // helper to read APVTS bool state
    auto getApvtsBool = [this](const char* paramId)->bool
        {
            if (auto* v = proc.apvts.getRawParameterValue(paramId))
                return v->load() > 0.5f;
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

    // Parameter listener wiring for host/preset changes — ensure the editor updates
    proc.apvts.addParameterListener("useTriplets", this);
    proc.apvts.addParameterListener("useDotted", this);

    timeSigBox.onChange = [this]
        {
            // Parse "N/D" from the box text (e.g., "4/4", "3/4", "5/8")
            int num = 4, den = 4;
            {
                const juce::String s = timeSigBox.getText().trim();
                const int slash = s.indexOfChar('/');
                if (slash > 0)
                {
                    num = s.substring(0, slash).getIntValue();
                    den = s.substring(slash + 1).getIntValue();
                }
            }

            // Push directly to both views — no processor setter needed
            pianoRoll.setTimeSignature(num, den);
            drumGrid.setTimeSignature(num, den);

            // (Optional) If you also store time sig in parameters elsewhere, keep doing that here too.
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

    // Drums
    addAndMakeVisible(drumStyleBox); drumStyleBox.addItemList(boom::styleChoices(), 1);
    addAndMakeVisible(restDrums); restDrums.setSliderStyle(juce::Slider::LinearHorizontal);
    restDrums.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    restDrums.setRange(0, 100);
    restDrums.setTooltip("Increase this slider for more gaps (rests) between notes/beats!");
    drumStyleAtt = std::make_unique<Attachment>(proc.apvts, "drumStyle", drumStyleBox);
    restDrumsAtt = std::make_unique<SAttachment>(proc.apvts, "restDensityDrums", restDrums);

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
    drumGridView.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);          // drag to scroll

    // PIANO ROLL
    addAndMakeVisible(pianoRollView);
    pianoRollView.setViewedComponent(&pianoRoll, false);
    pianoRollView.setScrollBarsShown(true, true);
    pianoRollView.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);

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



    btnAITools.setTooltip("Opens the AI Tools Window.");
    btnRolls.setTooltip("Opens the Rolls Window.");
    btnBumppit.setTooltip("Opens the BUMPPIT Window.");
    btnFlippit.setTooltip("Opens the FLIPPIT Window.");
    hatsBtn.setTooltip("Opens the HATS Window.");
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
    addAndMakeVisible(dragSelected);
    addAndMakeVisible(dragAll);

    // set images (use your existing helper; replace names if different)
    setButtonImages(dragAll, "dragAllBtn");           // or whichever image key you use
    setButtonImages(dragSelected, "dragSelectedBtn"); // must match your BinaryData names
    drumDragChoicesVisible = false;

    // ensure the choice buttons exist and start hidden
    addAndMakeVisible(dragSelected);
    addAndMakeVisible(dragAll);

    // image setup (use your existing image helper if you have one)
    setButtonImages(dragAll, "dragAllBtn");
    setButtonImages(dragSelected, "dragSelectedBtn");

    // make selected a latch
    dragSelected.setClickingTogglesState(true);
    dragSelected.setToggleState(false, juce::dontSendNotification);

    // start hidden and enabled
    dragSelected.setVisible(false);
    dragAll.setVisible(false);
    dragSelected.setEnabled(true);
    dragAll.setEnabled(true);

    // make sure they draw above everything else when shown
    dragSelected.toFront(true);
    dragAll.toFront(true);
    btnGenerate.setTooltip("Generates MIDI patterns according to the ENGINE selected at the top, the choices in the boxes on the left, and the humanization sliders on the right!");
    btnDragMidi.setTooltip("Allows you to drag and drop the MIDI you have generated into your DAW!");
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

            int uiDensity = 100 - juce::roundToInt(juce::jlimit(0.0f, 1.0f,
                proc.apvts.getParameter("restDensity808")->getValue()) * 100.0f);

            const bool uiTriplets = (/* read your 'useTriplets' toggle */ boomfix::readParamRaw(proc.apvts, "useTriplets", 0.0f) > 0.5f);
            const bool uiDotted = (/* read your 'useDotted' toggle */ boomfix::readParamRaw(proc.apvts, "useDotted", 0.0f) > 0.5f);

            const int seed = -1; // or a fixed int for reproducible results

            proc.generate808(uiBars, uiOctave, uiDensity, uiTriplets, uiDotted, seed);

            // ---- Refresh piano roll
// Update range from actual generated notes
            const auto bounds = proc.getMelodicPitchBounds();
            pianoRoll.setPitchRange(bounds.first, bounds.second);

            // Push the notes into the UI
            pianoRoll.setPattern(proc.getMelodicPattern());

            // Optional: center the viewport on the first note so the user sees it immediatel

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

            // ---- DENSITIES from APVTS raw values (0..100 floats) ----
            auto clampPct = [](float v) -> int { return juce::jlimit(0, 100, (int)juce::roundToInt(v)); };

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

            // ---- Read checkboxes: gate the sliders (if checkbox OFF, density forced 0)
            // ---- Generate + refresh UI ----
            proc.generateBassFromSpec(style, bars, octave, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1);
            pianoRoll.setPattern(proc.getMelodicPattern());
            pianoRoll.repaint();
            repaint();
            return;
        }
        if (eng == boom::Engine::Drums)
        {
            // ---- STYLE from APVTS
            juce::String style = "trap";
            if (auto* styleParam = proc.apvts.getParameter("style"))
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(styleParam))
                {
                    const int idx = choice->getIndex();
                    auto styles = boom::drums::styleNames();
                    if (styles.size() > 0)
                        style = styles[juce::jlimit(0, styles.size() - 1, idx)];
                }

            // ---- BARS from APVTS (fallback 4)
            int bars = 4;
            if (auto* barsParam = proc.apvts.getParameter("bars"))
                if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(barsParam))
                    bars = pi->get();

            // ---- DENSITIES from APVTS (0..100)
            auto clampPct = [](float v) -> int { return juce::jlimit(0, 100, (int)juce::roundToInt(v)); };

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

            // ---- Call database generator
            boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);
            boom::drums::DrumPattern pat;
            boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, pat);

            // Diagnostics: log how many events we got from the DB generator
            DBG("Generate (Drums) -> DB pat size = " << (int)pat.size());

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
            if (engine != boom::Engine::Drums)
            {
                // non-drums: fallback immediate drag
                juce::File f = writeTempMidiFile();
                if (f.existsAsFile())
                {
                    juce::StringArray files; files.add(f.getFullPathName());
                    performExternalDragDropOfFiles(files, true);
                }
                return;
            }

            // Defensive: ensure we have valid original bounds
            if (dragBtnOriginalBounds_.isEmpty())
            {
                dragBtnOriginalBounds_ = btnDragMidi.getBounds();
                DBG("WARN: dragBtnOriginalBounds_ was empty, using btnDragMidi.getBounds() -> " << dragBtnOriginalBounds_.toString());
            }

            // **Always show** the overlay for drums (don't rely on a stale toggle)
            drumDragChoicesVisible = true;

            // hide original main button
            btnDragMidi.setVisible(false);

            // compute placement (defensive fallback)
            juce::Rectangle<int> r = dragBtnOriginalBounds_;
            if (r.getWidth() <= 0 || r.getHeight() <= 0)
                r = juce::Rectangle<int>((getWidth() - 160) / 2, (getHeight() - 40) / 2, 160, 40);

            const int halfW = r.getWidth() / 2;
            dragSelected.setBounds(r.getX(), r.getY(), halfW, r.getHeight());
            dragAll.setBounds(r.getX() + halfW, r.getY(), r.getWidth() - halfW, r.getHeight());

            // show them and bring to front
            dragSelected.setVisible(true);
            dragAll.setVisible(true);
            dragSelected.toFront(true);
            dragAll.toFront(true);

            // ensure selected starts untoggled visually
            dragSelected.setToggleState(false, juce::dontSendNotification);

            DBG("UI: showing drum drag choices at " << r.toString());
            repaint();
        };


    dragAll.onClick = [this]()
        {
            DBG("UI: dragAll clicked");
            // build full drums MIDI and start external drag
            juce::File f = writeTempMidiFile();
            if (f.existsAsFile())
            {
                juce::StringArray files; files.add(f.getFullPathName());
                performExternalDragDropOfFiles(files, true);
            }

            // restore UI
            drumDragChoicesVisible = false;
            dragSelected.setVisible(false);
            dragAll.setVisible(false);
            btnDragMidi.setVisible(true);
            btnDragMidi.setBounds(dragBtnOriginalBounds_);
            btnDragMidi.toFront(true);
            resized(); // recompute canonical positions
            repaint();
            DBG("UI: restored main drag button after dragAll");
        };

    dragSelected.onClick = [this]()
        {
            // perform selected-rows drag immediately when clicked-on
            if (dragSelected.getToggleState())
            {
                uint32_t rowMask = drumGrid.getRowSelectionMask();
                DBG("UI: building midi for selected rows mask=" << (uint32_t)rowMask);
                juce::File f = buildTempMidiForSelectedRows(rowMask);
                if (f.existsAsFile())
                {
                    juce::StringArray files; files.add(f.getFullPathName());
                    performExternalDragDropOfFiles(files, true);
                }
            }

            // un-latch (defensive) and restore UI
            dragSelected.setToggleState(false, juce::dontSendNotification);
            drumDragChoicesVisible = false;
            dragSelected.setVisible(false);
            dragAll.setVisible(false);
            btnDragMidi.setVisible(true);
            btnDragMidi.setBounds(dragBtnOriginalBounds_);
            btnDragMidi.toFront(true);
            resized(); // recompute canonical positions
            repaint();
            DBG("UI: restored main drag button after dragSelected");
        };

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

    proc.apvts.addParameterListener("timeSig", this);
    proc.apvts.addParameterListener("bars", this);
}

void BoomAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "timeSig")
    {
        updateTimeSigAndBars();
    }
    else if (parameterID == "bars")
    {
        updateTimeSigAndBars();
    }
    else if (parameterID == "useTriplets")
    {
        const bool v = proc.apvts.getRawParameterValue("useTriplets")->load() > 0.5f;
        useTriplets.setToggleState(v, juce::dontSendNotification);
        tripletDensity.setEnabled(v);
        if (v)
        {
            useDotted.setToggleState(false, juce::dontSendNotification);
            dottedDensity.setEnabled(false);
        }
    }
    else if (parameterID == "useDotted")
    {
        const bool v = proc.apvts.getRawParameterValue("useDotted")->load() > 0.5f;
        useDotted.setToggleState(v, juce::dontSendNotification);
        dottedDensity.setEnabled(v);
        if (v)
        {
            useTriplets.setToggleState(false, juce::dontSendNotification);
            tripletDensity.setEnabled(false);
        }
    }
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
    btnFlippit.setBounds(S(580, 350, 200, 60));
    btnRolls.setBounds(S(40, 370, 160, 43));
    hatsBtn.setBounds(S(15, 180, 200, 60));
    btnAITools.setBounds(S(290, 350, 200, 60));

    // DRUM GRID (main window) — exact size
    {
        auto gridArea = S(40, 420, 700, 200);
        drumGridView.setBounds(gridArea);
        drumGrid.setTopLeftPosition(0, 0);
        drumGrid.setSize(gridArea.getWidth() * 2, gridArea.getHeight());
    }

    // PIANO ROLL (main window) — exact size
    {
        auto rollArea = S(40, 420, 700, 200);
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
    dragBtnOriginalBounds_ = btnDragMidi.getBounds();
    DBG("resized: dragBtnOriginalBounds_ = " << dragBtnOriginalBounds_.toString());

    // compute two-button rect but only apply when overlay is NOT visible
    juce::Rectangle<int> r = dragBtnOriginalBounds_;
    if (!drumDragChoicesVisible && r.getWidth() > 0 && r.getHeight() > 0)
    {
        const int halfW = r.getWidth() / 2;
        dragSelected.setBounds(r.getX(), r.getY(), halfW, r.getHeight());
        dragAll.setBounds(r.getX() + halfW, r.getY(), r.getWidth() - halfW, r.getHeight());
        dragSelected.setVisible(false);
        dragAll.setVisible(false);
    }
    else
    {
        // if visible, keep them on top
        dragSelected.toFront(true);
        dragAll.toFront(true);
    }
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
    const int den = proc.getTimeSigDenominator(); // safe even if you always return 4
    const int bars = proc.getBars();

    // Push into your components
    drumGrid.setTimeSignature(num, den);
    pianoRoll.setTimeSignature(num, den);
    drumGrid.setBarsToDisplay(bars);
    pianoRoll.setBarsToDisplay(bars);

    // Reset scroll so users see bar 1 when TS/bars change
    drumGridView.setViewPosition(0, 0);
    pianoRollView.setViewPosition(0, 0);

    // Force a repaint (if your setters don’t already do this)
    drumGrid.repaint();
    pianoRoll.repaint();
}

void BoomAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &btnDragMidi || e.originalComponent == &btnDragMidi)
        drumDragChoicesVisible = true;   // <---- ADD THIS
        startExternalMidiDrag();


    // if the user clicked the sounds-dope label image, show the splash dialog (only here)
    if (e.eventComponent == &soundsDopeLbl || e.originalComponent == &soundsDopeLbl)
    {
        juce::Colour parentBg = findColour(juce::ResizableWindow::backgroundColourId);
        if (!parentBg.isOpaque())
            parentBg = juce::Colours::black;

        auto* content = new SplashDialog(parentBg, 0.78f);

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(content);
        o.dialogTitle = "Sounds Dope Audio";
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = false;
        o.resizable = false;
        o.componentToCentreAround = this;
        if (auto* dw = o.launchAsync())
        {
            dw->setColour(juce::DialogWindow::backgroundColourId, juce::Colours::transparentBlack);
            dw->centreAroundComponent(this, content->getWidth(), content->getHeight());
            dw->setVisible(true);
        }
        return;

    }
    // otherwise ignore
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
    btnRolls.setVisible(isDrums);
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

    // If rowMask == 0 -> treat as "all rows" (keep existing buildBatch behavior)
    if (rowMask == 0u)
    {
        // buildBatchDrumMidi expects 0 => include all rows (per its comment)
        // nothing to change
    }

    // Get time signature numeric values from processor helpers (already used elsewhere)
    const int numerator = proc.getTimeSigNumerator();
    const int denominator = proc.getTimeSigDenominator();

    // Build one pattern (howMany = 1) using -1 seed (random per buildBatch logic)
    auto tmp = buildBatchDrumMidi(baseName,
        boom::drums::getSpec(style),
        bars,
        1,               // howMany
        restPct,
        dottedPct,
        tripletPct,
        swingPct,
        -1,              // seed
        rowMask,
        numerator,
        denominator);

    return tmp;
}

juce::File BoomAudioProcessorEditor::buildTempMidiForSelectedRows(uint32_t rowMask, const juce::String& baseName) const
{
    // ---- Bars / style ----
    const int bars = getBarsFromUI();

    juce::String style = "trap";
    if (auto* styleParam = proc.apvts.getParameter("style"))
        if (auto* ch = dynamic_cast<juce::AudioParameterChoice*>(styleParam))
            style = ch->getCurrentChoiceName();

    // ---- read densities and swing (defensive) ----
    auto clampPct = [](float v) -> int { return juce::jlimit(0, 100, (int)juce::roundToInt(v)); };

    int restPct = 0;
    if (auto* rp = proc.apvts.getRawParameterValue("restDensityDrums")) restPct = clampPct(rp->load());

    int dottedPct = 0;
    if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity")) dottedPct = clampPct(dp->load());

    int tripletPct = 0;
    if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity")) tripletPct = clampPct(tp->load());

    int swingPct = 0;
    if (auto* sp = proc.apvts.getRawParameterValue("swing")) swingPct = clampPct(sp->load());

    // ---- gate densities with checkboxes (use the APVTS boolean params you confirmed) ----
    bool useTriplets = false;
    bool useDotted = false;
    if (auto* b = proc.apvts.getRawParameterValue("useTriplets")) useTriplets = (b->load() > 0.5f);
    if (auto* b2 = proc.apvts.getRawParameterValue("useDotted"))  useDotted = (b2->load() > 0.5f);

    const int effectiveTripletPct = useTriplets ? tripletPct : 0;
    const int effectiveDottedPct = useDotted ? dottedPct : 0;

    // ---- time signature for buildBatchDrumMidi (numerator/denominator) ----
    int numerator = 4;
    int denominator = 4;
    {
        auto tsParam = proc.apvts.state.getProperty("timeSig").toString();
        if (tsParam.isNotEmpty())
        {
            auto parts = juce::StringArray::fromTokens(tsParam, "/", "");
            if (parts.size() == 2)
            {
                numerator = parts[0].getIntValue();
                denominator = parts[1].getIntValue();
                if (numerator <= 0) numerator = 4;
                if (denominator <= 0) denominator = 4;
            }
        }
    }

    // ---- Call the existing batch builder with the provided row mask
    // buildBatchDrumMidi(...) : (baseName, spec, bars, howMany, restPct, dottedPct, tripletPct, swingPct, seed, rowFilterMask, numerator, denominator)
    return buildBatchDrumMidi(baseName,
        boom::drums::getSpec(style),
        bars,
        1, // howMany
        restPct,
        effectiveDottedPct,
        effectiveTripletPct,
        swingPct,
        -1, // seed
        rowMask,
        numerator,
        denominator);
}

juce::File BoomAudioProcessorEditor::writeTempMidiFile() const
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    juce::MidiFile mf;
    if (engine == boom::Engine::Drums)
    {
        boom::midi::DrumPattern mp;
        for (const auto& n : proc.getDrumPattern())
            mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
        mf = boom::midi::buildMidiFromDrums(mp, 96);
    }
    else
    {
        boom::midi::MelodicPattern mp;
        for (const auto& n : proc.getMelodicPattern())
            mp.add({ n.pitch, n.startTick, n.lengthTicks, n.velocity, 1 });
        mf = boom::midi::buildMidiFromMelodic(mp, 96);
    }
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("BOOM_Pattern.mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}

void BoomAudioProcessorEditor::startExternalMidiDrag()
{
    const juce::File f = writeTempMidiFile();
    juce::StringArray files; files.add(f.getFullPathName());
    performExternalDragDropOfFiles(files, true);
}

juce::File AIToolsWindow::buildTempMidi(const juce::String& base) const
{
    juce::MidiFile mf;
    boom::midi::DrumPattern mp;

    // Convert processor’s current drum pattern to MIDI
    for (const auto& n : proc.getDrumPattern())
        mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });

    mf = boom::midi::buildMidiFromDrums(mp, 96);

    // Write to a temp file named after base
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile(base + ".mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}


AIToolsWindow::AIToolsWindow(BoomAudioProcessor& p, std::function<void()> onClose)
    : proc(p), onCloseFn(std::move(onClose))
{
    aiToolsLnf.reset(new AppLookAndFeel());
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
    addLbl(slapsmithLbl, "slapsmithLbl.png");
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
                            cb->setColour(juce::ComboBox::textColourId, juce::Colours::white);
                            cb->setColour(juce::ComboBox::outlineColourId, kPurple.darker(0.25f));
                            cb->setColour(juce::ComboBox::arrowColourId, kLime);
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
    addAndMakeVisible(toggleSlap);
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
    toggleSlap.onClick = [this] { makeToolActive(Tool::Slapsmith);   };
    toggleBlend.onClick = [this] { makeToolActive(Tool::StyleBlender); };
    toggleBeat.onClick = [this] { makeToolActive(Tool::Beatbox);     };

    // Default active tool (pick one; Rhythmimick here)
    makeToolActive(Tool::Rhythmimick);

    rhythmimickLbl.setTooltip("Record up to 60sec with your soundcard and have Rhythmimick make a MIDI pattern from what it hears. Works with all engines.");
    slapsmithLbl.setTooltip("Make a simple skeleton beat with the mini-grid and let Slapsmith handle the rest! Works with all engines.");
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

    // ---- Slapsmith row ----
    addAndMakeVisible(btnGen2);   setButtonImages(btnGen2, "generateBtn");
    addAndMakeVisible(btnSave2);  setButtonImages(btnSave2, "saveMidiBtn");
    addAndMakeVisible(btnDrag2);  setButtonImages(btnDrag2, "dragBtn");



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

    btnGen2.onClick = [this]
    {
        int bars = 4;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(proc.apvts.getParameter("bars")))
            bars = p->get();

        proc.aiSlapsmithExpand(bars);
        miniGrid.setPattern(proc.getDrumPattern());
        miniGrid.repaint();

        TransientMsgComponent::launchCentered(this, "MIDI GENERATED!", 1400);
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

    // Play: toggle preview start/stop and update visual
    btnPlay1.onClick = [this]()
        {
            juce::String msg = juce::String("UI: btnPlay1 clicked. aiHasCapture=") + juce::String((int)proc.aiHasCapture())
                + " aiIsPreviewing=" + juce::String((int)proc.aiIsPreviewing());
            DBG(msg);

            if (proc.aiHasCapture())
            {
                if (proc.aiIsPreviewing())
                {
                    proc.aiPreviewStop();
                    g_previewOwner.store(Preview_None);
                    DBG("UI: requested aiPreviewStop()");
                }
                else
                {
                    proc.aiPreviewStart();
                    g_previewOwner.store(Preview_Rhythm);
                    DBG("UI: requested aiPreviewStart()");
                }
            }
            else
            {
                DBG("UI: no capture available to preview (btnPlay1)");
            }
        };


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
    btnPlay4.onClick = [this]()
        {
            juce::String msg = juce::String("UI: btnPlay4 clicked. aiHasCapture=") + juce::String((int)proc.aiHasCapture())
                + " aiIsPreviewing=" + juce::String((int)proc.aiIsPreviewing());
            DBG(msg);

            if (proc.aiHasCapture())
            {
                if (proc.aiIsPreviewing())
                {
                    proc.aiPreviewStop();
                    g_previewOwner.store(Preview_None);
                    DBG("UI: requested aiPreviewStop()");
                }
                else
                {
                    proc.aiPreviewStart();
                    g_previewOwner.store(Preview_Beatbox);
                    DBG("UI: requested aiPreviewStart()");
                }
            }
            else
            {
                DBG("UI: no capture available to preview (btnPlay4)");
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
    hookupRow(btnSave2, btnDrag2, "BOOM_Slapsmith");
    // --- Slapsmith drag choices overlay (two buttons that pop up over the main drag) ---

    // Override the default drag handler: when user clicks Slapsmith's drag, show the two choices.
    btnDrag2.onClick = [this]
        {
            // Only show choices when DRUMS engine is active; otherwise use the normal drag flow
            const auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
            if (engine != boom::Engine::Drums)
            {
                // Fall back to the existing generic drag
                juce::File f = buildTempMidi("BOOM_Slapsmith");
                performFileDrag(f);
                return;
            }

        };
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
            auto tmp = buildTempMidi("AI_StyleBlend.mid"); // implement equivalent helper in AIToolsWindow if not present
            lastGeneratedTempFile = tmp; // store member juce::File lastGeneratedTempFile;

            // Refresh UI with whatever the processor produced
            if (auto* ed = findParentComponentOfClass<BoomAudioProcessorEditor>())
            {
                // call the editor's public refresh that already exists
                ed->refreshFromAI();
            }
            else
            {
                // fallback to updating only this window if we couldn't find the editor
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

    // --- Slapsmith mini-grid ---
    addAndMakeVisible(miniGrid);
    miniGrid.setRows(proc.getDrumRows()); // or a subset like first 4 rows
    miniGrid.setZoomX(10.0f);
    miniGrid.setRowHeightPixels(200);

    // Single, message-thread-safe callback so the processor can notify the UI when the pattern changes.
    proc.drumPatternChangedCallback = [this]()
        {
            juce::MessageManager::callAsync([this]()
                {
                    DBG("AIToolsWindow: processor callback -> updating miniGrid from proc pattern size=" << proc.getDrumPattern().size());
                    miniGrid.setPattern(proc.getDrumPattern());
                    miniGrid.repaint();
                });
        };

    // Ensure the mini-grid accepts mouse input and is z-ordered correctly.
    miniGrid.setInterceptsMouseClicks(true, true);
    miniGrid.setEnabled(false);   // start disabled until Slapsmith tool is active
    miniGrid.toFront(true);
    miniGrid.onCellEdited = [this](int row, int step, bool value)
        {
            const int startTick = step * 24;
            auto pat = proc.getDrumPattern();

            int found = -1;
            for (int i = 0; i < pat.size(); ++i)
                if (pat[i].row == row && pat[i].startTick == startTick) { found = i; break; }

            if (value) { if (found < 0) pat.add({ 0, row, startTick, 24, 100 }); }
            else { if (found >= 0) pat.remove(found); }

            proc.setDrumPattern(pat);
        };


    // --- Style blender combo boxes ---
    addAndMakeVisible(styleABox); styleABox.addItemList(boom::styleChoices(), 1); styleABox.setSelectedId(1);
    addAndMakeVisible(styleBBox); styleBBox.addItemList(boom::styleChoices(), 1); styleBBox.setSelectedId(2);

    startTimerHz(20); // update 8x per second
    // ---- Shared actions (Save/Drag) use real MIDI from processor ----

}

void AIToolsWindow::updateSeekFromProcessor()
{
    if (!proc.aiHasCapture())
    {
        rhythmSeek.setValue(0.0, juce::dontSendNotification);
        beatboxSeek.setValue(0.0, juce::dontSendNotification);
        return;
    }

    const double len = juce::jmax(0.000001, proc.getCaptureLengthSeconds());
    const double pos = proc.getCapturePositionSeconds();
    const double norm = juce::jlimit(0.0, 1.0, pos / len);

    // Both seek bars mirror the same capture (we have one capture buffer),
    // but you can hide/show them with your tool toggle if you want.
    rhythmSeek.setValue(norm, juce::dontSendNotification);
    beatboxSeek.setValue(norm, juce::dontSendNotification);
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

        // Keep MiniDrumGridComponent interactive even when the group is "disabled".
        // We still visually dim it, but let it receive mouse input when visible.
        if (auto* mg = dynamic_cast<MiniDrumGridComponent*>(c))
        {
            mg->setInterceptsMouseClicks(enabled, true); // allow or block clicks by group state
            mg->setAlpha(a);
            continue;
        }

        // Default behaviour for other components
        c->setEnabled(enabled);
        c->setAlpha(a);
    }
}

void AIToolsWindow::setActiveTool(Tool t)
{
    activeTool_ = t;

    const bool r = (t == Tool::Rhythmimick);
    const bool s = (t == Tool::Slapsmith);
    const bool b = (t == Tool::Beatbox);
    const bool y = (t == Tool::StyleBlender);

    // Dim when disabled (alpha 0.35) + block input via setEnabled(false)
    setGroupEnabled(rhythmimickGroup, r);
    setGroupEnabled(slapsmithGroup, s);
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
    setActive(toggleSlap, t == Tool::Slapsmith);
    setActive(toggleBlend, t == Tool::StyleBlender);
    setActive(toggleBeat, t == Tool::Beatbox);

    // Optional: enable/disable row controls based on active tool
    const bool r = (t == Tool::Rhythmimick);
    const bool s = (t == Tool::Slapsmith);
    const bool b = (t == Tool::Beatbox);
    const bool y = (t == Tool::StyleBlender);

    DBG("AIToolsWindow::makeToolActive tool=" << (int)t << " SlapsmithActive=" << (int)s);

    miniGrid.setEnabled(s);
    miniGrid.setInterceptsMouseClicks(s, s);
    if (s) miniGrid.toFront(true);

    miniGrid.onCellEdited = [this](int row, int step, bool value)
        {
            // DBG: confirm the editor saw the edit and what the values are
            DBG("AIToolsWindow::miniGrid.onCellEdited row=" << row << " step=" << step << " value=" << (int)value);

            const int startTick = step * 24;
            auto pat = proc.getDrumPattern();

            int found = -1;
            for (int i = 0; i < pat.size(); ++i)
                if (pat[i].row == row && pat[i].startTick == startTick) { found = i; break; }

            if (value)
            {
                if (found < 0) pat.add({ 0, row, startTick, 24, 100 });
            }
            else
            {
                if (found >= 0) pat.remove(found);
            }

            proc.setDrumPattern(pat);

            // DBG: confirm processor received the new pattern
            DBG("AIToolsWindow::miniGrid.onCellEdited -> proc.getDrumPattern().size()=" << proc.getDrumPattern().size());

            // Optionally request a UI refresh (safe): repaint this window so miniGrid redraws.
            miniGrid.repaint();
        };

    btnRec1.setEnabled(r);   btnPlay1.setEnabled(r); btnStop1.setEnabled(r);   btnGen1.setEnabled(r);   btnSave1.setEnabled(r);   btnDrag1.setEnabled(r);
    btnGen2.setEnabled(s);   btnSave2.setEnabled(s);   btnDrag2.setEnabled(s);  miniGrid.setEnabled(s);
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

    // --- Slap-Smith ---
    slapsmithLbl.setBounds(S(300, y, 220, label_height));
    toggleSlap.setBounds(S(600, y, 120, 40));
    arrowSlap.setBounds(S(530, y, 60, 40));
    miniGrid.setBounds(S(320, y + 65, 250, 80));
    miniGrid.toFront(true);
    btnGen2.setBounds(S(320, y + 150, 90, 30));
    btnSave2.setBounds(S(420, y + 150, 90, 30));
    btnDrag2.setBounds(S(520, y + 150, 90, 30));
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

    metersRhBounds.setBounds(5, y + 280, 90, 30);
    metersBxBounds.setBounds(5, y + 380, 90, 30);

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
        boom::midi::DrumPattern mp;
        for (const auto& n : proc.getDrumPattern())
            mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
        mf = boom::midi::buildMidiFromDrums(mp, 96);
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
        addAndMakeVisible(keyBox);    keyBox.addItemList(boom::keyChoices(), 1);     keyBox.setSelectedId(1);
        addAndMakeVisible(octaveBox); octaveBox.addItemList(juce::StringArray("-2", "-1", "0", "+1", "+2"), 1); octaveBox.setSelectedId(3);
        keyBox.setTooltip("Choose to keep the same settings or pick new ones!");
        octaveBox.setTooltip("Choose to keep the same settings or pick new ones!");

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
        // 808/Bass mockup (first image): four combo boxes centered column
        keyBox.setBounds(S(215, 130, 270, 46));
        octaveBox.setBounds(S(215, 180, 270, 46));

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

    addAndMakeVisible(styleLbl);
    addAndMakeVisible(timeSigLbl);
    addAndMakeVisible(barsLbl);
    addAndMakeVisible(howManyLbl);

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
    tripletsLblImg.setImage(loadSkin("tripletsLbl.png")); tripletsLblImg.setInterceptsMouseClicks(false, false);
    dottedLblImg.setImage(loadSkin("dottedNotesLbl.png")); dottedLblImg.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(tripletsLblImg);
    addAndMakeVisible(dottedLblImg);
    addAndMakeVisible(tripletDensity); tripletDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    tripletDensity.setRange(0, 100);
    addAndMakeVisible(dottedDensity);  dottedDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    dottedDensity.setRange(0, 100);
    boomui::makePercentSlider(dottedDensity);
    boomui::makePercentSlider(tripletDensity);
    dottedDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    tripletDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    addAndMakeVisible(tripletsChk);
    addAndMakeVisible(dottedChk);
    boomui::setToggleImages(tripletsChk, "checkboxOffBtn", "checkboxOnBtn");
    boomui::setToggleImages(dottedChk, "checkboxOffBtn", "checkboxOnBtn");

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
                    if (destFolder.isDirectory())
                    {
                        const int bars = barsBox.getText().getIntValue();
                        const juce::String style = styleBox.getText();
                        const int howMany = howManyBox.getSelectedId();

                        auto clampPct = [](float v) -> int { return juce::jlimit(0, 100, (int)juce::roundToInt(v)); };
                        int restPct = 0, dottedPct = 0, tripletPct = 0, swingPct = 0;
                        if (auto* rp = proc.apvts.getRawParameterValue("restDensity")) restPct = clampPct(rp->load());
                        if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity")) dottedPct = clampPct(dp->load());
                        if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity")) tripletPct = clampPct(tp->load());
                        if (auto* sp = proc.apvts.getRawParameterValue("swing")) swingPct = clampPct(sp->load());

                        boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);
                        uint32_t hatRowIndicesMask = (1u << 2) | (1u << 3);

                        int numerator = 4, denominator = 4;
                        juce::String ts = timeSigBox.getText();
                        auto parts = juce::StringArray::fromTokens(ts, "/", "");
                        if (parts.size() == 2)
                        {
                            numerator = parts[0].getIntValue();
                            denominator = parts[1].getIntValue();
                            if (numerator <= 0) numerator = 4;
                            if (denominator <= 0) denominator = 4;
                        }

                        for (int i = 0; i < howMany; ++i)
                        {
                            juce::String fileName = "BOOM_Hats_" + style + "_" + juce::String(i + 1) + ".mid";
                            juce::File destFile = destFolder.getChildFile(fileName);

                            juce::File tmp = buildBatchDrumMidi("BOOM_Hats_Temp",
                                spec,
                                bars,
                                1, 
                                restPct, dottedPct, tripletPct, swingPct,
                                i,
                                hatRowIndicesMask,
                                numerator,
                                denominator);

                            if (tmp.existsAsFile())
                            {
                                tmp.copyFileTo(destFile);
                                DBG("HatsWindow: created " << destFile.getFullPathName());
                            }
                        }
                    }
                    delete fc;
                });
        };


    btnDragMidi.onClick = [this]
        {
            // Same logic as generate, but creates one temp file to drag
            const int bars = barsBox.getText().getIntValue();
            const juce::String style = styleBox.getText();

            auto clampPct = [](float v) -> int { return juce::jlimit(0, 100, (int)juce::roundToInt(v)); };
            int restPct = 0, dottedPct = 0, tripletPct = 0, swingPct = 0;
            if (auto* rp = proc.apvts.getRawParameterValue("restDensity")) restPct = clampPct(rp->load());
            if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity")) dottedPct = clampPct(dp->load());
            if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity")) tripletPct = clampPct(tp->load());
            if (auto* sp = proc.apvts.getRawParameterValue("swing")) swingPct = clampPct(sp->load());

            boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);
            uint32_t hatRowIndicesMask = (1u << 2) | (1u << 3);

            int numerator = 4, denominator = 4;
            juce::String ts = timeSigBox.getText();
            auto parts = juce::StringArray::fromTokens(ts, "/", "");
            if (parts.size() == 2)
            {
                numerator = parts[0].getIntValue();
                denominator = parts[1].getIntValue();
                if (numerator <= 0) numerator = 4;
                if (denominator <= 0) denominator = 4;
            }

            juce::File tmp = buildBatchDrumMidi("BOOM_HatsTemp", spec, bars, 1,
                restPct, dottedPct, tripletPct, swingPct, -1,
                hatRowIndicesMask, numerator, denominator);

            performFileDrag(tmp);
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
    tripletsChk.setTooltip("Allow triplet-grid patterns.");
    dottedChk.setTooltip("Allow dotted-note spacing.");
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
    howManyLbl.setBounds((W - 220) / 2, boxTop + 82, 220, 32);
    howManyBox.setBounds((W - 90) / 2, howManyLbl.getBottom() + 28, 90, 26);

    // Triplets / Dotted on right side (like mock)
    const int rightX = W - 240;
    tripletsLblImg.setBounds(rightX + 50, rowTop - 70, 160, 24);
    tripletsChk.setBounds(rightX + 200, rowTop - 72, 28, 28);
    dottedLblImg.setBounds(rightX + 50, rowTop - 25, 160, 24);
    dottedChk.setBounds(rightX + 200, rowTop - 27, 28, 28);
    tripletDensity.setBounds(583, 65, 100, 20);
    dottedDensity.setBounds(568, 110, 100, 20);

    // Save + Home
    btnGenerate.setBounds((W - 150) / 2, howManyBox.getBottom() + 40, 150, 40);
    btnDragMidi.setBounds((W - 150) / 2, H - 100, 150, 50);
    btnHome.setBounds(W - 84 - 18, H - 84 + 2, 84, 84);
}

juce::File HatsWindow::buildTempMidi() const
{
    juce::MidiFile mf;

    // Build from the current drum pattern (same as Rolls/Flippit)
    boom::midi::DrumPattern mp;
    for (const auto& n : proc.getDrumPattern())
        mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });

    mf = boom::midi::buildMidiFromDrums(mp, 96);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("BOOM_Hats.mid");
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

    tripletsLblImg.setImage(loadSkin("tripletsLbl.png")); tripletsLblImg.setInterceptsMouseClicks(false, false);
    dottedLblImg.setImage(loadSkin("dottedNotesLbl.png")); dottedLblImg.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(tripletsLblImg);
    addAndMakeVisible(dottedLblImg);
    addAndMakeVisible(tripletDensity); tripletDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    tripletDensity.setRange(0, 100);
    addAndMakeVisible(dottedDensity);  dottedDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    dottedDensity.setRange(0, 100);
    dottedDensity.setLookAndFeel(&boomui::AltLNF());
    tripletDensity.setLookAndFeel(&boomui::AltLNF());
    boomui::makePercentSlider(dottedDensity);
    boomui::makePercentSlider(tripletDensity);
    dottedDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    tripletDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    addAndMakeVisible(tripletsChk);
    addAndMakeVisible(dottedChk);
    boomui::setToggleImages(tripletsChk, "checkboxOffBtn", "checkboxOnBtn");
    boomui::setToggleImages(dottedChk, "checkboxOffBtn", "checkboxOnBtn");


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
            if (destFolder.isDirectory())
            {

            // BARS
            int bars = 4;
            if (barsBox.getSelectedId() > 0)
                bars = barsBox.getText().getIntValue();


            // STYLE
            juce::String style = styleBox.getText();

            // HOW MANY
            int howMany = 5;
            if (howManyBox.getSelectedId() > 0)
                howMany = howManyBox.getText().getIntValue();


            // densities
            auto clampPct = [](float v) -> int { return juce::jlimit(0, 100, (int)juce::roundToInt(v)); };
            int restPct = 0, dottedPct = 0, tripletPct = 0, swingPct = 0, humanizeTimingPct = 0, humanizeVelocityPct;
            if (auto* rp = proc.apvts.getRawParameterValue("restDensityDrums")) restPct = clampPct(rp->load());
            if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity")) dottedPct = clampPct(dp->load());
            if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity")) tripletPct = clampPct(tp->load());
            if (auto* sp = proc.apvts.getRawParameterValue("swing")) swingPct = clampPct(sp->load());
            if (auto* sp = proc.apvts.getRawParameterValue("humanizeTiming")) humanizeTimingPct = clampPct(sp->load());
            if (auto* sp = proc.apvts.getRawParameterValue("humanizeVelocity")) humanizeVelocityPct = clampPct(sp->load());

            boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);

            // Build mask that only includes snare rows. Replace with your snare row index. (example: row 1)
            uint32_t snareMask = (1u << 1);

            // For rolls we might want amplified velocities and shorter lengths. You can post-process
            // the generated pattern before export if you want more "roll" character. For now we export as-is.
// --- Read time signature from APVTS ("timeSig" AudioParameterChoice) ---
// read time signature from APVTS
// read time signature from APVTS
            int numerator = 4, denominator = 4;
            if (auto* tsParam = proc.apvts.getParameter("timeSig"))
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(tsParam))
                {
                    juce::String ts = timeSigBox.getText();
                    auto parts = juce::StringArray::fromTokens(ts, "/", "");
                    if (parts.size() == 2)
                    {
                        numerator = parts[0].getIntValue();
                        denominator = parts[1].getIntValue();
                        if (numerator <= 0) numerator = 4;
                        if (denominator <= 0) denominator = 4;
                    }
                }

            for (int i = 0; i < howMany; ++i)
            {
                juce::String fileName = "BOOM_Roll_" + juce::String(i + 1) + ".mid";
                juce::File destFile = destFolder.getChildFile(fileName);


            juce::File tmp = buildBatchDrumMidi("BOOM_RollsBatch",
                spec,
                bars,
                howMany,
                restPct, dottedPct, tripletPct, swingPct,
                /*seed*/ -1,
                snareMask,
                numerator,
                denominator);

            if (tmp.existsAsFile())
            {
                tmp.copyFileTo(destFile);
                DBG("RollsWindow: created " << destFile.getFullPathName());
            }
            }
            }
            delete fc;
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
    const int verticalSpacing = 5;

    const int numItems = 3;
    const int totalLayoutWidth = (numItems * itemWidth) + ((numItems - 1) * horizontalSpacing);

    int currentX = (W - totalLayoutWidth) / 2;
    int labelY = titleImageHeight + 100;
    int boxY = labelY + labelHeight + verticalSpacing;

    // Time Sig
    auto area = getLocalBounds().reduced(12);
    const int topPad = 150;
    area.removeFromTop(topPad);
    auto row = area.removeFromTop(32);
    timeSigLbl.setBounds(row.removeFromLeft(100));
    timeSigBox.setBounds(row.removeFromLeft(120));
    currentX += itemWidth + horizontalSpacing;
    styleLbl.setBounds(row.removeFromLeft(120));
    styleBox.setBounds(row.removeFromLeft(160));
    howManyLbl.setBounds(currentX, labelY, itemWidth, labelHeight);
    howManyBox.setBounds(currentX, boxY, itemWidth, comboBoxHeight);
    barsLbl.setBounds(row.removeFromLeft(80));
    barsBox.setBounds(row.removeFromLeft(80));
    const int rowTop = 110; // top Y of label row
    const int rightX = W - 240;
    tripletsLblImg.setBounds(rightX + 50, rowTop - 70, 160, 24);
    tripletsChk.setBounds(rightX + 200, rowTop - 72, 28, 28);
    dottedLblImg.setBounds(rightX + 50, rowTop - 25, 160, 24);
    dottedChk.setBounds(rightX + 200, rowTop - 27, 28, 28);
    tripletDensity.setBounds(583, 65, 100, 20);
    dottedDensity.setBounds(568, 110, 100, 20);

    // 3. Buttons row
    const int generateButtonWidth = 190;
    const int otherButtonWidth = 150;
    const int buttonHeight = 50;
    const int buttonSpacing = 20;
    const int totalButtonWidth = generateButtonWidth + otherButtonWidth * 2 + buttonSpacing * 2;
    int x_buttons = (W - totalButtonWidth) / 2;
    int y_buttons = boxY + comboBoxHeight + 30; // 30px space after combos
    btnGenerate.setBounds(currentX - 25, y_buttons, generateButtonWidth, buttonHeight);
    x_buttons += generateButtonWidth + buttonSpacing;
    btnHome.setBounds(W - 80, H - 80, 60, 60);
}

juce::File RollsWindow::buildTempMidi() const
{
    juce::MidiFile mf;
    boom::midi::DrumPattern mp;
    for (const auto& n : proc.getDrumPattern())
        mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
    mf = boom::midi::buildMidiFromDrums(mp, 96);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("BOOM_Roll.mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
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
