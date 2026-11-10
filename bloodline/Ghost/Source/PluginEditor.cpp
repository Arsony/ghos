#include "PluginEditor.h"
#include "bloodline/Tokens.h"

using namespace juce;
namespace tk = bloodline::tokens;

GhostEditor::GhostEditor(GhostProcessor& p) : AudioProcessorEditor(&p), proc_(p) {
    static const char* ids[5] = {"mist", "presence", "spirit", "air", "mix"};
    static const char* names[5] = {"MIST", "PRESENCE", "SPIRIT", "AIR", "MIX"};

    for (size_t i = 0; i < knobs_.size(); ++i) {
        auto& k = knobs_[i];
        k.label = names[i];
        k.slider.setSliderStyle(Slider::RotaryVerticalDrag);
        k.slider.setTextBoxStyle(Slider::TextBoxBelow, false, 70, 18);
        k.slider.setDoubleClickReturnValue(true, p.apvts.getParameter(ids[i])->convertFrom0to1(
                                                     p.apvts.getParameter(ids[i])->getDefaultValue()));
        k.slider.setColour(Slider::rotarySliderFillColourId, Colour(tk::blood));
        k.slider.setColour(Slider::rotarySliderOutlineColourId, Colour(tk::panel));
        k.slider.setColour(Slider::thumbColourId, Colour(tk::silverHi));
        k.slider.setColour(Slider::textBoxTextColourId, Colour(tk::silver));
        k.slider.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        addAndMakeVisible(k.slider);
        k.attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids[i], k.slider);
    }

    setResizable(true, true);
    setResizeLimits(825, 525, 1650, 1050);
    getConstrainer()->setFixedAspectRatio(1100.0 / 700.0);
    setSize(1100, 700);
    startTimerHz(60);
}

void GhostEditor::timerCallback() {
    // Attack/release smoothing lives here, never in the audio thread.
    const float rms = proc_.features.rms.load(std::memory_order_relaxed);
    const float rev = proc_.features.revealed.load(std::memory_order_relaxed);
    glow_ += (rms > glow_ ? 0.35f : 0.06f) * (rms - glow_);
    revealed_ += (rev > revealed_ ? 0.35f : 0.06f) * (rev - revealed_);
    proc_.features.onset.exchange(false);
    repaint();
}

void GhostEditor::paint(Graphics& g) {
    const float s = getWidth() / 1100.0f;
    g.fillAll(Colour(tk::voidBlack));
    g.setGradientFill(ColourGradient(Colour(tk::obsidian), 0, 0, Colour(tk::voidBlack), 0, (float) getHeight(), false));
    g.fillRect(getLocalBounds());

    // Moon glow: breathes with the vocal; brighter as more is revealed.
    const float spirit = proc_.apvts.getRawParameterValue("spirit")->load() * 0.01f;
    const float amount = jlimit(0.f, 1.f, 0.12f + 0.25f * spirit + glow_ * 3.f + revealed_ * 6.f);
    const float cx = 550.f * s, cy = 300.f * s, r = (170.f + 140.f * amount) * s;
    g.setGradientFill(ColourGradient(Colour(tk::moonsilver).withAlpha(0.55f * amount), cx, cy,
                                     Colour(tk::spectralBlue).withAlpha(0.0f), cx + r, cy, true));
    g.fillEllipse(cx - r, cy - r, 2 * r, 2 * r);

    g.setColour(Colour(tk::silverHi));
    g.setFont(Font(FontOptions(30.f * s)).withExtraKerningFactor(0.25f));
    g.drawText("GHOST", Rectangle<float>(28 * s, 10 * s, 300 * s, 40 * s), Justification::centredLeft);
    g.setColour(Colour(tk::silver).withAlpha(0.45f));
    g.setFont(Font(FontOptions(12.f * s)).withExtraKerningFactor(0.14f));
    g.drawText("REVEAL WHAT LIVES INSIDE THE VOCAL.", Rectangle<float>(0, 14 * s, (float) getWidth(), 28 * s),
               Justification::centred);

    // Dock labels
    g.setColour(Colour(tk::silver));
    g.setFont(Font(FontOptions(10.f * s)).withExtraKerningFactor(0.14f));
    for (auto& k : knobs_)
        g.drawText(k.label, k.slider.getBounds().withHeight((int) (16 * s)).translated(0, (int) (-18 * s)),
                   Justification::centred);

    g.setColour(Colour(tk::silver).withAlpha(0.35f));
    g.drawText("v0.1  -  engine + skeleton; cathedral scene lands in G2", Rectangle<float>(20 * s, getHeight() - 26 * s, 600 * s, 20 * s),
               Justification::centredLeft);
}

void GhostEditor::resized() {
    const float s = getWidth() / 1100.0f;
    // Dock arch: MIST, PRESENCE, SPIRIT (large, 132 px), AIR, MIX (92 px).
    struct Slot { float cx, cy, size; };
    const Slot slots[5] = {{170, 560, 92}, {360, 535, 92}, {550, 505, 132}, {740, 535, 92}, {930, 560, 92}};
    for (size_t i = 0; i < knobs_.size(); ++i) {
        const float sz = slots[i].size * s;
        knobs_[i].slider.setBounds(Rectangle<float>(slots[i].cx * s - sz / 2, slots[i].cy * s - sz / 2, sz, sz + 22 * s).toNearestInt());
    }
}
