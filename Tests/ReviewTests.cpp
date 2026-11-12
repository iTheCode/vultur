#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }

// Measure settled energy over an integer number of tone periods, independently
// on both channels. Never mistake the filter's startup transient for attenuation.
std::array<double, 2> toneResponse(double rate, int channels, double frequency, const vultur::Settings& settings) {
    vultur::Effects engine; engine.prepare(rate);
    std::array<double, 2> input {}, output {};
    const int total = static_cast<int>(rate * 0.4), warmup = total / 2;
    for (int offset = 0; offset < total; offset += 257) {
        juce::AudioBuffer<float> block(channels, juce::jmin(257, total - offset));
        for (int ch = 0; ch < channels; ++ch)
            for (int n = 0; n < block.getNumSamples(); ++n) {
                const float sample = static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * frequency * (offset + n) / rate + ch * 0.37));
                block.setSample(ch, n, sample);
                if (offset + n >= warmup) input[static_cast<size_t>(ch)] += sample * sample;
            }
        engine.process(block, settings);
        for (int ch = 0; ch < channels; ++ch)
            for (int n = 0; n < block.getNumSamples(); ++n)
                if (offset + n >= warmup) {
                    const auto sample = block.getSample(ch, n);
                    if (!std::isfinite(sample)) throw std::runtime_error("Non-finite isolator output");
                    output[static_cast<size_t>(ch)] += sample * sample;
                }
    }
    for (int ch = 0; ch < channels; ++ch) output[static_cast<size_t>(ch)] = 10 * std::log10(juce::jmax(1e-20, output[static_cast<size_t>(ch)] / input[static_cast<size_t>(ch)]));
    return output;
}

// Normalised first-difference energy measures brightness without confusing
// a volume change with a spectral change. Use only the settled noise/reverb tail.
double brightness(double rate, int scene, float amount) {
    vultur::Effects engine; engine.prepare(rate);
    vultur::Settings settings; settings.scene = scene; settings.amount = amount; settings.mix = 1; settings.duck = 0;
    double energy = 0, differences = 0;
    float previous = 0;
    const int total = static_cast<int>(rate);
    for (int offset = 0; offset < total; offset += 257) {
        juce::AudioBuffer<float> block(2, juce::jmin(257, total - offset)); block.clear();
        if (scene != 2 && offset == 0) block.setSample(0, 0, 0.5f);
        engine.process(block, settings);
        for (int n = 0; n < block.getNumSamples(); ++n) {
            const float sample = block.getSample(0, n);
            if (offset + n > total / 4) {
                energy += sample * sample;
                differences += (sample - previous) * (sample - previous);
            }
            previous = sample;
        }
    }
    require(energy > 1e-10, "Missing noise/reverb output");
    return differences / energy;
}

void testLongTail() {
    vultur::Effects engine; engine.prepare(48000);
    vultur::Settings settings; settings.scene = 6; settings.amount = 1; settings.mix = 1;
    settings.feedback = 0.82f; settings.bpm = 40; settings.division = 6; settings.duck = 0;
    float latePeak = 0;
    for (int offset = 0; offset < 16 * 48000; offset += 256) {
        juce::AudioBuffer<float> block(1, 256); block.clear();
        for (int n = 0; n < 256; ++n)
            if (offset + n < 9600) block.setSample(0, n, static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * 80 * (offset + n) / 48000.0)));
        engine.process(block, settings);
        if (offset > 10 * 48000) latePeak = juce::jmax(latePeak, block.getMagnitude(0, 0, 256));
    }
    std::cout << "Echo tail after 10 seconds: " << juce::Decibels::gainToDecibels(latePeak) << " dBFS\n";
    require(latePeak > 0.001f, "Expected long echo tail was truncated");
    const double theoreticalDecay = 3 * std::log(0.001) / std::log(0.82);
    VulturProcessor processor;
    require(processor.getTailLengthSeconds() >= theoreticalDecay, "Host tail declaration is shorter than the maximum repeat decay");
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI initialise;
    try {
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
            for (int channels : { 1, 2 }) {
                for (double hz : { 60.0, 300.0, 900.0, 2800.0, 12000.0 }) {
                    const auto response = toneResponse(rate, channels, hz, {});
                    for (int ch = 0; ch < channels; ++ch) require(std::abs(response[static_cast<size_t>(ch)]) < 0.05, "Neutral isolator is not flat, including crossovers");
                }
                const double frequencies[] { 60, 900, 12000 };
                const double thresholds[] { -40, -30, -40 };
                for (int band = 0; band < 3; ++band) {
                    vultur::Settings settings;
                    if (band == 0) settings.low = -60;
                    if (band == 1) settings.mid = -60;
                    if (band == 2) settings.high = -60;
                    const auto response = toneResponse(rate, channels, frequencies[band], settings);
                    std::cout << "Kill rate=" << rate << " channels=" << channels << " band=" << band << " hz=" << frequencies[band] << " dB=" << response[0] << '\n';
                    for (int ch = 0; ch < channels; ++ch) require(response[static_cast<size_t>(ch)] < thresholds[band], "Single-band kill is too shallow");
                    // Killing one band must preserve a tone well inside another band.
                    const auto other = toneResponse(rate, channels, band == 0 ? 12000 : 60, settings);
                    for (int ch = 0; ch < channels; ++ch) require(std::abs(other[static_cast<size_t>(ch)]) < 0.05, "Kill damaged another band's passband");
                }
            }
            const double lowNoise = brightness(rate, 2, 0.15f), highNoise = brightness(rate, 2, 0.9f);
            const double upReverb = brightness(rate, 4, 0.85f), downReverb = brightness(rate, 9, 0.85f);
            std::cout << "Spectral changes rate=" << rate << " noise bright/dark=" << highNoise / lowNoise
                << " reverb down/up=" << downReverb / upReverb << '\n';
            require(highNoise > lowNoise * 2, "Noise intensity only changes volume, not brightness");
            require(downReverb < upReverb * 0.6, "Reverb Down is only a scaled copy of Reverb Up");
        }
        testLongTail();
        std::cout << "PASS: review regressions (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
