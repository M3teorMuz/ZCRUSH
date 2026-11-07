#pragma once

#include "PluginProcessor.h"

// ДЕМО-интерфейс: намеренно "сырой". Финальный дизайн подключается позже
// (картинки из папки Assets + отрисовка в paint()/resized()).
class PitcherAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit PitcherAudioProcessorEditor (PitcherAudioProcessor&);
    ~PitcherAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAtt> attachment;   // объявлен последним -> удаляется первым
    };

    void setupKnob (Knob& k, const juce::String& name, const char* paramId, const juce::String& suffix, int decimals);
    void timerCallback() override { repaint(); }

    PitcherAudioProcessor& proc;

    Knob pitch, formant, amount, mix, output;
    juce::ToggleButton preserveButton { "Preserve formants" };
    juce::ToggleButton snapButton { "Transpose (snap to semitones)" };
    juce::ComboBox voiceBox, qualityBox, rangeBox;
    juce::Label voiceLabel, qualityLabel, rangeLabel;

    std::unique_ptr<ButtonAtt> preserveAtt, snapAtt;
    std::unique_ptr<ComboAtt>  voiceAtt, qualityAtt, rangeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitcherAudioProcessorEditor)
};
