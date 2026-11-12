#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace {
const juce::Colour ink(0xff0b1117), cream(0xfff4e9ce), neon(0xff26baff), muted(0xff91a4b0);
void small(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> bounds, float size = 12, juce::Colour colour = muted,
           juce::Justification alignment = juce::Justification::centredLeft) {
    g.setFont(juce::FontOptions(size)); g.setColour(colour); g.drawText(text, bounds, alignment);
}
}
VulturTheme::VulturTheme() : typeface(juce::Typeface::createSystemTypefaceFor(BinaryData::LuckiestGuyRegular_ttf, BinaryData::LuckiestGuyRegular_ttfSize)) {
    setColour(juce::Slider::textBoxTextColourId, cream);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextButton::textColourOffId, cream);
    setColour(juce::TextButton::textColourOnId, neon);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff15232e));
    setColour(juce::ComboBox::outlineColourId, neon.withAlpha(0.3f));
    setColour(juce::ComboBox::textColourId, cream);
    setColour(juce::ComboBox::arrowColourId, neon);
    setColour(juce::PopupMenu::backgroundColourId, ink);
    setColour(juce::PopupMenu::textColourId, cream);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff15496a));
}
juce::Font VulturTheme::display(float size) const { return juce::Font(juce::FontOptions(typeface).withHeight(size)); }
juce::Font VulturTheme::getTextButtonFont(juce::TextButton&, int) { return display(15); }
void VulturTheme::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) {
    const auto r = button.getLocalBounds().toFloat().reduced(1);
    const bool on = button.getToggleState() || down;
    g.setColour(neon.withAlpha(on ? 0.2f : over ? 0.12f : 0.035f)); g.fillRoundedRectangle(r, 6);
    g.setColour(neon.withAlpha(on ? 0.95f : over ? 0.6f : 0.22f)); g.drawRoundedRectangle(r, 6, on ? 1.5f : 1);
    if (on) { g.setColour(neon); g.fillRoundedRectangle(r.getX() + 1, r.getCentreY() - 6, 3, 12, 1); }
}
void VulturTheme::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position, float start, float end, juce::Slider&) {
    const auto size = static_cast<float>(juce::jmin(width, height)) - 16;
    const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float radius = size * 0.5f, angle = start + position * (end - start);
    const auto arc = [=](float a, float b, float r) { juce::Path p; p.addCentredArc(cx, cy, r, r, 0, a, b, true); return p; };
    for (int i = 0; i <= 24; ++i) {
        const auto a = start + (end - start) * static_cast<float>(i) / 24;
        g.setColour(neon.withAlpha(static_cast<float>(i) / 24 <= position ? 0.65f : 0.13f));
        g.drawLine(cx + std::sin(a) * (radius + 3), cy - std::cos(a) * (radius + 3),
                   cx + std::sin(a) * (radius + 6), cy - std::cos(a) * (radius + 6), 1);
    }
    g.setColour(neon.withAlpha(0.12f)); g.strokePath(arc(start, end, radius - 3), juce::PathStrokeType(3));
    for (auto w : { 13.0f, 7.0f, 3.0f }) {
        g.setColour(neon.withAlpha(w > 7 ? 0.04f : w > 3 ? 0.12f : 1.0f));
        g.strokePath(arc(start, angle, radius - 3), juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const auto body = juce::Rectangle<float>(cx - radius + 7, cy - radius + 7, size - 14, size - 14);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff29353f), cx, body.getY(), ink, cx, body.getBottom(), false));
    g.fillEllipse(body);
    g.setColour(juce::Colour(0xff465763)); g.drawEllipse(body, 1);
    g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawEllipse(body.reduced(3), 1);
    g.setColour(neon);
    g.drawLine(cx + std::sin(angle) * radius * 0.54f, cy - std::cos(angle) * radius * 0.54f,
               cx + std::sin(angle) * radius * 0.77f, cy - std::cos(angle) * radius * 0.77f, size > 120 ? 4.0f : 2.5f);
    if (size > 120) {
        g.setFont(display(24)); g.setColour(cream); g.drawText("SCENE", static_cast<int>(cx - 65), static_cast<int>(cy - 22), 130, 30, juce::Justification::centred);
        g.setFont(juce::FontOptions(11)); g.setColour(neon); g.drawText("INTENSITY", static_cast<int>(cx - 65), static_cast<int>(cy + 12), 130, 20, juce::Justification::centred);
    }
}
VulturEditor::VulturEditor(VulturProcessor& p) : AudioProcessorEditor(&p), owner(p),
    background(juce::ImageCache::getFromMemory(BinaryData::cuphead_jpg, BinaryData::cuphead_jpgSize)) {
    setLookAndFeel(&theme);
    const char* ids[] { "amount", "low", "mid", "high", "filter", "duck", "feedback", "mix", "output", "bpm" };
    const char* names[] { "INTENSITY", "LOW", "MID", "HIGH", "FILTER", "DUCK", "FEEDBACK", "FX MIX", "OUTPUT", "MANUAL BPM" };
    const char* tips[] { "Cantidad de Scene FX. Doble clic: cero.", "Graves por debajo de 300 Hz. -60 dB = kill.",
        "Medios entre 300 y 2800 Hz. -60 dB = kill.", "Agudos por encima de 2800 Hz. -60 dB = kill.",
        "Izquierda: low-pass. Centro: neutro. Derecha: high-pass.", "Reduce el efecto sobre transitorios para conservar el groove.",
        "Repeticiones. Maximo 82%.", "Mezcla de Scene FX. El aislador y Release FX son independientes.",
        "Nivel de salida. Techo de seguridad a 0 dBFS.", "Tempo cuando el host no proporciona BPM o HOST SYNC esta apagado." };
    for (size_t i = 0; i < 10; ++i) {
        auto& knob = knobs[i];
        knob.setName(names[i]); knob.setTooltip(tips[i]);
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, i == 0 ? 110 : 86, 22);
        addAndMakeVisible(knob);
        labels[i].setText(names[i], juce::dontSendNotification); labels[i].setFont(theme.display(14));
        labels[i].setColour(juce::Label::textColourId, cream); labels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(labels[i]);
        knobAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, ids[i], knob);
        knob.setDoubleClickReturnValue(true, owner.parameters.getParameter(ids[i])->convertFrom0to1(owner.parameters.getParameter(ids[i])->getDefaultValue()));
        if (i == 0 || i == 5 || i == 6 || i == 7) {
            knob.textFromValueFunction = [](double value) { return juce::String(juce::roundToInt(value * 100)) + " %"; };
            knob.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
        }
        if ((i >= 1 && i <= 3) || i == 8) knob.setTextValueSuffix(" dB");
        if (i == 4) knob.textFromValueFunction = [](double value) { return juce::String(std::abs(value) < 0.00001 ? 0.0 : value, 2); };
        knob.updateText();
    }
    for (size_t i = 0; i < scenes.size(); ++i) {
        scenes[i].setButtonText(vultur::sceneNames[i]);
        scenes[i].onClick = [this, i] { owner.setControlValue("scene", static_cast<float>(i)); timerCallback(); };
        addAndMakeVisible(scenes[i]);
    }
    const char* presetNames[] { "TECH HOUSE", "HOUSE SPACE", "BUILD UP" };
    for (size_t i = 0; i < presets.size(); ++i) {
        presets[i].setButtonText(presetNames[i]); presets[i].onClick = [this, i] { owner.applyPreset(static_cast<int>(i)); };
        addAndMakeVisible(presets[i]);
    }
    for (int i = 0; i < 7; ++i) division.addItem(vultur::beatNames[i], i + 1);
    division.setTooltip("Duracion en beats: 1 = una negra; 1/2 = una corchea.");
    releaseType.addItemList({ "ECHO OUT", "VINYL BRAKE", "BACK SPIN" }, 1);
    addAndMakeVisible(division); addAndMakeVisible(releaseType);
    timeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, "time", division);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, "releaseType", releaseType);
    bypass.setClickingTogglesState(true); sync.setClickingTogglesState(true);
    addAndMakeVisible(bypass); addAndMakeVisible(sync); addAndMakeVisible(release); addAndMakeVisible(reset);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, "bypass", bypass);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, "sync", sync);
    release.setTooltip("Manten pulsado para salir con el efecto seleccionado. Suelta para volver al audio.");
    release.onStateChange = [this] {
        if (releaseHeld != release.isDown()) {
            releaseHeld = release.isDown(); owner.setControlValue("release", releaseHeld ? 1.0f : 0.0f);
        }
    };
    reset.onClick = [this] {
        for (const char* id : { "scene", "amount", "time", "feedback", "low", "mid", "high", "filter", "mix", "duck", "releaseType", "release" }) {
            auto* parameter = owner.parameters.getParameter(id); owner.setControlValue(id, parameter->convertFrom0to1(parameter->getDefaultValue()));
        }
    };
    setSize(1120, 780); timerCallback(); startTimerHz(30);
}
VulturEditor::~VulturEditor() { stopTimer(); if (releaseHeld) owner.setControlValue("release", 0); setLookAndFeel(nullptr); }
void VulturEditor::timerCallback() {
    const int selected = static_cast<int>(owner.parameters.getRawParameterValue("scene")->load());
    for (size_t i = 0; i < scenes.size(); ++i) scenes[i].setToggleState(static_cast<int>(i) == selected, juce::dontSendNotification);
    meter = juce::jmax(owner.peak.load(), meter * 0.86f); repaint();
}
void VulturEditor::resized() {
    bypass.setBounds(970, 28, 126, 34); sync.setBounds(956, 172, 140, 30);
    knobs[9].setBounds(971, 69, 124, 78); labels[9].setVisible(false);
    for (int i = 0; i < 3; ++i) presets[static_cast<size_t>(i)].setBounds(24 + i * 150, 181, 138, 34);
    reset.setBounds(586, 181, 142, 34);
    for (int i = 0; i < 10; ++i) scenes[static_cast<size_t>(i)].setBounds(i < 5 ? 44 : 550, 298 + (i % 5) * 45, 154, 35);
    knobs[0].setBounds(244, 286, 260, 240); labels[0].setVisible(false);
    for (int i = 1; i <= 3; ++i) {
        const int x = 756 + (i - 1) * 110;
        labels[static_cast<size_t>(i)].setBounds(x, 288, 110, 24);
        knobs[static_cast<size_t>(i)].setBounds(x + 4, 316, 102, 116);
    }
    labels[4].setBounds(777, 440, 130, 22); knobs[4].setBounds(785, 461, 114, 81);
    labels[5].setBounds(938, 440, 130, 22); knobs[5].setBounds(946, 461, 114, 81);
    division.setBounds(44, 629, 112, 34);
    for (int i = 6; i <= 8; ++i) {
        const int x = 184 + (i - 6) * 135;
        labels[static_cast<size_t>(i)].setBounds(x, 592, 110, 22);
        knobs[static_cast<size_t>(i)].setBounds(x, 614, 110, 105);
    }
    releaseType.setBounds(644, 629, 190, 36); release.setBounds(854, 629, 222, 36);
}
void VulturEditor::paint(juce::Graphics& g) {
    g.fillAll(ink);
    g.drawImage(background, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
    g.fillAll(ink.withAlpha(0.38f));
    g.setColour(neon); g.fillRoundedRectangle(24, 29, 5, 82, 2);
    const auto display = [this, &g](const juce::String& text, juce::Rectangle<int> r, float height, juce::Colour colour = cream) {
        g.setColour(colour); g.setFont(theme.display(height)); g.drawText(text, r, juce::Justification::centredLeft);
    };
    display("VULTUR", { 43, 24, 390, 79 }, 77);
    small(g, "REMIX EFFECTS  /  HOUSE + TECH HOUSE", { 46, 109, 390, 22 }, 13, neon);
    small(g, "A little mischief. A lot of groove.", { 46, 137, 370, 22 }, 13, cream.withAlpha(0.65f));
    const bool host = owner.hostTempo.load();
    small(g, host ? "HOST  /  " + juce::String(owner.tempo.load(), 1) + " BPM" : "MANUAL TEMPO", { 938, 150, 158, 20 }, 11, neon, juce::Justification::centred);
    const auto panel = [&g](juce::Rectangle<float> r) {
        g.setColour(juce::Colour(0xff111d27).withAlpha(0.74f)); g.fillRoundedRectangle(r, 12);
        g.setColour(neon.withAlpha(0.16f)); g.drawRoundedRectangle(r.reduced(0.5f), 12, 1);
    };
    panel({ 24, 234, 704, 320 }); panel({ 744, 234, 352, 320 }); panel({ 24, 574, 1072, 156 });
    display("01", { 44, 251, 35, 25 }, 19, neon); display("SCENE FX", { 85, 249, 200, 28 }, 23);
    small(g, "BUILD", { 44, 277, 154, 18 }, 10, neon);
    small(g, "BREAK", { 550, 277, 154, 18 }, 10, neon);
    small(g, "TURN IT UP. LET IT GO.", { 250, 529, 250, 18 }, 10, muted, juce::Justification::centred);
    display("02", { 764, 251, 35, 25 }, 19, neon); display("ISOLATOR", { 805, 249, 240, 28 }, 23);
    small(g, "300 Hz", { 767, 431, 83, 16 }, 9, muted, juce::Justification::centred);
    small(g, "300 - 2.8k", { 878, 431, 83, 16 }, 9, muted, juce::Justification::centred);
    small(g, "2.8 kHz", { 990, 431, 83, 16 }, 9, muted, juce::Justification::centred);
    display("BEAT", { 44, 594, 112, 22 }, 15);
    small(g, "1 = QUARTER NOTE", { 44, 674, 128, 18 }, 9);
    g.setColour(neon.withAlpha(0.15f)); g.drawVerticalLine(610, 596, 707);
    display("03", { 644, 594, 34, 24 }, 17, neon); display("RELEASE FX", { 681, 592, 290, 28 }, 21);
    small(g, "HOLD TO EXIT  /  LET GO TO RETURN", { 644, 684, 430, 20 }, 11);
    small(g, "VULTUR  /  VOL. 01", { 25, 745, 180, 20 }, 10);
    small(g, owner.parameters.getRawParameterValue("bypass")->load() > 0.5f ? "BYPASSED" : "STEREO REMIX ENGINE", { 420, 745, 280, 20 }, 10, neon, juce::Justification::centred);
    small(g, "OUT", { 897, 745, 40, 20 }, 10);
    const float level = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(meter, -60.0f) + 60.0f) / 60.0f);
    for (int i = 0; i < 24; ++i) { g.setColour(neon.withAlpha(static_cast<float>(i) / 24 < level ? 0.95f : 0.12f)); g.fillRoundedRectangle(936.0f + static_cast<float>(i) * 6.6f, 750, 4, 10, 1); }
}
