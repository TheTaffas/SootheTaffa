#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class ResonanceSuppressorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit ResonanceSuppressorEditor (ResonanceSuppressorProcessor&);
    ~ResonanceSuppressorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    static constexpr int numPts = SuppressorEngine::numDisplayPoints;

    struct KnobLookAndFeel : public juce::LookAndFeel_V4
    {
        KnobLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                               float pos, float startAngle, float endAngle, juce::Slider&) override;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;

    ResonanceSuppressorProcessor& proc;
    KnobLookAndFeel laf;
    std::array<Knob, 10> knobs;
    juce::TextButton deltaButton { "DELTA" }, autoGainButton { "AUTO GAIN" }, sidechainButton { "SIDECHAIN" };
    juce::ComboBox modeBox, latencyBox;
    std::unique_ptr<ButtonAttachment> deltaAttachment, autoGainAttachment, sidechainAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, latencyAttachment;

    std::array<float, numPts> level {}, reduction {}, smLevel {}, smRed {};
    juce::Rectangle<int> specArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonanceSuppressorEditor)
};
