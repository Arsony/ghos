#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

#include "GhostEngine.h"
#include "bloodline/AudioFeatures.h"

class GhostProcessor : public juce::AudioProcessor {
public:
    GhostProcessor();
    ~GhostProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GHOST"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    bloodline::AudioFeatures features;   // read by the editor at display rate

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    bloodline::ghost::Params readParams() const;

    bloodline::ghost::Engine engine_;
    bloodline::FeatureAnalyzer analyzer_;
};
