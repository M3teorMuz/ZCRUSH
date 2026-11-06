#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class ZCrushAudioProcessor : public juce::AudioProcessor
{
public:
    ZCrushAudioProcessor();
    ~ZCrushAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ZCRUSH"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> level { 0.0f };   // для анимации интерфейса

private:
    std::atomic<float>* pCrush  = nullptr;
    std::atomic<float>* pBits   = nullptr;
    std::atomic<float>* pRate   = nullptr;
    std::atomic<float>* pMix    = nullptr;
    std::atomic<float>* pOut    = nullptr;
    std::atomic<float>* pBypass = nullptr;

    float held[2]  { 0.0f, 0.0f };
    float phase[2] { 1.0f, 1.0f };
    juce::SmoothedValue<float> mixSm, gainSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZCrushAudioProcessor)
};
