#include "Effects.h"
#include <cmath>

namespace vultur {
void Effects::prepare(double sampleRate) {
    sr = sampleRate;
    size = static_cast<int>(sr * 6.0) + 4;
    for (auto* buffers : { &delay, &history })
        for (auto& channel : *buffers) channel.assign(static_cast<size_t>(size), 0.0f);
    reverb.setSampleRate(sr);
    const juce::dsp::ProcessSpec spec { sr, 1, 2 };
    for (auto* crossover : { &lowSplit, &highSplit, &lowPhase }) crossover->prepare(spec);
    lowSplit.setCutoffFrequency(300);
    highSplit.setCutoffFrequency(2800);
    lowPhase.setCutoffFrequency(2800);
    lowPhase.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    for (auto& value : smooth) value.reset(sr, 0.025);
    smooth[delayTime].reset(sr, 0.08);
    reset();
}
void Effects::reset() {
    for (auto* buffers : { &delay, &history })
        for (auto& channel : *buffers) std::fill(channel.begin(), channel.end(), 0.0f);
    lowSplit.reset(); highSplit.reset(); lowPhase.reset();
    toneState = {}; echoLow = {}; echoHigh = {}; noiseLow = {}; noiseHigh = {}; reverbDown = {};
    reverb.reset(); write = 0; pitchPhase = 0; releaseAge = 0; wasReleased = false; first = true;
}
float Effects::read(const std::vector<float>& buffer, double position) const noexcept {
    while (position < 0) position += size;
    while (position >= size) position -= size;
    const int a = static_cast<int>(position), b = (a + 1) % size;
    return buffer[static_cast<size_t>(a)] + static_cast<float>(position - a)
        * (buffer[static_cast<size_t>(b)] - buffer[static_cast<size_t>(a)]);
}
void Effects::process(juce::AudioBuffer<float>& buffer, const Settings& s) {
    if (size == 0) return;
    const int channels = juce::jmin(2, buffer.getNumChannels());
    const auto gain = [](float db) { return db <= -59.9f ? 0.0f : std::abs(db) < 0.0001f ? 1.0f : juce::Decibels::decibelsToGain(db); };
    const float time = static_cast<float>(sr * 60.0 / juce::jlimit(40.0, 240.0, s.bpm)) * beats[juce::jlimit(0, 6, s.division)];
    const std::array<float, count> targets { s.amount, s.feedback, gain(s.low), gain(s.mid), gain(s.high), std::abs(s.filter) < 0.00001f ? 0.0f : s.filter,
        s.mix, gain(s.output), s.duck, time, s.release ? 1.0f : 0.0f, s.bypass ? 0.0f : 1.0f, 1.0f };
    for (int i = 0; i < count - 1; ++i) {
        if (first) smooth[static_cast<size_t>(i)].setCurrentAndTargetValue(targets[static_cast<size_t>(i)]);
        else smooth[static_cast<size_t>(i)].setTargetValue(targets[static_cast<size_t>(i)]);
    }
    if (first) { scene = s.scene; smooth[sceneFade].setCurrentAndTargetValue(1.0f); first = false; }
    if (s.scene != scene) smooth[sceneFade].setTargetValue(0.0f);
    else smooth[sceneFade].setTargetValue(1.0f);
    if (s.release && !wasReleased) {
        releasePosition = write - 1.0; releaseAge = 0; releaseMode = s.releaseType;
    }
    wasReleased = s.release;
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.45f + 0.45f * s.amount; rp.damping = 0.55f;
    rp.wetLevel = 1; rp.dryLevel = 0; rp.width = 0.85f; rp.freezeMode = 0;
    reverb.setParameters(rp);
    const auto coefficient = [this](float hz) { return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(sr)); };
    for (int n = 0; n < buffer.getNumSamples(); ++n) {
        std::array<float, count> v {};
        for (int i = 0; i < count; ++i) v[static_cast<size_t>(i)] = smooth[static_cast<size_t>(i)].getNextValue();
        if (scene != s.scene && !smooth[sceneFade].isSmoothing()) { scene = s.scene; smooth[sceneFade].setTargetValue(1); }
        const float amt = v[amount], rel = v[releaseMix];
        const float echoC = coefficient(scene == 0 ? 6500.0f * std::pow(0.22f, amt) : scene == 6 ? 6500.0f * std::pow(0.07f, amt) : 6500.0f);
        const float bassC = coefficient(scene == 0 ? 180.0f * std::pow(5.0f, amt) : scene == 5 ? 180.0f * std::pow(12.0f, amt) : 180.0f);
        const float toneHz = v[filter] < 0 ? 18000.0f * std::pow(0.012f, -v[filter]) : 30.0f * std::pow(220.0f, v[filter]);
        const float toneC = coefficient(juce::jmin(toneHz, static_cast<float>(sr * 0.45)));
        const float noiseHz = 150.0f * std::pow(40.0f, amt);
        const float noiseLowC = coefficient(juce::jmin(noiseHz * 3, static_cast<float>(sr * 0.45)));
        const float noiseHighC = coefficient(noiseHz);
        const float downC = coefficient(juce::jmin(18000.0f * std::pow(0.02f, amt), static_cast<float>(sr * 0.45)));
        std::array<float, 2> dry {}, isolated {}, wet {}, echoes {};
        for (int ch = 0; ch < channels; ++ch) {
            const auto c = static_cast<size_t>(ch);
            dry[c] = buffer.getSample(ch, n);
            float lowBand, upperBand, midBand, highBand;
            lowSplit.processSample(ch, dry[c], lowBand, upperBand);
            highSplit.processSample(ch, upperBand, midBand, highBand);
            // Match the second split's phase on the low branch. The neutral sum
            // is all-pass (flat magnitude), not a bit-identical dry signal.
            lowBand = lowPhase.processSample(ch, lowBand);
            float x = lowBand * v[low] + midBand * v[mid] + highBand * v[high];
            toneState[c] += toneC * (x - toneState[c]);
            const float filtered = v[filter] < 0 ? toneState[c] : x - toneState[c];
            x += juce::jmin(1.0f, std::abs(v[filter]) * 12.0f) * (filtered - x);
            isolated[c] = x;
            history[c][static_cast<size_t>(write)] = x;
            const double base = static_cast<double>(write) - v[delayTime];
            float echo = read(delay[c], base);
            if (scene == 3 || scene == 8) {
                // Two moving read heads with complementary windows transpose the repeats.
                const double window = sr * 0.06;
                const double p2 = std::fmod(pitchPhase + 0.5, 1.0);
                const float w = static_cast<float>(0.5 - 0.5 * std::cos(pitchPhase * juce::MathConstants<double>::twoPi));
                echo = read(delay[c], base - pitchPhase * window) * w + read(delay[c], base - p2 * window) * (1 - w);
            }
            echoLow[c] += echoC * (echo - echoLow[c]);
            echoHigh[c] += bassC * (echoLow[c] - echoHigh[c]);
            echo = echoLow[c] - echoHigh[c];
            if (scene == 6) echo = echoLow[c];
            echoes[c] = echo;
            // Quantise only the audible repeats: quantisation inside feedback
            // can sustain limit cycles instead of allowing the tail to decay.
            if (scene == 7) { const float steps = std::pow(2.0f, 12.0f - 8.0f * amt); echoes[c] = std::round(echo * steps) / steps; }
            // Bounded feedback and preallocated delay keep the audio callback predictable.
            delay[c][static_cast<size_t>(write)] = std::tanh(x * (1 - rel) + echo * juce::jlimit(0.0f, 0.82f, v[feedback]));
            wet[c] = x * (1 - rel);
        }
        if (channels == 1) wet[1] = wet[0];
        reverb.processStereo(&wet[0], &wet[1], 1);
        const float noise = random.nextFloat() * 2 - 1;
        for (int ch = 0; ch < channels; ++ch) {
            const auto c = static_cast<size_t>(ch);
            float fx = echoes[c];
            noiseLow[c] += noiseLowC * (noise - noiseLow[c]);
            noiseHigh[c] += noiseHighC * (noiseLow[c] - noiseHigh[c]);
            if (scene == 2) fx = (noiseLow[c] - noiseHigh[c]) * 0.3f;
            reverbDown[c] += downC * (wet[c] - reverbDown[c]);
            if (scene == 4) fx = wet[c];
            if (scene == 9) fx = reverbDown[c];
            const float wetGain = amt * v[mix] * v[sceneFade];
            const float duckGain = 1.0f / (1.0f + std::abs(dry[c]) * v[duck] * 8.0f);
            const float dryGain = (1 - wetGain * 0.45f) * (scene == 9 ? 1 - wetGain * 0.8f : 1.0f);
            float result = isolated[c] * dryGain + fx * wetGain * duckGain;
            if (rel > 0) {
                float releaseAudio = echoes[c];
                if (releaseMode != 0) {
                    const float envelope = static_cast<float>(juce::jmax(0.0, 1.0 - releaseAge / (sr * 1.2)));
                    releaseAudio = read(history[c], releasePosition) * envelope;
                }
                result += rel * (releaseAudio - result);
            }
            result *= v[output];
            // Transparent below full scale; a final ceiling contains excessive wet feedback.
            result = juce::jlimit(-1.0f, 1.0f, result);
            buffer.setSample(ch, n, v[bypassMix] <= 0 ? dry[c] : dry[c] + v[bypassMix] * (result - dry[c]));
        }
        if (rel > 0) {
            releaseAge += 1;
            const double speed = juce::jmax(0.0, 1.0 - releaseAge / (sr * 1.2));
            releasePosition += releaseMode == 2 ? -speed * 2.5 : speed;
            if (releasePosition < 0) releasePosition += size;
            if (releasePosition >= size) releasePosition -= size;
        }
        const double ratio = std::pow(2.0, (scene == 8 ? -1.0 : 1.0) * amt * 7.0 / 12.0);
        pitchPhase += (1.0 - ratio) / (sr * 0.06);
        pitchPhase -= std::floor(pitchPhase);
        write = (write + 1) % size;
    }
}
}
