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

// ----- Neon, comic-style slider look (alternative to PurpleSliderLNF) -----
struct NeonSliderLNF : public juce::LookAndFeel_V4
{
    // Textbox font (so you can also change size/weight centrally)
    juce::Label* createSliderTextBox(juce::Slider&) override
    {
        auto* l = new juce::Label();
        l->setFont(juce::Font(15.0f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(0xFF2D2E41));
        l->setColour(juce::Label::backgroundColourId, juce::Colour(0xFF7CD400));
        l->setColour(juce::Label::outlineColourId, juce::Colour(0xFF3A1484));
        l->setJustificationType(juce::Justification::centredRight);
        l->setMinimumHorizontalScale(1.0f);
        l->setInterceptsMouseClicks(false, false);
        return l;
    }

    // Horizontal slider drawing (keeps your thick comic look, but in neon)
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
        const juce::Slider::SliderStyle /*style*/, juce::Slider& s) override
    {
        auto r = juce::Rectangle<int>(x, y, width, height).toFloat();

        // Track
        const float trackH = juce::jmax(8.0f, r.getHeight() * 0.20f);
        auto track = r.withHeight(trackH).withCentre(r.getCentre());

        // Outline (bold comic edge)
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(track.expanded(2.0f), trackH * 0.5f);

        // Inner track (dark)
        auto inner = track.reduced(2.0f);
        g.setColour(juce::Colour::fromString("FF6E138B")); // deep green-black
        g.fillRoundedRectangle(inner, trackH * 0.45f);

        // Fill up to the thumb (neon)
        auto filled = inner.removeFromLeft(juce::jlimit(0.0f, inner.getWidth(), sliderPos - x));
        g.setColour(juce::Colour::fromString("FF6E138B")); // your boomtheme::NoteFill() neon green
        g.fillRoundedRectangle(filled, trackH * 0.45f);

        // Thumb
        const float thumbW = juce::jmax(14.0f, trackH * 1.15f);
        auto thumbX = juce::jlimit(inner.getX(), inner.getRight() - thumbW, sliderPos - thumbW * 0.5f);
        juce::Rectangle<float> thumb(thumbX, inner.getCentreY() - thumbW * 0.5f, thumbW, thumbW);

        // Thumb outline
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(thumb.expanded(2.0f), thumbW * 0.5f);

        // Thumb body (purple glow center to match your brand)
        g.setColour(juce::Colour::fromString("FF6E138B"));
        g.fillRoundedRectangle(thumb, thumbW * 0.5f);

        // Highlight
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillRoundedRectangle(thumb.reduced(thumbW * 0.25f, thumbW * 0.35f), thumbW * 0.25f);

        // Value text (optional, JUCE handles the textbox separately)
        juce::ignoreUnused(s);
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

namespace boomui
{
    // (existing)
    // inline PurpleSliderLNF& LNF() { static PurpleSliderLNF instance; return instance; }

    // NEW: Global alt L&F instance — use &boomui::AltLNF() anywhere to opt-in
    inline NeonSliderLNF& AltLNF()
    {
        static NeonSliderLNF instance;
        return instance;
    }

    // (keep your existing helpers like makePercentSlider(...) exactly as-is)
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
    inline juce::Colour HeaderBackground()  { return juce::Colour::fromString("FF3a1484"); }
    inline juce::Colour LightAccent()       { return juce::Colour::fromString("FFC9D2A7"); }
    inline juce::Colour NoteFill()          { return juce::Colour::fromString("FF6e138b"); }
    inline juce::Colour PanelStroke()       { return juce::Colour::fromString("FF3A1484"); }

    inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 12.f)
    {
        g.setColour(GridBackground()); g.fillRoundedRectangle(r, radius);
        g.setColour(PanelStroke());    g.drawRoundedRectangle(r, radius, 1.5f);
    }
}

