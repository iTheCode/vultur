#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class VulturTheme final : public juce::LookAndFeel_V4 {
public:
    VulturTheme();
    juce::Font display(float) const;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
private:
    juce::Typeface::Ptr typeface;
};
class VulturEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit VulturEditor(VulturProcessor&);
    ~VulturEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    VulturProcessor& owner;
    VulturTheme theme;
    juce::Image background;
    std::array<juce::Slider, 10> knobs;
    std::array<juce::Label, 10> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 10> knobAttachments;
    std::array<juce::TextButton, 10> scenes;
    std::array<juce::TextButton, 3> presets;
    juce::TextButton bypass { "BYPASS" }, sync { "HOST SYNC" }, release { "HOLD TO RELEASE" }, reset { "RESET FX" };
    juce::ComboBox division, releaseType;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> timeAttachment, releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, syncAttachment;
    juce::TooltipWindow tooltips { this, 650 };
    float meter = 0;
    bool releaseHeld = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VulturEditor)
};
