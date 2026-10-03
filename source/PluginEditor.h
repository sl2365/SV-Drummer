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
class SVDrummerGlobalFxPanel;
class SVDrummerTransientTooltip;

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
    void showFxView();
    void selectPadFromIndicator (int padIndex);
    void updatePadSelection (int padIndex);
    void updateViewButtons();
    void showPadVolumeTooltip (juce::Point<int> screenPosition,
                               const juce::String& text);
    int loadSavedZoomPercent() const;
    juce::Result saveZoomSetting() const;
    juce::Result saveAllSettings();
    void handleLibraryLoadCompleted();
    void offerToRelinkMissingSamples();
    void chooseMissingSampleFolder();

    SVDrummerAudioProcessor& processor;
    std::unique_ptr<SVDrummerLookAndFeel> lookAndFeel;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    std::unique_ptr<SVDrummerTransientTooltip> padVolumeTooltip;
    juce::Image logoImage;
    std::unique_ptr<SVDrummerBrowserPanel> browserPanel;
    std::array<std::unique_ptr<SVDrummerPadComponent>,
               SVDrummerAudioProcessor::numberOfPads> padComponents;
    std::unique_ptr<SVDrummerPadSettingsPanel> padSettingsPanel;
    std::unique_ptr<SVDrummerSequencerPanel> sequencerPanel;
    std::unique_ptr<SVDrummerGlobalFxPanel> globalFxPanel;
    std::unique_ptr<juce::FileChooser> missingSampleFolderChooser;

    juce::TextButton sequencerViewButton { "SEQUENCER" };
    juce::TextButton settingsViewButton { "PAD SETTINGS" };
    juce::TextButton fxViewButton { "FX + Mixer" };
    int activeView = 0;
    bool initialLayoutComplete = false;
    bool zoomSavePending = false;
    float uiScale = 1.0f;
    float uiOffsetX = 0.0f;
    float uiOffsetY = 0.0f;
    int selectedPad = 0;
    int portableSettingsTimerTicks = 0;
    double padVolumeTooltipHideAtMilliseconds = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SVDrummerAudioProcessorEditor)
};
