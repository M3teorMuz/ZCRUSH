#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PitcherAudioProcessor::createParameterLayout()
{
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray voiceNames;
    for (int i = 0; i < pitcher::kNumVoices; ++i)
        voiceNames.add (pitcher::voiceDef (i).name);

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ParameterID { ids::voice, 1 }, "Voice", voiceNames, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { ids::amount, 1 }, "Voice Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    // Как в Little AlterBoy: шкалы в полутонах, ноль по центру, +-12
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { ids::pitch, 2 }, "Pitch",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        ParameterID { ids::snap, 1 }, "Transpose (snap to semitones)", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { ids::formant, 2 }, "Formant",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        ParameterID { ids::preserve, 1 }, "Preserve Formants", true));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ParameterID { ids::quality, 1 }, "Quality",
        juce::StringArray { "Live (low latency)", "Smooth 2048", "Smooth 4096 (max quality)" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ParameterID { ids::range, 1 }, "Voice Range",
        juce::StringArray { "Low (bass/male)", "Mid", "High (female/child)" }, 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { ids::mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterID { ids::output, 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

//==============================================================================
PitcherAudioProcessor::PitcherAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    voiceP    = apvts.getRawParameterValue (ids::voice);
    amountP   = apvts.getRawParameterValue (ids::amount);
    pitchP    = apvts.getRawParameterValue (ids::pitch);
    snapP     = apvts.getRawParameterValue (ids::snap);
    formantP  = apvts.getRawParameterValue (ids::formant);
    preserveP = apvts.getRawParameterValue (ids::preserve);
    qualityP  = apvts.getRawParameterValue (ids::quality);
    rangeP    = apvts.getRawParameterValue (ids::range);
    mixP      = apvts.getRawParameterValue (ids::mix);
    outputP   = apvts.getRawParameterValue (ids::output);
}

bool PitcherAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

double PitcherAudioProcessor::getTailLengthSeconds() const
{
    return currentSampleRate > 0.0 ? (double) latency / currentSampleRate : 0.0;
}

//==============================================================================
void PitcherAudioProcessor::activateQuality (int q)
{
    activeQuality = q;
    for (auto& ch : shifters[(size_t) q])
        if (ch != nullptr)
            ch->reset();
    for (auto& d : dryBuf)
        std::fill (d.begin(), d.end(), 0.0f);

    latency = shifters[(size_t) q][0] != nullptr ? shifters[(size_t) q][0]->latencySamples() : 0;
    setLatencySamples (latency);
}

void PitcherAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    for (int c = 0; c < kMaxChannels; ++c)
    {
        shifters[0][(size_t) c] = std::make_unique<pitcher::LiveShifter> (sampleRate);
        shifters[1][(size_t) c] = std::make_unique<pitcher::ChannelShifter> (2048, sampleRate);
        shifters[2][(size_t) c] = std::make_unique<pitcher::ChannelShifter> (4096, sampleRate);
    }

    for (int c = 0; c < kMaxChannels; ++c)
        colors[(size_t) c] = std::make_unique<pitcher::VoiceColor> (sampleRate, c);

    for (auto& d : dryBuf)
        d.assign ((size_t) kDryBufSize, 0.0f);
    dryPos = 0;

    mixSmoothed.reset (sampleRate, 0.03);
    gainSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (mixP->load() / 100.0f);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputP->load()));

    activeVoice  = juce::jlimit (0, pitcher::kNumVoices - 1, (int) voiceP->load());
    activeAmount = amountP->load();
    for (auto& col : colors)
        col->setVoice (activeVoice, (double) activeAmount / 100.0);

    activateQuality ((int) qualityP->load());
}

//==============================================================================
void PitcherAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), kMaxChannels);

    for (int c = numCh; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    if (shifters[0][0] == nullptr || numCh == 0)
        return;

    const int q = juce::jlimit (0, kNumQualities - 1, (int) qualityP->load());
    if (q != activeQuality)
        activateQuality (q);

    const int voice = juce::jlimit (0, pitcher::kNumVoices - 1, (int) voiceP->load());
    const float amountPct = amountP->load();
    if (voice != activeVoice || std::abs (amountPct - activeAmount) > 0.05f)
    {
        activeVoice = voice;
        activeAmount = amountPct;
        for (auto& col : colors)
            col->setVoice (voice, (double) amountPct / 100.0);
    }

    // Ручки PITCH / FORMANT принадлежат пользователю; характер (Voice Transform) - отдельный эффект поверх.
    double userPitchSt = (double) pitchP->load();
    if (snapP->load() > 0.5f)
        userPitchSt = std::round (userPitchSt);            // режим Transpose, как в AlterBoy
    const double userFormantSt = (double) formantP->load();
    const auto shift = pitcher::resolveVoice (voice, (double) amountPct / 100.0, userPitchSt, userFormantSt);
    const bool preserve = preserveP->load() > 0.5f;

    static constexpr double kMaxF0[3] = { 300.0, 500.0, 900.0 };
    const double maxF0 = kMaxF0[juce::jlimit (0, 2, (int) rangeP->load())];

    mixSmoothed.setTargetValue (mixP->load() / 100.0f);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputP->load()));

    float* ptr[kMaxChannels] = { nullptr, nullptr };
    for (int c = 0; c < numCh; ++c)
    {
        ptr[c] = buffer.getWritePointer (c);
        shifters[(size_t) q][(size_t) c]->setParameters (shift.pitchRatio, shift.formantRatio, preserve, maxF0);
        shifters[(size_t) q][(size_t) c]->setRobot (shift.robotHz);
        shifters[(size_t) q][(size_t) c]->setCharacter (shift.intonation, shift.vibratoCents, shift.vibratoHz);
    }

    const int dryMask = kDryBufSize - 1;
    int pos = dryPos;

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix  = mixSmoothed.getNextValue();
        const float gain = gainSmoothed.getNextValue();
        const int readPos = (pos - latency) & dryMask;

        for (int c = 0; c < numCh; ++c)
        {
            const float in = ptr[c][i];
            dryBuf[(size_t) c][(size_t) pos] = in;

            const float wet = colors[(size_t) c]->process (shifters[(size_t) q][(size_t) c]->processSample (in));
            const float dry = dryBuf[(size_t) c][(size_t) readPos];

            ptr[c][i] = (dry * (1.0f - mix) + wet * mix) * gain;
        }
        pos = (pos + 1) & dryMask;
    }
    dryPos = pos;
}

//==============================================================================
juce::AudioProcessorEditor* PitcherAudioProcessor::createEditor()
{
    return new PitcherAudioProcessorEditor (*this);
}

void PitcherAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PitcherAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitcherAudioProcessor();
}
