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

// Now that BpmPoller is complete, define the editor dtor here:
BoomAudioProcessorEditor::~BoomAudioProcessorEditor()
{
    bpmPoller.reset();
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

// ======= small helper =======
int BoomAudioProcessorEditor::barsFromBox(const juce::ComboBox& b) { return b.getSelectedId() == 2 ? 8 : 4; }

int BoomAudioProcessorEditor::getBarsFromUI() const
{
    return barsFromBox(barsBox); // you already have barsFromBox(...)
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


    // Engine label + buttons
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
    bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "bpm", bpmSlider);

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
    timeSigAtt = std::make_unique<Attachment>(proc.apvts, "timeSig", timeSigBox);
    barsAtt = std::make_unique<Attachment>(proc.apvts, "bars", barsBox);
    humanizeTimingAtt = std::make_unique<SAttachment>(proc.apvts, "humanizeTiming", humanizeTiming);
    humanizeVelocityAtt = std::make_unique<SAttachment>(proc.apvts, "humanizeVelocity", humanizeVelocity);
    swingAtt = std::make_unique<SAttachment>(proc.apvts, "swing", swing);
    useTripletsAtt = std::make_unique<BAttachment>(proc.apvts, "useTriplets", useTriplets);
    tripletDensityAtt = std::make_unique<SAttachment>(proc.apvts, "tripletDensity", tripletDensity);
    useDottedAtt = std::make_unique<BAttachment>(proc.apvts, "useDotted", useDotted);
    dottedDensityAtt = std::make_unique<SAttachment>(proc.apvts, "dottedDensity", dottedDensity);

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
    setButtonImages(dragSelected, "dragSelectedBtn");  addAndMakeVisible(dragSelected);  dragSelected.setVisible(false);
    setButtonImages(dragAll, "dragAllBtn");       addAndMakeVisible(dragAll);       dragAll.setVisible(false);
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
            if (auto* rp = proc.apvts.getRawParameterValue("restDensity808"))
                restPct = clampPct(rp->load());

            int dottedPct = 0;
            if (auto* dp = proc.apvts.getRawParameterValue("dottedDensity"))
                dottedPct = clampPct(dp->load());

            int tripletPct = 0;
            if (auto* tp = proc.apvts.getRawParameterValue("tripletDensity"))
                tripletPct = clampPct(tp->load());

            const int swingPct = 0; // hook your swing slider/param here later

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
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(barsParam))
                    bars = choice->getCurrentChoiceName().getIntValue();

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

            // ---- Call database generator, convert to your processor pattern, refresh UI
            boom::drums::DrumStyleSpec spec = boom::drums::getSpec(style);
            boom::drums::DrumPattern pat;
            boom::drums::generate(spec, bars, restPct, dottedPct, tripletPct, swingPct, /*seed*/ -1, pat);

            // Convert to your processor's pattern container (row,start,len,vel @ 96 PPQ)
            auto procPat = proc.getDrumPattern();
            procPat.clearQuick();
            for (const auto& n : pat)
                procPat.add({ 0, n.row, n.startTick, n.lenTicks, n.vel }); // channel=0 per your earlier struct

            proc.setDrumPattern(procPat);
            drumGrid.setPattern(proc.getDrumPattern());
            drumGrid.repaint();
            repaint();
            return;
        }

        // Refresh both grid/roll according to engine & current patterns
        regenerate();
    };

    auto writeSplitTracksAndDragMain = [this](const BoomAudioProcessor::Pattern& pat, const juce::String& tempName)
        {
            const int ppq = 96;
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote(ppq);

            juce::HashMap<int, std::shared_ptr<juce::MidiMessageSequence>> perRow;

            auto noteForRow = [this](int row) -> int
                {
                    const auto& names = proc.getDrumRows();
                    if ((unsigned)row < (unsigned)names.size())
                    {
                        auto n = names[(int)row].toLowerCase();
                        if (n.contains("kick"))  return 36;
                        if (n.contains("snare")) return 38;
                        if (n.contains("clap"))  return 39;
                        if (n.contains("rim"))   return 37;
                        if (n.contains("open") && n.contains("hat"))   return 46;
                        if (n.contains("closed") && n.contains("hat")) return 42;
                        if (n.contains("hat"))   return 42;
                        if (n.contains("low") && n.contains("tom"))    return 45;
                        if (n.contains("mid") && n.contains("tom"))    return 47;
                        if (n.contains("high") && n.contains("tom"))   return 50;
                        if (n.contains("perc"))  return 48;
                        if (n.contains("crash")) return 49;
                        if (n.contains("ride"))  return 51;
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
                seq.addEvent(juce::MidiMessage::noteOn(10, pitch, (juce::uint8)vel), onPPQ);
                seq.addEvent(juce::MidiMessage::noteOff(10, pitch), offPPQ);
            }

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

            juce::StringArray files; files.add(tmp.getFullPathName());
            performExternalDragDropOfFiles(files, true);

            drumDragChoicesVisible = false;
            dragSelected.setVisible(false);
            dragAll.setVisible(false);
        };


    // Choice 1: ONLY enabled rows from the main drum grid
    dragSelected.onClick = [this, writeSplitTracksAndDragMain]
        {
            const auto pat = drumGrid.getPatternEnabledRows();   // the center grid’s selected rows only
            writeSplitTracksAndDragMain(pat, "BOOM_Drums_Selected");
        };

    // Choice 2: ALL rows from the main drum grid
    dragAll.onClick = [this, writeSplitTracksAndDragMain]
        {
            const auto pat = drumGrid.getPatternAllRows();
            writeSplitTracksAndDragMain(pat, "BOOM_Drums_All");
        };

    btnDragMidi.onClick = [this]
        {
            const auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
            if (engine != boom::Engine::Drums)
            {
                // Non-drum engines keep the original behavior
                juce::File f = writeTempMidiFile();
                juce::StringArray files; files.add(f.getFullPathName());
                performExternalDragDropOfFiles(files, true);
                return;
            }

            drumDragChoicesVisible = !drumDragChoicesVisible;
            dragSelected.setVisible(drumDragChoicesVisible);
            dragAll.setVisible(drumDragChoicesVisible);
        };

    // Init engine & layout
    syncVisibility();
    regenerate();

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

    syncVisibility();
}

void BoomAudioProcessorEditor::timerCallback()
{
    // Show the *effective* BPM (host when locked, UI value otherwise)
    const double bpm = proc.getHostBpm();
    bpmValue.setText(juce::String(juce::roundToInt(bpm)), juce::dontSendNotification);

    // Also keep the slider enabled/disabled in case the lock changed via automation
    const bool locked = proc.apvts.getRawParameterValue("bpmLock")->load() > 0.5f;
    bpmSlider.setEnabled(!locked);

    // read once
    static int lastNum = -1, lastDen = -1, lastBars = -1;

    const int num = proc.getTimeSigNumerator();
    const int den = proc.getTimeSigDenominator();
    const int bars = getBarsFromUI();

    bool changed = false;
    if (num != lastNum) { drumGrid.setTimeSignature(num, den); pianoRoll.setTimeSignature(num, den); changed = true; }
    if (bars != lastBars) { drumGrid.setBarsToDisplay(bars); pianoRoll.setBarsToDisplay(bars); changed = true; }

    if (changed)
    {
        drumGridView.setViewPosition(0, 0);
        pianoRollView.setViewPosition(0, 0);
        lastNum = num;
        lastDen = den;
        lastBars = bars;
    }
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
        startExternalMidiDrag();
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
    setLookAndFeel(&boomui::LNF());
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



    btnRec1.onClick = [this]() {
        auto& p = proc;
        if (!isRecRh_) {
            // Start Rhythmimick capture (Loopback)
            p.aiStartCapture(BoomAudioProcessor::CaptureSource::Loopback);
            isRecRh_ = true;
            isRecBx_ = false; // only one capture at a time
            startTimerHz(2);  // slow blink
            setButtonImages(btnRec1, "recordBtn_down"); // immediate visual feedback
        }
        else {
            // Stop Rhythmimick capture
            p.aiStopCapture(BoomAudioProcessor::CaptureSource::Loopback);
            isRecRh_ = false;
            setButtonImages(btnRec1, "recordBtn"); // back to normal
        }
        };

    btnPlay1.onClick = [this]() {
        if (proc.aiHasCapture()) {
            proc.aiPreviewStart();
        }
        };

    btnStop1.onClick = [this]() {
        // Stop any preview and make sure capture is not left armed
        proc.aiPreviewStop();
        proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Loopback);
        isRecRh_ = false;
        setButtonImages(btnRec1, "recordBtn");
        };

    btnGen1.onClick = [this] {                // Rhythmimick: analyze captured audio → drums
        proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Loopback);
        proc.aiAnalyzeCapturedToDrums(/*bars*/4, /*bpm*/120); // keep your actual args
    };

    btnGen2.onClick = [this]
    {
        int bars = 4;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(proc.apvts.getParameter("bars")))
            bars = p->get();

        proc.aiSlapsmithExpand(bars);
        miniGrid.setPattern(proc.getDrumPattern());
        miniGrid.repaint();
    };

    btnGen1.setTooltip("Generates MIDI patterns from audio you have recorded from your soundcard, depending on which engine you have selected at the top of the main window!");
    btnGen2.setTooltip("Generates MIDI patterns according to the engine you have selected at the top of the main window, and your patterns on the Slapsmith Mini Drum Grid!");
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

    blendAB.setTooltip("Blends two styles together to make interesting MIDI patterns!");


    addAndMakeVisible(rhythmSeek);
    rhythmSeek.setLookAndFeel(&boomui::LNF());
    rhythmSeek.setSliderStyle(juce::Slider::LinearHorizontal);
    rhythmSeek.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rhythmSeek.setRange(0.0, 1.0, 0.0);
    rhythmSeek.onDragStart = [this] { if (proc.aiIsPreviewing()) proc.aiPreviewStop(); };
    rhythmSeek.onValueChange = [this]
    {
        if (rhythmSeek.isMouseButtonDown() && proc.aiHasCapture())
        {
            const double sec = rhythmSeek.getValue() * proc.getCaptureLengthSeconds();
            proc.aiSeekToSeconds(sec);
        }
    };


    addAndMakeVisible(beatboxSeek);
    beatboxSeek.setLookAndFeel(&boomui::LNF());
    beatboxSeek.setSliderStyle(juce::Slider::LinearHorizontal);
    beatboxSeek.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    beatboxSeek.setRange(0.0, 1.0, 0.0);
    beatboxSeek.onDragStart = [this] { if (proc.aiIsPreviewing()) proc.aiPreviewStop(); };
    beatboxSeek.onValueChange = [this]
    {
        if (beatboxSeek.isMouseButtonDown() && proc.aiHasCapture())
        {
            const double sec = beatboxSeek.getValue() * proc.getCaptureLengthSeconds();
            proc.aiSeekToSeconds(sec);
        }
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

    btnRec4.onClick = [this]() {
        auto& p = proc;
        if (!isRecBx_) {
            // Start Beatbox capture (Microphone)
            p.aiStartCapture(BoomAudioProcessor::CaptureSource::Microphone);
            isRecBx_ = true;
            isRecRh_ = false; // only one capture at a time
            startTimerHz(2);
            setButtonImages(btnRec4, "recordBtn_down");
        }
        else {
            p.aiStopCapture(BoomAudioProcessor::CaptureSource::Microphone);
            isRecBx_ = false;
            setButtonImages(btnRec4, "recordBtn");
        }
        };


    btnPlay4.onClick = [this]() {
        if (proc.aiHasCapture()) {
            proc.aiPreviewStart();
        }
        };

    btnStop4.onClick = [this]() {
        proc.aiPreviewStop();
        proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Microphone);
        isRecBx_ = false;
        setButtonImages(btnRec4, "recordBtn");
        };


    auto hookupRow = [this](juce::ImageButton& save, juce::ImageButton& drag, const juce::String& baseFile)
    {
                save.onClick = [this]
                {
                    juce::File src = buildTempMidi("BOOM_Slapsmith.mid");
                    launchSaveMidiChooserAsync("Save MIDI...", src, "BOOM_SS.mid");
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
    setButtonImages(dragSelected2, "dragSelectedBtn");   addAndMakeVisible(dragSelected2);   dragSelected2.setVisible(false);
    setButtonImages(dragAll2, "dragAllBtn");        addAndMakeVisible(dragAll2);        dragAll2.setVisible(false);

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

            // Toggle visibility
            dragChoices2Visible = !dragChoices2Visible;
            dragSelected2.setVisible(dragChoices2Visible);
            dragAll2.setVisible(dragChoices2Visible);
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


    // Choice 1: ONLY enabled rows from the Slapsmith mini-grid
    dragSelected2.onClick = [this, writeSplitTracksAndDrag]
        {
            const auto pat = miniGrid.getPatternEnabledRows(); // reads grid’s enabled rows only
            writeSplitTracksAndDrag(pat, "BOOM_Slapsmith_Selected");
        };

    // Choice 2: ALL rows from the Slapsmith mini-grid
    dragAll2.onClick = [this, writeSplitTracksAndDrag]
        {
            const auto pat = miniGrid.getPatternAllRows();
            writeSplitTracksAndDrag(pat, "BOOM_Slapsmith_All");
        };

    hookupRow(btnSave3, btnDrag3, "BOOM_StyleBlender");
    hookupRow(btnSave4, btnDrag4, "BOOM_Beatbox");

    btnGen3.onClick = [this]
    {
        const juce::String a = styleABox.getText();
        const juce::String b = styleBBox.getText();

        int bars = 4;
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(proc.apvts.getParameter("bars")))
            bars = p->get();

        const float wA = (float)juce::jlimit(0, 100, (int)juce::roundToInt(blendAB.getValue())) / 100.0f;
        const float wB = 1.0f - wA;

        proc.aiStyleBlendDrums(a, b, bars, wA, wB);

    };

    btnGen4.onClick = [this] {                // Beatbox: analyze captured mic → drums
        proc.aiStopCapture(BoomAudioProcessor::CaptureSource::Microphone);
        proc.aiAnalyzeCapturedToDrums(/*bars*/4, /*bpm*/120);
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

    startTimerHz(26); // update 8x per second
    // ---- Shared actions (Save/Drag) use real MIDI from processor ----

}

void AIToolsWindow::timerCallback()
{

    const bool blink = ((juce::Time::getMillisecondCounter() / 400) % 2) == 0;

    if (isRecRh_) {
        setButtonImages(btnRec1, blink ? "recordBtn_down" : "recordBtn");
    }
    else {
        setButtonImages(btnRec1, "recordBtn");
    }

    if (isRecBx_) {
        setButtonImages(btnRec4, blink ? "recordBtn_down" : "recordBtn");
    }
    else {
        setButtonImages(btnRec4, "recordBtn");
    }

    // Pull fresh levels from the processor (these are atomics)
    rhL_ = proc.ai_getRhInL();
    rhR_ = proc.ai_getRhInR();
    bxL_ = proc.ai_getBxInL();
    bxR_ = proc.ai_getBxInR();

    const bool rhRec = proc.ai_isRhRecording();
    const bool bxRec = proc.ai_isBxRecording();

    // If neither is recording and not previewing, we can stop the flash timer to save CPU.
    if (!rhRec && !bxRec && !proc.aiIsPreviewing())
        stopTimer();

    // Seek bar / preview position display uses your existing helpers
    updateSeekFromProcessor();

    repaint();
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

    btnRec1.setEnabled(r);   btnStop1.setEnabled(r);   btnGen1.setEnabled(r);   btnSave1.setEnabled(r);   btnDrag1.setEnabled(r); btnPlay1.setEnabled(r);
    btnGen2.setEnabled(s);   btnSave2.setEnabled(s);   btnDrag2.setEnabled(s);  miniGrid.setEnabled(s);
    btnGen4.setEnabled(b);   btnRec4.setEnabled(b);    btnPlay4.setEnabled(b); btnStop4.setEnabled(b);  btnSave4.setEnabled(b);   btnDrag4.setEnabled(b);
    btnGen3.setEnabled(y);   btnSave3.setEnabled(y);   btnDrag3.setEnabled(y);  blendAB.setEnabled(y); styleABox.setEnabled(y); styleBBox.setEnabled(y);

    repaint();
}

AIToolsWindow::~AIToolsWindow()
{

    rhythmSeek.setLookAndFeel(nullptr);
    beatboxSeek.setLookAndFeel(nullptr);
}

void AIToolsWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());


    auto drawMeter = [&](juce::Graphics& g, juce::Rectangle<int> r, float v)
    {
        v = juce::jlimit(0.0f, 1.0f, v);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRect(r);
        const int w = juce::roundToInt(r.getWidth() * v);
        g.setColour(juce::Colours::limegreen.withAlpha(0.85f));
        g.fillRect(r.removeFromLeft(w));
    };

    drawMeter(g, { rightPanelX + 36, rhY + 52, 120, 8 }, rhL_);
    drawMeter(g, { rightPanelX + 36, rhY + 64, 120, 8 }, rhR_);
    drawMeter(g, { rightPanelX + 36, bxY + 490, 120, 8 }, bxL_);
    drawMeter(g, { rightPanelX + 36, bxY + 502, 120, 8 }, bxR_);

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
    setLookAndFeel(&boomui::LNF());
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
    setLookAndFeel(&boomui::LNF());
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

    btnBump.onClick = [this] { if (onBumpFn) onBumpFn(); };
    btnHome.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    // Melodic options only for 808/Bass layout
    showMelodicOptions = !isDrums;
    if (showMelodicOptions)
    {
        addAndMakeVisible(keyBox);    keyBox.addItemList(boom::keyChoices(), 1);     keyBox.setSelectedId(1);
        addAndMakeVisible(scaleBox);  scaleBox.addItemList(boom::scaleChoices(), 1); scaleBox.setSelectedId(1);
        addAndMakeVisible(octaveBox); octaveBox.addItemList(juce::StringArray("-2", "-1", "0", "+1", "+2"), 1); octaveBox.setSelectedId(3);
        addAndMakeVisible(barsBox);   barsBox.addItemList(juce::StringArray("1", "2", "4", "8"), 1);          barsBox.setSelectedId(3);
        keyBox.setTooltip("Choose to keep the same settings or pick new ones!");
        scaleBox.setTooltip("Choose to keep the same settings or pick new ones!");
        octaveBox.setTooltip("Choose to keep the same settings or pick new ones!");
        barsBox.setTooltip("Choose to keep the same settings or pick new ones!");
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
        scaleBox.setBounds(S(215, 180, 270, 46));
        octaveBox.setBounds(S(215, 230, 270, 46));
        barsBox.setBounds(S(215, 280, 270, 46));

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
    setLookAndFeel(&boomui::LNF());
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
    styleBox.addItemList(boom::styleChoices(), 1);
    styleBox.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(timeSigBox);
    timeSigBox.addItemList(boom::timeSigChoices(), 1);
    timeSigBox.setSelectedId(1, juce::dontSendNotification); // "4/4" in your list

    addAndMakeVisible(barsBox);
    barsBox.addItem("4", 1);
    barsBox.addItem("8", 2);
    barsBox.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(howManyBox);
    howManyBox.addItem("5", 1);
    howManyBox.addItem("25", 2);
    howManyBox.addItem("50", 3);
    howManyBox.addItem("100", 4);
    howManyBox.setSelectedId(1, juce::dontSendNotification);

    // --- triplets / dotted with your checkbox art ---
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

    // --- Save + Home ---
    addAndMakeVisible(btnSaveMidi); setButtonImages(btnSaveMidi, "saveMidiBtn");
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

    btnSaveMidi.onClick = [this]
        {
            juce::File src = buildTempMidi();
            launchSaveMidiChooserAsync("Save MIDI...", src, "BOOM_HH_MIDI.mid");
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
    hatsLbl.setBounds((W - 420) / 2, H - 260, 420, 80);
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
    btnSaveMidi.setBounds((W - 150) / 2, howManyBox.getBottom() + 40, 150, 60);
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
    setLookAndFeel(&boomui::LNF());
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

    // BARS box (Rolls-specific: 1,2,4,8)
    barsBox.clear();
    barsBox.addItem("1", 1);
    barsBox.addItem("2", 2);
    barsBox.addItem("4", 3);
    barsBox.addItem("8", 4);
    barsBox.setSelectedId(3, juce::dontSendNotification); // default to "4"

    addAndMakeVisible(timeSigBox);
    timeSigBox.addItemList(boom::timeSigChoices(), 1);

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
    addAndMakeVisible(btnSaveMidi); setButtonImages(btnSaveMidi, "saveMidiBtn");
    addAndMakeVisible(btnDragMidi); setButtonImages(btnDragMidi, "dragBtn");

    diceBtn.setTooltip("Randomizes the parameteres in the boxes on the left and the humanization sliders on the right. Then just press GENERATE, and BOOM, random fun!");
    barsBox.setTooltip("Choose how long you want your drumroll midi to be.");
    styleBox.setTooltip("Choose your drumroll style.");
    timeSigBox.setTooltip("Choose your drumroll's time signature.");
    btnGenerate.setTooltip("Generate your midi drumroll.");
    btnSaveMidi.setTooltip("Choose where to save your drumroll midi file.");
    btnDragMidi.setTooltip("Drag your drumroll midi to your DAW.");
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


    btnGenerate.onClick = [this]
    {
        const juce::String style = styleBox.getText();

        int bars = 4;
        switch (barsBox.getSelectedId())
        {
        case 1: bars = 1; break;
        case 2: bars = 2; break;
        case 3: bars = 4; break;
        case 4: bars = 8; break;
        default: break;
        }

        proc.generateRolls(style, bars, /*seed*/ -1);

        miniGrid.setPattern(proc.getDrumPattern());
        miniGrid.repaint();
    };

    btnSaveMidi.onClick = [this]
        {
            juce::File src = buildTempMidi();
            launchSaveMidiChooserAsync("Save MIDI...", src, "BOOM_Rolls.mid");
        };


    
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
    int labelY = titleImageHeight + 30;
    int boxY = labelY + labelHeight + verticalSpacing;

    // Time Sig
    timeSigLbl.setBounds(currentX, labelY, itemWidth, labelHeight);
    timeSigBox.setBounds(currentX, boxY, itemWidth, comboBoxHeight);
    currentX += itemWidth + horizontalSpacing;

    // Bars
    barsLbl.setBounds(currentX, labelY, itemWidth, labelHeight);
    barsBox.setBounds(currentX, boxY, itemWidth, comboBoxHeight);
    currentX += itemWidth + horizontalSpacing;

    // Style
    styleLbl.setBounds(currentX, labelY, itemWidth, labelHeight);
    styleBox.setBounds(currentX, boxY, itemWidth, comboBoxHeight);

    // 3. Buttons row
    const int generateButtonWidth = 190;
    const int otherButtonWidth = 150;
    const int buttonHeight = 50;
    const int buttonSpacing = 20;
    const int totalButtonWidth = generateButtonWidth + otherButtonWidth * 2 + buttonSpacing * 2;
    int x_buttons = (W - totalButtonWidth) / 2;
    int y_buttons = boxY + comboBoxHeight + 30; // 30px space after combos
    btnGenerate.setBounds(x_buttons, y_buttons, generateButtonWidth, buttonHeight);
    x_buttons += generateButtonWidth + buttonSpacing;
    btnSaveMidi.setBounds(x_buttons, y_buttons, otherButtonWidth, buttonHeight);
    x_buttons += otherButtonWidth + buttonSpacing;
    btnDragMidi.setBounds(x_buttons, y_buttons, otherButtonWidth, buttonHeight);
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
