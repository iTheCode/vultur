#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Effects.h"

class VulturProcessor final : public juce::AudioProcessor {
public:
    VulturProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override { effects.reset(); }
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vultur"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // Up to 3 seconds per repeat at 40 BPM. At feedback 0.82, an ideal
    // repeat train needs ~105 seconds to drop 60 dB; 10 seconds is insufficient.
    double getTailLengthSeconds() const override { return 120.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void setControlValue(const char*, float);
    void applyPreset(int);
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> peak { 0 }, tempo { 125 };
    std::atomic<bool> hostTempo { false };
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    vultur::Effects effects;
    enum Param { Scene, Amount, Time, Feedback, Low, Mid, High, Filter, Mix, Output, Duck, Bpm, Sync, Bypass, ReleaseType, Release, paramCount };
    std::array<std::atomic<float>*, paramCount> values {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VulturProcessor)
};
