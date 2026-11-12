#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void fill(juce::AudioBuffer<float>& buffer, int offset, double rate) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            buffer.setSample(ch, n, static_cast<float>(0.22 * std::sin((offset + n) * 6.28318530718 * (ch == 0 ? 210 : 830) / rate)));
}
void finite(const juce::AudioBuffer<float>& buffer) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (!std::isfinite(buffer.getSample(ch, n)) || std::abs(buffer.getSample(ch, n)) > 1.00001f) throw std::runtime_error("Non-finite or unbounded output");
    ++checks;
}
struct Playhead final : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo info; info.setBpm(bpm); return info; }
    double bpm = 128;
};
}
int main() {
    juce::ScopedJuceInitialiser_GUI initialise;
    try {
        juce::MidiBuffer midi;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
            for (int channels : { 1, 2 }) {
                VulturProcessor p;
                auto buses = p.getBusesLayout();
                buses.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
                buses.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
                require(p.setBusesLayout(buses), "Mono/stereo layout rejected");
                p.prepareToPlay(rate, 257);
                juce::AudioBuffer<float> buffer(channels, 257), original(channels, 257);
                fill(buffer, 0, rate); original.makeCopyOf(buffer); p.processBlock(buffer, midi);
                // LR4 changes phase at neutral gain. ReviewTests verifies the flat
                // magnitude at crossover and passband frequencies; only bypass is bit-transparent.
                finite(buffer);
                require(buffer.getMagnitude(0, 0, 257) > 0, "Neutral isolator muted the input");
                require(p.getLatencySamples() == 0, "Unexpected latency");
                p.setControlValue("amount", 1); p.setControlValue("mix", 1); p.setControlValue("feedback", 0.82f);
                for (int effect = 0; effect < 10; ++effect) {
                    p.setControlValue("scene", static_cast<float>(effect));
                    double difference = 0;
                    for (int block = 0; block < 120; ++block) {
                        fill(buffer, block * 257, rate); original.makeCopyOf(buffer); p.processBlock(buffer, midi); finite(buffer);
                        for (int n = 0; n < 257; ++n) difference += std::abs(buffer.getSample(0, n) - original.getSample(0, n));
                    }
                    require(difference > 1, "Scene FX produced no change");
                }
                for (int effect = 0; effect < 3; ++effect) {
                    p.setControlValue("releaseType", static_cast<float>(effect)); p.setControlValue("release", 1);
                    double difference = 0;
                    for (int block = 0; block < 160; ++block) {
                        fill(buffer, block * 257, rate); original.makeCopyOf(buffer); p.processBlock(buffer, midi); finite(buffer);
                        for (int n = 0; n < 257; ++n) difference += std::abs(buffer.getSample(0, n) - original.getSample(0, n));
                    }
                    require(difference > 1, "Release FX had no effect");
                    p.setControlValue("release", 0);
                    for (int block = 0; block < 30; ++block) { fill(buffer, block * 257, rate); p.processBlock(buffer, midi); }
                }
                p.setControlValue("bypass", 1);
                for (int i = 0; i < 30; ++i) { fill(buffer, i * 257, rate); p.processBlock(buffer, midi); }
                fill(buffer, 0, rate); original.makeCopyOf(buffer); p.processBlock(buffer, midi);
                for (int ch = 0; ch < channels; ++ch)
                    require(std::memcmp(buffer.getReadPointer(ch), original.getReadPointer(ch), 257 * sizeof(float)) == 0, "Bypass not bit-transparent after ramp");
                p.releaseResources(); p.prepareToPlay(rate, 64);
                juce::AudioBuffer<float> empty(channels, 0); p.processBlock(empty, midi);
            }
        }
        VulturProcessor p;
        p.applyPreset(0); p.setControlValue("low", -12); p.setControlValue("bpm", 126); p.setControlValue("release", 1);
        juce::MemoryBlock state; p.getStateInformation(state);
        VulturProcessor restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(restored.parameters.getRawParameterValue("release")->load() <= 0, "Restoring state triggered release");
        require(std::abs(restored.parameters.getRawParameterValue("low")->load() + 12) < 0.0001f, "EQ state lost");
        require(std::abs(restored.parameters.getRawParameterValue("bpm")->load() - 126) < 0.0001f, "BPM state lost");
        const char bad[] = "broken"; restored.setStateInformation(bad, sizeof(bad));
        require(std::abs(restored.parameters.getRawParameterValue("low")->load() + 12) < 0.0001f, "Invalid state corrupted parameters");
        Playhead playhead; restored.setPlayHead(&playhead); restored.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> block(2, 256); block.clear(); restored.processBlock(block, midi);
        require(restored.hostTempo.load() && std::abs(restored.tempo.load() - 128) < 0.0001f, "Host BPM not used");
        playhead.bpm = std::numeric_limits<double>::quiet_NaN(); restored.processBlock(block, midi);
        require(!restored.hostTempo.load() && std::abs(restored.tempo.load() - 126) < 0.0001f, "Invalid host BPM must fall back");
        restored.setPlayHead(nullptr); restored.processBlock(block, midi);
        require(!restored.hostTempo.load() && std::abs(restored.tempo.load() - 126) < 0.0001f, "Missing host must fall back");
        // Echo at 120 BPM and half a beat must first arrive after exactly 12000 samples.
        vultur::Effects engine; engine.prepare(48000);
        vultur::Settings settings; settings.amount = 1; settings.mix = 1; settings.feedback = 0; settings.bpm = 120; settings.duck = 0;
        juce::AudioBuffer<float> impulse(2, 16000); impulse.clear(); impulse.setSample(0, 0, 0.5f);
        vultur::Effects dryEngine; dryEngine.prepare(48000);
        juce::AudioBuffer<float> dryImpulse; dryImpulse.makeCopyOf(impulse);
        dryEngine.process(dryImpulse, {});
        engine.process(impulse, settings);
        // Remove the phase-aligned dry path before looking for the first echo.
        for (int n = 0; n < 16000; ++n) impulse.setSample(0, n, impulse.getSample(0, n) - 0.55f * dryImpulse.getSample(0, n));
        require(impulse.getMagnitude(0, 0, 12000) < 1e-7f, "Echo arrived before selected beat");
        require(std::abs(impulse.getSample(0, 12000)) > 0.05f, "Echo timing incorrect");
        require(impulse.getMagnitude(1, 0, 16000) <= 0, "Echo leaked into silent channel");
        // Kill all bands while Scene FX is at zero.
        engine.reset(); settings.amount = 0; settings.low = settings.mid = settings.high = -60;
        fill(block, 0, 48000); engine.process(block, settings);
        require(block.getMagnitude(0, 0, 256) < 1e-6f, "Three-band kill failed");
        engine.reset(); settings = {}; block.clear(); engine.process(block, settings);
        require(block.getMagnitude(0, 0, 256) <= 0, "Silence generated audio at defaults");
        // Verify actual tails, not only dry attenuation, for each delay/reverb variant.
        for (int fx = 0; fx < 10; ++fx) {
            engine.reset(); settings = {}; settings.scene = fx; settings.amount = 0.7f; settings.mix = 1; settings.duck = 0;
            juce::AudioBuffer<float> response(2, 48000); response.clear(); response.setSample(0, 0, 0.5f);
            engine.process(response, settings); finite(response);
            require(response.getMagnitude(0, 1, 47999) > 0.0001f, "Missing wet output/tail");
        }
        // Release from a constant bed must fade to silence, then restore the original bed.
        for (int fx = 1; fx <= 2; ++fx) {
            engine.reset(); settings = {};
            for (int n = 0; n < 256; ++n) { block.setSample(0, n, 0.25f); block.setSample(1, n, 0.25f); }
            engine.process(block, settings); settings.releaseType = fx; settings.release = true;
            for (int b = 0; b < 250; ++b) {
                for (int n = 0; n < 256; ++n) { block.setSample(0, n, 0.25f); block.setSample(1, n, 0.25f); }
                engine.process(block, settings);
            }
            require(block.getMagnitude(0, 0, 256) < 0.0001f, "Vinyl release did not stop");
            settings.release = false;
            for (int b = 0; b < 10; ++b) {
                for (int n = 0; n < 256; ++n) { block.setSample(0, n, 0.25f); block.setSample(1, n, 0.25f); }
                engine.process(block, settings);
            }
            require(std::abs(block.getSample(0, 255) - 0.25f) < 0.00001f, "Release did not restore the bed");
        }
        std::cout << "PASS: " << checks << " checks; 4 sample rates, mono/stereo, 10 scenes, 3 releases, bounds, bypass, state, tempo, echo timing, kill, silence, lifecycle\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
