#include "PluginEditor.h"
#include "Theme.h"

// moreBoomWindow implementation
moreBoomWindow::moreBoomWindow(BoomAudioProcessor& p, std::function<void()> onClose)
    : proc(p), onCloseFn(std::move(onClose))
{
    setSize(700, 700);
    
    // Create tooltip window so tooltips actually show up
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 700);

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
        auto titleImg = loadSkin("moreBoomLbl.png");
        if (titleImg.isValid())
            moreBoomLbl.setImage(titleImg);
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
        
        // ---------------- New moreBoom (Drums-only) features ----------------

    // GlitchSwitch (combo)
    addAndMakeVisible(glitchSwitchIconLbl);
    glitchSwitchIconLbl.setImage(loadSkin("glitchswitchIconLbl.png"));
    addAndMakeVisible(glitchSwitchLbl);
    glitchSwitchLbl.setImage(loadSkin("glitchswitchLbl.png"));
    addAndMakeVisible(glitchSwitchModeBox);
    glitchSwitchModeBox.addItem("OFF", 1);
    glitchSwitchModeBox.addItem("ON", 2);
    glitchSwitchModeBox.addItem("ON+REGEN", 3);
    glitchSwitchModeBox.setJustificationType(juce::Justification::centredLeft);
    glitchSwitchModeBox.setScrollWheelEnabled(false);
    glitchSwitchModeBox.setTooltip("GlitchSwitch adds fast glitch rolls. ON preserves the existing pattern; ON+REGEN regenerates the pattern and then adds glitch rolls.");

    // Holy Rollie (combo box: OFF / ON / ON+REGEN)
    addAndMakeVisible(holyRollieIconLbl);
    holyRollieIconLbl.setImage(loadSkin("holyRollieIconLbl.png"));
    addAndMakeVisible(holyRollieLbl);
    holyRollieLbl.setImage(loadSkin("holyRollieLbl.png"));
    addAndMakeVisible(holyRollieModeBox);
    holyRollieModeBox.addItem("OFF", 1);
    holyRollieModeBox.addItem("ON", 2);
    holyRollieModeBox.addItem("ON+REGEN", 3);
    holyRollieModeBox.setJustificationType(juce::Justification::centredLeft);
    holyRollieModeBox.setScrollWheelEnabled(false);
    holyRollieModeBox.setTooltip("Holy Rollie replaces one drum row (usually snare) with a dedicated roll passage while keeping all other rows the same.");

    // -2 In, Drop Out (checkbox)
    addAndMakeVisible(twoInDropOutIconLbl);
    twoInDropOutIconLbl.setImage(loadSkin("twoInDropOutIconLbl.png"));
    addAndMakeVisible(twoInDropOutLbl);
    twoInDropOutLbl.setImage(loadSkin("twoInDropOutLbl.png"));
    addAndMakeVisible(twoInDropOutToggle);
    twoInDropOutToggle.setClickingTogglesState(true);
    setToggleImages(twoInDropOutToggle, "checkBoxOffBtn", "checkBoxOnBtn");
        twoInDropOutToggle.setTooltip("-2 In, Drop Out drops up to two rows completely and regenerates up to two other rows completely.");

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
            tripFlipDensity.setAlpha(enabled ? 1.0f : 0.5f);
            repaint();
        };

    polyGodToggle.onClick = [this]()
        {
            const bool v = polyGodToggle.getToggleState();
            if (auto* p = proc.apvts.getParameter("mode_PolyGod"))
                p->setValueNotifyingHost(v ? 1.0f : 0.0f);
            polyGodRatioBox.setEnabled(v);
            polyGodRatioBox.setAlpha(v ? 1.0f : 0.5f);
        };

    scatterBtn.onClick = [this]()
        {
            const bool v = scatterBtn.getToggleState();
            if (auto* p = proc.apvts.getParameter("mode_Scatter"))
                p->setValueNotifyingHost(v ? 1.0f : 0.0f);
            scatterDepthSlider.setEnabled(v);
            scatterDensityBox.setEnabled(v);
            scatterDensityBox.setAlpha(v ? 1.0f : 0.5f);
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
            bounceSyncStrength.setAlpha(newState ? 1.0f : 0.5f);
        };

    // New feature handlers

    glitchSwitchModeBox.onChange = [this]()
        {
            // ComboBoxAttachment already writes to APVTS; we just repaint for immediate UI feedback
            repaint();
        };

    holyRollieModeBox.onChange = [this]()
        {
            // ComboBoxAttachment already writes to APVTS; we just repaint for immediate UI feedback
            repaint();
        };

    twoInDropOutToggle.onClick = [this]()
        {
            const bool v = twoInDropOutToggle.getToggleState();
            if (auto* p = proc.apvts.getParameter("mode_TwoInDropOut"))
                p->setValueNotifyingHost(v ? 1.0f : 0.0f);
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
    // New feature attachments
    safeCreateCombo("glitchswitch_mode", glitchSwitchModeAtt, glitchSwitchModeBox);
    safeCreateCombo("holyrollie_mode", holyRollieModeAtt, holyRollieModeBox);
        safeCreateButton("mode_TwoInDropOut", twoInDropOutToggleAtt, twoInDropOutToggle);

    // Initialize enabled states from APVTS with null checks
    if (auto* ghxParam = proc.apvts.getRawParameterValue("mode_GHXSTGRID"))
    {
        const bool ghxOn = ghxParam->load() > 0.5f;
        ghxstToggle.setToggleState(ghxOn, juce::dontSendNotification);
        ghxstIntensity.setEnabled(ghxOn);
    }

    if (auto* bounceSyncParam = proc.apvts.getRawParameterValue("mode_BounceSync"))
    {
        const bool bounceSyncOn = bounceSyncParam->load() > 0.5f;
        bounceSyncToggle.setToggleState(bounceSyncOn, juce::dontSendNotification);
        bounceSyncStrength.setEnabled(bounceSyncOn);
        bounceSyncStrength.setAlpha(bounceSyncOn ? 1.0f : 0.5f);
    }

    if (auto* polyGodParam = proc.apvts.getRawParameterValue("mode_PolyGod"))
    {
        const bool polyGodOn = polyGodParam->load() > 0.5f;
        polyGodToggle.setToggleState(polyGodOn, juce::dontSendNotification);
        polyGodRatioBox.setEnabled(polyGodOn);
        polyGodRatioBox.setAlpha(polyGodOn ? 1.0f : 0.5f);
    }

    if (auto* scatterParam = proc.apvts.getRawParameterValue("mode_Scatter"))
    {
        const bool scatterOn = scatterParam->load() > 0.5f;
        scatterBtn.setToggleState(scatterOn, juce::dontSendNotification);
        scatterDepthSlider.setEnabled(scatterOn);
        scatterDensityBox.setEnabled(scatterOn);
        scatterDensityBox.setAlpha(scatterOn ? 1.0f : 0.5f);
    }

    // Initialize TripFlip density slider state based on mode
    if (auto* tripFlipParam = proc.apvts.getRawParameterValue("mode_TripFlip"))
    {
        const int tripFlipMode = (int)tripFlipParam->load();
        const bool enabled = (tripFlipMode > 0); // Off=0, Light=1, Normal=2, Aggressive=3
        tripFlipDensity.setEnabled(enabled);
        tripFlipDensity.setAlpha(enabled ? 1.0f : 0.5f);
    }

    if (auto* v = proc.apvts.getRawParameterValue("glitchswitch_mode"))
        glitchSwitchModeBox.setSelectedId((int)v->load() + 1, juce::dontSendNotification);
    else
        glitchSwitchModeBox.setSelectedId(1, juce::dontSendNotification);

    if (auto* holyParam = proc.apvts.getRawParameterValue("holyrollie_mode"))
        holyRollieModeBox.setSelectedId((int)holyParam->load() + 1, juce::dontSendNotification);
    else
        holyRollieModeBox.setSelectedId(1, juce::dontSendNotification);

    if (auto* twoInParam = proc.apvts.getRawParameterValue("mode_TwoInDropOut"))
    {
        const bool twoInOn = twoInParam->load() > 0.5f;
            twoInDropOutToggle.setToggleState(twoInOn, juce::dontSendNotification);
        }

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
    const float W = 700.f, H = 700.f;  // Updated height to match new window size
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

    // Position title label at top center - GUARD AGAINST INVALID IMAGE
    {
        auto img = moreBoomLbl.getImage();
        if (img.isValid())
        {
            const int titleW = juce::roundToInt(img.getWidth() * sx);
            const int titleH = juce::roundToInt(img.getHeight() * sy);
            const int titleX = (bounds.getWidth() - titleW) / 2;
            const int titleY = juce::roundToInt(15 * sy);
            moreBoomLbl.setBounds(titleX, titleY, titleW, titleH);
        }
        else
        {
            // Fallback if image is invalid
            moreBoomLbl.setBounds(S(200, 15, 300, 40));
        }
    }

    // New layout: toggle/combo -> label -> icon for each row
    const int toggleX = 50;
    const int lblX = 90;
    const int iconX = 280;
    const int controlX = 360;
    const int toggleSize = 24;
    const int lblWidth = 180;
    const int lblHeight = 24;  // Standardized label height
    const int iconSize = 48;   // Standardized icon size
    const int controlWidth = 200;  // Width for sliders
    const int rowHeight = 70;
    int y = 95;

    // 1. GHXSTGRID: toggle -> label -> icon -> slider
    ghxstToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    ghxstGridLbl.setBounds(S(lblX, y, lblWidth, lblHeight));
    ghxstGridIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    ghxstIntensity.setBounds(S(controlX, y + 5, controlWidth, 20));
    y += rowHeight;

    // 2. BounceSync: toggle -> label -> icon -> combobox
    bounceSyncToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    bounceSyncLblImg.setBounds(S(lblX, y, lblWidth, lblHeight));
    bounceSyncIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    bounceSyncStrength.setBounds(S(controlX, y, 120, 24));
    y += rowHeight;

    // 3. NegSpace: toggle -> label -> icon -> slider
    negSpaceToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    negSpaceLblImg.setBounds(S(lblX, y, lblWidth, lblHeight));
    negSpaceIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    negSpaceGapSlider.setBounds(S(controlX, y + 5, controlWidth, 20));
    y += rowHeight;

    // 4. TripFlip: label -> icon -> combo + slider (no toggle - uses combo box instead)
    tripFlipLblImg.setBounds(S(lblX, y, lblWidth, lblHeight));
    tripFlipIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    tripFlipModeBox.setBounds(S(controlX, y, 100, 24));
    tripFlipDensity.setBounds(S(controlX + 110, y + 5, 90, 20));
    y += rowHeight;

    // 5. PolyGod: toggle -> label -> icon -> combobox
    polyGodToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    polyGodLblImg.setBounds(S(lblX, y, lblWidth, lblHeight));
    polyGodIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    polyGodRatioBox.setBounds(S(controlX, y, 120, 24));
    y += rowHeight;

    // 6. Scatter: toggle -> label -> icon -> combo + slider
    scatterBtn.setBounds(S(toggleX, y, toggleSize, toggleSize));
    scatterLblImg.setBounds(S(lblX, y, lblWidth, lblHeight));
    scatterIconLbl.setBounds(S(iconX, y, iconSize, iconSize));
    scatterDensityBox.setBounds(S(controlX, y, 100, 24));
    scatterDepthSlider.setBounds(S(controlX + 110, y + 5, 90, 20));
    y += rowHeight;

    // 7. GlitchSwitch: label -> icon -> combo (no toggle on left)
    glitchSwitchLbl.setBounds(S(lblX, y, lblWidth - 2, lblHeight - 2));
    glitchSwitchIconLbl.setBounds(S(iconX, y, iconSize - 2, iconSize - 2));
    glitchSwitchModeBox.setBounds(S(controlX, y, 140, 24));
    y += 60;

    // 8. Holy Rollie: label -> icon -> combo (no toggle on left)
    holyRollieLbl.setBounds(S(lblX, y, lblWidth - 2, lblHeight - 2));
    holyRollieIconLbl.setBounds(S(iconX, y, iconSize - 2, iconSize - 2));
    holyRollieModeBox.setBounds(S(controlX, y, 140, 24));
    y += 55;

    // 9. -2 In, Drop Out: toggle -> label -> icon (reduced row height since no right control)
    twoInDropOutToggle.setBounds(S(toggleX, y, toggleSize, toggleSize));
    twoInDropOutLbl.setBounds(S(lblX, y, lblWidth - 2, lblHeight - 2));
    twoInDropOutIconLbl.setBounds(S(iconX, y, iconSize - 2, iconSize - 2));
    y += 55;

    // Home button at bottom right corner (updated Y position for larger window)
    btnHome.setBounds(S(600, 640, 80, 40));
}
