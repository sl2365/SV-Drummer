#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <vector>

class SVDrummerAudioProcessor final : public juce::AudioProcessor,
                                      private juce::AsyncUpdater
{
public:
    static constexpr int numberOfPads = 16;
    static constexpr int numberOfPadOutputBuses = numberOfPads;
    static constexpr int numberOfOutputBuses = numberOfPadOutputBuses + 1;
    static constexpr int numberOfPatterns = 16;
    static constexpr int voicesPerPad = 4;
    static constexpr int sequencerDivisionCount = 9;
    static constexpr int maximumPatternBars = 16;
    static constexpr int maximumStepsPerBar = 64;
    static constexpr int maximumStepsPerLane = maximumPatternBars * maximumStepsPerBar;
    static constexpr int maximumPatternPlaybackSteps = numberOfPatterns;

    enum class PatternMidiMode
    {
        select = 0,
        gate,
        hold
    };

    enum class PatternSyncMode
    {
        played = 0,
        bar,
        beat
    };

    enum class PadFilterType
    {
        off = 0,
        lowPass,
        bandPass,
        highPass,
        comb,
        formant,
        ladder,
        notch
    };

    enum class PadLoopMode
    {
        normal = 0,
        pingPong
    };

    enum class BrowserMode
    {
        samples = 0,
        kits,
        patterns,
        projects
    };

    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sourceSampleRate = 44100.0;
        juce::String displayName;
        juce::String fullPath;
        std::vector<juce::Range<float>> waveform;
        std::vector<int> zeroCrossings;
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
    juce::Result previewSampleFile (const juce::File& file);
    void clearPadSample (int padIndex);
    void resetPadToDefault (int padIndex);
    void copyPad (int padIndex);
    bool canPastePad() const noexcept;
    void pastePad (int padIndex);
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
    int getPadOutputBus (int padIndex) const;
    void setPadOutputBus (int padIndex, int outputBus);
    static juce::String getPadOutputBusName (int outputBus);
    float getPadDelaySend (int padIndex) const;
    void setPadDelaySend (int padIndex, float amount);
    float getPadReverbSend (int padIndex) const;
    void setPadReverbSend (int padIndex, float amount);
    int getPadChokeGroup (int padIndex) const;
    void setPadChokeGroup (int padIndex, int chokeGroup);
    float getPadAmpCurve (int padIndex) const;
    void setPadAmpCurve (int padIndex, float curve);
    float getPadAmpAttackMs (int padIndex) const;
    void setPadAmpAttackMs (int padIndex, float milliseconds);
    float getPadAmpDecayMs (int padIndex) const;
    void setPadAmpDecayMs (int padIndex, float milliseconds);
    float getPadAmpSustain (int padIndex) const;
    void setPadAmpSustain (int padIndex, float level);
    float getPadAmpReleaseMs (int padIndex) const;
    void setPadAmpReleaseMs (int padIndex, float milliseconds);
    float getPadFilterCutoffHz (int padIndex) const;
    void setPadFilterCutoffHz (int padIndex, float frequencyHz);
    float getPadFilterResonance (int padIndex) const;
    void setPadFilterResonance (int padIndex, float amount);
    float getPadFilterDriveDb (int padIndex) const;
    void setPadFilterDriveDb (int padIndex, float decibels);
    bool isPadFilterEnabled (int padIndex) const;
    void setPadFilterEnabled (int padIndex, bool shouldBeEnabled);
    PadFilterType getPadFilterType (int padIndex) const;
    void setPadFilterType (int padIndex, PadFilterType type);
    int getPadFilterSlopeIndex (int padIndex) const;
    void setPadFilterSlopeIndex (int padIndex, int slopeIndex);
    float getPadHighPassCutoffHz (int padIndex) const;
    void setPadHighPassCutoffHz (int padIndex, float frequencyHz);
    bool isPadCompressorEnabled (int padIndex) const;
    void setPadCompressorEnabled (int padIndex, bool shouldBeEnabled);
    float getPadCompressorThresholdDb (int padIndex) const;
    void setPadCompressorThresholdDb (int padIndex, float decibels);
    float getPadCompressorRatio (int padIndex) const;
    void setPadCompressorRatio (int padIndex, float ratio);
    float getPadCompressorAttackMs (int padIndex) const;
    void setPadCompressorAttackMs (int padIndex, float milliseconds);
    float getPadCompressorReleaseMs (int padIndex) const;
    void setPadCompressorReleaseMs (int padIndex, float milliseconds);
    float getPadCompressorKneeDb (int padIndex) const;
    void setPadCompressorKneeDb (int padIndex, float decibels);
    float getPadCompressorGainDb (int padIndex) const;
    void setPadCompressorGainDb (int padIndex, float decibels);
    float getPadSaturationAmount (int padIndex) const;
    void setPadSaturationAmount (int padIndex, float amount);
    float getPadSaturationHardClipAmount (int padIndex) const;
    void setPadSaturationHardClipAmount (int padIndex, float amount);
    bool isPadSaturationEnabled (int padIndex) const;
    void setPadSaturationEnabled (int padIndex, bool shouldBeEnabled);
    int getPadSampleLength (int padIndex) const;
    int getPadSampleStart (int padIndex) const;
    void setPadSampleStart (int padIndex, int samplePosition,
                            int snapDirection = 0);
    int getPadSampleEnd (int padIndex) const;
    void setPadSampleEnd (int padIndex, int samplePosition,
                          int snapDirection = 0);
    bool isPadLoopEnabled (int padIndex) const;
    void setPadLoopEnabled (int padIndex, bool shouldLoop);
    PadLoopMode getPadLoopMode (int padIndex) const;
    void setPadLoopMode (int padIndex, PadLoopMode mode);
    bool isPadSequencerGated (int padIndex) const;
    void setPadSequencerGated (int padIndex, bool shouldBeGated);
    int getPadLoopStart (int padIndex) const;
    void setPadLoopStart (int padIndex, int samplePosition,
                          int snapDirection = 0);
    int getPadLoopEnd (int padIndex) const;
    void setPadLoopEnd (int padIndex, int samplePosition,
                        int snapDirection = 0);
    void resetPadSampleMarkers (int padIndex);
    bool isSampleMarkerSnapEnabled() const;
    void setSampleMarkerSnapEnabled (bool shouldSnap);
    juce::String getPadSamplePath (int padIndex) const;
    juce::String getPadDisplayName (int padIndex) const;
    std::uint64_t getPadActivityCounter (int padIndex) const;

    void triggerPadFromInterface (int padIndex, float velocity = 1.0f);
    void releasePadFromInterface (int padIndex);

    bool isGlobalDelayEnabled() const noexcept;
    void setGlobalDelayEnabled (bool shouldBeEnabled);
    bool isGlobalDelaySyncEnabled() const noexcept;
    void setGlobalDelaySyncEnabled (bool shouldBeEnabled);
    int getGlobalDelaySyncDivision() const noexcept;
    void setGlobalDelaySyncDivision (int divisionIndex);
    float getGlobalDelayTimeMs() const noexcept;
    void setGlobalDelayTimeMs (float milliseconds);
    float getGlobalDelayFeedback() const noexcept;
    void setGlobalDelayFeedback (float amount);
    float getGlobalDelayMix() const noexcept;
    void setGlobalDelayMix (float amount);
    float getGlobalDelayDuck() const noexcept;
    void setGlobalDelayDuck (float amount);
    float getGlobalDelayDuckAttackMs() const noexcept;
    void setGlobalDelayDuckAttackMs (float milliseconds);
    float getGlobalDelayDuckReleaseMs() const noexcept;
    void setGlobalDelayDuckReleaseMs (float milliseconds);
    bool isGlobalReverbEnabled() const noexcept;
    void setGlobalReverbEnabled (bool shouldBeEnabled);
    float getGlobalReverbSize() const noexcept;
    void setGlobalReverbSize (float amount);
    float getGlobalReverbDamping() const noexcept;
    void setGlobalReverbDamping (float amount);
    float getGlobalReverbWidth() const noexcept;
    void setGlobalReverbWidth (float amount);
    float getGlobalReverbMix() const noexcept;
    void setGlobalReverbMix (float amount);
    float getGlobalReverbDuck() const noexcept;
    void setGlobalReverbDuck (float amount);
    float getMasterVolumeDb() const noexcept;
    void setMasterVolumeDb (float decibels);

    bool isSequencerEnabled() const;
    void setSequencerEnabled (bool shouldBeEnabled);
    void stopPatternMidiPlayback();
    PatternMidiMode getPatternMidiMode() const;
    void setPatternMidiMode (PatternMidiMode newMode);
    PatternSyncMode getPatternSyncMode() const noexcept;
    void setPatternSyncMode (PatternSyncMode newMode);
    int getPatternBars() const;
    void setPatternBars (int bars);
    int getLaneDivision (int laneIndex) const;
    void setLaneDivision (int laneIndex, int divisionIndex);
    int getLaneLoopLength (int laneIndex) const;
    void setLaneLoopLength (int laneIndex, int lengthInSteps);
    int getLaneMaximumLoopLength (int laneIndex) const;
    int getSequenceStepVelocity (int laneIndex, int stepIndex) const;
    void setSequenceStepVelocity (int laneIndex, int stepIndex, int velocity);
    int getActiveSequenceStep (int laneIndex) const;
    double getSequencerPatternPositionQuarterNotes() const;
    int getPatternPlaybackStep (int stepIndex) const;
    void setPatternPlaybackStep (int stepIndex, int patternIndex);
    int getActivePatternPlaybackStep() const noexcept;
    int getPatternPlaybackBars() const noexcept;
    bool isPatternPlaybackChainEnabled() const noexcept;
    void setPatternPlaybackChainEnabled (bool shouldBeEnabled);
    bool isPatternPlaybackLoopEnabled() const noexcept;
    void setPatternPlaybackLoopEnabled (bool shouldLoop);
    void copySequenceLane (int laneIndex);
    bool canPasteSequenceLane() const noexcept;
    void pasteSequenceLane (int laneIndex);
    void randomiseSequenceLane (int laneIndex);
    void clearSequenceLane (int laneIndex);
    void nudgeSequenceLane (int laneIndex, int direction);
    bool canUndoSequenceLaneOperation (int laneIndex) const noexcept;
    void undoSequenceLaneOperation (int laneIndex);
    void clearPatternPlaybackChain();
    bool canUndoPatternPlaybackChain() const noexcept;
    void undoPatternPlaybackChain();

    int getCurrentPatternIndex() const;
    std::uint64_t getPatternChangeRevision() const;
    void selectPattern (int patternIndex);
    bool isPatternAssigned (int patternIndex) const;
    bool patternHasSteps (int patternIndex) const;
    juce::String getPatternName (int patternIndex) const;
    juce::File getPatternFile (int patternIndex) const;
    juce::String getCurrentKitName() const;
    juce::File getCurrentKitFile() const;
    juce::String getCurrentPatternSetName() const;
    juce::File getCurrentPatternSetFile() const;
    juce::String getCurrentProjectName() const;
    juce::File getCurrentProjectFile() const;
    int getPatternMidiNote (int patternIndex) const;
    void setPatternMidiNote (int patternIndex, int midiNote);
    juce::Result loadPatternIntoSlot (int patternIndex, const juce::File& file);
    juce::Result savePatternSlotToFile (int patternIndex, const juce::File& file);
    void copyPatternSlot (int patternIndex);
    bool canPastePatternSlot() const noexcept;
    void pastePatternSlot (int patternIndex);
    void clearPatternSlot (int patternIndex);
    void randomisePatternSlot (int patternIndex);
    bool canUndoPatternOperation (int patternIndex) const noexcept;
    void undoPatternOperation (int patternIndex);
    bool patternSetHasSteps() const;
    juce::Result loadPatternSetFromFile (const juce::File& file);
    juce::Result savePatternSetToFile (const juce::File& file);
    juce::Result loadKitFromFile (const juce::File& file);
    juce::Result saveKitToFile (const juce::File& file);
    bool kitHasSamples() const;
    juce::Result loadProjectFromFile (const juce::File& file);
    juce::Result saveProjectToFile (const juce::File& file);
    juce::StringArray getMissingSampleDescriptions() const;
    int relinkMissingSamplesFromFolder (const juce::File& folder,
                                        bool searchSubfolders = true);

    static juce::String getSequencerDivisionName (int divisionIndex);
    static juce::String getDelaySyncDivisionName (int divisionIndex);
    static juce::String getPadFilterTypeName (PadFilterType type);
    static juce::String getPadFilterSlopeName (int slopeIndex);
    static juce::String getPadLoopModeName (PadLoopMode mode);
    static int getSequencerStepsPerBar (int divisionIndex);
    static double getSequencerQuarterNotesPerStep (int divisionIndex);

    juce::StringArray getBrowserFolders() const;
    juce::Result addBrowserFolder (const juce::File& folder);
    juce::Result removeBrowserFolder (const juce::File& folder);

    int getEditorSelectedPad() const noexcept;
    void setEditorSelectedPad (int padIndex) noexcept;
    bool isEditorShowingPadSettings() const noexcept;
    void setEditorShowingPadSettings (bool shouldShow) noexcept;
    int getEditorViewIndex() const noexcept;
    void setEditorViewIndex (int viewIndex) noexcept;
    BrowserMode getEditorBrowserMode() const noexcept;
    void setEditorBrowserMode (BrowserMode mode) noexcept;
    juce::String getEditorBrowserTreeState (BrowserMode mode) const;
    void setEditorBrowserTreeState (BrowserMode mode,
                                    const juce::String& state);

    juce::File getPortableDataDirectory() const;
    juce::File getPortableSamplesDirectory() const;
    juce::File getPortableKitsDirectory() const;
    juce::File getPortablePatternsDirectory() const;
    juce::File getPortableProjectsDirectory() const;
    juce::Result savePortableSettingsNow();
    int getEditorZoomPercent() const noexcept;
    void setEditorZoomPercent (int zoomPercent) noexcept;
    juce::Result saveEditorZoomNow();
    bool hasUnsavedPortableChanges() const noexcept;

    static bool isSupportedAudioFile (const juce::File& file);
    static bool isSupportedKitFile (const juce::File& file);
    static bool isSupportedPatternFile (const juce::File& file);
    static bool isSupportedPatternSetFile (const juce::File& file);
    static bool isSupportedProjectFile (const juce::File& file);

private:
    struct PadHostParameters
    {
        juce::RangedAudioParameter* volume = nullptr;
        juce::RangedAudioParameter* pan = nullptr;
        juce::RangedAudioParameter* tune = nullptr;
        juce::RangedAudioParameter* outputBus = nullptr;
        juce::RangedAudioParameter* delaySend = nullptr;
        juce::RangedAudioParameter* reverbSend = nullptr;
        juce::RangedAudioParameter* midiNote = nullptr;
        juce::RangedAudioParameter* mute = nullptr;
        juce::RangedAudioParameter* solo = nullptr;
        juce::RangedAudioParameter* reverse = nullptr;
        juce::RangedAudioParameter* chokeGroup = nullptr;
        juce::RangedAudioParameter* ampCurve = nullptr;
        juce::RangedAudioParameter* ampAttack = nullptr;
        juce::RangedAudioParameter* ampDecay = nullptr;
        juce::RangedAudioParameter* ampSustain = nullptr;
        juce::RangedAudioParameter* ampRelease = nullptr;
        juce::RangedAudioParameter* loopEnabled = nullptr;
        juce::RangedAudioParameter* loopMode = nullptr;
        juce::RangedAudioParameter* sequencerGated = nullptr;
        juce::RangedAudioParameter* filterCutoff = nullptr;
        juce::RangedAudioParameter* filterResonance = nullptr;
        juce::RangedAudioParameter* filterDrive = nullptr;
        juce::RangedAudioParameter* filterEnabled = nullptr;
        juce::RangedAudioParameter* filterType = nullptr;
        juce::RangedAudioParameter* filterSlope = nullptr;
        juce::RangedAudioParameter* highPassCutoff = nullptr;
        juce::RangedAudioParameter* compressorEnabled = nullptr;
        juce::RangedAudioParameter* compressorThreshold = nullptr;
        juce::RangedAudioParameter* compressorRatio = nullptr;
        juce::RangedAudioParameter* compressorAttack = nullptr;
        juce::RangedAudioParameter* compressorRelease = nullptr;
        juce::RangedAudioParameter* compressorKnee = nullptr;
        juce::RangedAudioParameter* compressorGain = nullptr;
        juce::RangedAudioParameter* saturationAmount = nullptr;
        juce::RangedAudioParameter* saturationHardClip = nullptr;
        juce::RangedAudioParameter* saturationEnabled = nullptr;
    };

    struct LaneHostParameters
    {
        juce::RangedAudioParameter* division = nullptr;
        juce::RangedAudioParameter* loopLength = nullptr;
    };

    enum class EnvelopeStage
    {
        attack,
        decay,
        sustain,
        release
    };

    struct Voice
    {
        std::shared_ptr<const SampleData> sample;
        double position = 0.0;
        double increment = 1.0;
        double rangeStart = 0.0;
        double rangeEnd = 1.0;
        double loopStart = 0.0;
        double loopEnd = 1.0;
        float velocity = 1.0f;
        int delaySamples = 0;
        int chokeAtOutputSample = -1;
        int releaseAtOutputSample = -1;
        float envelopeLevel = 1.0f;
        float envelopeAttackCurve = 0.0f;
        float envelopeSustain = 1.0f;
        float envelopeDelta = 0.0f;
        int envelopeSamplesRemaining = 0;
        int envelopeDecaySamples = 0;
        int envelopeReleaseSamples = 0;
        EnvelopeStage envelopeStage = EnvelopeStage::sustain;
        std::uint64_t sampleRevision = 0;
        bool looping = false;
        bool pingPongLoop = false;
        bool interfaceTriggered = false;
        bool sequencerTriggered = false;
        bool sequencerGated = false;
        bool active = false;
    };

    struct LaneSequenceState
    {
        LaneSequenceState()
        {
            for (auto& velocity : stepVelocities)
                velocity.store (0);
        }

        std::array<std::atomic<std::uint8_t>, maximumStepsPerLane> stepVelocities;
        std::atomic<int> division { 4 };
        std::atomic<int> loopLength { 16 };
        std::atomic<int> activeStep { -1 };
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
        std::atomic<int> outputBus { 0 };
        std::atomic<float> delaySend { 0.0f };
        std::atomic<float> reverbSend { 0.0f };
        std::atomic<int> chokeGroup { 0 };
        std::atomic<float> ampCurve { 0.0f };
        std::atomic<float> ampAttackMs { 0.0f };
        std::atomic<float> ampDecayMs { 0.0f };
        std::atomic<float> ampSustain { 1.0f };
        std::atomic<float> ampReleaseMs { 0.0f };
        std::atomic<float> filterCutoffHz { 20000.0f };
        std::atomic<float> filterResonance { 0.0f };
        std::atomic<float> filterDriveDb { 0.0f };
        std::atomic<bool> filterEnabled { false };
        std::atomic<int> filterType { static_cast<int> (PadFilterType::lowPass) };
        std::atomic<int> filterSlope { 1 };
        std::atomic<float> highPassCutoffHz { 0.0f };
        std::atomic<bool> compressorEnabled { false };
        std::atomic<float> compressorThresholdDb { -18.0f };
        std::atomic<float> compressorRatio { 4.0f };
        std::atomic<float> compressorAttackMs { 10.0f };
        std::atomic<float> compressorReleaseMs { 100.0f };
        std::atomic<float> compressorKneeDb { 6.0f };
        std::atomic<float> compressorGainDb { 0.0f };
        std::atomic<float> saturationAmount { 0.0f };
        std::atomic<float> saturationHardClipAmount { 0.0f };
        std::atomic<bool> saturationEnabled { false };
        std::atomic<int> sampleStart { 0 };
        std::atomic<int> sampleEnd { 0 };
        std::atomic<bool> loopEnabled { false };
        std::atomic<int> loopMode { static_cast<int> (PadLoopMode::normal) };
        std::atomic<bool> sequencerGated { false };
        std::atomic<int> loopStart { 0 };
        std::atomic<int> loopEnd { 0 };
        std::shared_ptr<const SampleData> sample;
        std::array<Voice, voicesPerPad> voices;
        std::atomic<std::uint64_t> sampleRevision { 0 };
        std::atomic<std::uint64_t> activityCounter { 0 };
        int nextVoice = 0;
        juce::String samplePath;
        juce::String displayName { "EMPTY" };
    };

    struct PatternLaneState
    {
        std::array<std::uint8_t, maximumStepsPerLane> stepVelocities {};
        int division = 4;
        int loopLength = 16;
    };

    struct KitPadState
    {
        std::shared_ptr<const SampleData> sample;
        juce::String samplePath;
        juce::String displayName { "EMPTY" };
        int midiNote = 36;
        bool muted = false;
        bool soloed = false;
        bool reversed = false;
        float volumeDb = 0.0f;
        float pan = 0.0f;
        float tuneSemitones = 0.0f;
        int outputBus = 0;
        float delaySend = 0.0f;
        float reverbSend = 0.0f;
        int chokeGroup = 0;
        float ampCurve = 0.0f;
        float ampAttackMs = 0.0f;
        float ampDecayMs = 0.0f;
        float ampSustain = 1.0f;
        float ampReleaseMs = 0.0f;
        float filterCutoffHz = 20000.0f;
        float filterResonance = 0.0f;
        float filterDriveDb = 0.0f;
        bool filterEnabled = false;
        int filterType = static_cast<int> (PadFilterType::lowPass);
        int filterSlope = 1;
        float highPassCutoffHz = 0.0f;
        bool compressorEnabled = false;
        float compressorThresholdDb = -18.0f;
        float compressorRatio = 4.0f;
        float compressorAttackMs = 10.0f;
        float compressorReleaseMs = 100.0f;
        float compressorKneeDb = 6.0f;
        float compressorGainDb = 0.0f;
        float saturationAmount = 0.0f;
        float saturationHardClipAmount = 0.0f;
        bool saturationEnabled = false;
        int sampleStart = 0;
        int sampleEnd = 0;
        bool loopEnabled = false;
        int loopMode = static_cast<int> (PadLoopMode::normal);
        bool sequencerGated = false;
        int loopStart = 0;
        int loopEnd = 0;
    };

    struct StoredPattern
    {
        std::array<PatternLaneState, numberOfPads> lanes;
        juce::String name;
        juce::String filePath;
        int bars = 1;
        bool assigned = false;
    };

    struct PadFilterDspState
    {
        std::array<std::array<float, 4>, 2> mainZ1 {};
        std::array<std::array<float, 4>, 2> mainZ2 {};
        std::array<float, 2> mainOnePole {};
        std::array<std::array<float, 4>, 2> formantAZ1 {};
        std::array<std::array<float, 4>, 2> formantAZ2 {};
        std::array<std::array<float, 4>, 2> formantBZ1 {};
        std::array<std::array<float, 4>, 2> formantBZ2 {};
        std::array<std::array<float, 8>, 2> ladderStages {};
        std::array<float, 2> highPassZ1 {};
        std::array<float, 2> highPassZ2 {};
        std::array<std::vector<float>, 2> combDelay;
        int combWritePosition = 0;
        int lastFilterType = -1;
        float wetMix = 0.0f;
        bool wasEnabled = false;
        bool outputWasEnabled = false;
    };

    struct PadCompressorDspState
    {
        float gain = 1.0f;
        float outputGain = 1.0f;
        float wetMix = 0.0f;
        bool wasEnabled = false;
    };

    struct PadSaturationDspState
    {
        float wetMix = 0.0f;
        bool wasEnabled = false;
    };

    std::array<PadState, numberOfPads> pads;
    std::shared_ptr<const SampleData> browserPreviewSample;
    Voice browserPreviewVoice;
    std::atomic<bool> browserPreviewTriggerPending { false };
    std::array<LaneSequenceState, numberOfPads> sequenceLanes;
    std::array<std::atomic<float>, numberOfPads> pendingInterfaceVelocities;
    std::atomic<std::uint32_t> pendingInterfaceTriggers { 0 };
    std::atomic<std::uint32_t> pendingInterfaceReleases { 0 };
    std::atomic<bool> sequencerEnabled { false };
    std::atomic<int> patternMidiMode {
        static_cast<int> (PatternMidiMode::select)
    };
    std::atomic<int> patternSyncMode {
        static_cast<int> (PatternSyncMode::played)
    };
    std::atomic<bool> patternGateActive { false };
    std::atomic<bool> patternGateWaitingForSelection { false };
    std::atomic<int> activePatternGateNote { -1 };
    std::atomic<int> pendingPatternGateStartNote { -1 };
    std::atomic<std::uint64_t> patternGateRestartCounter { 0 };
    std::atomic<int> patternBars { 1 };
    std::atomic<double> sequencerPatternPositionQuarterNotes { 0.0 };
    std::array<StoredPattern, numberOfPatterns> storedPatterns;
    juce::String currentKitName { "New Kit" };
    juce::String currentKitFilePath;
    juce::String currentPatternSetName { "New Pattern Set" };
    juce::String currentPatternSetFilePath;
    juce::String currentProjectName { "New Project" };
    juce::String currentProjectFilePath;
    KitPadState copiedPad;
    std::atomic<bool> copiedPadAvailable { false };
    StoredPattern copiedPattern;
    std::atomic<bool> copiedPatternAvailable { false };
    StoredPattern undoPattern;
    std::atomic<int> undoPatternIndex { -1 };
    PatternLaneState copiedSequenceLane;
    std::atomic<bool> copiedSequenceLaneAvailable { false };
    PatternLaneState undoSequenceLane;
    std::atomic<int> undoSequenceLaneIndex { -1 };
    std::array<int, maximumPatternPlaybackSteps> undoPatternPlaybackSteps {};
    std::atomic<bool> undoPatternPlaybackAvailable { false };
    std::array<std::atomic<int>, numberOfPatterns> patternMidiNotes;
    std::atomic<int> currentPatternIndex { 0 };
    std::atomic<int> pendingPatternSelection { -1 };
    std::atomic<int> queuedSyncedPatternSelection { -1 };
    std::atomic<int> queuedSyncedPatternGateNote { -1 };
    std::atomic<double> queuedSyncedPatternBoundaryPpq { 0.0 };
    std::atomic<int> pendingPatternPlaybackSelection { -1 };
    std::atomic<bool> patternPlaybackSwitchPending { false };
    std::atomic<std::uint64_t> patternChangeCounter { 0 };
    std::atomic<std::uint64_t> patternTimelineResetCounter { 0 };
    std::atomic<std::uint64_t> patternPlaybackSwitchCounter { 0 };
    std::array<std::atomic<int>, maximumPatternPlaybackSteps>
        patternPlaybackSteps;
    std::array<std::atomic<int>, numberOfPatterns> patternPlaybackPatternBars;
    std::atomic<int> patternPlaybackBars { 1 };
    std::atomic<int> activePatternPlaybackStep { -1 };
    std::atomic<bool> patternPlaybackChainEnabled { true };
    std::atomic<bool> patternPlaybackLoopEnabled { false };
    std::atomic<bool> sampleMarkerSnapEnabled { false };

    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> padRenderBuffer;
    juce::AudioBuffer<float> delaySendBuffer;
    juce::AudioBuffer<float> reverbSendBuffer;
    std::array<PadFilterDspState, numberOfPads> padFilterDspStates;
    std::array<PadCompressorDspState, numberOfPads> padCompressorDspStates;
    std::array<PadSaturationDspState, numberOfPads> padSaturationDspStates;
    std::atomic<bool> globalDelayEnabled { false };
    std::atomic<bool> globalDelaySyncEnabled { false };
    std::atomic<int> globalDelaySyncDivision { 5 };
    std::atomic<float> globalDelayTimeMs { 250.0f };
    std::atomic<float> globalDelayFeedback { 0.35f };
    std::atomic<float> globalDelayMix { 0.25f };
    std::atomic<float> globalDelayDuck { 0.0f };
    std::atomic<float> globalDelayDuckAttackMs { 10.0f };
    std::atomic<float> globalDelayDuckReleaseMs { 250.0f };
    std::atomic<bool> globalReverbEnabled { false };
    std::atomic<float> globalReverbSize { 0.50f };
    std::atomic<float> globalReverbDamping { 0.50f };
    std::atomic<float> globalReverbWidth { 1.0f };
    std::atomic<float> globalReverbMix { 0.20f };
    std::atomic<float> globalReverbDuck { 0.0f };
    std::atomic<float> masterVolumeDb { 0.0f };
    std::array<std::vector<float>, 2> globalDelayBuffer;
    std::vector<float> globalReverbDuckGain;
    std::array<float, 2> globalDelayFeedbackLowPass {};
    int globalDelayWritePosition = 0;
    float globalDelayCurrentSamples = 0.0f;
    float globalDelayBypassMix = 0.0f;
    float globalReverbBypassMix = 0.0f;
    float globalDelayDuckEnvelope = 0.0f;
    float globalReverbDuckEnvelope = 0.0f;
    float currentMasterGain = 1.0f;
    std::atomic<double> currentHostTempoBpm { 120.0 };
    std::atomic<double> currentHostPpqPosition { 0.0 };
    std::atomic<bool> currentHostTransportPlaying { false };
    std::atomic<bool> currentHostPpqAvailable { false };
    juce::Reverb globalReverb;
    bool globalDelayWasEnabled = false;
    bool globalReverbWasEnabled = false;
    double currentSampleRate = 44100.0;
    std::array<juce::int64, numberOfPads> lastSequenceAbsoluteSteps;
    int patternPlaybackChainSlot = -1;
    double patternPlaybackChainPositionQuarterNotes = 0.0;
    double patternPlaybackChainLengthQuarterNotes = 0.0;
    std::uint64_t lastPatternTimelineResetCounter = 0;
    std::uint64_t lastPatternPlaybackSwitchCounter = 0;
    std::uint64_t lastPatternGateRestartCounter = 0;
    double fallbackSequencerPpq = 0.0;
    bool sequencerWasPlaying = false;

    mutable juce::CriticalSection stateLock;
    juce::StringArray browserFolders;
    juce::String samplesBrowserTreeState;
    juce::String kitsBrowserTreeState;
    juce::String patternsBrowserTreeState;
    juce::String projectsBrowserTreeState;
    std::atomic<int> editorSelectedPad { 0 };
    std::atomic<int> editorViewIndex { 0 };
    std::atomic<int> editorBrowserMode {
        static_cast<int> (BrowserMode::samples)
    };
    std::atomic<int> editorZoomPercent { 100 };
    std::atomic<bool> portableSettingsDirty { false };
    std::atomic<bool> restoringHostState { false };
    std::atomic<float> hostStateRevision { 0.0f };
    std::array<PadHostParameters, numberOfPads> padHostParameters;
    std::array<LaneHostParameters, numberOfPads> laneHostParameters;
    juce::RangedAudioParameter* sequencerEnabledParameter = nullptr;
    juce::RangedAudioParameter* patternMidiModeParameter = nullptr;
    juce::RangedAudioParameter* patternSyncModeParameter = nullptr;
    juce::RangedAudioParameter* patternPlaybackChainEnabledParameter = nullptr;
    juce::RangedAudioParameter* patternPlaybackLoopParameter = nullptr;
    juce::RangedAudioParameter* patternBarsParameter = nullptr;
    juce::RangedAudioParameter* sampleMarkerSnapParameter = nullptr;
    juce::RangedAudioParameter* stateRevisionParameter = nullptr;
    juce::RangedAudioParameter* globalDelayEnabledParameter = nullptr;
    juce::RangedAudioParameter* globalDelaySyncEnabledParameter = nullptr;
    juce::RangedAudioParameter* globalDelaySyncDivisionParameter = nullptr;
    juce::RangedAudioParameter* globalDelayTimeParameter = nullptr;
    juce::RangedAudioParameter* globalDelayFeedbackParameter = nullptr;
    juce::RangedAudioParameter* globalDelayMixParameter = nullptr;
    juce::RangedAudioParameter* globalDelayDuckParameter = nullptr;
    juce::RangedAudioParameter* globalDelayDuckAttackParameter = nullptr;
    juce::RangedAudioParameter* globalDelayDuckReleaseParameter = nullptr;
    juce::RangedAudioParameter* globalReverbEnabledParameter = nullptr;
    juce::RangedAudioParameter* globalReverbSizeParameter = nullptr;
    juce::RangedAudioParameter* globalReverbDampingParameter = nullptr;
    juce::RangedAudioParameter* globalReverbWidthParameter = nullptr;
    juce::RangedAudioParameter* globalReverbMixParameter = nullptr;
    juce::RangedAudioParameter* globalReverbDuckParameter = nullptr;
    juce::RangedAudioParameter* masterVolumeParameter = nullptr;

    void triggerPadOnAudioThread (int padIndex, float velocity,
                                  int delaySamples = 0,
                                  bool triggeredBySequencer = false,
                                  bool triggeredByInterface = false);
    void startVoiceEnvelope (Voice& voice, const PadState& pad);
    void startVoiceDecay (Voice& voice);
    void beginVoiceRelease (Voice& voice);
    void advanceVoiceEnvelope (Voice& voice);
    void releaseSequencerVoicesOnAudioThread();
    void schedulePadSequencerGateRelease (int padIndex, int outputSample);
    void triggerBrowserPreviewOnAudioThread();
    void renderBrowserPreview (juce::AudioBuffer<float>& output);
    void processSequencerTriggers (int numSamples);
    void resetSequencerTimeline();
    void renderPadVoices (int padIndex,
                          juce::AudioBuffer<float>& output,
                          bool outputEnabled);
    void preparePadFilterDsp (int maximumBlockSize);
    void resetPadFilterDspState (int padIndex);
    void processPadFilter (int padIndex, juce::AudioBuffer<float>& buffer);
    void resetPadCompressorDspState (int padIndex);
    void processPadCompressor (int padIndex, juce::AudioBuffer<float>& buffer);
    void processPadSaturation (int padIndex, juce::AudioBuffer<float>& buffer);
    void prepareGlobalEffects();
    void processGlobalDelay (juce::AudioBuffer<float>& buffer);
    void processGlobalReverb (juce::AudioBuffer<float>& buffer);
    bool anyPadIsSoloed() const;
    int snapMarkerPosition (int padIndex, int requestedPosition,
                            int minimumPosition, int maximumPosition,
                            int currentPosition, int direction) const;
    bool isValidPadIndex (int padIndex) const noexcept;
    bool isValidPatternIndex (int patternIndex) const noexcept;
    void initialiseHostParameters();
    bool setHostParameterValue (juce::RangedAudioParameter* parameter,
                                float denormalisedValue);
    void markHostParameterStateChanged() noexcept;
    void markPortableSettingsDirty() noexcept;

    void handleAsyncUpdate() override;
    void captureCurrentPattern();
    void capturePatternUndoState (int patternIndex);
    PatternLaneState captureSequenceLaneState (int laneIndex) const;
    void applySequenceLaneState (int laneIndex,
                                 const PatternLaneState& state);
    void captureSequenceLaneUndoState (int laneIndex);
    void capturePatternPlaybackUndoState();
    void selectPatternImmediately (int patternIndex);
    void queueSyncedPatternSelection (int patternIndex, int gateNote,
                                      double requestPpq);
    void dispatchQueuedPatternSelection();
    void serviceQueuedPatternSelection (double blockStartPpq);
    double getNextPatternSyncBoundary (double requestPpq) const noexcept;
    void applyStoredPattern (int patternIndex, bool restartTimeline = true);
    void selectPatternFromPlaybackLane (int patternIndex);
    int getPatternBarsForPlayback (int patternIndex) const noexcept;
    void refreshPatternPlaybackBars();
    void initialisePatternSlot (int patternIndex);
    std::shared_ptr<const SampleData> createSampleData (const juce::File& file,
                                                        juce::String& errorMessage);
    KitPadState capturePadState (int padIndex) const;
    void applyPadState (int padIndex, const KitPadState& state);
    std::unique_ptr<juce::XmlElement> createKitXml() const;
    juce::Result loadKitXml (const juce::XmlElement& kitXml);
    std::unique_ptr<juce::XmlElement> createPatternXml (int patternIndex) const;
    std::unique_ptr<juce::XmlElement> createPatternSetXml() const;
    std::unique_ptr<juce::XmlElement> createGlobalFxXml() const;
    void loadGlobalFxXml (const juce::XmlElement* globalFxXml);
    juce::Result parsePatternXml (int patternIndex,
                                  const juce::XmlElement& patternXml,
                                  StoredPattern& destination) const;
    juce::Result loadPatternXmlIntoSlot (int patternIndex,
                                         const juce::XmlElement& patternXml);
    juce::Result loadPatternSetXml (const juce::XmlElement& patternSetXml);
    juce::Result saveStoredPatternToFile (int patternIndex,
                                           const juce::File& file);

    void loadPortableSettings();
    juce::Result savePortableSettings();
    juce::Result saveBrowserFoldersNow();
    juce::File getPortableSettingsFile() const;
    juce::String makeStoredPath (const juce::File& file) const;
    juce::File resolveStoredPath (const juce::String& storedPath) const;
    juce::String encodeLaneSteps (int laneIndex) const;
    void decodeLaneSteps (int laneIndex, const juce::String& encodedSteps);
    juce::String encodePatternPlaybackSteps() const;
    void decodePatternPlaybackSteps (const juce::String& encodedSteps);
    static juce::File getThisModuleFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SVDrummerAudioProcessor)
};
