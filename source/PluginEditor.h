#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>

#include "PluginProcessor.h"

class SVDrummerBrowserPanel;
class SVDrummerLookAndFeel;
class SVDrummerPadComponent;
class SVDrummerPadSettingsPanel;
class SVDrummerSequencerPanel;

class SVDrummerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            public juce::DragAndDropContainer,
                                            private juce::Timer
{
public:
    explicit SVDrummerAudioProcessorEditor (SVDrummerAudioProcessor&);
    ~SVDrummerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showSequencerView();
    void showPadSettings (int padIndex);
    void updateViewButtons();

    SVDrummerAudioProcessor& processor;
    std::unique_ptr<SVDrummerLookAndFeel> lookAndFeel;
    std::unique_ptr<SVDrummerBrowserPanel> browserPanel;
    std::array<std::unique_ptr<SVDrummerPadComponent>,
               SVDrummerAudioProcessor::numberOfPads> padComponents;
    std::unique_ptr<SVDrummerPadSettingsPanel> padSettingsPanel;
    std::unique_ptr<SVDrummerSequencerPanel> sequencerPanel;

    juce::TextButton sequencerViewButton { "SEQUENCER" };
    juce::TextButton settingsViewButton { "PAD SETTINGS" };
    bool showingSettings = false;
    int selectedPad = 0;
    int portableSettingsTimerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SVDrummerAudioProcessorEditor)
};
