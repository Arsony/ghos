#pragma once
#include "PluginProcessor.h"

// PLACEHOLDER editor (milestone G3 skeleton). Five Bloodline-coloured knobs in
// the dock arch plus a moon-glow that breathes with the vocal, which proves the
// audio-reactive path end to end. The cathedral / spirit scene (G2) replaces
// paint() later; the parameter attachments and layout maths carry over.
class GhostEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit GhostEditor(GhostProcessor&);
    ~GhostEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob {
        juce::Slider slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    GhostProcessor& proc_;
    std::array<Knob, 5> knobs_;   // MIST, PRESENCE, SPIRIT, AIR, MIX
    float glow_ = 0.f, revealed_ = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostEditor)
};
