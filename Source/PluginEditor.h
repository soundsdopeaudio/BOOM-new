#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EngineDefs.h"
#include "DrumGridComponent.h"
#include "PianoRollComponent.h"
#include "MidiUtils.h"
#include "FlipUtils.h"
#include "PatternAdapters.h"
#include <cstdint>
#include <functional>

// === shared UI helpers (inline so header users can see/inline them) ===
namespace boomui
{
    inline juce::Image loadSkin(const juce::String& fileName)
    {
        // try ../Resources/<file>, then CWD
        auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        auto res1 = exeDir.getChildFile("Resources").getChildFile(fileName);
        if (res1.existsAsFile()) return juce::ImageFileFormat::loadFrom(res1);

        auto res2 = juce::File::getCurrentWorkingDirectory().getChildFile(fileName);
        if (res2.existsAsFile()) return juce::ImageFileFormat::loadFrom(res2);

        return {};
    }

    inline void setButtonImages(juce::ImageButton& b, const juce::String& baseNoExt)
    {
        b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        b.setImages(false, true, true,
            loadSkin(baseNoExt + ".png"), 1.0f, juce::Colour(),
            loadSkin(baseNoExt + "_hover.png"), 1.0f, juce::Colour(),
            loadSkin(baseNoExt + "_down.png"), 1.0f, juce::Colour());
    }

    inline void setToggleImages(juce::ImageButton& b,
        const juce::String& offBaseNoExt,
        const juce::String& onBaseNoExt)
    {
        auto apply = [&b, offBaseNoExt, onBaseNoExt]()
        {
            const auto& base = b.getToggleState() ? onBaseNoExt : offBaseNoExt;
            setButtonImages(b, base);
        };
        b.setClickingTogglesState(true);
        b.onStateChange = apply;
        apply();
    }
} // namespace boomui

// Helper: always create & launch FileChooser on the message thread so dialogs reliably appear
static void launchSaveMidiChooserAsync(const juce::String& dialogTitle,
    const juce::File& srcTemp,
    const juce::String& defaultFileName)
{
    // Post to the message thread to be safe in plugin hosts that are picky about dialogs
    juce::MessageManager::callAsync([dialogTitle, srcTemp, defaultFileName]()
        {
            DBG("launchSaveMidiChooserAsync: opening chooser for " + defaultFileName);

            // Build default file location
            juce::File defaultFile = srcTemp.getParentDirectory().getChildFile(defaultFileName);

            // Allocate on heap to ensure it survives until the async callback runs / completes
            auto* fc = new juce::FileChooser(dialogTitle, defaultFile, "*.mid");

            // launchAsync takes a callback executed when user finishes or cancels
            fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [fc, srcTemp](const juce::FileChooser& chooser)
                {
                    juce::File dest = chooser.getResult();
                    if (dest.getFullPathName().isEmpty())
                    {
                        DBG("launchSaveMidiChooserAsync: user cancelled or empty result");
                        delete fc;
                        return;
                    }

                    if (!dest.hasFileExtension(".mid"))
                        dest = dest.withFileExtension(".mid");

                    if (dest.existsAsFile())
                        dest.deleteFile();

                    const bool copied = srcTemp.copyFileTo(dest);
                    DBG("launchSaveMidiChooserAsync: copied temp -> " + dest.getFullPathName() + (copied ? " (OK)" : " (FAILED)"));
                    delete fc;
                });
        });
}

namespace boomui
{
    inline void setButtonImagesSelected(juce::ImageButton& b, const juce::String& baseNoExt)
    {
        // Force the _down image in *all* states to keep the selected visual
        b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        auto imgDown = loadSkin(baseNoExt + "_down.png");
        b.setImages(false, true, true,
            imgDown, 1.0f, juce::Colour(),
            imgDown, 1.0f, juce::Colour(),
            imgDown, 1.0f, juce::Colour());
    }
}
using boomui::setButtonImagesSelected;

// ----- Purple, thick, outlined slider look -----
// ----- Purple, thick, outlined slider look -----
// ----- Purple, thick, outlined slider look -----

// bring into current namespace for existing calls
using boomui::loadSkin;
using boomui::setButtonImages;
using boomui::setToggleImages;

class FlippitWindow;
class BumppitWindow;
class RollsWindow;
class AIToolsWindow;
class HatsWindow;
class BpmPoller; 

class BoomAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::DragAndDropContainer,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit BoomAudioProcessorEditor(BoomAudioProcessor& p);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    ~BoomAudioProcessorEditor() override;

    juce::ImageComponent lockToBpmLbl;
    juce::ImageComponent bpmLbl;
    juce::ImageButton    bpmLockChk;
    juce::Label          bpmValueLbl;
    juce::ImageButton dragSelected, dragAll;
    bool drumDragChoicesVisible = false;
// shows the current BPM number next to the label

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override; // start file-drag from dragBtn

    // --- NEW: scrollable wrappers ---
    juce::Viewport drumGridView;
    juce::Viewport pianoRollView;

    // APVTS attachments (no lambdas here)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> timeSigAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> barsAtt;

    // helper to push current param values into the views
    void updateTimeSigAndBars();

private:
    std::unique_ptr<BpmPoller> bpmPoller;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    void timerCallback();
    // Layout helpers
    static int barsFromBox(const juce::ComboBox& b);
    // Attachment for the checkbox <-> apvts param
    using BAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<BAttachment> bpmLockAtt;
    juce::Slider bpmSlider;
    juce::Label  bpmValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt;
    // Timer tick used to refresh the BPM tex
    void setEngine(boom::Engine e);
    void syncVisibility();
    void regenerate();
    void startExternalMidiDrag();
    PurpleSliderLNF purpleLNF;
 // temporary placeholder type
    int getBarsFromUI() const;   // <-- add this

    void toggleDrumCell(int row, int tick);
    BoomAudioProcessor::DrumPattern makeDemoPatternDrums(int bars) const;
    BoomAudioProcessor::MelPattern makeDemoPatternMelodic(int bars) const;
    juce::File writeTempMidiFile() const;

    BoomAudioProcessor& proc;


    // === Background ===

    // === Engine buttons (ImageButtons) ===
    juce::ImageButton btn808, btnBass, btnDrums;

    // === Left column controls ===
    juce::ComboBox timeSigBox, barsBox;
    juce::Slider humanizeTiming, humanizeVelocity, swing, tripletDensity, dottedDensity;

    // Dice button + switches as ImageButton toggles
    juce::ImageButton diceBtn;
    juce::ImageButton useTriplets, useDotted;
    juce::ImageComponent soundsDopeLbl;


    // Melodic
    juce::ComboBox keyBox, scaleBox, octaveBox, bassStyleBox;
    juce::Slider   rest808;

    // Drums
    juce::ComboBox drumStyleBox;
    juce::Slider   restDrums;

    // === Action buttons (ImageButtons) ===
    juce::ImageButton btnGenerate;  // generateBtn*.png
    juce::ImageButton btnDragMidi;  // dragBtn*.png
    juce::ImageButton btnFlippit;   // flippitBtn*.png
    juce::ImageButton btnBumppit;   // bumppitBtn*.png
    juce::ImageButton btnRolls;     // rollsBtn*.png
    juce::ImageButton btnAITools;   // aiToolsBtn*.png
    juce::ImageButton hatsBtn;

    // === Label images (EXACT art) ===
    juce::ImageComponent logoImg;
    juce::ImageComponent engineLblImg;
    juce::ImageComponent scaleLblImg, timeSigLblImg, barsLblImg, humanizeLblImg, styleLblImg;
    juce::ImageComponent tripletsLblImg, dottedNotesLblImg, restDensityLblImg;
    juce::ImageComponent keyLblImg, octaveLblImg;
    juce::ImageComponent bassSelectorLblImg, drumsSelectorLblImg;
    juce::ImageComponent eightOhEightLblImg; // "808BassLbl.png"

    // Views
    DrumGridComponent   drumGrid;
    PianoRollComponent  pianoRoll;

    using Attachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<Attachment> keyAtt, scaleAtt, octaveAtt, bassStyleAtt, drumStyleAtt;
    std::unique_ptr<SAttachment> humanizeTimingAtt, humanizeVelocityAtt, swingAtt, rest808Att, restDrumsAtt, tripletDensityAtt, dottedDensityAtt;
    std::unique_ptr<BAttachment> useTripletsAtt, useDottedAtt;

    std::unique_ptr<AIToolsWindow> aiTools;
    std::unique_ptr<FlippitWindow> flippit;
    std::unique_ptr<BumppitWindow> bumppit;
    std::unique_ptr<RollsWindow>  rolls;
    std::unique_ptr<HatsWindow> hats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomAudioProcessorEditor)
};

// ===== Sub windows — ALL buttons are ImageButtons =====
class FlippitWindow : public juce::Component
{
public:
    // Engine decides layout + art (background + three buttons).
    FlippitWindow(BoomAudioProcessor& p,
        std::function<void()> onClose,
        std::function<void(int density)> onFlip,
        boom::Engine engine);

    juce::File saveWithChooserOrDesktop(const juce::String& baseName, const juce::File& srcTemp)
    {
        // Default: Desktop/baseName.mid
        auto dest = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(baseName).withFileExtension(".mid");

        // Show a save dialog; if the user cancels, fall back to Desktop path
        juce::FileChooser fc("Save MIDI...", dest, "*.mid");
        juce::File result;
        fc.launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [srcTemp](const juce::FileChooser& ch) mutable
            {
                juce::File d = ch.getResult();
                if (!d.getFullPathName().isEmpty())
                {
                    if (!d.hasFileExtension(".mid")) d = d.withFileExtension(".mid");
                    if (d.existsAsFile()) d.deleteFile();
                    srcTemp.copyFileTo(d);
                }
            });

        // Return the Desktop default either way (drag uses it if chooser canceled)
        return dest;
    }

    void resized() override;

private:
    BoomAudioProcessor& proc;
    std::function<void()> onCloseFn;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    std::function<void(int)> onFlipFn;

    // Background swaps to flippitBassMockUp.png or flippitDrumsMockUp.png
    void paint(juce::Graphics& g) override;

    // Controls common to both layouts
    juce::Slider variation; // "Variation Density"

    // Buttons (engine-specific art)
    juce::ImageComponent titleLbl;
    juce::ImageButton btnFlip;     // flippitBtn808Bass* OR flippitBtnDrums*
    juce::ImageButton btnSaveMidi; // saveMidiFlippit808Bass* OR saveMidiFlippitDrums*
    juce::ImageButton btnDragMidi; // dragBtnFlippit808Bass* OR dragBtnFlippitDrums*
    juce::ImageButton btnHome;     // homeBtn*

    // helpers
    juce::File buildTempMidi() const;
    void performFileDrag(const juce::File& f);
};

class BumppitWindow : public juce::Component
{
public:
    // Engine decides layout + art.
    BumppitWindow(BoomAudioProcessor& p,
        std::function<void()> onClose,
        std::function<void()> onBump,
        boom::Engine engine);

    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    void resized() override;

private:
    BoomAudioProcessor& proc;
    std::function<void()> onCloseFn, onBumpFn;

    // Background swaps to bumppitBassMockup.png or bumppitDrumsMockup.png
    void paint(juce::Graphics& g) override;

    // Buttons (art depends on engine)
    juce::ImageComponent titleLbl;
    juce::ImageButton btnBump;   // bumppitBtn808Bass* OR bumppitBtnDrums*
    juce::ImageButton btnHome;   // homeBtn*

    // Controls shown ONLY in 808/Bass layout
    juce::ComboBox keyBox, scaleBox, octaveBox, barsBox;
    bool showMelodicOptions = false;
};

class RollsWindow : public juce::Component
{
public:
    RollsWindow(BoomAudioProcessor& p, std::function<void()> onClose, std::function<void(juce::String, int, int)> onGen);
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    void resized() override;
private:
    BoomAudioProcessor& proc;
    juce::ImageComponent rollsTitleImg;
    juce::ImageComponent barsLbl;
    juce::ImageComponent styleLbl;
    juce::ImageComponent timeSigLbl;
    juce::ImageComponent howManyLbl;
    juce::ComboBox styleBox, barsBox, timeSigBox, howManyBox;
    juce::Slider variation;
    juce::ImageButton diceBtn;
    juce::ImageButton btnGenerate; // generateBtn*.png
    juce::ImageButton btnSaveMidi, btnDragMidi;
    juce::ImageButton btnHome;
    std::function<void()> onCloseFn;
    std::function<void(juce::String, int, int)> onGenerateFn;
    void paint(juce::Graphics& g) override;
    juce::File buildTempMidi() const;
    void performFileDrag(const juce::File& f);
    DrumGridComponent miniGrid{ proc };
};

class HatsWindow : public juce::Component
{
public:
    // onGen(style, bars, howMany) is still accepted but not used now;
    // saving happens directly in this window via folder chooser + processor call.
    HatsWindow(BoomAudioProcessor& p, std::function<void()> onClose, std::function<void(juce::String, int, int)> onGen);

    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    juce::ImageButton    tripletsChk, dottedChk;
    bool tripletsOn_ = false;
    bool dottedOn_ = false;
    juce::ImageButton btnGenerate;


    void performFileDrag(const juce::File& f);

    // --- Read time signature from APVTS ("timeSig" AudioParameterChoice) ---



    juce::File saveWithChooserOrDesktop(const juce::String& baseName, const juce::File& srcTemp)
    {
        // Default: Desktop/baseName.mid
        auto dest = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(baseName).withFileExtension(".mid");

        // Show a save dialog; if the user cancels, fall back to Desktop path
        juce::FileChooser fc("Save MIDI...", dest, "*.mid");
        juce::File result;
        fc.launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [srcTemp](const juce::FileChooser& ch) mutable
            {
                juce::File d = ch.getResult();
                if (!d.getFullPathName().isEmpty())
                {
                    if (!d.hasFileExtension(".mid")) d = d.withFileExtension(".mid");
                    if (d.existsAsFile()) d.deleteFile();
                    srcTemp.copyFileTo(d);
                }
            });

        // Return the Desktop default either way (drag uses it if chooser canceled)
        return dest;
    }



    void resized() override;



private:
    BoomAudioProcessor& proc;

    // Static art (bottom)
    juce::ImageComponent hatsLbl, hatsDescriptionLbl;

    juce::ImageButton btnDragMidi;   // dragBtn*.png
    juce::File buildTempMidi() const;

    // Labels (png art)
    juce::ImageComponent styleLbl, timeSigLbl, barsLbl, howManyLbl;
    juce::ImageComponent tripletsLblImg, dottedLblImg;
    juce::Slider dottedDensity, tripletDensity;

    // Controls
    juce::ComboBox styleBox, timeSigBox, barsBox, howManyBox;
    juce::ImageButton btnSaveMidi, btnHome;

    bool isTripletsOn() const noexcept { return tripletsOn_; }
    bool isDottedOn()   const noexcept { return dottedOn_; }


    std::function<void()> onCloseFn;

    void paint(juce::Graphics& g) override;
};

// ================= AIToolsWindow (all ImageButtons, all art from your set) =================
class AIToolsWindow : public juce::Component
    , juce::Timer
{
public:
    AIToolsWindow(BoomAudioProcessor& p, std::function<void()> onClose = {}); // <-- default
    ~AIToolsWindow() override;

    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::ImageComponent arrowRhythm, arrowSlap, arrowBlend, arrowBeat;
    juce::Slider blendAB;
    juce::ImageComponent lockToBpmLbl, bpmLbl, aiToolsDescLbl;
    juce::ImageButton    bpmLockChk;

    juce::File buildTempMidi(const juce::String& base) const;
    void performFileDrag(const juce::File& f);

    static void launchSaveMidiChooserAsync(const juce::String& dialogTitle,
        const juce::File& srcTemp,
        const juce::String& defaultFileName)
    {
        // Post to the message thread to be safe in plugin hosts that are picky about dialogs
        juce::MessageManager::callAsync([dialogTitle, srcTemp, defaultFileName]()
            {
                DBG("launchSaveMidiChooserAsync: opening chooser for " + defaultFileName);

                // Build default file location
                juce::File defaultFile = srcTemp.getParentDirectory().getChildFile(defaultFileName);

                // Allocate on heap to ensure it survives until the async callback runs / completes
                auto* fc = new juce::FileChooser(dialogTitle, defaultFile, "*.mid");

                // launchAsync takes a callback executed when user finishes or cancels
                fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                    [fc, srcTemp](const juce::FileChooser& chooser)
                    {
                        juce::File dest = chooser.getResult();
                        if (dest.getFullPathName().isEmpty())
                        {
                            DBG("launchSaveMidiChooserAsync: user cancelled or empty result");
                            delete fc;
                            return;
                        }

                        if (!dest.hasFileExtension(".mid"))
                            dest = dest.withFileExtension(".mid");

                        if (dest.existsAsFile())
                            dest.deleteFile();

                        const bool copied = srcTemp.copyFileTo(dest);
                        DBG("launchSaveMidiChooserAsync: copied temp -> " + dest.getFullPathName() + (copied ? " (OK)" : " (FAILED)"));
                        delete fc;
                    });
            });
    }

    juce::File saveWithChooserOrDesktop(const juce::String& baseName, const juce::File& srcTemp)
    {
        // Default: Desktop/baseName.mid
        auto dest = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(baseName).withFileExtension(".mid");

        // Show a save dialog; if the user cancels, fall back to Desktop path
        juce::FileChooser fc("Save MIDI...", dest, "*.mid");
        juce::File result;
        fc.launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [srcTemp](const juce::FileChooser& ch) mutable
            {
                juce::File d = ch.getResult();
                if (!d.getFullPathName().isEmpty())
                {
                    if (!d.hasFileExtension(".mid")) d = d.withFileExtension(".mid");
                    if (d.existsAsFile()) d.deleteFile();
                    srcTemp.copyFileTo(d);
                }
            });

        // Return the Desktop default either way (drag uses it if chooser canceled)
        return dest;
    }

    void drawMiniMeters(juce::Graphics& g, juce::Rectangle<int> area, float l, float r)
    {
        auto left = area.removeFromTop(area.getHeight() / 2).reduced(1);
        auto right = area.reduced(1);

        // backgrounds
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(left);
        g.fillRect(right);

        // fill widths
        const int lw = juce::roundToInt(left.getWidth() * l);
        const int rw = juce::roundToInt(right.getWidth() * r);

        // meters (use your theme colours if you have them)
        g.setColour(juce::Colours::limegreen.withAlpha(0.9f));
        g.fillRect(left.withWidth(lw));
        g.fillRect(right.withWidth(rw));
    }

    enum class Tool { Rhythmimick, Slapsmith, StyleBlender, Beatbox };

    void setActiveTool(Tool t);

    void updateSeekFromProcessor();

private:
    BoomAudioProcessor& proc;
    std::function<void()> onCloseFn;
    // AIToolsWindow private:
    juce::ImageButton dragSelected2, dragAll2;
    bool dragChoices2Visible = false;
    int rightPanelX = 560;   // keep in sync with your resized() scaling
    int rhY = 120;
    int bxY = 340;           // rhY + vertical_spacing (220) in your layout

    float rhL_ = 0.0f, rhR_ = 0.0f, bxL_ = 0.0f, bxR_ = 0.0f; // cached meter view

    void timerCallback() override;

    Tool activeTool_ = Tool::Rhythmimick;

    // Groups we’ll enable/disable together
    juce::Array<juce::Component*> rhythmimickGroup;
    juce::Array<juce::Component*> slapsmithGroup;
    juce::Array<juce::Component*> styleBlendGroup;
    juce::Array<juce::Component*> beatboxGroup;

    std::atomic<bool> recRh_{ false }, recBx_{ false };
    std::atomic<float> rmsRhL_{ 0.0f }, rmsRhR_{ 0.0f };
    std::atomic<float> rmsBxL_{ 0.0f }, rmsBxR_{ 0.0f };

    // --- Recording flash support ---
    bool recFlash_ = false;
    juce::Image recImgNormal_, recImgHover_, recImgDown_;


    // OPTIONAL: if you have getters on the processor, great; if not we’ll just branch on isCapturing
    // bool isRecordingLoopback() const;
    // bool isRecordingMic() const;

    void forceButtonImage(juce::ImageButton& b, const juce::Image& img)
    {
        // Show the same image for normal/over/down so it "locks" visually
        b.setImages(
            false,  // resizeToFit
            true,  // preserveProportions
            false, // don't force opaque
            img, 1.0f, juce::Colours::transparentBlack,  // normal
            img, 1.0f, juce::Colours::transparentBlack,  // over
            img, 1.0f, juce::Colours::transparentBlack   // down
        );
    }

    // helpers
    void setGroupEnabled(const juce::Array<juce::Component*>& group, bool enabled, float dimAlpha = 0.35f);
    void uncheckAllToggles();


    // Labels (all ImageComponent)
    juce::ImageComponent titleLbl, selectAToolLbl;
    juce::ImageComponent rhythmimickLbl, rhythmimickDescLbl, recordUpTo60LblTop;
    juce::ImageComponent slapsmithLbl, slapsmithDescLbl, slapSmithGridLbl;
    juce::ImageComponent styleBlenderLbl, styleBlenderDescLbl;
    juce::ImageComponent beatboxLbl, beatboxDescLbl, recordUpTo60LblBottom;

    // Toggles
    juce::ImageButton toggleRhythm, toggleSlap, toggleBlend, toggleBeat;

    juce::ImageButton btnRec1, btnPlay1, btnPlay4, btnStop1, btnGen2, btnSave2, btnDrag2, btnStop4, btnGen1, btnSave1, btnDrag1, btnGen4, btnSave4, btnDrag4,
        btnGen3, btnSave3, btnDrag3, btnRec4;

    // Style Blender controls
    juce::Slider      rhythmSeek;
    juce::Slider      beatboxSeek;

    void makeToolActive(Tool t);  // turns one on, others off

    // Home
    juce::ImageButton btnHome;

    // helpers (same pattern export used in other windows

    float  levelL{ 0.0f }, levelR{ 0.0f };
    double playbackSeconds{ 0.0 }, lengthSeconds{ 0.0 };

    // --- Add under other members inside AIToolsWindow in PluginEditor.h ---
    bool isRecRh_ = false;   // Rhythmimick record state
    bool isRecBx_ = false;   // Beatbox record state

    // flash state (half-second pulse)
    bool flashRh_ = false;
    bool flashBx_ = false;
    int  flashCounter_ = 0;

    // where to paint little meters (move them away from controls)
    juce::Rectangle<int> metersRhBounds { 360, 62, 100, 10 };
    juce::Rectangle<int> metersBxBounds { 360, 162, 100, 10 };

    // optional tiny seek positions (seconds)
    double playPosRh_ = 0.0, lengthRh_ = 0.0;
    double playPosBx_ = 0.0, lengthBx_ = 0.0;

public:
    DrumGridComponent miniGrid { proc }; // if your ctor needs a proc, adjust accordingly
    juce::ComboBox styleABox, styleBBox;
};
