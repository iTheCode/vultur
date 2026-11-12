#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace vultur {
inline constexpr const char* sceneNames[] = { "BPF ECHO", "ECHO", "NOISE", "SPIRAL UP", "REVERB UP",
    "HPF ECHO", "LPF ECHO", "CRUSH ECHO", "SPIRAL DOWN", "REVERB DOWN" };
inline constexpr const char* beatNames[] = { "1/16", "1/8", "1/4", "1/2", "3/4", "1", "2" };
inline constexpr float beats[] = { 0.0625f, 0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f };
struct Settings {
    int scene = 1, division = 3, releaseType = 0;
    float amount = 0, feedback = 0.38f, low = 0, mid = 0, high = 0;
    float filter = 0, mix = 0.35f, output = 0, duck = 0.3f;
    double bpm = 125;
    bool release = false, bypass = false;
};
class Effects {
public:
    void prepare(double sampleRate);
    void reset();
    void process(juce::AudioBuffer<float>&, const Settings&);
private:
    float read(const std::vector<float>&, double position) const noexcept;
    double sr = 48000;
    int write = 0, size = 0, scene = 1, releaseMode = 0;
    bool wasReleased = false, first = true;
    double releasePosition = 0, releaseAge = 0, pitchPhase = 0;
    std::array<std::vector<float>, 2> delay, history;
    juce::dsp::LinkwitzRileyFilter<float> lowSplit, highSplit, lowPhase;
    std::array<float, 2> toneState {}, echoLow {}, echoHigh {}, noiseLow {}, noiseHigh {}, reverbDown {};
    juce::Reverb reverb;
    juce::Random random { 8128 };
    enum Smooth { amount, feedback, low, mid, high, filter, mix, output, duck, delayTime, releaseMix, bypassMix, sceneFade, count };
    std::array<juce::SmoothedValue<float>, count> smooth;
};
}
