#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>

class Preview final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Vultur Preview"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& args) override {
        processor = std::make_unique<VulturProcessor>();
        processor->prepareToPlay(48000, 256);
        processor->applyPreset(0);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        if (args.contains("--show")) {
            window = std::make_unique<Window>(); window->setContentOwned(editor.release(), true);
            window->centreWithSize(1120, 780); window->setVisible(true);
            return;
        }
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        auto destination = juce::File::getCurrentWorkingDirectory().getChildFile("docs/images/vultur.png");
        destination.getParentDirectory().createDirectory();
        { juce::FileOutputStream output(destination);
          if (!output.openedOk()) { setApplicationReturnValue(1); quit(); return; }
          output.setPosition(0); output.truncate();
          if (!juce::PNGImageFormat().writeImageToStream(image, output)) { setApplicationReturnValue(1); quit(); return; } }
        std::cout << "Preview: " << destination.getFullPathName() << '\n';
        quit();
    }
    void shutdown() override { window.reset(); processor.reset(); }
private:
    struct Window final : juce::DocumentWindow {
        Window() : DocumentWindow("Vultur / Preview sin audio", juce::Colour(0xff0b1117), allButtons) { setUsingNativeTitleBar(true); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<VulturProcessor> processor;
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(Preview)
