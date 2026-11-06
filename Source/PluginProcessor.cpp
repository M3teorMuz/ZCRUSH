#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout ZCrushAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "crush", 1 }, "Crush",
                 NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "bits", 1 }, "Bit Depth",
                 NormalisableRange<float> (1.0f, 16.0f, 0.1f), 8.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "rate", 1 }, "Sample Rate",
                 NormalisableRange<float> (1.0f, 32.0f, 0.1f, 0.5f), 4.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Dry/Wet",
                 NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output",
                 NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

    return { p.begin(), p.end() };
}

ZCrushAudioProcessor::ZCrushAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pCrush  = apvts.getRawParameterValue ("crush");
    pBits   = apvts.getRawParameterValue ("bits");
    pRate   = apvts.getRawParameterValue ("rate");
    pMix    = apvts.getRawParameterValue ("mix");
    pOut    = apvts.getRawParameterValue ("out");
    pBypass = apvts.getRawParameterValue ("bypass");
}

void ZCrushAudioProcessor::prepareToPlay (double sampleRate, int)
{
    mixSm.reset (sampleRate, 0.02);
    gainSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (pMix->load() * 0.01f);
    gainSm.setCurrentAndTargetValue (Decibels::decibelsToGain (pOut->load()));
    held[0] = held[1] = 0.0f;
    phase[0] = phase[1] = 1.0f;
}

bool ZCrushAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in  = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return in == out;
}

void ZCrushAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numCh = jmin (buffer.getNumChannels(), 2);
    const int numSm = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSm);

    if (pBypass->load() > 0.5f)
    {
        float pk = 0.0f;
        for (int c = 0; c < numCh; ++c)
            pk = jmax (pk, buffer.getMagnitude (c, 0, numSm));
        level.store (jmax (pk, level.load() * 0.9f));
        return;
    }

    const float crush = jlimit (0.0f, 1.0f, pCrush->load() * 0.01f);
    const float bits  = pBits->load();
    const float rate  = pRate->load();

    // CRUSH — общая «ручка силы»: 0% = чистый звук, 100% = полные значения BIT DEPTH / SAMPLE RATE
    const float bitsEff = 16.0f + (bits - 16.0f) * crush;
    const float factor  = std::pow (rate, crush);
    const float inc     = 1.0f / jmax (1.0f, factor);
    const float levels  = std::pow (2.0f, bitsEff);
    const float step    = 2.0f / levels;

    mixSm.setTargetValue (pMix->load() * 0.01f);
    gainSm.setTargetValue (Decibels::decibelsToGain (pOut->load()));

    float* data[2] = { nullptr, nullptr };
    for (int c = 0; c < numCh; ++c)
        data[c] = buffer.getWritePointer (c);

    float peak = 0.0f;

    for (int i = 0; i < numSm; ++i)
    {
        const float m = mixSm.getNextValue();
        const float g = gainSm.getNextValue();

        for (int c = 0; c < numCh; ++c)
        {
            const float dry = data[c][i];

            phase[c] += inc;
            if (phase[c] >= 1.0f)
            {
                phase[c] -= 1.0f;
                held[c] = step * std::floor (dry / step + 0.5f);
            }

            const float out = (dry * (1.0f - m) + held[c] * m) * g;
            data[c][i] = out;
            peak = jmax (peak, std::abs (out));
        }
    }

    level.store (jmax (peak, level.load() * 0.9f));
}

AudioProcessorEditor* ZCrushAudioProcessor::createEditor()
{
    return new ZCrushAudioProcessorEditor (*this);
}

void ZCrushAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ZCrushAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZCrushAudioProcessor();
}
