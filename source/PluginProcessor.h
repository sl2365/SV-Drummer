#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class SVDrummerAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int numberOfPads = 16;
    static constexpr int voicesPerPad = 4;

    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sourceSampleRate = 44100.0;
        juce::String displayName;
        juce::String fullPath;
        std::vector<juce::Range<float>> waveform;
    };

    SVDrummerAudioProcessor();
    ~SVDrummerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::Result loadSampleIntoPad (int padIndex, const juce::File& file);
    void clearPadSample (int padIndex);
    std::shared_ptr<const SampleData> getPadSample (int padIndex) const;

    int getPadMidiNote (int padIndex) const;
    void setPadMidiNote (int padIndex, int midiNote);
    bool isPadMuted (int padIndex) const;
    void setPadMuted (int padIndex, bool shouldBeMuted);
    bool isPadSoloed (int padIndex) const;
    void setPadSoloed (int padIndex, bool shouldBeSoloed);
    bool isPadReversed (int padIndex) const;
    void setPadReversed (int padIndex, bool shouldBeReversed);
    float getPadVolumeDb (int padIndex) const;
    void setPadVolumeDb (int padIndex, float decibels);
    float getPadPan (int padIndex) const;
    void setPadPan (int padIndex, float pan);
    float getPadTuneSemitones (int padIndex) const;
    void setPadTuneSemitones (int padIndex, float semitones);
    juce::String getPadSamplePath (int padIndex) const;
    juce::String getPadDisplayName (int padIndex) const;
    std::uint64_t getPadActivityCounter (int padIndex) const;

    void triggerPadFromInterface (int padIndex, float velocity = 1.0f);

    juce::StringArray getBrowserFolders() const;
    void addBrowserFolder (const juce::File& folder);
    void removeBrowserFolder (const juce::File& folder);

    juce::File getPortableDataDirectory() const;
    juce::File getPortableSamplesDirectory() const;
    void flushPortableSettingsIfNeeded();

    static bool isSupportedAudioFile (const juce::File& file);

private:
    struct Voice
    {
        std::shared_ptr<const SampleData> sample;
        double position = 0.0;
        double increment = 1.0;
        float velocity = 1.0f;
        std::uint64_t sampleRevision = 0;
        bool active = false;
    };

    struct PadState
    {
        std::atomic<int> midiNote { 36 };
        std::atomic<bool> muted { false };
        std::atomic<bool> soloed { false };
        std::atomic<bool> reversed { false };
        std::atomic<float> volumeDb { 0.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> tuneSemitones { 0.0f };
        std::shared_ptr<const SampleData> sample;
        std::array<Voice, voicesPerPad> voices;
        std::atomic<std::uint64_t> sampleRevision { 0 };
        std::atomic<std::uint64_t> activityCounter { 0 };
        int nextVoice = 0;
        juce::String samplePath;
        juce::String displayName { "EMPTY" };
    };

    std::array<PadState, numberOfPads> pads;
    std::array<std::atomic<float>, numberOfPads> pendingInterfaceVelocities;
    std::atomic<std::uint32_t> pendingInterfaceTriggers { 0 };

    juce::AudioFormatManager formatManager;
    double currentSampleRate = 44100.0;

    mutable juce::CriticalSection stateLock;
    juce::StringArray browserFolders;
    std::atomic<bool> portableSettingsDirty { false };

    void triggerPadOnAudioThread (int padIndex, float velocity);
    void renderPadVoices (int padIndex,
                          juce::AudioBuffer<float>& output,
                          bool outputEnabled);
    bool anyPadIsSoloed() const;
    bool isValidPadIndex (int padIndex) const noexcept;
    void markPortableSettingsDirty() noexcept;

    void loadPortableSettings();
    void savePortableSettings() const;
    juce::File getPortableSettingsFile() const;
    juce::String makeStoredPath (const juce::File& file) const;
    juce::File resolveStoredPath (const juce::String& storedPath) const;
    static juce::File getThisModuleFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SVDrummerAudioProcessor)
};
