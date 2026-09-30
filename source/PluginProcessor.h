#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class SVDrummerAudioProcessor final : public juce::AudioProcessor,
                                      private juce::AsyncUpdater
{
public:
    static constexpr int numberOfPads = 16;
    static constexpr int numberOfPatterns = 16;
    static constexpr int voicesPerPad = 4;
    static constexpr int sequencerDivisionCount = 9;
    static constexpr int maximumPatternBars = 16;
    static constexpr int maximumStepsPerBar = 64;
    static constexpr int maximumStepsPerLane = maximumPatternBars * maximumStepsPerBar;

    enum class PatternMidiMode
    {
        select = 0,
        gate,
        hold
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
    int getPadChokeGroup (int padIndex) const;
    void setPadChokeGroup (int padIndex, int chokeGroup);
    float getPadAmpAttackMs (int padIndex) const;
    void setPadAmpAttackMs (int padIndex, float milliseconds);
    float getPadAmpDecayMs (int padIndex) const;
    void setPadAmpDecayMs (int padIndex, float milliseconds);
    float getPadAmpSustain (int padIndex) const;
    void setPadAmpSustain (int padIndex, float level);
    float getPadAmpReleaseMs (int padIndex) const;
    void setPadAmpReleaseMs (int padIndex, float milliseconds);
    int getPadSampleLength (int padIndex) const;
    int getPadSampleStart (int padIndex) const;
    void setPadSampleStart (int padIndex, int samplePosition,
                            int snapDirection = 0);
    int getPadSampleEnd (int padIndex) const;
    void setPadSampleEnd (int padIndex, int samplePosition,
                          int snapDirection = 0);
    bool isPadLoopEnabled (int padIndex) const;
    void setPadLoopEnabled (int padIndex, bool shouldLoop);
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

    bool isSequencerEnabled() const;
    void setSequencerEnabled (bool shouldBeEnabled);
    void stopPatternMidiPlayback();
    PatternMidiMode getPatternMidiMode() const;
    void setPatternMidiMode (PatternMidiMode newMode);
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

    int getCurrentPatternIndex() const;
    std::uint64_t getPatternChangeRevision() const;
    void selectPattern (int patternIndex);
    bool isPatternAssigned (int patternIndex) const;
    bool patternHasSteps (int patternIndex) const;
    juce::String getPatternName (int patternIndex) const;
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
    juce::Result saveKitToFile (const juce::File& file) const;
    bool kitHasSamples() const;
    juce::Result loadProjectFromFile (const juce::File& file);
    juce::Result saveProjectToFile (const juce::File& file);
    juce::StringArray getMissingSampleDescriptions() const;
    int relinkMissingSamplesFromFolder (const juce::File& folder,
                                        bool searchSubfolders = true);

    static juce::String getSequencerDivisionName (int divisionIndex);
    static int getSequencerStepsPerBar (int divisionIndex);
    static double getSequencerQuarterNotesPerStep (int divisionIndex);

    juce::StringArray getBrowserFolders() const;
    juce::Result addBrowserFolder (const juce::File& folder);
    juce::Result removeBrowserFolder (const juce::File& folder);

    int getEditorSelectedPad() const noexcept;
    void setEditorSelectedPad (int padIndex) noexcept;
    bool isEditorShowingPadSettings() const noexcept;
    void setEditorShowingPadSettings (bool shouldShow) noexcept;
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
        float envelopeSustain = 1.0f;
        float envelopeDelta = 0.0f;
        int envelopeSamplesRemaining = 0;
        int envelopeDecaySamples = 0;
        int envelopeReleaseSamples = 0;
        EnvelopeStage envelopeStage = EnvelopeStage::sustain;
        std::uint64_t sampleRevision = 0;
        bool looping = false;
        bool sequencerTriggered = false;
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
        std::atomic<int> chokeGroup { 0 };
        std::atomic<float> ampAttackMs { 0.0f };
        std::atomic<float> ampDecayMs { 0.0f };
        std::atomic<float> ampSustain { 1.0f };
        std::atomic<float> ampReleaseMs { 0.0f };
        std::atomic<int> sampleStart { 0 };
        std::atomic<int> sampleEnd { 0 };
        std::atomic<bool> loopEnabled { false };
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
        int chokeGroup = 0;
        float ampAttackMs = 0.0f;
        float ampDecayMs = 0.0f;
        float ampSustain = 1.0f;
        float ampReleaseMs = 0.0f;
        int sampleStart = 0;
        int sampleEnd = 0;
        bool loopEnabled = false;
        int loopStart = 0;
        int loopEnd = 0;
    };

    struct StoredPattern
    {
        std::array<PatternLaneState, numberOfPads> lanes;
        juce::String name;
        int bars = 1;
        bool assigned = false;
    };

    std::array<PadState, numberOfPads> pads;
    std::shared_ptr<const SampleData> browserPreviewSample;
    Voice browserPreviewVoice;
    std::atomic<bool> browserPreviewTriggerPending { false };
    std::array<LaneSequenceState, numberOfPads> sequenceLanes;
    std::array<std::atomic<float>, numberOfPads> pendingInterfaceVelocities;
    std::atomic<std::uint32_t> pendingInterfaceTriggers { 0 };
    std::atomic<bool> sequencerEnabled { false };
    std::atomic<int> patternMidiMode {
        static_cast<int> (PatternMidiMode::select)
    };
    std::atomic<bool> patternGateActive { false };
    std::atomic<bool> patternGateWaitingForSelection { false };
    std::atomic<int> activePatternGateNote { -1 };
    std::atomic<int> pendingPatternGateStartNote { -1 };
    std::atomic<std::uint64_t> patternGateRestartCounter { 0 };
    std::atomic<int> patternBars { 1 };
    std::atomic<double> sequencerPatternPositionQuarterNotes { 0.0 };
    std::array<StoredPattern, numberOfPatterns> storedPatterns;
    StoredPattern copiedPattern;
    std::atomic<bool> copiedPatternAvailable { false };
    StoredPattern undoPattern;
    std::atomic<int> undoPatternIndex { -1 };
    std::array<std::atomic<int>, numberOfPatterns> patternMidiNotes;
    std::atomic<int> currentPatternIndex { 0 };
    std::atomic<int> pendingPatternSelection { -1 };
    std::atomic<std::uint64_t> patternChangeCounter { 0 };
    std::atomic<bool> sampleMarkerSnapEnabled { false };

    juce::AudioFormatManager formatManager;
    double currentSampleRate = 44100.0;
    std::array<juce::int64, numberOfPads> lastSequenceAbsoluteSteps;
    std::uint64_t lastPatternChangeCounter = 0;
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
    std::atomic<bool> editorShowingPadSettings { false };
    std::atomic<int> editorBrowserMode {
        static_cast<int> (BrowserMode::samples)
    };
    std::atomic<int> editorZoomPercent { 100 };
    std::atomic<bool> portableSettingsDirty { false };
    std::atomic<bool> restoringHostState { false };
    std::atomic<float> hostStateRevision { 0.0f };
    std::array<PadHostParameters, numberOfPads> padHostParameters;
    juce::RangedAudioParameter* sequencerEnabledParameter = nullptr;
    juce::RangedAudioParameter* patternMidiModeParameter = nullptr;
    juce::RangedAudioParameter* patternBarsParameter = nullptr;
    juce::RangedAudioParameter* sampleMarkerSnapParameter = nullptr;
    juce::RangedAudioParameter* stateRevisionParameter = nullptr;

    void triggerPadOnAudioThread (int padIndex, float velocity,
                                  int delaySamples = 0,
                                  bool triggeredBySequencer = false);
    void startVoiceEnvelope (Voice& voice, const PadState& pad);
    void startVoiceDecay (Voice& voice);
    void beginVoiceRelease (Voice& voice);
    void advanceVoiceEnvelope (Voice& voice);
    void stopSequencerLoopVoicesOnAudioThread();
    void triggerBrowserPreviewOnAudioThread();
    void renderBrowserPreview (juce::AudioBuffer<float>& output);
    void processSequencerTriggers (int numSamples);
    void resetSequencerTimeline();
    void renderPadVoices (int padIndex,
                          juce::AudioBuffer<float>& output,
                          bool outputEnabled);
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
    void applyStoredPattern (int patternIndex);
    void initialisePatternSlot (int patternIndex);
    std::shared_ptr<const SampleData> createSampleData (const juce::File& file,
                                                        juce::String& errorMessage);
    std::unique_ptr<juce::XmlElement> createKitXml() const;
    juce::Result loadKitXml (const juce::XmlElement& kitXml);
    std::unique_ptr<juce::XmlElement> createPatternXml (int patternIndex) const;
    std::unique_ptr<juce::XmlElement> createPatternSetXml() const;
    juce::Result parsePatternXml (int patternIndex,
                                  const juce::XmlElement& patternXml,
                                  StoredPattern& destination) const;
    juce::Result loadPatternXmlIntoSlot (int patternIndex,
                                         const juce::XmlElement& patternXml);
    juce::Result loadPatternSetXml (const juce::XmlElement& patternSetXml);
    juce::Result saveStoredPatternToFile (int patternIndex,
                                           const juce::File& file) const;

    void loadPortableSettings();
    juce::Result savePortableSettings();
    juce::Result saveBrowserFoldersNow();
    juce::File getPortableSettingsFile() const;
    juce::String makeStoredPath (const juce::File& file) const;
    juce::File resolveStoredPath (const juce::String& storedPath) const;
    juce::String encodeLaneSteps (int laneIndex) const;
    void decodeLaneSteps (int laneIndex, const juce::String& encodedSteps);
    static juce::File getThisModuleFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SVDrummerAudioProcessor)
};
