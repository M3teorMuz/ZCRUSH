#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "PitcherDSP.h"
#include "LiveShifter.h"
#include "VoiceFX.h"

namespace ids
{
    inline constexpr const char* voice    = "voice";
    inline constexpr const char* amount   = "amount";
    inline constexpr const char* pitch    = "pitch";
    inline constexpr const char* snap     = "snap";
    inline constexpr const char* formant  = "formant";
    inline constexpr const char* preserve = "preserve";
    inline constexpr const char* quality  = "quality";
    inline constexpr const char* range    = "range";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
}

class PitcherAudioProcessor : public juce::AudioProcessor
{
public:
    PitcherAudioProcessor();
    ~PitcherAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pitcher"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kNumQualities = 3;   // 0: Live (PSOLA), 1: Smooth 2048, 2: Smooth 4096
    static constexpr int kMaxChannels  = 2;
    static constexpr int kDryBufSize   = 16384;

    void activateQuality (int q);

    std::array<std::array<std::unique_ptr<pitcher::IShifter>, kMaxChannels>, kNumQualities> shifters;
    std::array<std::unique_ptr<pitcher::VoiceColor>, kMaxChannels> colors;
    std::array<std::vector<float>, kMaxChannels> dryBuf;
    int dryPos = 0;
    int activeQuality = 0;
    int activeVoice = -1;
    float activeAmount = -1.0f;
    int latency = 0;
    double currentSampleRate = 44100.0;

    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;

    std::atomic<float>* voiceP    = nullptr;
    std::atomic<float>* amountP   = nullptr;
    std::atomic<float>* pitchP    = nullptr;
    std::atomic<float>* snapP     = nullptr;
    std::atomic<float>* formantP  = nullptr;
    std::atomic<float>* preserveP = nullptr;
    std::atomic<float>* qualityP  = nullptr;
    std::atomic<float>* rangeP    = nullptr;
    std::atomic<float>* mixP      = nullptr;
    std::atomic<float>* outputP   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitcherAudioProcessor)
};
