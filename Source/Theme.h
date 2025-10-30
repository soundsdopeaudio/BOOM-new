#pragma once
#include <JuceHeader.h>

// ----- Purple, thick, outlined slider look -----
struct PurpleSliderLNF : public juce::LookAndFeel_V4
{
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
        const juce::Slider::SliderStyle /*style*/, juce::Slider& /*s*/) override
    {
        auto r = juce::Rectangle<int>(x, y, width, height).toFloat();

        // Track
        const float trackH = juce::jmax(6.0f, r.getHeight() * 0.20f);
        auto track = r.withHeight(trackH).withCentre(r.getCentre());
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(track, trackH * 0.5f);
        g.setColour(juce::Colours::darkgrey);
        g.drawRoundedRectangle(track, trackH * 0.5f, 2.0f);

        // Filled part
        auto filled = track; filled.setRight(sliderPos);
        g.setColour(juce::Colour(0xFF7B3DFF)); // purple
        g.fillRoundedRectangle(filled, trackH * 0.5f);

        // Knob
        const float knobR = juce::jmax(10.0f, trackH * 1.2f);
        juce::Rectangle<float> knob(sliderPos - knobR * 0.5f,
            track.getCentreY() - knobR * 0.5f,
            knobR, knobR);
        g.setColour(juce::Colour(0xFF7B3DFF));
        g.fillEllipse(knob);
        g.setColour(juce::Colours::black);
        g.drawEllipse(knob, 2.0f);
    }
};

namespace boomui
{
    // Global L&F instance — safe to pass &boomui::LNF() anywhere.
    inline PurpleSliderLNF& LNF()
    {
        static PurpleSliderLNF instance;
        return instance;
    }

    // 0–100% integer slider
    inline void makePercentSlider(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
        s.setRange(0.0, 100.0, 1.0);     // integers only
        s.setNumDecimalPlacesToDisplay(0);
        s.setTextValueSuffix("%");
    }
}

namespace boomtheme
{
    inline juce::Colour PurpleLight() noexcept { return juce::Colour(0xFF8E6BFF); }
}

namespace boomtheme
{
    inline juce::Colour MainBackground()    { return juce::Colour::fromString("FF7CD400"); }
    inline juce::Colour GridBackground()    { return juce::Colour::fromString("FF092806"); }
    inline juce::Colour GridLine()          { return juce::Colour::fromString("FF2D2E41"); }
    inline juce::Colour HeaderBackground()  { return juce::Colour::fromString("FF6E138B"); }
    inline juce::Colour LightAccent()       { return juce::Colour::fromString("FFC9D2A7"); }
    inline juce::Colour NoteFill()          { return juce::Colour::fromString("FF7CD400"); }
    inline juce::Colour PanelStroke()       { return juce::Colour::fromString("FF3A1484"); }

    inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.f)
    {
        g.setColour(GridBackground()); g.fillRoundedRectangle(r, radius);
        g.setColour(PanelStroke());    g.drawRoundedRectangle(r, radius, 1.5f);
    }
}

