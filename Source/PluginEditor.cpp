#include "PluginEditor.h"

PitcherAudioProcessorEditor::PitcherAudioProcessorEditor (PitcherAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setupKnob (pitch,   "PITCH",   ids::pitch,   " st",    2);
    setupKnob (formant, "FORMANT", ids::formant, " st",    2);
    setupKnob (amount,  "VOICE AMOUNT", ids::amount, " %", 0);
    setupKnob (mix,     "MIX",     ids::mix,     " %",     1);
    setupKnob (output,  "OUTPUT",  ids::output,  " dB",    1);

    addAndMakeVisible (snapButton);
    snapAtt = std::make_unique<ButtonAtt> (proc.apvts, ids::snap, snapButton);

    addAndMakeVisible (preserveButton);
    preserveAtt = std::make_unique<ButtonAtt> (proc.apvts, ids::preserve, preserveButton);

    juce::StringArray voiceNames;
    for (int i = 0; i < pitcher::kNumVoices; ++i)
        voiceNames.add (pitcher::voiceDef (i).name);
    voiceBox.addItemList (voiceNames, 1);
    addAndMakeVisible (voiceBox);
    voiceAtt = std::make_unique<ComboAtt> (proc.apvts, ids::voice, voiceBox);
    voiceLabel.setText ("VOICE", juce::dontSendNotification);
    voiceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (voiceLabel);

    qualityBox.addItemList (juce::StringArray { "Live (low latency)", "Smooth 2048", "Smooth 4096 (max quality)" }, 1);
    rangeBox.addItemList (juce::StringArray { "Low (bass/male)", "Mid", "High (female/child)" }, 1);
    addAndMakeVisible (qualityBox);
    addAndMakeVisible (rangeBox);
    qualityAtt = std::make_unique<ComboAtt> (proc.apvts, ids::quality, qualityBox);
    rangeAtt   = std::make_unique<ComboAtt> (proc.apvts, ids::range, rangeBox);

    qualityLabel.setText ("Quality", juce::dontSendNotification);
    rangeLabel.setText ("Voice range", juce::dontSendNotification);
    addAndMakeVisible (qualityLabel);
    addAndMakeVisible (rangeLabel);

    setSize (640, 480);
    startTimerHz (4);
}

PitcherAudioProcessorEditor::~PitcherAudioProcessorEditor()
{
    stopTimer();
}

void PitcherAudioProcessorEditor::setupKnob (Knob& k, const juce::String& name, const char* paramId,
                                             const juce::String& suffix, int decimals)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
    k.slider.setTextValueSuffix (suffix);
    k.slider.setNumDecimalPlacesToDisplay (decimals);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<SliderAtt> (proc.apvts, paramId, k.slider);
}

void PitcherAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15161a));

    g.setColour (juce::Colours::white);
    g.setFont (28.0f);
    g.drawText ("PITCHER", 20, 10, 300, 36, juce::Justification::centredLeft);

    g.setColour (juce::Colours::grey);
    g.setFont (13.0f);
    g.drawText ("demo build - raw UI (final design comes later)", 20, 44, 400, 18, juce::Justification::centredLeft);

    {
        const int vi = juce::jlimit (0, pitcher::kNumVoices - 1, (int) proc.apvts.getRawParameterValue (ids::voice)->load());
        const auto& v = pitcher::voiceDef (vi);
        juce::String info = juce::String (v.description);
        if (vi != 0)
            info << "   |   Voice Transform is separate from the PITCH / FORMANT knobs (they stay yours)";
        g.setColour (juce::Colours::orange);
        g.setFont (13.0f);
        g.drawText (info, 20, 70, getWidth() - 40, 18, juce::Justification::centredLeft);
    }

    g.setColour (juce::Colours::grey);
    g.setFont (13.0f);
    const double sr = proc.getSampleRate();
    const int lat = proc.getLatencySamples();
    const juce::String latText = "Latency: " + juce::String (lat) + " samples"
                               + (sr > 0 ? " (" + juce::String (1000.0 * lat / sr, 1) + " ms)" : juce::String());
    g.drawText (latText, getWidth() - 320, getHeight() - 28, 300, 20, juce::Justification::centredRight);
}

void PitcherAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    voiceLabel.setBounds (getWidth() - 340, 18, 60, 28);
    voiceBox.setBounds (getWidth() - 275, 18, 255, 28);
    area.removeFromTop (80);

    auto place = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (20));
        k.slider.setBounds (r);
    };

    auto top = area.removeFromTop (170);
    const int w = top.getWidth() / 3;
    place (pitch,   top.removeFromLeft (w));
    place (formant, top.removeFromLeft (w));
    place (amount,  top);

    area.removeFromTop (10);
    auto bottom = area.removeFromTop (160);
    const int w2 = bottom.getWidth() / 3;
    place (mix,    bottom.removeFromLeft (w2));
    place (output, bottom.removeFromLeft (w2));

    auto right = bottom.reduced (10, 0);
    preserveButton.setBounds (right.removeFromTop (26));
    snapButton.setBounds (right.removeFromTop (26));
    right.removeFromTop (4);
    qualityLabel.setBounds (right.removeFromTop (18));
    qualityBox.setBounds (right.removeFromTop (28));
    right.removeFromTop (8);
    rangeLabel.setBounds (right.removeFromTop (18));
    rangeBox.setBounds (right.removeFromTop (28));
}
