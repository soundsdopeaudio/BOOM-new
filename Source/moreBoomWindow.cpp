#include "PluginEditor.h"
#include "Theme.h"

// moreBoomWindow implementation
moreBoomWindow::moreBoomWindow(BoomAudioProcessor& p, std::function<void()> onClose)
    : proc(p), onCloseFn(std::move(onClose))
{
    setSize(700, 500);

    // Helper lambdas for APVTS interaction
    auto safeCreateButton = [&](const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& out, juce::Button& b)
        {
            if (proc.apvts.getParameter(id) != nullptr)
                out = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, b);
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

    auto safeCreateCombo = [&](const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& out, juce::ComboBox& cb)
        {
            if (proc.apvts.getParameter(id) != nullptr)
                out = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, id, cb);
            else
                juce::Logger::writeToLog(juce::String("APVTS parameter missing: ") + id);
        };

    auto setApvtsBool = [this](const char* paramId, bool v)
        {
            const float normalized = v ? 1.0f : 0.0f;
            if (auto* p = proc.apvts.getParameter(paramId))
            {
                p->setValueNotifyingHost(normalized);
                return;
            }
            if (auto* raw = proc.apvts.getRawParameterValue(paramId))
            {
                raw->store(normalized);
                return;
            }
            DBG("setApvtsBool: WARNING - parameter not found for id='" << paramId << "'");
        };

    // Title label
    addAndMakeVisible(moreBoomLbl);
    moreBoomLbl.setImage(loadSkin("moreBoomLbl.png"));
    moreBoomLbl.setInterceptsMouseClicks(false, false);

    // GHXSTGRID controls
    addAndMakeVisible(ghxstGridIconLbl);
    ghxstGridIconLbl.setImage(loadSkin("ghxstGridIconLbl.png"));
    ghxstGridLbl.setImage(loadSkin("ghxstgridLbl.png")); 
    addAndMakeVisible(ghxstGridLbl);
    addAndMakeVisible(ghxstToggle);
    ghxstToggle.setClickingTogglesState(true);
    setToggleImages(ghxstToggle, "checkBoxOffBtn", "checkBoxOnBtn");
    addAndMakeVisible(ghxstIntensity);
    ghxstIntensity.setSliderStyle(juce::Slider::LinearHorizontal);
    ghxstIntensity.setRange(0, 100, 1);
    ghxstIntensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    boomui::makePercentSlider(ghxstIntensity);

    // BounceSync UI
    addAndMakeVisible(bounceSyncIconLbl);
    bounceSyncIconLbl.setImage(loadSkin("bounceSyncIconLbl.png"));
    bounceSyncLblImg.setImage(loadSkin("bounceSyncLbl.png")); 
    addAndMakeVisible(bounceSyncLblImg);
    addAndMakeVisible(bounceSyncToggle);
    bounceSyncToggle.setClickingTogglesState(true);
    setToggleImages(bounceSyncToggle, "checkBoxOffBtn", "checkBoxOnBtn");

    addAndMakeVisible(bounceSyncStrength);
    bounceSyncStrength.addItem("Light", 1);
    bounceSyncStrength.addItem("Medium", 2);
    bounceSyncStrength.addItem("Hard", 3);
    bounceSyncStrength.setJustificationType(juce::Justification::centredLeft);
    bounceSyncStrength.setScrollWheelEnabled(false);
    bounceSyncStrength.setTooltip("BounceSync strength (LITE / MED / HARD)");

    bounceSyncStrength.onChange = [this, setApvtsBool]()
        {
            if (auto* p = proc.apvts.getParameter("bouncesync_strength"))
                p->setValueNotifyingHost((float)(bounceSyncStrength.getSelectedId() - 1));
        };

    // NegSpace UI creation
    addAndMakeVisible(negSpaceIconLbl);
    negSpaceIconLbl.setImage(boomui::loadSkin("negSpaceIconLbl.png"));
    addAndMakeVisible(negSpaceLblImg);
    negSpaceLblImg.setImage(boomui::loadSkin("negSpaceLbl.png"));
    addAndMakeVisible(negSpaceToggle);
    negSpaceToggle.setClickingTogglesState(true);
    setToggleImages(negSpaceToggle, "checkBoxOffBtn", "checkBoxOnBtn");
    addAndMakeVisible(negSpaceGapSlider);
    negSpaceGapSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    negSpaceGapSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    boomui::makePercentSlider(negSpaceGapSlider);

    // TripFlip UI creation
    addAndMakeVisible(tripFlipIconLbl);
    tripFlipIconLbl.setImage(boomui::loadSkin("tripFlipIconLbl.png"));
    addAndMakeVisible(tripFlipLblImg);
    tripFlipLblImg.setImage(boomui::loadSkin("tripFlipLbl.png"));
    addAndMakeVisible(tripFlipModeBox);
    tripFlipModeBox.addItem("Off", 1);
    tripFlipModeBox.addItem("Light", 2);
    tripFlipModeBox.addItem("Normal", 3);
    tripFlipModeBox.addItem("Aggressive", 4);
    addAndMakeVisible(tripFlipDensity);
    tripFlipDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    tripFlipDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    boomui::makePercentSlider(tripFlipDensity);

    // PolyGod UI creation
    addAndMakeVisible(polyGodIconLbl);
    polyGodIconLbl.setImage(boomui::loadSkin("polyGodIconLbl.png"));
    addAndMakeVisible(polyGodLblImg);
    polyGodLblImg.setImage(boomui::loadSkin("polyGodLbl.png"));
    addAndMakeVisible(polyGodToggle);
    polyGodToggle.setClickingTogglesState(true);
    setToggleImages(polyGodToggle, "checkBoxOffBtn", "checkBoxOnBtn");
    addAndMakeVisible(polyGodRatioBox);
    polyGodRatioBox.addItem("3:4", 1);
    polyGodRatioBox.addItem("5:4", 2);
    polyGodRatioBox.addItem("7:4", 3);
    polyGodRatioBox.addItem("3:2", 4);
    polyGodRatioBox.addItem("5:3", 5);

    // Scatter UI creation
    addAndMakeVisible(scatterIconLbl);
    scatterIconLbl.setImage(boomui::loadSkin("scatterIconLbl.png"));
    addAndMakeVisible(scatterLblImg);
    scatterLblImg.setImage(boomui::loadSkin("scatterLbl.png"));
    addAndMakeVisible(scatterBtn);
    scatterBtn.setClickingTogglesState(true);
    setToggleImages(scatterBtn, "checkBoxOffBtn", "checkBoxOnBtn");
    addAndMakeVisible(scatterDepthSlider);
    scatterDepthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    scatterDepthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    boomui::makePercentSlider(scatterDepthSlider);
    addAndMakeVisible(scatterDensityBox);
    scatterDensityBox.addItem("Mild", 1);
    scatterDensityBox.addItem("Normal", 2);
    scatterDensityBox.addItem("Spicy", 3);
    scatterDensityBox.setJustificationType(juce::Justification::centredLeft);
    scatterDensityBox.setScrollWheelEnabled(false);

    // Home button
    addAndMakeVisible(btnHome);
    setButtonImages(btnHome, "homeBtn");

    // onClick handlers
    ghxstToggle.onClick = [this, setApvtsBool]()
        {
            const bool newState = ghxstToggle.getToggleState();
            setApvtsBool("mode_GHXSTGRID", newState);
            ghxstIntensity.setEnabled(newState);
            repaint();
        };

    negSpaceToggle.onClick = [this]()
        {
            const bool v = negSpaceToggle.getToggleState();
            if (auto* p = proc.apvts.getParameter("mode_NegSpace"))
                p->setValueNotifyingHost(v ? 1.0f : 0.0f);
            negSpaceGapSlider.setEnabled(v);
            repaint();
        };

    tripFlipModeBox.onChange = [this]()
        {
            const int idx = tripFlipModeBox.getSelectedId() - 1; // 0=Off,1=Light,2=Normal,3=Aggressive
            const bool enabled = (idx > 0);
            tripFlipDensity.setEnabled(enabled);
            repaint();
        };

    polyGodToggle.onClick = [this]()
        {
            const bool v = polyGodToggle.getToggleState();
            if (auto* p = proc.apvts.getParameter("mode_PolyGod"))
                p->setValueNotifyingHost(v ? 1.0f : 0.0f);
            polyGodRatioBox.setEnabled(v);
        };

    scatterBtn.onClick = [this]()
        {
            const bool v = scatterBtn.getToggleState();
            if (auto* p = proc.apvts.getParameter("mode_Scatter"))
                p->setValueNotifyingHost(v ? 1.0f : 0.0f);
            scatterDepthSlider.setEnabled(v);
            scatterDensityBox.setEnabled(v);
            repaint();
        };

    btnHome.onClick = [this]()
        {
            // Find and close the parent DialogWindow properly
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
            // Also notify parent callback for cleanup
            if (onCloseFn)
                onCloseFn();
        };

    bounceSyncToggle.onClick = [this]()
        {
            const bool newState = bounceSyncToggle.getToggleState();
            bounceSyncStrength.setEnabled(newState);
        };

    // Create attachments using safe helpers
    safeCreateButton("mode_GHXSTGRID", ghxstToggleAtt, ghxstToggle);
    safeCreateSlider("ghxst_intensity", ghxstIntensityAtt, ghxstIntensity);
    safeCreateButton("mode_BounceSync", bounceSyncToggleAtt, bounceSyncToggle);
    safeCreateCombo("bouncesync_strength", bounceSyncStrengthAtt, bounceSyncStrength);
    safeCreateButton("mode_NegSpace", negSpaceToggleAtt, negSpaceToggle);
    safeCreateSlider("negspace_gapPct", negSpaceGapAtt, negSpaceGapSlider);
    // TripFlip: mode_TripFlip is a CHOICE param (Off/Light/Normal/Aggressive).
    // So: ComboBoxAttachment -> "mode_TripFlip". The toggle is just UI sugar (not attached).
    safeCreateCombo("mode_TripFlip", tripFlipModeAtt, tripFlipModeBox);
    safeCreateSlider("tripflip_density", tripFlipDensityAtt, tripFlipDensity);
    safeCreateButton("mode_PolyGod", polyGodToggleAtt, polyGodToggle);
    safeCreateCombo("polygod_ratio", polyGodRatioAtt, polyGodRatioBox);
    safeCreateButton("mode_Scatter", scatterToggleAtt, scatterBtn);
    safeCreateSlider("scatter_depth", scatterDepthAtt, scatterDepthSlider);
    safeCreateCombo("scatter_density", scatterDensityAtt, scatterDensityBox);

    // Initialize enabled states from APVTS
    const bool ghxOn = proc.apvts.getRawParameterValue("mode_GHXSTGRID")->load() > 0.5f;
    ghxstToggle.setToggleState(ghxOn, juce::dontSendNotification);
    ghxstIntensity.setEnabled(ghxOn);

    const bool scatterOn = proc.apvts.getRawParameterValue("mode_Scatter")->load() > 0.5f;
    scatterBtn.setToggleState(scatterOn, juce::dontSendNotification);
    scatterDepthSlider.setEnabled(scatterOn);
    scatterDensityBox.setEnabled(scatterOn);

    ghxstToggle.setTooltip(
        "GHXSTGRID introduces controlled rhythmic ghosting by subtly shifting, duplicating, or omitting hits to create darker, more unstable grooves."
    );
    bounceSyncToggle.setTooltip(
        "BounceSync applies rhythmic push-and-pull timing to hits, creating a bouncing feel that enhances groove and movement."
    );
    negSpaceToggle.setTooltip(
        "NegSpace removes expected hits to create space, silence, and breathing room in the rhythm for a looser, more expressive feel."
    );
    tripFlipModeBox.setTooltip(
        "TripFlip blends straight and triplet rhythms, flipping between them to create hybrid grooves and unexpected rhythmic variations."
    );
    polyGodToggle.setTooltip(
        "PolyGod overlays a secondary polyrhythm on top of the main groove, creating complex rhythmic tension and motion."
    );
    scatterDepthSlider.setTooltip(
        "Scatter randomly distributes percussion hits across the grid to create chaotic, energetic, and unpredictable rhythmic textures."
    );


    // Set look and feel to nullptr to use default
    ghxstIntensity.setLookAndFeel(nullptr);
    tripFlipDensity.setLookAndFeel(nullptr);
    negSpaceGapSlider.setLookAndFeel(nullptr);
    scatterDepthSlider.setLookAndFeel(nullptr);
}

moreBoomWindow::~moreBoomWindow()
{
    ghxstIntensity.setLookAndFeel(nullptr);
    tripFlipDensity.setLookAndFeel(nullptr);
    negSpaceGapSlider.setLookAndFeel(nullptr);
    scatterDepthSlider.setLookAndFeel(nullptr);
}

void moreBoomWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void moreBoomWindow::resized()
{
    const float W = 700.f, H = 500.f;
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

    // Position title label at top center
    {
        auto img = moreBoomLbl.getImage();
        const int titleW = juce::roundToInt(img.getWidth() * sx);
        const int titleH = juce::roundToInt(img.getHeight() * sy);
        const int titleX = (bounds.getWidth() - titleW) / 2;
        const int titleY = juce::roundToInt(15 * sy);
        moreBoomLbl.setBounds(titleX, titleY, titleW, titleH);
    }

    // New layout: toggle/combo -> label -> icon for each row
    const int toggleX = 50;
    const int lblX = 90;
    const int iconX = 280;
    const int controlX = 360;  // moved right to accommodate larger icons
    const int toggleSize = 24;
    const int lblWidth = 180;
    const int iconSize = 48;  // increased from 32 to 48 for better visibility
    const int controlWidth = 200;
    const int rowHeight = 70;
    int y = 95;  // increased from 80 to add space below title

    // 1. GHXSTGRID: toggle -> label -> icon -> slider
    ghxstToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    ghxstGridLbl.setBounds(S(lblX, y, lblWidth, 24));
    ghxstGridIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    ghxstIntensity.setBounds(S(controlX, y + 5, controlWidth, 20));
    y += rowHeight;

    // 2. BounceSync: toggle -> label -> icon -> combobox
    bounceSyncToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    bounceSyncLblImg.setBounds(S(lblX, y, lblWidth, 24));
    bounceSyncIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    bounceSyncStrength.setBounds(S(controlX, y, 120, 24));
    y += rowHeight;

    // 3. NegSpace: toggle -> label -> icon -> slider
    negSpaceToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    negSpaceLblImg.setBounds(S(lblX, y, lblWidth, 24));
    negSpaceIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    negSpaceGapSlider.setBounds(S(controlX, y + 5, controlWidth, 20));
    y += rowHeight;

    // 4. TripFlip: toggle -> label -> icon -> combo + slider
    tripFlipToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    tripFlipLblImg.setBounds(S(lblX, y, lblWidth, 24));
    tripFlipIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    tripFlipModeBox.setBounds(S(controlX, y, 100, 24));
    tripFlipDensity.setBounds(S(controlX + 110, y + 5, 90, 20));
    y += rowHeight;

    // 5. PolyGod: toggle -> label -> icon -> combobox
    polyGodToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    polyGodLblImg.setBounds(S(lblX, y, lblWidth, 24));
    polyGodIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    polyGodRatioBox.setBounds(S(controlX, y, 120, 24));
    y += rowHeight;

    // 6. Scatter: toggle -> label -> icon -> combo + slider
    scatterBtn.setBounds(S(toggleX, y, toggleSize, toggleSize));
    scatterLblImg.setBounds(S(lblX, y, lblWidth, 24));
    scatterIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    scatterDensityBox.setBounds(S(controlX, y, 100, 24));
    scatterDepthSlider.setBounds(S(controlX + 110, y + 5, 90, 20));
    y += rowHeight;

    // Home button at bottom right corner
    btnHome.setBounds(S(600, 440, 80, 40));
}
