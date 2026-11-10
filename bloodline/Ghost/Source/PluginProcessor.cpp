#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout GhostProcessor::createLayout() {
    AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = [&l](const char* id, const char* name, float def) {
        l.add(std::make_unique<AudioParameterFloat>(
            ParameterID{id, 1}, name, NormalisableRange<float>(0.f, 100.f, 0.1f), def,
            AudioParameterFloatAttributes().withLabel("%")));
    };
    pct("spirit", "Spirit", 35.f);
    pct("presence", "Presence", 40.f);
    pct("air", "Air", 30.f);
    pct("mist", "Mist", 25.f);
    pct("mix", "Mix", 70.f);

    l.add(std::make_unique<AudioParameterBool>(ParameterID{"sibguard", 1}, "Sibilance Guard", true));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"voice", 1}, "Voice",
                                                 StringArray{"Auto", "Male", "Female", "Dry rap"}, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"oversampling", 1}, "Oversampling",
                                                 StringArray{"2x", "4x"}, 0));
    l.add(std::make_unique<AudioParameterFloat>(
        ParameterID{"outtrim", 1}, "Output Trim", NormalisableRange<float>(-12.f, 12.f, 0.1f), 0.f,
        AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"match", 1}, "Match", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"limiter", 1}, "Limiter", false));
    return l;
}

GhostProcessor::GhostProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::stereo(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GHOST", createLayout()) {}

bloodline::ghost::Params GhostProcessor::readParams() const {
    auto v = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    bloodline::ghost::Params p;
    p.spirit = v("spirit") * 0.01f;
    p.presence = v("presence") * 0.01f;
    p.air = v("air") * 0.01f;
    p.mist = v("mist") * 0.01f;
    p.mix = v("mix") * 0.01f;
    p.sibGuard = v("sibguard") > 0.5f;
    p.voice = static_cast<bloodline::ghost::Voice>(juce::jlimit(0, 3, static_cast<int>(v("voice") + 0.5f)));
    p.oversampling = v("oversampling") > 0.5f ? 4 : 2;
    p.outTrimDb = v("outtrim");
    p.match = v("match") > 0.5f;
    p.limiter = v("limiter") > 0.5f;
    return p;
}

void GhostProcessor::prepareToPlay(double sampleRate, int) {
    engine_.prepare(sampleRate);
    engine_.setParams(readParams(), true);
    analyzer_.prepare(sampleRate);
    setLatencySamples(engine_.latencySamples());
}

bool GhostProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void GhostProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&) {
    ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    engine_.setParams(readParams());
    engine_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());

    analyzer_.process(buffer.getReadPointer(0), buffer.getReadPointer(1), buffer.getNumSamples(), features);
    features.revealed.store(engine_.revealedEnergy(), std::memory_order_relaxed);
}

AudioProcessorEditor* GhostProcessor::createEditor() { return new GhostEditor(*this); }

void GhostProcessor::getStateInformation(MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void GhostProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(ValueTree::fromXml(*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GhostProcessor(); }
