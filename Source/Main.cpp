#include <JuceHeader.h>
#include "PluginProcessor.h"

// Minimal DocumentWindow that actually quits when you click the X
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name, juce::Component* contentToOwn)
        : juce::DocumentWindow(std::move(name),
            juce::Colours::black,
            juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);               // keep if you want the main window resizable
        setContentOwned(contentToOwn, true);    // takes ownership of the editor
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        // THIS makes the app actually quit when X is clicked
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
        // (equivalently: juce::JUCEApplicationBase::quit();)
    }
};

class BoomApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "BOOM Standalone"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed()          override { return true; }

    void initialise(const juce::String&) override
    {
        proc = std::make_unique<BoomAudioProcessor>();

        // Editor is owned by the window
        auto* editor = proc->createEditor();
        win = std::make_unique<MainWindow>("BOOM", editor);
    }



    void shutdown() override
    {
        win = nullptr;   // delete window first (it owns the editor)
        proc = nullptr;  // then delete the processor
    }

    void systemRequestedQuit() override
    {
        quit();          // calls shutdown() and exits cleanly
    }

private:
    std::unique_ptr<MainWindow>        win;
    std::unique_ptr<BoomAudioProcessor> proc;
};

START_JUCE_APPLICATION(BoomApp)
