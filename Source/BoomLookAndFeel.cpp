#include "BoomLookAndFeel.h"

BoomLookAndFeel::BoomLookAndFeel()
{
}

void BoomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
    const juce::Slider::SliderStyle /*style*/, juce::Slider& /*slider*/)
{
    juce::Rectangle<float> r((float)x, (float)y, (float)width, (float)height);
    const float trackH = juce::jmax(6.0f, r.getHeight() * 0.22f);
    auto track = r.withHeight(trackH).withCentre(r.getCentre());

    // Outline
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(track.expanded(1.5f), trackH * 0.5f);

    // Fill track (purple)
    g.setColour(juce::Colour::fromString("FF6e138b"));
    g.fillRoundedRectangle(track, trackH * 0.5f);

    // Knob
    const float knobR = juce::jmax(10.0f, trackH * 1.2f);
    juce::Rectangle<float> knob(sliderPos - knobR * 0.5f,
        track.getCentreY() - knobR * 0.5f,
        knobR, knobR);
    g.setColour(juce::Colour::fromString("FF6e138b"));
    g.fillEllipse(knob);
    g.setColour(juce::Colours::black);
    g.drawEllipse(knob, 2.0f);
}

BoomLookAndFeel::~BoomLookAndFeel() = default;

int BoomLookAndFeel::getComboBoxHeight(juce::ComboBox& box)
{
    // slightly taller than default to match mockup
    return juce::jmax(24, box.getHeight());
}

void BoomLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
    bool isButtonDown, int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box)
{
    const juce::Colour fill = juce::Colour::fromString("FF3a1484"); // indigo
    const juce::Colour border = juce::Colour::fromString("FF2D2E41");
    const juce::Colour textCol = juce::Colour::fromString("FFF6F5EF"); // off-white

    juce::Rectangle<int> area { 0, 0, width, height };

    g.setColour(border);
    g.fillRoundedRectangle(area.toFloat().reduced(0.0f), 8.0f);

    g.setColour(fill);
    g.fillRoundedRectangle(area.toFloat().reduced(2.0f), 6.0f);

    // subtle inner glow
    g.setColour(fill.contrasting(0.15f).withAlpha(0.08f));
    g.fillRoundedRectangle(area.toFloat().reduced(3.0f), 6.0f);

    // draw text using the internal label's text if available
    juce::String text = box.getText();
    g.setColour(textCol);
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawFittedText(text, area.reduced(12, 2), juce::Justification::centredLeft, 1);

    // arrow
    const int arrowW = 26;
    juce::Rectangle<int> arrowRect(width - arrowW - 6, 6, arrowW, height - 12);
    g.setColour(border.darker(0.2f));
    g.fillRoundedRectangle(arrowRect.toFloat(), 4.0f);
    g.setColour(textCol);
    juce::Path p;
    p.startNewSubPath((float)arrowRect.getX() + arrowRect.getWidth() * 0.3f, (float)arrowRect.getCentreY() - 3.0f);
    p.lineTo((float)arrowRect.getCentreX(), (float)arrowRect.getCentreY() + 4.0f);
    p.lineTo((float)arrowRect.getX() + arrowRect.getWidth() * 0.7f, (float)arrowRect.getCentreY() - 3.0f);
    p.closeSubPath();
    g.fillPath(p);
}

void BoomLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
    const bool /*isSeparator*/, const bool /*isActive*/, const bool isHighlighted,
    const bool /*isTicked*/, const bool /*hasSubMenu*/, const juce::String& text,
    const juce::String& /*shortcutKeyText*/, const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/)
{
    const juce::Colour base = juce::Colour::fromString("FF6e138b");
    g.setColour(isHighlighted ? base.withAlpha(0.18f) : juce::Colours::transparentBlack);
    g.fillRoundedRectangle(area.toFloat().reduced(2.0f), 6.0f);

    g.setColour(juce::Colour::fromString("FFF6F5EF"));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText(text, area.reduced(8, 2), juce::Justification::centredLeft, true);
}
