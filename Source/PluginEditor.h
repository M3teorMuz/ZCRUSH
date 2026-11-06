#pragma once
#include "PluginProcessor.h"

struct ZKnobLookAndFeel : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
};

class ZCrushAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit ZCrushAudioProcessorEditor (ZCrushAudioProcessor&);
    ~ZCrushAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Blob { float x, y, r, threshold, drip; };

    void timerCallback() override;
    void buildBackground();
    void buildStageStatic();
    void drawStaticStage (juce::Graphics&, juce::Rectangle<float> frame);
    void drawStage (juce::Graphics&);
    void drawCharacter (juce::Graphics&, float cx, float y0, float H, float c, float squash);
    void drawSpeakerStatic (juce::Graphics&, juce::Rectangle<float>);
    static void drawValueBox (juce::Graphics&, juce::Rectangle<int>, const juce::String&);

    ZCrushAudioProcessor& proc;
    ZKnobLookAndFeel lnf;

    juce::Slider crushS, bitsS, rateS, mixS, outS;
    using Att = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Att> aCrush, aBits, aRate, aMix, aOut;

    juce::Image background, stageStatic;
    std::vector<Blob> blobs;
    float anim = 0.0f, lvl = 0.0f;
    int tick = 0;

    const juce::Rectangle<float> stageR { 14.f, 78.f, 392.f, 345.f };
    const juce::Rectangle<float> sceneR { 20.f, 84.f, 380.f, 333.f };
    const juce::Rectangle<int>   bypassR { 318, 20, 84, 30 };
    const juce::Rectangle<int>   readR   { 14, 434, 392, 42 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZCrushAudioProcessorEditor)
};
