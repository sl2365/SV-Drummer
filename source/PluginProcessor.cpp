#include "PluginProcessor.h"
#include "PluginEditor.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

#include <cmath>
#include <limits>

namespace
{
constexpr int waveformPointCount = 160;

#if JUCE_WINDOWS
int moduleLocationAnchor = 0;
#endif
}

SVDrummerAudioProcessor::SVDrummerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();

    for (int index = 0; index < numberOfPads; ++index)
    {
        pads[static_cast<std::size_t> (index)].midiNote.store (36 + index);
        pendingInterfaceVelocities[static_cast<std::size_t> (index)].store (1.0f);
    }

    for (int index = 0; index < numberOfPatterns; ++index)
    {
        storedPatterns[static_cast<std::size_t> (index)].name
            = "Pattern " + juce::String (index + 1).paddedLeft ('0', 2);
        patternMidiNotes[static_cast<std::size_t> (index)].store (-1);
    }

    lastSequenceAbsoluteSteps.fill ((std::numeric_limits<juce::int64>::min)());

    loadPortableSettings();

    if (! storedPatterns[0].assigned)
        captureCurrentPattern();

    if (getBrowserFolders().isEmpty())
    {
        const auto portableSamples = getPortableSamplesDirectory();

        if (portableSamples.createDirectory().wasOk())
            addBrowserFolder (portableSamples);
    }
}

SVDrummerAudioProcessor::~SVDrummerAudioProcessor()
{
    cancelPendingUpdate();
    flushPortableSettingsIfNeeded();
}

const juce::String SVDrummerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SVDrummerAudioProcessor::acceptsMidi() const       { return true; }
bool SVDrummerAudioProcessor::producesMidi() const      { return false; }
bool SVDrummerAudioProcessor::isMidiEffect() const      { return false; }
double SVDrummerAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SVDrummerAudioProcessor::getNumPrograms()                 { return 1; }
int SVDrummerAudioProcessor::getCurrentProgram()              { return 0; }
void SVDrummerAudioProcessor::setCurrentProgram (int)         {}
const juce::String SVDrummerAudioProcessor::getProgramName (int) { return {}; }
void SVDrummerAudioProcessor::changeProgramName (int, const juce::String&) {}

void SVDrummerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    lastPatternChangeCounter = patternChangeCounter.load();
    resetSequencerTimeline();

    for (auto& pad : pads)
        for (auto& voice : pad.voices)
            voice.active = false;
}

void SVDrummerAudioProcessor::releaseResources()
{
}

bool SVDrummerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void SVDrummerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (! message.isNoteOn())
            continue;

        const int note = message.getNoteNumber();
        const float velocity = juce::jlimit (0.0f, 1.0f, message.getFloatVelocity());
        bool selectsPattern = false;

        for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
        {
            if (patternMidiNotes[static_cast<std::size_t> (patternIndex)].load() == note)
            {
                pendingPatternSelection.store (patternIndex);
                triggerAsyncUpdate();
                selectsPattern = true;
                break;
            }
        }

        if (selectsPattern)
            continue;

        for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
            if (pads[static_cast<std::size_t> (padIndex)].midiNote.load() == note)
                triggerPadOnAudioThread (
                    padIndex, velocity,
                    juce::jlimit (0, juce::jmax (0, buffer.getNumSamples() - 1),
                                  metadata.samplePosition));
    }

    const auto pending = pendingInterfaceTriggers.exchange (0);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
        if ((pending & (std::uint32_t { 1 } << static_cast<unsigned int> (padIndex))) != 0)
            triggerPadOnAudioThread (
                padIndex,
                pendingInterfaceVelocities[static_cast<std::size_t> (padIndex)].load(),
                0);

    processSequencerTriggers (buffer.getNumSamples());

    const bool hasSolo = anyPadIsSoloed();

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& pad = pads[static_cast<std::size_t> (padIndex)];
        const bool outputEnabled = ! pad.muted.load()
                                && (! hasSolo || pad.soloed.load());

        renderPadVoices (padIndex, buffer, outputEnabled);
    }
}

void SVDrummerAudioProcessor::triggerPadOnAudioThread (int padIndex,
                                                        float velocity,
                                                        int delaySamples)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const auto sample = std::atomic_load_explicit (&pad.sample, std::memory_order_acquire);

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
        return;

    int voiceIndex = -1;

    for (int index = 0; index < voicesPerPad; ++index)
    {
        if (! pad.voices[static_cast<std::size_t> (index)].active)
        {
            voiceIndex = index;
            break;
        }
    }

    if (voiceIndex < 0)
        voiceIndex = pad.nextVoice;

    pad.nextVoice = (voiceIndex + 1) % voicesPerPad;

    auto& voice = pad.voices[static_cast<std::size_t> (voiceIndex)];
    const bool reversed = pad.reversed.load();
    const double tuneRatio = std::pow (2.0,
                                      static_cast<double> (pad.tuneSemitones.load()) / 12.0);
    const double sourceRatio = sample->sourceSampleRate / juce::jmax (1.0, currentSampleRate);

    voice.sample = sample;
    voice.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    voice.delaySamples = juce::jmax (0, delaySamples);
    voice.increment = sourceRatio * tuneRatio * (reversed ? -1.0 : 1.0);
    voice.position = reversed ? static_cast<double> (sample->audio.getNumSamples() - 1)
                              : 0.0;
    voice.sampleRevision = pad.sampleRevision.load();
    voice.active = true;

    pad.activityCounter.fetch_add (1);
}

void SVDrummerAudioProcessor::renderPadVoices (int padIndex,
                                                juce::AudioBuffer<float>& output,
                                                bool outputEnabled)
{
    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const auto currentRevision = pad.sampleRevision.load();
    const float gain = juce::Decibels::decibelsToGain (pad.volumeDb.load(), -80.0f);
    const float pan = juce::jlimit (-1.0f, 1.0f, pad.pan.load());
    const float panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float leftPan = std::cos (panAngle);
    const float rightPan = std::sin (panAngle);
    const int outputChannels = output.getNumChannels();
    const int outputSamples = output.getNumSamples();

    for (auto& voice : pad.voices)
    {
        if (! voice.active || voice.sample == nullptr)
            continue;

        if (voice.sampleRevision != currentRevision)
        {
            voice.active = false;
            voice.sample.reset();
            continue;
        }

        const auto& source = voice.sample->audio;
        const int sourceSamples = source.getNumSamples();
        const int sourceChannels = source.getNumChannels();

        const int firstOutputSample = juce::jmin (outputSamples, voice.delaySamples);
        voice.delaySamples -= firstOutputSample;

        for (int outputSample = firstOutputSample; outputSample < outputSamples; ++outputSample)
        {
            if (voice.position < 0.0 || voice.position >= static_cast<double> (sourceSamples))
            {
                voice.active = false;
                voice.sample.reset();
                break;
            }

            const int firstIndex = juce::jlimit (
                0, sourceSamples - 1, static_cast<int> (std::floor (voice.position)));
            const int secondIndex = juce::jmin (sourceSamples - 1, firstIndex + 1);
            const float fraction = static_cast<float> (voice.position
                                                       - static_cast<double> (firstIndex));
            const float voiceGain = gain * voice.velocity;

            if (outputEnabled && outputChannels > 0)
            {
                const float firstLeft = source.getSample (0, firstIndex);
                const float secondLeft = source.getSample (0, secondIndex);
                const float left = juce::jmap (fraction, firstLeft, secondLeft);

                if (outputChannels == 1)
                {
                    output.addSample (0, outputSample, left * voiceGain);
                }
                else
                {
                    const int rightSourceChannel = juce::jmin (1, sourceChannels - 1);
                    const float firstRight = source.getSample (rightSourceChannel, firstIndex);
                    const float secondRight = source.getSample (rightSourceChannel, secondIndex);
                    const float right = juce::jmap (fraction, firstRight, secondRight);

                    output.addSample (0, outputSample, left * voiceGain * leftPan);
                    output.addSample (1, outputSample, right * voiceGain * rightPan);
                }
            }

            voice.position += voice.increment;
        }
    }
}

bool SVDrummerAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SVDrummerAudioProcessor::createEditor()
{
    return new SVDrummerAudioProcessorEditor (*this);
}

juce::Result SVDrummerAudioProcessor::loadSampleIntoPad (int padIndex,
                                                          const juce::File& file)
{
    if (! isValidPadIndex (padIndex))
        return juce::Result::fail ("Invalid pad number.");

    juce::String errorMessage;
    const auto newSample = createSampleData (file, errorMessage);

    if (newSample == nullptr)
        return juce::Result::fail (errorMessage);

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    pad.sampleRevision.fetch_add (1);
    std::atomic_store_explicit (&pad.sample, newSample, std::memory_order_release);

    {
        const juce::ScopedLock lock (stateLock);
        pad.samplePath = file.getFullPathName();
        pad.displayName = file.getFileNameWithoutExtension();
    }

    markPortableSettingsDirty();
    return juce::Result::ok();
}

std::shared_ptr<const SVDrummerAudioProcessor::SampleData>
SVDrummerAudioProcessor::createSampleData (const juce::File& file,
                                            juce::String& errorMessage)
{
    if (! file.existsAsFile())
    {
        errorMessage = "The sample file does not exist.";
        return {};
    }

    if (! isSupportedAudioFile (file))
    {
        errorMessage = "Supported formats are WAV, MP3, OGG, FLAC and AIFF.";
        return {};
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        errorMessage = "The sample could not be read.";
        return {};
    }

    if (reader->lengthInSamples > static_cast<juce::int64> ((std::numeric_limits<int>::max)()))
    {
        errorMessage = "The sample is too long to load into memory.";
        return {};
    }

    auto newSample = std::make_shared<SampleData>();
    const int sampleCount = static_cast<int> (reader->lengthInSamples);
    const int channelCount = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    newSample->audio.setSize (channelCount, sampleCount);
    newSample->audio.clear();

    if (! reader->read (&newSample->audio, 0, sampleCount, 0, true, true))
    {
        errorMessage = "The sample data could not be decoded.";
        return {};
    }

    newSample->sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    newSample->displayName = file.getFileNameWithoutExtension();
    newSample->fullPath = file.getFullPathName();
    newSample->waveform.reserve (waveformPointCount);

    for (int point = 0; point < waveformPointCount; ++point)
    {
        const int start = static_cast<int> (
            (static_cast<juce::int64> (point) * sampleCount) / waveformPointCount);
        const int end = juce::jmax (
            start + 1,
            static_cast<int> ((static_cast<juce::int64> (point + 1) * sampleCount)
                              / waveformPointCount));
        float minimum = 1.0f;
        float maximum = -1.0f;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto range = newSample->audio.findMinMax (
                channel, start, juce::jmin (sampleCount, end) - start);
            minimum = juce::jmin (minimum, range.getStart());
            maximum = juce::jmax (maximum, range.getEnd());
        }

        newSample->waveform.emplace_back (minimum, maximum);
    }

    errorMessage.clear();
    return std::shared_ptr<const SampleData> (std::move (newSample));
}

void SVDrummerAudioProcessor::clearPadSample (int padIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    pad.sampleRevision.fetch_add (1);
    std::atomic_store_explicit (&pad.sample,
                                std::shared_ptr<const SampleData>(),
                                std::memory_order_release);

    {
        const juce::ScopedLock lock (stateLock);
        pad.samplePath.clear();
        pad.displayName = "EMPTY";
    }

    markPortableSettingsDirty();
}

std::shared_ptr<const SVDrummerAudioProcessor::SampleData>
SVDrummerAudioProcessor::getPadSample (int padIndex) const
{
    if (! isValidPadIndex (padIndex))
        return {};

    return std::atomic_load_explicit (
        &pads[static_cast<std::size_t> (padIndex)].sample,
        std::memory_order_acquire);
}

int SVDrummerAudioProcessor::getPadMidiNote (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].midiNote.load()
         : 0;
}

void SVDrummerAudioProcessor::setPadMidiNote (int padIndex, int midiNote)
{
    if (! isValidPadIndex (padIndex))
        return;

    pads[static_cast<std::size_t> (padIndex)].midiNote.store (
        juce::jlimit (0, 127, midiNote));
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadMuted (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].muted.load();
}

void SVDrummerAudioProcessor::setPadMuted (int padIndex, bool shouldBeMuted)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].muted.store (shouldBeMuted);
        markPortableSettingsDirty();
    }
}

bool SVDrummerAudioProcessor::isPadSoloed (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].soloed.load();
}

void SVDrummerAudioProcessor::setPadSoloed (int padIndex, bool shouldBeSoloed)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].soloed.store (shouldBeSoloed);
        markPortableSettingsDirty();
    }
}

bool SVDrummerAudioProcessor::isPadReversed (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].reversed.load();
}

void SVDrummerAudioProcessor::setPadReversed (int padIndex, bool shouldBeReversed)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].reversed.store (shouldBeReversed);
        markPortableSettingsDirty();
    }
}

float SVDrummerAudioProcessor::getPadVolumeDb (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].volumeDb.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadVolumeDb (int padIndex, float decibels)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].volumeDb.store (
            juce::jlimit (-60.0f, 6.0f, decibels));
        markPortableSettingsDirty();
    }
}

float SVDrummerAudioProcessor::getPadPan (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].pan.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadPan (int padIndex, float pan)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].pan.store (
            juce::jlimit (-1.0f, 1.0f, pan));
        markPortableSettingsDirty();
    }
}

float SVDrummerAudioProcessor::getPadTuneSemitones (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].tuneSemitones.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadTuneSemitones (int padIndex, float semitones)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].tuneSemitones.store (
            juce::jlimit (-24.0f, 24.0f, semitones));
        markPortableSettingsDirty();
    }
}

juce::String SVDrummerAudioProcessor::getPadSamplePath (int padIndex) const
{
    if (! isValidPadIndex (padIndex))
        return {};

    const juce::ScopedLock lock (stateLock);
    return pads[static_cast<std::size_t> (padIndex)].samplePath;
}

juce::String SVDrummerAudioProcessor::getPadDisplayName (int padIndex) const
{
    if (! isValidPadIndex (padIndex))
        return {};

    const juce::ScopedLock lock (stateLock);
    return pads[static_cast<std::size_t> (padIndex)].displayName;
}

std::uint64_t SVDrummerAudioProcessor::getPadActivityCounter (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].activityCounter.load()
         : 0;
}

void SVDrummerAudioProcessor::triggerPadFromInterface (int padIndex, float velocity)
{
    if (! isValidPadIndex (padIndex))
        return;

    pendingInterfaceVelocities[static_cast<std::size_t> (padIndex)].store (
        juce::jlimit (0.0f, 1.0f, velocity));
    pendingInterfaceTriggers.fetch_or (
        std::uint32_t { 1 } << static_cast<unsigned int> (padIndex));
}

bool SVDrummerAudioProcessor::isSequencerEnabled() const
{
    return sequencerEnabled.load();
}

void SVDrummerAudioProcessor::setSequencerEnabled (bool shouldBeEnabled)
{
    sequencerEnabled.store (shouldBeEnabled);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPatternBars() const
{
    return patternBars.load();
}

void SVDrummerAudioProcessor::setPatternBars (int bars)
{
    const int newBars = juce::jlimit (1, maximumPatternBars, bars);
    patternBars.store (newBars);

    for (int lane = 0; lane < numberOfPads; ++lane)
    {
        auto& sequence = sequenceLanes[static_cast<std::size_t> (lane)];
        sequence.loopLength.store (juce::jlimit (
            1,
            getLaneMaximumLoopLength (lane),
            sequence.loopLength.load()));
    }

    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getLaneDivision (int laneIndex) const
{
    return isValidPadIndex (laneIndex)
         ? sequenceLanes[static_cast<std::size_t> (laneIndex)].division.load()
         : 3;
}

void SVDrummerAudioProcessor::setLaneDivision (int laneIndex, int divisionIndex)
{
    if (! isValidPadIndex (laneIndex))
        return;

    auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
    lane.division.store (juce::jlimit (0, sequencerDivisionCount - 1, divisionIndex));
    lane.loopLength.store (juce::jlimit (
        1, getLaneMaximumLoopLength (laneIndex), lane.loopLength.load()));
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getLaneLoopLength (int laneIndex) const
{
    return isValidPadIndex (laneIndex)
         ? sequenceLanes[static_cast<std::size_t> (laneIndex)].loopLength.load()
         : 1;
}

void SVDrummerAudioProcessor::setLaneLoopLength (int laneIndex, int lengthInSteps)
{
    if (! isValidPadIndex (laneIndex))
        return;

    sequenceLanes[static_cast<std::size_t> (laneIndex)].loopLength.store (
        juce::jlimit (1, getLaneMaximumLoopLength (laneIndex), lengthInSteps));
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getLaneMaximumLoopLength (int laneIndex) const
{
    if (! isValidPadIndex (laneIndex))
        return 1;

    return juce::jlimit (
        1,
        maximumStepsPerLane,
        getPatternBars() * getSequencerStepsPerBar (getLaneDivision (laneIndex)));
}

int SVDrummerAudioProcessor::getSequenceStepVelocity (int laneIndex, int stepIndex) const
{
    if (! isValidPadIndex (laneIndex)
        || stepIndex < 0 || stepIndex >= maximumStepsPerLane)
        return 0;

    return static_cast<int> (
        sequenceLanes[static_cast<std::size_t> (laneIndex)]
            .stepVelocities[static_cast<std::size_t> (stepIndex)].load());
}

void SVDrummerAudioProcessor::setSequenceStepVelocity (int laneIndex,
                                                        int stepIndex,
                                                        int velocity)
{
    if (! isValidPadIndex (laneIndex)
        || stepIndex < 0 || stepIndex >= maximumStepsPerLane)
        return;

    sequenceLanes[static_cast<std::size_t> (laneIndex)]
        .stepVelocities[static_cast<std::size_t> (stepIndex)]
        .store (static_cast<std::uint8_t> (juce::jlimit (0, 127, velocity)));
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getActiveSequenceStep (int laneIndex) const
{
    return isValidPadIndex (laneIndex)
         ? sequenceLanes[static_cast<std::size_t> (laneIndex)].activeStep.load()
         : -1;
}

double SVDrummerAudioProcessor::getSequencerPatternPositionQuarterNotes() const
{
    return sequencerPatternPositionQuarterNotes.load();
}

int SVDrummerAudioProcessor::getCurrentPatternIndex() const
{
    return currentPatternIndex.load();
}

void SVDrummerAudioProcessor::selectPattern (int newPatternIndex)
{
    if (! isValidPatternIndex (newPatternIndex))
        return;

    const int oldPatternIndex = currentPatternIndex.load();

    if (newPatternIndex == oldPatternIndex)
        return;

    captureCurrentPattern();

    if (! storedPatterns[static_cast<std::size_t> (newPatternIndex)].assigned)
        initialisePatternSlot (newPatternIndex, true);

    currentPatternIndex.store (newPatternIndex);
    applyStoredPattern (newPatternIndex);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPatternAssigned (int patternIndex) const
{
    return isValidPatternIndex (patternIndex)
        && storedPatterns[static_cast<std::size_t> (patternIndex)].assigned;
}

juce::String SVDrummerAudioProcessor::getPatternName (int patternIndex) const
{
    if (! isValidPatternIndex (patternIndex))
        return {};

    const juce::ScopedLock lock (stateLock);
    return storedPatterns[static_cast<std::size_t> (patternIndex)].name;
}

int SVDrummerAudioProcessor::getPatternMidiNote (int patternIndex) const
{
    return isValidPatternIndex (patternIndex)
         ? patternMidiNotes[static_cast<std::size_t> (patternIndex)].load()
         : -1;
}

void SVDrummerAudioProcessor::setPatternMidiNote (int patternIndex, int midiNote)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    patternMidiNotes[static_cast<std::size_t> (patternIndex)].store (
        juce::jlimit (-1, 127, midiNote));
    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::handleAsyncUpdate()
{
    const int requestedPattern = pendingPatternSelection.exchange (-1);

    if (isValidPatternIndex (requestedPattern))
        selectPattern (requestedPattern);
}

void SVDrummerAudioProcessor::captureCurrentPattern()
{
    const int patternIndex = currentPatternIndex.load();

    if (! isValidPatternIndex (patternIndex))
        return;

    auto& stored = storedPatterns[static_cast<std::size_t> (patternIndex)];
    stored.assigned = true;
    stored.bars = getPatternBars();

    if (stored.name.isEmpty())
        stored.name = "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2);

    for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
    {
        auto& targetLane = stored.lanes[static_cast<std::size_t> (laneIndex)];
        targetLane.division = getLaneDivision (laneIndex);
        targetLane.loopLength = getLaneLoopLength (laneIndex);

        for (int step = 0; step < maximumStepsPerLane; ++step)
            targetLane.stepVelocities[static_cast<std::size_t> (step)]
                = static_cast<std::uint8_t> (getSequenceStepVelocity (laneIndex, step));
    }

    const juce::ScopedLock lock (stateLock);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& sourcePad = pads[static_cast<std::size_t> (padIndex)];
        auto& targetPad = stored.pads[static_cast<std::size_t> (padIndex)];
        targetPad.sample = std::atomic_load_explicit (&sourcePad.sample,
                                                      std::memory_order_acquire);
        targetPad.samplePath = sourcePad.samplePath;
        targetPad.displayName = sourcePad.displayName;
        targetPad.midiNote = sourcePad.midiNote.load();
        targetPad.muted = sourcePad.muted.load();
        targetPad.soloed = sourcePad.soloed.load();
        targetPad.reversed = sourcePad.reversed.load();
        targetPad.volumeDb = sourcePad.volumeDb.load();
        targetPad.pan = sourcePad.pan.load();
        targetPad.tuneSemitones = sourcePad.tuneSemitones.load();
    }
}

void SVDrummerAudioProcessor::initialisePatternSlot (int patternIndex,
                                                      bool copyCurrentKit)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    StoredPattern newPattern;
    newPattern.assigned = true;
    newPattern.name = "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
        newPattern.pads[static_cast<std::size_t> (padIndex)].midiNote = 36 + padIndex;

    if (copyCurrentKit)
    {
        const int sourceIndex = currentPatternIndex.load();

        if (isValidPatternIndex (sourceIndex)
            && storedPatterns[static_cast<std::size_t> (sourceIndex)].assigned)
        {
            const auto& source = storedPatterns[static_cast<std::size_t> (sourceIndex)];
            newPattern.bars = source.bars;
            newPattern.pads = source.pads;

            for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
            {
                newPattern.lanes[static_cast<std::size_t> (laneIndex)].division
                    = source.lanes[static_cast<std::size_t> (laneIndex)].division;
                newPattern.lanes[static_cast<std::size_t> (laneIndex)].loopLength
                    = source.lanes[static_cast<std::size_t> (laneIndex)].loopLength;
            }
        }
    }

    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (newPattern);
}

void SVDrummerAudioProcessor::applyStoredPattern (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    const auto& stored = storedPatterns[static_cast<std::size_t> (patternIndex)];
    patternBars.store (juce::jlimit (1, maximumPatternBars, stored.bars));

    for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
    {
        const auto& sourceLane = stored.lanes[static_cast<std::size_t> (laneIndex)];
        auto& targetLane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
        targetLane.division.store (juce::jlimit (
            0, sequencerDivisionCount - 1, sourceLane.division));
        targetLane.loopLength.store (juce::jlimit (
            1, getLaneMaximumLoopLength (laneIndex), sourceLane.loopLength));

        for (int step = 0; step < maximumStepsPerLane; ++step)
            targetLane.stepVelocities[static_cast<std::size_t> (step)].store (
                sourceLane.stepVelocities[static_cast<std::size_t> (step)]);
    }

    {
        const juce::ScopedLock lock (stateLock);

        for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
        {
            const auto& sourcePad = stored.pads[static_cast<std::size_t> (padIndex)];
            auto& targetPad = pads[static_cast<std::size_t> (padIndex)];
            targetPad.sampleRevision.fetch_add (1);
            std::atomic_store_explicit (&targetPad.sample, sourcePad.sample,
                                        std::memory_order_release);
            targetPad.samplePath = sourcePad.samplePath;
            targetPad.displayName = sourcePad.displayName;
            targetPad.midiNote.store (juce::jlimit (0, 127, sourcePad.midiNote));
            targetPad.muted.store (sourcePad.muted);
            targetPad.soloed.store (sourcePad.soloed);
            targetPad.reversed.store (sourcePad.reversed);
            targetPad.volumeDb.store (juce::jlimit (-60.0f, 6.0f, sourcePad.volumeDb));
            targetPad.pan.store (juce::jlimit (-1.0f, 1.0f, sourcePad.pan));
            targetPad.tuneSemitones.store (
                juce::jlimit (-24.0f, 24.0f, sourcePad.tuneSemitones));
        }
    }

    patternChangeCounter.fetch_add (1);
}

juce::String SVDrummerAudioProcessor::getSequencerDivisionName (int divisionIndex)
{
    static const std::array<juce::String, sequencerDivisionCount> names
    {
        "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"
    };

    return names[static_cast<std::size_t> (
        juce::jlimit (0, sequencerDivisionCount - 1, divisionIndex))];
}

int SVDrummerAudioProcessor::getSequencerStepsPerBar (int divisionIndex)
{
    static constexpr std::array<int, sequencerDivisionCount> steps
    {
        4, 8, 12, 16, 24, 32, 48
    };

    return steps[static_cast<std::size_t> (
        juce::jlimit (0, sequencerDivisionCount - 1, divisionIndex))];
}

double SVDrummerAudioProcessor::getSequencerQuarterNotesPerStep (int divisionIndex)
{
    static constexpr std::array<double, sequencerDivisionCount> lengths
    {
        1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0
    };

    return lengths[static_cast<std::size_t> (
        juce::jlimit (0, sequencerDivisionCount - 1, divisionIndex))];
}

void SVDrummerAudioProcessor::processSequencerTriggers (int numSamples)
{
    const auto currentPatternRevision = patternChangeCounter.load();

    if (currentPatternRevision != lastPatternChangeCounter)
    {
        lastPatternChangeCounter = currentPatternRevision;
        resetSequencerTimeline();
    }

    auto playing = false;
    auto bpm = 120.0;
    auto blockStartPpq = fallbackSequencerPpq;
    auto hostPpqAvailable = false;

    if (auto* currentPlayHead = getPlayHead())
    {
        if (const auto position = currentPlayHead->getPosition())
        {
            playing = position->getIsPlaying();

            if (const auto hostBpm = position->getBpm())
                bpm = juce::jmax (1.0, *hostBpm);

            if (const auto ppq = position->getPpqPosition())
            {
                blockStartPpq = *ppq;
                hostPpqAvailable = true;
            }
        }
    }

    if (! sequencerEnabled.load() || ! playing || numSamples <= 0)
    {
        if (sequencerWasPlaying)
            resetSequencerTimeline();

        for (auto& lane : sequenceLanes)
            lane.activeStep.store (-1);

        if (! playing)
        {
            fallbackSequencerPpq = 0.0;
            sequencerPatternPositionQuarterNotes.store (0.0);
        }

        sequencerWasPlaying = false;
        return;
    }

    const double quarterNotesPerSample = bpm / (60.0 * juce::jmax (1.0, currentSampleRate));
    const bool forceInitialTrigger = ! sequencerWasPlaying;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double ppq = blockStartPpq
                         + static_cast<double> (sample) * quarterNotesPerSample;

        for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
        {
            auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
            const double stepLength = getSequencerQuarterNotesPerStep (lane.division.load());
            const auto absoluteStep = static_cast<juce::int64> (
                std::floor ((ppq + 1.0e-10) / stepLength));
            auto& lastStep = lastSequenceAbsoluteSteps[static_cast<std::size_t> (laneIndex)];

            if (! (forceInitialTrigger && sample == 0) && absoluteStep == lastStep)
                continue;

            lastStep = absoluteStep;
            const int loopLength = juce::jmax (1, lane.loopLength.load());
            const int sequenceStep = static_cast<int> (
                (absoluteStep % loopLength + loopLength) % loopLength);
            lane.activeStep.store (sequenceStep);

            const int velocity = static_cast<int> (
                lane.stepVelocities[static_cast<std::size_t> (sequenceStep)].load());

            if (velocity > 0)
                triggerPadOnAudioThread (laneIndex,
                                         static_cast<float> (velocity) / 127.0f,
                                         sample);
        }
    }

    const double blockEndPpq = blockStartPpq
                             + static_cast<double> (numSamples) * quarterNotesPerSample;
    fallbackSequencerPpq = hostPpqAvailable ? blockEndPpq : fallbackSequencerPpq
                                                              + static_cast<double> (numSamples)
                                                                    * quarterNotesPerSample;

    const double patternLength = static_cast<double> (getPatternBars()) * 4.0;
    double patternPosition = std::fmod (blockEndPpq, patternLength);

    if (patternPosition < 0.0)
        patternPosition += patternLength;

    sequencerPatternPositionQuarterNotes.store (patternPosition);
    sequencerWasPlaying = true;
}

void SVDrummerAudioProcessor::resetSequencerTimeline()
{
    lastSequenceAbsoluteSteps.fill ((std::numeric_limits<juce::int64>::min)());
    fallbackSequencerPpq = 0.0;
    sequencerWasPlaying = false;

    for (auto& lane : sequenceLanes)
        lane.activeStep.store (-1);
}

juce::StringArray SVDrummerAudioProcessor::getBrowserFolders() const
{
    const juce::ScopedLock lock (stateLock);
    return browserFolders;
}

void SVDrummerAudioProcessor::addBrowserFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;

    const auto path = folder.getFullPathName();

    {
        const juce::ScopedLock lock (stateLock);

        for (const auto& existing : browserFolders)
            if (juce::File (existing) == folder)
                return;

        browserFolders.add (path);
    }

    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::removeBrowserFolder (const juce::File& folder)
{
    bool changed = false;

    {
        const juce::ScopedLock lock (stateLock);

        for (int index = browserFolders.size(); --index >= 0;)
        {
            if (juce::File (browserFolders[index]) == folder)
            {
                browserFolders.remove (index);
                changed = true;
            }
        }
    }

    if (changed)
        markPortableSettingsDirty();
}

juce::File SVDrummerAudioProcessor::getPortableDataDirectory() const
{
    return getThisModuleFile().getParentDirectory().getChildFile ("Data");
}

juce::File SVDrummerAudioProcessor::getPortableSamplesDirectory() const
{
    return getPortableDataDirectory().getChildFile ("Samples");
}

juce::File SVDrummerAudioProcessor::getPortablePatternsDirectory() const
{
    return getPortableDataDirectory().getChildFile ("Patterns");
}

juce::File SVDrummerAudioProcessor::getPortableSettingsFile() const
{
    return getPortableDataDirectory()
        .getChildFile ("Settings")
        .getChildFile ("SV-Drummer.ini");
}

juce::File SVDrummerAudioProcessor::getThisModuleFile()
{
   #if JUCE_WINDOWS
    HMODULE module = nullptr;

    if (::GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR> (&moduleLocationAnchor),
                              &module) != 0)
    {
        wchar_t path[32768] = {};
        const DWORD length = ::GetModuleFileNameW (module, path, 32768);

        if (length > 0 && length < 32768)
            return juce::File (juce::String (path));
    }
   #endif

    return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
}

juce::String SVDrummerAudioProcessor::makeStoredPath (const juce::File& file) const
{
    if (file == juce::File())
        return {};

    const auto dataDirectory = getPortableDataDirectory();

    if (file.isAChildOf (dataDirectory))
        return "@DATA@/" + file.getRelativePathFrom (dataDirectory)
                                .replaceCharacter ('\\', '/');

    return file.getFullPathName();
}

juce::File SVDrummerAudioProcessor::resolveStoredPath (const juce::String& storedPath) const
{
    if (storedPath.startsWith ("@DATA@/"))
        return getPortableDataDirectory().getChildFile (storedPath.substring (7));

    return juce::File (storedPath);
}

std::unique_ptr<juce::XmlElement>
SVDrummerAudioProcessor::createPatternXml (int patternIndex) const
{
    if (! isValidPatternIndex (patternIndex))
        return {};

    const auto& pattern = storedPatterns[static_cast<std::size_t> (patternIndex)];
    auto xml = std::make_unique<juce::XmlElement> ("SVDRUMMER_PATTERN");
    xml->setAttribute ("version", "1.0");
    xml->setAttribute ("name", pattern.name);
    xml->setAttribute ("bars", pattern.bars);

    for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
    {
        const auto& lane = pattern.lanes[static_cast<std::size_t> (laneIndex)];
        juce::String encodedSteps;

        for (int step = 0; step < maximumStepsPerLane; ++step)
        {
            const int velocity = static_cast<int> (
                lane.stepVelocities[static_cast<std::size_t> (step)]);

            if (velocity <= 0)
                continue;

            if (encodedSteps.isNotEmpty())
                encodedSteps << ";";

            encodedSteps << step << ":" << velocity;
        }

        auto* laneXml = xml->createNewChildElement ("LANE");
        laneXml->setAttribute ("index", laneIndex);
        laneXml->setAttribute ("division", lane.division);
        laneXml->setAttribute ("length", lane.loopLength);
        laneXml->setAttribute ("steps", encodedSteps);
    }

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& pad = pattern.pads[static_cast<std::size_t> (padIndex)];
        auto* padXml = xml->createNewChildElement ("PAD");
        padXml->setAttribute ("index", padIndex);
        padXml->setAttribute ("note", pad.midiNote);
        padXml->setAttribute ("mute", pad.muted);
        padXml->setAttribute ("solo", pad.soloed);
        padXml->setAttribute ("reverse", pad.reversed);
        padXml->setAttribute ("volumeDb", static_cast<double> (pad.volumeDb));
        padXml->setAttribute ("pan", static_cast<double> (pad.pan));
        padXml->setAttribute ("tune", static_cast<double> (pad.tuneSemitones));
        padXml->setAttribute ("sample", pad.samplePath.isNotEmpty()
                                         ? makeStoredPath (juce::File (pad.samplePath))
                                         : juce::String());
    }

    return xml;
}

juce::Result SVDrummerAudioProcessor::loadPatternXmlIntoSlot (
    int patternIndex, const juce::XmlElement& patternXml)
{
    if (! isValidPatternIndex (patternIndex))
        return juce::Result::fail ("Invalid pattern slot.");

    if (! patternXml.hasTagName ("SVDRUMMER_PATTERN"))
        return juce::Result::fail ("This is not an SV-Drummer pattern file.");

    StoredPattern loaded;
    loaded.assigned = true;
    loaded.name = patternXml.getStringAttribute (
        "name", "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2));
    loaded.bars = juce::jlimit (
        1, maximumPatternBars, patternXml.getIntAttribute ("bars", 1));

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
        loaded.pads[static_cast<std::size_t> (padIndex)].midiNote = 36 + padIndex;

    for (auto* item : patternXml.getChildWithTagNameIterator ("LANE"))
    {
        const int laneIndex = item->getIntAttribute ("index", -1);

        if (! isValidPadIndex (laneIndex))
            continue;

        auto& lane = loaded.lanes[static_cast<std::size_t> (laneIndex)];
        lane.division = juce::jlimit (
            0, sequencerDivisionCount - 1, item->getIntAttribute ("division", 3));
        lane.loopLength = juce::jlimit (
            1,
            loaded.bars * getSequencerStepsPerBar (lane.division),
            item->getIntAttribute ("length", 16));

        juce::StringArray entries;
        entries.addTokens (item->getStringAttribute ("steps"), ";", {});

        for (const auto& entry : entries)
        {
            const int separator = entry.indexOfChar (':');

            if (separator <= 0)
                continue;

            const int step = entry.substring (0, separator).getIntValue();
            const int velocity = entry.substring (separator + 1).getIntValue();

            if (step >= 0 && step < maximumStepsPerLane)
                lane.stepVelocities[static_cast<std::size_t> (step)]
                    = static_cast<std::uint8_t> (juce::jlimit (0, 127, velocity));
        }
    }

    for (auto* item : patternXml.getChildWithTagNameIterator ("PAD"))
    {
        const int padIndex = item->getIntAttribute ("index", -1);

        if (! isValidPadIndex (padIndex))
            continue;

        auto& pad = loaded.pads[static_cast<std::size_t> (padIndex)];
        pad.midiNote = juce::jlimit (
            0, 127, item->getIntAttribute ("note", 36 + padIndex));
        pad.muted = item->getBoolAttribute ("mute", false);
        pad.soloed = item->getBoolAttribute ("solo", false);
        pad.reversed = item->getBoolAttribute ("reverse", false);
        pad.volumeDb = juce::jlimit (
            -60.0f, 6.0f,
            static_cast<float> (item->getDoubleAttribute ("volumeDb", 0.0)));
        pad.pan = juce::jlimit (
            -1.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute ("pan", 0.0)));
        pad.tuneSemitones = juce::jlimit (
            -24.0f, 24.0f,
            static_cast<float> (item->getDoubleAttribute ("tune", 0.0)));

        const auto storedPath = item->getStringAttribute ("sample");

        if (storedPath.isEmpty())
            continue;

        const auto sampleFile = resolveStoredPath (storedPath);
        pad.samplePath = sampleFile.getFullPathName();
        juce::String errorMessage;
        pad.sample = createSampleData (sampleFile, errorMessage);
        pad.displayName = pad.sample != nullptr
                            ? sampleFile.getFileNameWithoutExtension()
                            : sampleFile.getFileNameWithoutExtension() + " (MISSING)";
    }

    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (loaded);
    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::saveStoredPatternToFile (
    int patternIndex, const juce::File& file) const
{
    const auto xml = createPatternXml (patternIndex);

    if (xml == nullptr)
        return juce::Result::fail ("Invalid pattern slot.");

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The pattern folder could not be created.");

    if (! file.replaceWithText (xml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The pattern file could not be written.");

    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::loadPatternIntoSlot (
    int patternIndex, const juce::File& file)
{
    if (! isValidPatternIndex (patternIndex))
        return juce::Result::fail ("Invalid pattern slot.");

    if (! isSupportedPatternFile (file) || ! file.existsAsFile())
        return juce::Result::fail ("Select an SV-Drummer .svpattern file.");

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return juce::Result::fail ("The pattern file could not be read.");

    captureCurrentPattern();
    const auto result = loadPatternXmlIntoSlot (patternIndex, *xml);

    if (result.failed())
        return result;

    if (patternIndex == currentPatternIndex.load())
        applyStoredPattern (patternIndex);

    markPortableSettingsDirty();
    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::savePatternSlotToFile (
    int patternIndex, const juce::File& file)
{
    if (! isValidPatternIndex (patternIndex))
        return juce::Result::fail ("Invalid pattern slot.");

    if (patternIndex == currentPatternIndex.load())
        captureCurrentPattern();

    return saveStoredPatternToFile (patternIndex, file);
}

juce::String SVDrummerAudioProcessor::encodeLaneSteps (int laneIndex) const
{
    if (! isValidPadIndex (laneIndex))
        return {};

    juce::String encoded;

    for (int step = 0; step < maximumStepsPerLane; ++step)
    {
        const int velocity = getSequenceStepVelocity (laneIndex, step);

        if (velocity <= 0)
            continue;

        if (encoded.isNotEmpty())
            encoded << ";";

        encoded << step << ":" << velocity;
    }

    return encoded;
}

void SVDrummerAudioProcessor::decodeLaneSteps (int laneIndex,
                                                const juce::String& encodedSteps)
{
    if (! isValidPadIndex (laneIndex))
        return;

    auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];

    for (auto& velocity : lane.stepVelocities)
        velocity.store (0);

    juce::StringArray entries;
    entries.addTokens (encodedSteps, ";", {});

    for (const auto& entry : entries)
    {
        const int separator = entry.indexOfChar (':');

        if (separator <= 0)
            continue;

        const int step = entry.substring (0, separator).getIntValue();
        const int velocity = entry.substring (separator + 1).getIntValue();

        if (step >= 0 && step < maximumStepsPerLane)
            lane.stepVelocities[static_cast<std::size_t> (step)].store (
                static_cast<std::uint8_t> (juce::jlimit (0, 127, velocity)));
    }
}

void SVDrummerAudioProcessor::markPortableSettingsDirty() noexcept
{
    portableSettingsDirty.store (true);
}

void SVDrummerAudioProcessor::flushPortableSettingsIfNeeded()
{
    if (portableSettingsDirty.exchange (false))
        savePortableSettings();
}

void SVDrummerAudioProcessor::savePortableSettings()
{
    captureCurrentPattern();
    const auto settingsFile = getPortableSettingsFile();
    settingsFile.getParentDirectory().createDirectory();
    getPortableSamplesDirectory().createDirectory();
    getPortablePatternsDirectory().createDirectory();

    juce::StringArray lines;
    lines.add ("; SV-Drummer portable settings");
    lines.add ("; Paths under the portable Data folder are stored relative to the plug-in.");
    lines.add (juce::String());
    lines.add ("[Browser]");

    juce::StringArray folderSnapshot;

    {
        const juce::ScopedLock lock (stateLock);
        folderSnapshot = browserFolders;
    }

    lines.add ("FolderCount=" + juce::String (folderSnapshot.size()));

    for (int index = 0; index < folderSnapshot.size(); ++index)
        lines.add ("Folder" + juce::String (index + 1) + "="
                   + makeStoredPath (juce::File (folderSnapshot[index])));

    lines.add (juce::String());
    lines.add ("[Pads]");

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto prefix = "Pad" + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto& pad = pads[static_cast<std::size_t> (padIndex)];
        juce::String samplePath;

        {
            const juce::ScopedLock lock (stateLock);
            samplePath = pad.samplePath;
        }

        lines.add (prefix + "Note=" + juce::String (pad.midiNote.load()));
        lines.add (prefix + "Mute=" + juce::String (pad.muted.load() ? 1 : 0));
        lines.add (prefix + "Solo=" + juce::String (pad.soloed.load() ? 1 : 0));
        lines.add (prefix + "Reverse=" + juce::String (pad.reversed.load() ? 1 : 0));
        lines.add (prefix + "VolumeDb=" + juce::String (pad.volumeDb.load(), 3));
        lines.add (prefix + "Pan=" + juce::String (pad.pan.load(), 3));
        lines.add (prefix + "Tune=" + juce::String (pad.tuneSemitones.load(), 3));
        lines.add (prefix + "Sample="
                   + (samplePath.isNotEmpty()
                          ? makeStoredPath (juce::File (samplePath))
                          : juce::String()));
    }

    lines.add (juce::String());
    lines.add ("[Sequencer]");
    lines.add ("Enabled=" + juce::String (isSequencerEnabled() ? 1 : 0));
    lines.add ("PatternBars=" + juce::String (getPatternBars()));

    for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
    {
        const auto prefix = "Lane" + juce::String (laneIndex + 1).paddedLeft ('0', 2);
        lines.add (prefix + "Division=" + juce::String (getLaneDivision (laneIndex)));
        lines.add (prefix + "Length=" + juce::String (getLaneLoopLength (laneIndex)));
        lines.add (prefix + "Steps=" + encodeLaneSteps (laneIndex));
    }

    lines.add (juce::String());
    lines.add ("[Patterns]");
    lines.add ("Current=" + juce::String (getCurrentPatternIndex() + 1));
    const auto slotFolder = getPortablePatternsDirectory().getChildFile ("Slots");
    slotFolder.createDirectory();

    for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
    {
        const auto prefix = "Pattern"
                          + juce::String (patternIndex + 1).paddedLeft ('0', 2);
        const bool assigned = isPatternAssigned (patternIndex);
        lines.add (prefix + "Assigned=" + juce::String (assigned ? 1 : 0));
        lines.add (prefix + "MidiNote=" + juce::String (getPatternMidiNote (patternIndex)));

        if (assigned)
        {
            const auto patternFile = slotFolder.getChildFile (
                "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2)
                    + ".svpattern");
            saveStoredPatternToFile (patternIndex, patternFile);
            lines.add (prefix + "File=" + makeStoredPath (patternFile));
        }
    }

    settingsFile.replaceWithText (lines.joinIntoString ("\r\n") + "\r\n", false, false);
}

void SVDrummerAudioProcessor::loadPortableSettings()
{
    const auto settingsFile = getPortableSettingsFile();

    if (! settingsFile.existsAsFile())
        return;

    juce::StringPairArray values;
    juce::StringArray lines;
    lines.addLines (settingsFile.loadFileAsString());

    for (auto line : lines)
    {
        line = line.trim();

        if (line.isEmpty() || line.startsWithChar (';') || line.startsWithChar ('['))
            continue;

        const int equals = line.indexOfChar ('=');

        if (equals > 0)
            values.set (line.substring (0, equals).trim(), line.substring (equals + 1).trim());
    }

    const int folderCount = juce::jmax (0, values.getValue ("FolderCount", "0").getIntValue());

    {
        const juce::ScopedLock lock (stateLock);
        browserFolders.clear();

        for (int index = 0; index < folderCount; ++index)
        {
            const auto stored = values.getValue ("Folder" + juce::String (index + 1), {});

            if (stored.isNotEmpty())
                browserFolders.addIfNotAlreadyThere (resolveStoredPath (stored).getFullPathName());
        }
    }

    sequencerEnabled.store (values.getValue ("Enabled", "0").getIntValue() != 0);
    patternBars.store (juce::jlimit (
        1,
        maximumPatternBars,
        values.getValue ("PatternBars", "1").getIntValue()));

    for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
    {
        const auto prefix = "Lane" + juce::String (laneIndex + 1).paddedLeft ('0', 2);
        auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
        lane.division.store (juce::jlimit (
            0,
            sequencerDivisionCount - 1,
            values.getValue (prefix + "Division", "3").getIntValue()));
        lane.loopLength.store (juce::jlimit (
            1,
            getLaneMaximumLoopLength (laneIndex),
            values.getValue (prefix + "Length", "16").getIntValue()));
        decodeLaneSteps (laneIndex, values.getValue (prefix + "Steps", {}));
    }

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto prefix = "Pad" + juce::String (padIndex + 1).paddedLeft ('0', 2);
        auto& pad = pads[static_cast<std::size_t> (padIndex)];

        pad.midiNote.store (juce::jlimit (
            0, 127, values.getValue (prefix + "Note", juce::String (36 + padIndex)).getIntValue()));
        pad.muted.store (values.getValue (prefix + "Mute", "0").getIntValue() != 0);
        pad.soloed.store (values.getValue (prefix + "Solo", "0").getIntValue() != 0);
        pad.reversed.store (values.getValue (prefix + "Reverse", "0").getIntValue() != 0);
        pad.volumeDb.store (juce::jlimit (
            -60.0f, 6.0f, values.getValue (prefix + "VolumeDb", "0").getFloatValue()));
        pad.pan.store (juce::jlimit (
            -1.0f, 1.0f, values.getValue (prefix + "Pan", "0").getFloatValue()));
        pad.tuneSemitones.store (juce::jlimit (
            -24.0f, 24.0f, values.getValue (prefix + "Tune", "0").getFloatValue()));

        const auto storedSample = values.getValue (prefix + "Sample", {});

        if (storedSample.isNotEmpty())
        {
            const auto sampleFile = resolveStoredPath (storedSample);

            if (sampleFile.existsAsFile())
            {
                loadSampleIntoPad (padIndex, sampleFile);
            }
            else
            {
                const juce::ScopedLock lock (stateLock);
                pad.samplePath = sampleFile.getFullPathName();
                pad.displayName = sampleFile.getFileNameWithoutExtension() + " (MISSING)";
            }
        }
    }

    bool loadedAnyPattern = false;

    for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
    {
        const auto prefix = "Pattern"
                          + juce::String (patternIndex + 1).paddedLeft ('0', 2);
        patternMidiNotes[static_cast<std::size_t> (patternIndex)].store (
            juce::jlimit (-1, 127,
                          values.getValue (prefix + "MidiNote", "-1").getIntValue()));

        if (values.getValue (prefix + "Assigned", "0").getIntValue() == 0)
            continue;

        const auto storedFile = values.getValue (prefix + "File", {});

        if (storedFile.isEmpty())
            continue;

        const auto patternFile = resolveStoredPath (storedFile);
        const auto xml = juce::XmlDocument::parse (patternFile);

        if (xml != nullptr && loadPatternXmlIntoSlot (patternIndex, *xml).wasOk())
            loadedAnyPattern = true;
    }

    if (loadedAnyPattern)
    {
        int restoredPattern = juce::jlimit (
            0, numberOfPatterns - 1,
            values.getValue ("Current", "1").getIntValue() - 1);

        if (! isPatternAssigned (restoredPattern))
        {
            restoredPattern = 0;

            while (restoredPattern < numberOfPatterns
                   && ! isPatternAssigned (restoredPattern))
                ++restoredPattern;

            restoredPattern = juce::jlimit (0, numberOfPatterns - 1, restoredPattern);
        }

        currentPatternIndex.store (restoredPattern);
        applyStoredPattern (restoredPattern);
    }
    else
    {
        currentPatternIndex.store (0);
        captureCurrentPattern();
    }

    portableSettingsDirty.store (false);
}

void SVDrummerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    captureCurrentPattern();
    juce::XmlElement state ("SVDRUMMER_STATE");
    state.setAttribute ("version", "1.0.0");

    auto* browser = state.createNewChildElement ("BROWSER");

    for (const auto& folder : getBrowserFolders())
    {
        auto* item = browser->createNewChildElement ("FOLDER");
        item->setAttribute ("path", makeStoredPath (juce::File (folder)));
    }

    auto* sequencer = state.createNewChildElement ("SEQUENCER");
    sequencer->setAttribute ("enabled", isSequencerEnabled());
    sequencer->setAttribute ("bars", getPatternBars());

    for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
    {
        auto* lane = sequencer->createNewChildElement ("LANE");
        lane->setAttribute ("index", laneIndex);
        lane->setAttribute ("division", getLaneDivision (laneIndex));
        lane->setAttribute ("length", getLaneLoopLength (laneIndex));
        lane->setAttribute ("steps", encodeLaneSteps (laneIndex));
    }

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        auto* item = state.createNewChildElement ("PAD");
        item->setAttribute ("index", padIndex);
        item->setAttribute ("note", getPadMidiNote (padIndex));
        item->setAttribute ("mute", isPadMuted (padIndex));
        item->setAttribute ("solo", isPadSoloed (padIndex));
        item->setAttribute ("reverse", isPadReversed (padIndex));
        item->setAttribute ("volumeDb", static_cast<double> (getPadVolumeDb (padIndex)));
        item->setAttribute ("pan", static_cast<double> (getPadPan (padIndex)));
        item->setAttribute ("tune", static_cast<double> (getPadTuneSemitones (padIndex)));

        const auto path = getPadSamplePath (padIndex);
        item->setAttribute ("sample", path.isNotEmpty()
                                       ? makeStoredPath (juce::File (path))
                                       : juce::String());
    }

    auto* patternsXml = state.createNewChildElement ("PATTERNS");
    patternsXml->setAttribute ("current", getCurrentPatternIndex());

    for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
    {
        auto* slotXml = patternsXml->createNewChildElement ("SLOT");
        slotXml->setAttribute ("index", patternIndex);
        slotXml->setAttribute ("assigned", isPatternAssigned (patternIndex));
        slotXml->setAttribute ("midiNote", getPatternMidiNote (patternIndex));

        if (isPatternAssigned (patternIndex))
            if (auto patternXml = createPatternXml (patternIndex))
                slotXml->addChildElement (patternXml.release());
    }

    copyXmlToBinary (state, destData);
}

void SVDrummerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = getXmlFromBinary (data, sizeInBytes);

    if (state == nullptr || ! state->hasTagName ("SVDRUMMER_STATE"))
        return;

    if (auto* browser = state->getChildByName ("BROWSER"))
    {
        juce::StringArray restoredFolders;

        for (auto* folder : browser->getChildWithTagNameIterator ("FOLDER"))
        {
            const auto path = folder->getStringAttribute ("path");

            if (path.isNotEmpty())
                restoredFolders.addIfNotAlreadyThere (resolveStoredPath (path).getFullPathName());
        }

        const juce::ScopedLock lock (stateLock);
        browserFolders = restoredFolders;
    }

    if (auto* sequencer = state->getChildByName ("SEQUENCER"))
    {
        sequencerEnabled.store (sequencer->getBoolAttribute ("enabled", false));
        patternBars.store (juce::jlimit (
            1, maximumPatternBars, sequencer->getIntAttribute ("bars", 1)));

        for (auto* item : sequencer->getChildWithTagNameIterator ("LANE"))
        {
            const int laneIndex = item->getIntAttribute ("index", -1);

            if (! isValidPadIndex (laneIndex))
                continue;

            auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
            lane.division.store (juce::jlimit (
                0,
                sequencerDivisionCount - 1,
                item->getIntAttribute ("division", 3)));
            lane.loopLength.store (juce::jlimit (
                1,
                getLaneMaximumLoopLength (laneIndex),
                item->getIntAttribute ("length", 16)));
            decodeLaneSteps (laneIndex, item->getStringAttribute ("steps"));
        }
    }

    for (auto* item : state->getChildWithTagNameIterator ("PAD"))
    {
        const int padIndex = item->getIntAttribute ("index", -1);

        if (! isValidPadIndex (padIndex))
            continue;

        auto& pad = pads[static_cast<std::size_t> (padIndex)];
        pad.midiNote.store (juce::jlimit (0, 127, item->getIntAttribute ("note", 36 + padIndex)));
        pad.muted.store (item->getBoolAttribute ("mute", false));
        pad.soloed.store (item->getBoolAttribute ("solo", false));
        pad.reversed.store (item->getBoolAttribute ("reverse", false));
        pad.volumeDb.store (juce::jlimit (
            -60.0f, 6.0f, static_cast<float> (item->getDoubleAttribute ("volumeDb", 0.0))));
        pad.pan.store (juce::jlimit (
            -1.0f, 1.0f, static_cast<float> (item->getDoubleAttribute ("pan", 0.0))));
        pad.tuneSemitones.store (juce::jlimit (
            -24.0f, 24.0f, static_cast<float> (item->getDoubleAttribute ("tune", 0.0))));

        const auto storedSample = item->getStringAttribute ("sample");

        if (storedSample.isEmpty())
        {
            clearPadSample (padIndex);
            continue;
        }

        const auto sampleFile = resolveStoredPath (storedSample);

        if (sampleFile.existsAsFile())
        {
            loadSampleIntoPad (padIndex, sampleFile);
        }
        else
        {
            clearPadSample (padIndex);
            const juce::ScopedLock lock (stateLock);
            pad.samplePath = sampleFile.getFullPathName();
            pad.displayName = sampleFile.getFileNameWithoutExtension() + " (MISSING)";
        }
    }

    if (auto* patternsXml = state->getChildByName ("PATTERNS"))
    {
        for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
        {
            storedPatterns[static_cast<std::size_t> (patternIndex)] = StoredPattern();
            storedPatterns[static_cast<std::size_t> (patternIndex)].name
                = "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2);
            patternMidiNotes[static_cast<std::size_t> (patternIndex)].store (-1);
        }

        for (auto* slotXml : patternsXml->getChildWithTagNameIterator ("SLOT"))
        {
            const int patternIndex = slotXml->getIntAttribute ("index", -1);

            if (! isValidPatternIndex (patternIndex))
                continue;

            patternMidiNotes[static_cast<std::size_t> (patternIndex)].store (
                juce::jlimit (-1, 127, slotXml->getIntAttribute ("midiNote", -1)));

            if (slotXml->getBoolAttribute ("assigned", false))
                if (auto* patternXml = slotXml->getChildByName ("SVDRUMMER_PATTERN"))
                    loadPatternXmlIntoSlot (patternIndex, *patternXml);
        }

        int restoredPattern = juce::jlimit (
            0, numberOfPatterns - 1, patternsXml->getIntAttribute ("current", 0));

        if (! isPatternAssigned (restoredPattern))
        {
            restoredPattern = 0;

            while (restoredPattern < numberOfPatterns
                   && ! isPatternAssigned (restoredPattern))
                ++restoredPattern;

            if (restoredPattern >= numberOfPatterns)
            {
                restoredPattern = 0;
                currentPatternIndex.store (0);
                captureCurrentPattern();
            }
        }

        currentPatternIndex.store (restoredPattern);
        applyStoredPattern (restoredPattern);
    }
    else
    {
        currentPatternIndex.store (0);
        captureCurrentPattern();
    }

    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::anyPadIsSoloed() const
{
    for (const auto& pad : pads)
        if (pad.soloed.load())
            return true;

    return false;
}

bool SVDrummerAudioProcessor::isValidPadIndex (int padIndex) const noexcept
{
    return padIndex >= 0 && padIndex < numberOfPads;
}

bool SVDrummerAudioProcessor::isValidPatternIndex (int patternIndex) const noexcept
{
    return patternIndex >= 0 && patternIndex < numberOfPatterns;
}

bool SVDrummerAudioProcessor::isSupportedAudioFile (const juce::File& file)
{
    return file.hasFileExtension ("wav;mp3;ogg;flac;aif;aiff");
}

bool SVDrummerAudioProcessor::isSupportedPatternFile (const juce::File& file)
{
    return file.hasFileExtension ("svpattern");
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SVDrummerAudioProcessor();
}
