#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout VulturProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    const auto addFloat = [&p](const char* id, const char* name, float min, float max, float initial, float interval = 0.01f) {
        auto range = juce::NormalisableRange<float>(min, max, interval);
        if (min < 0 && max > 0) range.setSkewForCentre(0);
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name,
            range, initial));
    };
    juce::StringArray scenes, divisions;
    for (auto* name : vultur::sceneNames) scenes.add(name);
    for (auto* name : vultur::beatNames) divisions.add(name);
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "scene", 1 }, "Scene FX", scenes, 1));
    addFloat("amount", "Intensity", 0, 1, 0);
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "time", 1 }, "Beats (quarter note = 1)", divisions, 3));
    addFloat("feedback", "Feedback", 0, 0.82f, 0.38f);
    addFloat("low", "Low", -60, 6, 0, 0.1f); addFloat("mid", "Mid", -60, 6, 0, 0.1f); addFloat("high", "High", -60, 6, 0, 0.1f);
    addFloat("filter", "Filter LP / HP", -1, 1, 0);
    addFloat("mix", "FX Mix", 0, 1, 0.35f); addFloat("output", "Output", -24, 6, 0, 0.1f);
    addFloat("duck", "Ducking", 0, 1, 0.3f); addFloat("bpm", "Manual BPM", 40, 240, 125, 0.1f);
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "sync", 1 }, "Host Sync", true));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "bypass", 1 }, "Bypass", false));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "releaseType", 1 }, "Release FX", juce::StringArray { "ECHO OUT", "VINYL BRAKE", "BACK SPIN" }, 0));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "release", 1 }, "Release Hold", false));
    return p;
}
VulturProcessor::VulturProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)), parameters(*this, nullptr, "VULTUR", layout()) {
    const char* ids[] { "scene", "amount", "time", "feedback", "low", "mid", "high", "filter", "mix", "output", "duck", "bpm", "sync", "bypass", "releaseType", "release" };
    for (size_t i = 0; i < values.size(); ++i) values[i] = parameters.getRawParameterValue(ids[i]);
}
void VulturProcessor::prepareToPlay(double rate, int) { effects.prepare(rate); peak.store(0); setLatencySamples(0); }
bool VulturProcessor::isBusesLayoutSupported(const BusesLayout& buses) const {
    return buses.getMainInputChannelSet() == buses.getMainOutputChannelSet()
        && (buses.getMainOutputChannelSet() == juce::AudioChannelSet::mono() || buses.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
}
void VulturProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    const auto get = [this](Param p) { return values[p]->load(); };
    vultur::Settings s;
    s.scene = static_cast<int>(get(Scene)); s.amount = get(Amount); s.division = static_cast<int>(get(Time));
    s.feedback = get(Feedback); s.low = get(Low); s.mid = get(Mid); s.high = get(High); s.filter = get(Filter);
    s.mix = get(Mix); s.output = get(Output); s.duck = get(Duck); s.bpm = get(Bpm);
    s.bypass = get(Bypass) > 0.5f; s.release = get(Release) > 0.5f; s.releaseType = static_cast<int>(get(ReleaseType));
    bool synced = false;
    if (get(Sync) > 0.5f)
        if (auto* playhead = getPlayHead())
            if (auto position = playhead->getPosition())
                if (auto bpm = position->getBpm(); bpm && std::isfinite(*bpm) && *bpm >= 40 && *bpm <= 240) { s.bpm = *bpm; synced = true; }
    hostTempo.store(synced); tempo.store(static_cast<float>(s.bpm));
    effects.process(buffer, s);
    float level = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) level = juce::jmax(level, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
    peak.store(level);
}
void VulturProcessor::setControlValue(const char* id, float value) {
    if (auto* p = parameters.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
    }
}
void VulturProcessor::applyPreset(int preset) {
    // Musical starting points; do not overwrite routing, host sync, tempo or output trim.
    for (const auto* id : { "low", "mid", "high", "filter", "release" }) setControlValue(id, 0);
    setControlValue("scene", preset == 0 ? 1.0f : preset == 1 ? 4.0f : 5.0f);
    setControlValue("time", preset == 0 ? 3.0f : preset == 1 ? 4.0f : 2.0f);
    setControlValue("amount", preset == 2 ? 0.55f : 0.38f);
    setControlValue("feedback", preset == 2 ? 0.58f : 0.38f);
    setControlValue("mix", preset == 0 ? 0.3f : 0.42f);
    setControlValue("duck", preset == 0 ? 0.5f : 0.25f);
}
void VulturProcessor::getStateInformation(juce::MemoryBlock& data) {
    auto state = parameters.copyState();
    // Momentary performance triggers must never fire when a project is reopened.
    for (auto child : state) if (child.getProperty("id").toString() == "release") child.setProperty("value", 0, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, data);
}
void VulturProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName("VULTUR")) {
        auto state = juce::ValueTree::fromXml(*xml);
        for (auto child : state) if (child.getProperty("id").toString() == "release") child.setProperty("value", 0, nullptr);
        parameters.replaceState(state);
    }
}
juce::AudioProcessorEditor* VulturProcessor::createEditor() { return new VulturEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VulturProcessor(); }
