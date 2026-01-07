#pragma once
#include <JuceHeader.h>

class BoomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BoomLookAndFeel();
    ~BoomLookAndFeel() override;

    void drawComboBox (juce::Graphics& g, int width, int height,
                       bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox& box) override;

    int getComboBoxHeight (juce::ComboBox& box);
    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            const bool isSeparator, const bool isActive,
                            const bool isHighlighted, const bool isTicked,
                            const bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float minSliderPos, float maxSliderPos,
        const juce::Slider::SliderStyle style, juce::Slider& slider) override;
};