#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using Range = juce::NormalisableRange<float>;

juce::AudioProcessorValueTreeState::ParameterLayout ResonanceSuppressorProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = [] (float v, int) { return juce::String ((int) std::round (v)) + " %"; };
    auto hz  = [] (float v, int) { return v >= 1000.f ? juce::String (v / 1000.f, 1) + " kHz"
                                                      : juce::String ((int) v) + " Hz"; };
    auto hzParse = [] (const juce::String& t)
    {
        float v = t.getFloatValue();
        if (t.containsIgnoreCase ("k"))
            v *= 1000.f;
        return v;
    };
    auto ms  = [] (float v, int) { return juce::String (v, v < 10.f ? 1 : 0) + " ms"; };
    auto db1 = [] (float v, int) { return juce::String (v, 1) + " dB"; };
    auto dbs = [] (float v, int) { return juce::String (v > 0.f ? "+" : "") + juce::String (v, 1) + " dB"; };

    l.add (std::make_unique<APF> (juce::ParameterID { "depth", 1 }, "Depth", Range (0.f, 100.f, 1.f), 60.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<APF> (juce::ParameterID { "sharpness", 1 }, "Sharpness", Range (0.f, 100.f, 1.f), 50.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<APF> (juce::ParameterID { "selectivity", 1 }, "Selectivity", Range (0.f, 100.f, 1.f), 50.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<APF> (juce::ParameterID { "low", 1 }, "Low Limit",
                                  Range (20.f, 2000.f, 1.f, 0.4f), 40.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz)
                                                                       .withValueFromStringFunction (hzParse)));
    l.add (std::make_unique<APF> (juce::ParameterID { "high", 1 }, "High Limit",
                                  Range (1000.f, 20000.f, 1.f, 0.4f), 16000.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz)
                                                                       .withValueFromStringFunction (hzParse)));
    l.add (std::make_unique<APF> (juce::ParameterID { "attack", 1 }, "Attack",
                                  Range (1.f, 100.f, 0.1f, 0.5f), 10.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<APF> (juce::ParameterID { "release", 1 }, "Release",
                                  Range (10.f, 1000.f, 1.f, 0.5f), 120.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<APF> (juce::ParameterID { "maxred", 1 }, "Max Reduction",
                                  Range (1.f, 30.f, 0.5f), 12.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (db1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "mix", 1 }, "Mix", Range (0.f, 100.f, 1.f), 100.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<APF> (juce::ParameterID { "output", 1 }, "Output",
                                  Range (-12.f, 12.f, 0.1f), 0.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbs)));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delta", 1 }, "Delta", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autogain", 1 }, "Auto Gain", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "stereomode", 1 }, "Stereo Mode",
                                                         juce::StringArray { "Stereo", "Mid/Side" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sidechain", 1 }, "Sidechain", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "latency", 1 }, "Latency",
                                                         juce::StringArray { "Low (2048)", "High quality (4096)" }, 1));
    return l;
}

ResonanceSuppressorProcessor::ResonanceSuppressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

bool ResonanceSuppressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out || (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void ResonanceSuppressorProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, getMainBusNumInputChannels());
    const int order = (int) apvts.getRawParameterValue ("latency")->load() == 0 ? 11 : 12;
    reportedLatency = 1 << order;
    setLatencySamples (reportedLatency.load());
}

void ResonanceSuppressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    auto mainBuffer = getBusBuffer (buffer, true, 0);
    const bool hasSidechainBus = getBusCount (true) > 1 && getBus (true, 1)->isEnabled();
    juce::AudioBuffer<float> scBuffer = hasSidechainBus ? getBusBuffer (buffer, true, 1)
                                                        : juce::AudioBuffer<float>();

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    SuppressorEngine::Params p;
    p.depth          = get ("depth") / 100.f;
    p.sharpness      = get ("sharpness") / 100.f;
    p.selectivity    = get ("selectivity") / 100.f;
    p.lowHz          = get ("low");
    p.highHz         = get ("high");
    p.attackMs       = get ("attack");
    p.releaseMs      = get ("release");
    p.maxReductionDb = get ("maxred");
    p.mix            = get ("mix") / 100.f;
    p.outputDb       = get ("output");
    p.delta          = get ("delta") > 0.5f;
    p.autoGain       = get ("autogain") > 0.5f;
    p.midSide        = (int) get ("stereomode") == 1;
    p.useSidechain   = get ("sidechain") > 0.5f;
    p.order          = (int) get ("latency") == 0 ? 11 : 12;

    engine.process (mainBuffer, scBuffer.getNumChannels() > 0 ? &scBuffer : nullptr, p);

    if (engine.getLatency() != reportedLatency.load())
    {
        reportedLatency = engine.getLatency();
        triggerAsyncUpdate();
    }
}

juce::AudioProcessorEditor* ResonanceSuppressorProcessor::createEditor()
{
    return new ResonanceSuppressorEditor (*this);
}

void ResonanceSuppressorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ResonanceSuppressorProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonanceSuppressorProcessor();
}
