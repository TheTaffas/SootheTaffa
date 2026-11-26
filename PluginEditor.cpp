#include "PluginEditor.h"

namespace
{
    const juce::Colour bgColour      (0xff14171c);
    const juce::Colour panelColour   (0xff0b0d11);
    const juce::Colour accentColour  (0xff4fc3f7);
    const juce::Colour reductionCol  (0xffff9f43);
    const juce::Colour gridColour    (0xff232831);
    const juce::Colour textDim       (0xff8a93a0);

    juce::Font makeFont (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
ResonanceSuppressorEditor::KnobLookAndFeel::KnobLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2f38));
    setColour (juce::TextButton::buttonOnColourId, reductionCol);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff101010));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2f38));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff343a45));
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::arrowColourId, juce::Colours::white);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2128));
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff4fc3f7));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff101010));
}

void ResonanceSuppressorEditor::KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                                                   float pos, float startAngle, float endAngle,
                                                                   juce::Slider&)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float lw = 4.f;
    const float arcR = r - lw * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startAngle, endAngle, true);
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startAngle, angle, true);

    const juce::PathStrokeType stroke (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (juce::Colour (0xff2a2f38));
    g.strokePath (track, stroke);
    g.setColour (accentColour);
    g.strokePath (value, stroke);

    const float bodyR = r - lw * 2.2f;
    g.setColour (juce::Colour (0xff1d2128));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f);
    g.setColour (juce::Colour (0xff343a45));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f, 1.f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -bodyR + 3.f, 3.f, bodyR * 0.45f, 1.5f);
    g.setColour (juce::Colours::white);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

//==============================================================================
ResonanceSuppressorEditor::ResonanceSuppressorEditor (ResonanceSuppressorProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    struct Def { const char* id; const char* name; };
    static const Def defs[10] = {
        { "depth", "DEPTH" },       { "sharpness", "SHARPNESS" }, { "selectivity", "SELECTIVITY" },
        { "attack", "ATTACK" },     { "release", "RELEASE" },
        { "low", "LOW LIMIT" },     { "high", "HIGH LIMIT" },     { "maxred", "MAX REDUCTION" },
        { "mix", "MIX" },           { "output", "OUTPUT" }
    };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        k.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
        k.label.setText (defs[i].name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (makeFont (12.f, true));
        k.label.setColour (juce::Label::textColourId, textDim);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<SliderAttachment> (proc.apvts, defs[i].id, k.slider);
    }

    deltaButton.setClickingTogglesState (true);
    autoGainButton.setClickingTogglesState (true);
    sidechainButton.setClickingTogglesState (true);
    addAndMakeVisible (deltaButton);
    addAndMakeVisible (autoGainButton);
    addAndMakeVisible (sidechainButton);

    if (auto* mode = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("stereomode")))
        modeBox.addItemList (mode->choices, 1);
    if (auto* lat = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("latency")))
        latencyBox.addItemList (lat->choices, 1);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (latencyBox);
    modeAttachment    = std::make_unique<ComboAttachment> (proc.apvts, "stereomode", modeBox);
    latencyAttachment = std::make_unique<ComboAttachment> (proc.apvts, "latency", latencyBox);
    deltaAttachment    = std::make_unique<ButtonAttachment> (proc.apvts, "delta", deltaButton);
    autoGainAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "autogain", autoGainButton);
    sidechainAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "sidechain", sidechainButton);

    setSize (820, 624);
    startTimerHz (30);
}

ResonanceSuppressorEditor::~ResonanceSuppressorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ResonanceSuppressorEditor::timerCallback()
{
    proc.getEngine().getDisplay (level.data(), reduction.data());
    for (int i = 0; i < numPts; ++i)
    {
        const auto u = (size_t) i;
        smLevel[u] = level[u] > smLevel[u] ? level[u] : smLevel[u] + (level[u] - smLevel[u]) * 0.15f;
        smRed[u]   = smRed[u] + (reduction[u] - smRed[u]) * 0.4f;
    }
    repaint (specArea);
}

void ResonanceSuppressorEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 0);

    area.removeFromTop (44);                         // titulo

    auto bar = area.removeFromTop (34);              // barra de controles
    modeBox.setBounds (bar.removeFromLeft (110).withSizeKeepingCentre (110, 26));
    bar.removeFromLeft (8);
    latencyBox.setBounds (bar.removeFromLeft (160).withSizeKeepingCentre (160, 26));
    autoGainButton.setBounds (bar.removeFromRight (100).withSizeKeepingCentre (100, 26));
    bar.removeFromRight (8);
    deltaButton.setBounds (bar.removeFromRight (80).withSizeKeepingCentre (80, 26));
    bar.removeFromRight (8);
    sidechainButton.setBounds (bar.removeFromRight (100).withSizeKeepingCentre (100, 26));
    area.removeFromTop (6);

    specArea = area.removeFromTop (230);
    area.removeFromTop (14);

    const int cellW = area.getWidth() / 5;
    const int rowH  = 132;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = (int) i / 5, col = (int) i % 5;
        juce::Rectangle<int> cell (area.getX() + col * cellW, area.getY() + row * rowH, cellW, rowH);
        knobs[i].label.setBounds (cell.removeFromTop (16));
        knobs[i].slider.setBounds (cell.reduced (20, 0));
    }
}

void ResonanceSuppressorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    g.setColour (juce::Colours::white);
    g.setFont (makeFont (20.f, true));
    g.drawText ("RESONANCE SUPPRESSOR", 16, 0, 400, 44, juce::Justification::centredLeft);

    const auto r = specArea.toFloat();
    g.setColour (panelColour);
    g.fillRoundedRectangle (r, 6.f);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (specArea);

    // rejilla
    g.setFont (makeFont (10.f));
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        const float x = r.getX() + r.getWidth() * std::log10 (f / 20.f) / 3.f;
        g.setColour (gridColour);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (textDim);
        g.drawText (f >= 1000.f ? juce::String ((int) (f / 1000.f)) + "k" : juce::String ((int) f),
                    (int) x + 3, (int) r.getBottom() - 14, 30, 12, juce::Justification::left);
    }
    for (float db : { -80.f, -60.f, -40.f, -20.f })
    {
        const float y = r.getBottom() - r.getHeight() * (db + 100.f) / 100.f;
        g.setColour (gridColour);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (textDim);
        g.drawText (juce::String ((int) db), (int) r.getX() + 4, (int) y - 12, 30, 12, juce::Justification::left);
    }

    auto xAt = [&] (int i) { return r.getX() + r.getWidth() * (float) i / (float) (numPts - 1); };

    // espectro de entrada
    {
        juce::Path fill, line;
        for (int i = 0; i < numPts; ++i)
        {
            const float v = juce::jlimit (0.f, 1.f, (smLevel[(size_t) i] + 100.f) / 100.f);
            const float y = r.getBottom() - v * r.getHeight();
            if (i == 0) { line.startNewSubPath (xAt (i), y); fill.startNewSubPath (xAt (i), r.getBottom()); fill.lineTo (xAt (i), y); }
            else        { line.lineTo (xAt (i), y); fill.lineTo (xAt (i), y); }
        }
        fill.lineTo (r.getRight(), r.getBottom());
        fill.closeSubPath();
        g.setColour (accentColour.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (accentColour.withAlpha (0.9f));
        g.strokePath (line, juce::PathStrokeType (1.4f));
    }

    // reduccion (cuelga desde arriba, escala 24 dB = mitad de la altura)
    {
        const float scale = r.getHeight() * 0.5f / 24.f;
        juce::Path fill, line;
        fill.startNewSubPath (r.getX(), r.getY());
        for (int i = 0; i < numPts; ++i)
        {
            const float y = r.getY() + juce::jlimit (0.f, 24.f, -smRed[(size_t) i]) * scale;
            fill.lineTo (xAt (i), y);
            if (i == 0) line.startNewSubPath (xAt (i), y); else line.lineTo (xAt (i), y);
        }
        fill.lineTo (r.getRight(), r.getY());
        fill.closeSubPath();
        g.setColour (reductionCol.withAlpha (0.35f));
        g.fillPath (fill);
        g.setColour (reductionCol);
        g.strokePath (line, juce::PathStrokeType (1.6f));
    }

    // leyenda
    g.setFont (makeFont (11.f, true));
    g.setColour (accentColour);
    g.drawText ("INPUT", (int) r.getRight() - 150, (int) r.getBottom() - 30, 60, 14, juce::Justification::right);
    g.setColour (reductionCol);
    g.drawText ("REDUCTION", (int) r.getRight() - 90, (int) r.getBottom() - 30, 84, 14, juce::Justification::right);

    if (proc.apvts.getRawParameterValue ("sidechain")->load() > 0.5f)
    {
        const bool active = proc.getEngine().isSidechainActive();
        g.setColour (active ? juce::Colour (0xff7bd88f) : juce::Colour (0xffff6b6b));
        g.drawText (active ? "Sidechain: detecting from external input" : "Sidechain: no input connected (using main)",
                    (int) r.getX() + 8, (int) r.getBottom() - 30, 320, 14, juce::Justification::left);
    }

    if (proc.apvts.getRawParameterValue ("autogain")->load() > 0.5f)
    {
        const float ag = proc.getEngine().getAutoGainDb();
        g.setColour (juce::Colours::white);
        g.drawText ("Auto gain: +" + juce::String (ag, 1) + " dB", (int) r.getRight() - 170, (int) r.getY() + 6,
                    162, 14, juce::Justification::right);
    }
}
