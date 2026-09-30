#include "PluginProcessor.h"
#include "PluginEditor.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>

namespace
{
constexpr int waveformPointCount = 2048;

class StateBackedParameter final : public juce::RangedAudioParameter
{
public:
    using Getter = std::function<float()>;
    using Setter = std::function<void(float)>;
    using Formatter = std::function<juce::String(float, int)>;
    using Parser = std::function<float(const juce::String&)>;

    StateBackedParameter (const juce::ParameterID& parameterID,
                          const juce::String& name,
                          juce::NormalisableRange<float> valueRange,
                          float defaultDenormalisedValue,
                          juce::String label,
                          Getter valueGetter,
                          Setter valueSetter,
                          int parameterSteps = 0,
                          bool parameterIsBoolean = false,
                          Formatter valueFormatter = {},
                          Parser valueParser = {},
                          bool automatable = true,
                          bool metaParameter = false)
        : RangedAudioParameter (
              parameterID,
              name,
              juce::AudioProcessorParameterWithIDAttributes()
                  .withLabel (std::move (label))
                  .withAutomatable (automatable)
                  .withMeta (metaParameter)),
          range (std::move (valueRange)),
          defaultValue (defaultDenormalisedValue),
          getter (std::move (valueGetter)),
          setter (std::move (valueSetter)),
          formatter (std::move (valueFormatter)),
          parser (std::move (valueParser)),
          steps (parameterSteps),
          boolean (parameterIsBoolean)
    {
    }

    const juce::NormalisableRange<float>& getNormalisableRange() const override
    {
        return range;
    }

    float getValue() const override
    {
        return convertTo0to1 (range.snapToLegalValue (getter()));
    }

    void setValue (float normalisedValue) override
    {
        setter (convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalisedValue)));
    }

    float getDefaultValue() const override
    {
        return convertTo0to1 (range.snapToLegalValue (defaultValue));
    }

    int getNumSteps() const override
    {
        return steps > 0 ? steps : getDefaultNumParameterSteps();
    }

    bool isDiscrete() const override
    {
        return steps > 0;
    }

    bool isBoolean() const override
    {
        return boolean;
    }

    juce::String getText (float normalisedValue,
                          int maximumStringLength) const override
    {
        const float actualValue = convertFrom0to1 (
            juce::jlimit (0.0f, 1.0f, normalisedValue));
        auto text = formatter != nullptr
                      ? formatter (actualValue, maximumStringLength)
                      : juce::String (actualValue, range.interval > 0.0f ? 2 : 3);
        return maximumStringLength > 0
                 ? text.substring (0, maximumStringLength)
                 : text;
    }

    float getValueForText (const juce::String& text) const override
    {
        const float actualValue = parser != nullptr ? parser (text)
                                                    : text.getFloatValue();
        return convertTo0to1 (range.snapToLegalValue (actualValue));
    }

private:
    juce::NormalisableRange<float> range;
    float defaultValue = 0.0f;
    Getter getter;
    Setter setter;
    Formatter formatter;
    Parser parser;
    int steps = 0;
    bool boolean = false;
};

juce::String patternMidiModeToString (
    SVDrummerAudioProcessor::PatternMidiMode mode)
{
    switch (mode)
    {
        case SVDrummerAudioProcessor::PatternMidiMode::gate: return "Gate";
        case SVDrummerAudioProcessor::PatternMidiMode::hold: return "Hold";
        case SVDrummerAudioProcessor::PatternMidiMode::select:
        default: return "Select";
    }
}

SVDrummerAudioProcessor::PatternMidiMode patternMidiModeFromString (
    const juce::String& text)
{
    if (text.equalsIgnoreCase ("Gate"))
        return SVDrummerAudioProcessor::PatternMidiMode::gate;

    if (text.equalsIgnoreCase ("Hold"))
        return SVDrummerAudioProcessor::PatternMidiMode::hold;

    return SVDrummerAudioProcessor::PatternMidiMode::select;
}

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

    initialiseHostParameters();

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
}

void SVDrummerAudioProcessor::initialiseHostParameters()
{
    const auto addStateParameter = [this] (
        std::unique_ptr<StateBackedParameter> parameter)
    {
        auto* const result = parameter.get();
        addParameter (parameter.release());
        return static_cast<juce::RangedAudioParameter*> (result);
    };

    const auto booleanText = [] (float value, int)
    {
        return value >= 0.5f ? juce::String ("On") : juce::String ("Off");
    };

    sequencerEnabledParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "sequencer_play", 1 },
            "Sequencer Play",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
            0.0f,
            juce::String(),
            [this] { return sequencerEnabled.load() ? 1.0f : 0.0f; },
            [this] (float value)
            {
                const bool next = value >= 0.5f;

                if (sequencerEnabled.exchange (next) != next)
                    markHostParameterStateChanged();
            },
            2,
            true,
            booleanText));

    patternMidiModeParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "midi_mode", 1 },
            "MIDI Mode",
            juce::NormalisableRange<float> { 0.0f, 2.0f, 1.0f },
            0.0f,
            juce::String(),
            [this] { return static_cast<float> (patternMidiMode.load()); },
            [this] (float value)
            {
                const int next = juce::jlimit (0, 2, juce::roundToInt (value));

                if (patternMidiMode.exchange (next) == next)
                    return;

                patternGateActive.store (false);
                patternGateWaitingForSelection.store (false);
                activePatternGateNote.store (-1);
                pendingPatternGateStartNote.store (-1);
                sequencerEnabled.store (false);
                patternGateRestartCounter.fetch_add (1);
                markHostParameterStateChanged();
            },
            3,
            false,
            [] (float value, int)
            {
                switch (juce::jlimit (0, 2, juce::roundToInt (value)))
                {
                    case 1:  return juce::String ("GATE");
                    case 2:  return juce::String ("HOLD");
                    case 0:
                    default: return juce::String ("SELECT");
                }
            }));

    patternBarsParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "pattern_length_bars", 1 },
            "Pattern Length",
            juce::NormalisableRange<float> {
                1.0f, static_cast<float> (maximumPatternBars), 1.0f },
            1.0f,
            "Bars",
            [this] { return static_cast<float> (patternBars.load()); },
            [this] (float value)
            {
                const int next = juce::jlimit (
                    1, maximumPatternBars, juce::roundToInt (value));

                if (patternBars.exchange (next) == next)
                    return;

                for (int lane = 0; lane < numberOfPads; ++lane)
                {
                    auto& sequence = sequenceLanes[static_cast<std::size_t> (lane)];
                    sequence.loopLength.store (juce::jlimit (
                        1,
                        getLaneMaximumLoopLength (lane),
                        sequence.loopLength.load()));
                }

                markHostParameterStateChanged();
            },
            maximumPatternBars,
            false,
            [] (float value, int)
            {
                const int bars = juce::roundToInt (value);
                return juce::String (bars) + (bars == 1 ? " Bar" : " Bars");
            }));

    sampleMarkerSnapParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "sample_marker_snap", 1 },
            "Sample Marker Snap",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
            0.0f,
            juce::String(),
            [this] { return sampleMarkerSnapEnabled.load() ? 1.0f : 0.0f; },
            [this] (float value)
            {
                const bool next = value >= 0.5f;

                if (sampleMarkerSnapEnabled.exchange (next) != next)
                    markHostParameterStateChanged();
            },
            2,
            true,
            booleanText));

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";

        padHostParameters[index].volume = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_volume_db", 1 },
                namePrefix + "Volume",
                juce::NormalisableRange<float> { -60.0f, 6.0f, 0.1f },
                0.0f,
                "dB",
                [this, index] { return pads[index].volumeDb.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (-60.0f, 6.0f, value);

                    if (std::abs (pads[index].volumeDb.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return value <= -59.95f ? juce::String ("-inf dB")
                                            : juce::String (value, 1) + " dB";
                }));

        padHostParameters[index].pan = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_pan", 1 },
                namePrefix + "Pan",
                juce::NormalisableRange<float> { -1.0f, 1.0f, 0.01f },
                0.0f,
                juce::String(),
                [this, index] { return pads[index].pan.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (-1.0f, 1.0f, value);

                    if (std::abs (pads[index].pan.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    if (std::abs (value) < 0.005f)
                        return juce::String ("Centre");

                    return juce::String (juce::roundToInt (std::abs (value) * 100.0f))
                         + (value < 0.0f ? "% L" : "% R");
                }));

        padHostParameters[index].tune = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_tune", 1 },
                namePrefix + "Tune",
                juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f },
                0.0f,
                "st",
                [this, index] { return pads[index].tuneSemitones.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (-24.0f, 24.0f, value);

                    if (std::abs (
                            pads[index].tuneSemitones.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return (value > 0.005f ? "+" : juce::String())
                         + juce::String (value, 2) + " st";
                }));
    }

    stateRevisionParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "internal_state_revision", 1 },
            "Internal State Revision",
            juce::NormalisableRange<float> { 0.0f, 1.0f },
            0.0f,
            juce::String(),
            [this] { return hostStateRevision.load(); },
            [this] (float value) { hostStateRevision.store (value); },
            0,
            false,
            StateBackedParameter::Formatter(),
            StateBackedParameter::Parser(),
            false,
            true));
}

const juce::String SVDrummerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SVDrummerAudioProcessor::acceptsMidi() const       { return true; }
bool SVDrummerAudioProcessor::producesMidi() const      { return false; }
bool SVDrummerAudioProcessor::isMidiEffect() const      { return false; }
double SVDrummerAudioProcessor::getTailLengthSeconds() const { return 5.0; }

int SVDrummerAudioProcessor::getNumPrograms()                 { return 1; }
int SVDrummerAudioProcessor::getCurrentProgram()              { return 0; }
void SVDrummerAudioProcessor::setCurrentProgram (int)         {}
const juce::String SVDrummerAudioProcessor::getProgramName (int) { return {}; }
void SVDrummerAudioProcessor::changeProgramName (int, const juce::String&) {}

void SVDrummerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    lastPatternChangeCounter = patternChangeCounter.load();
    lastPatternGateRestartCounter = patternGateRestartCounter.load();
    patternGateActive.store (false);
    patternGateWaitingForSelection.store (false);
    activePatternGateNote.store (-1);
    pendingPatternGateStartNote.store (-1);

    if (getPatternMidiMode() != PatternMidiMode::select)
        sequencerEnabled.store (false);

    resetSequencerTimeline();

    for (auto& pad : pads)
        for (auto& voice : pad.voices)
            voice.active = false;

    browserPreviewVoice.active = false;
    browserPreviewVoice.sample.reset();
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

        if (! message.isNoteOn() && ! message.isNoteOff())
            continue;

        const int note = message.getNoteNumber();
        bool isPatternNote = false;
        const auto midiMode = getPatternMidiMode();

        for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
        {
            if (patternMidiNotes[static_cast<std::size_t> (patternIndex)].load() == note)
            {
                isPatternNote = true;

                if (message.isNoteOn())
                {
                    const bool stopsHeldPattern =
                        midiMode == PatternMidiMode::hold
                        && activePatternGateNote.load() == note
                        && sequencerEnabled.load();

                    if (stopsHeldPattern)
                    {
                        stopPatternMidiPlayback();
                    }
                    else
                    {
                        pendingPatternSelection.store (patternIndex);

                        if (midiMode != PatternMidiMode::select)
                        {
                            activePatternGateNote.store (note);
                            pendingPatternGateStartNote.store (note);
                            patternGateActive.store (false);
                            patternGateWaitingForSelection.store (true);
                            sequencerEnabled.store (true);
                        }

                        triggerAsyncUpdate();
                    }
                }
                else if (midiMode == PatternMidiMode::gate
                         && activePatternGateNote.load() == note)
                {
                    stopPatternMidiPlayback();
                }

                break;
            }
        }

        if (isPatternNote)
            continue;

        for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
        {
            if (pads[static_cast<std::size_t> (padIndex)].midiNote.load() == note)
            {
                auto& pad = pads[static_cast<std::size_t> (padIndex)];

                if (message.isNoteOn())
                {
                    const float velocity = juce::jlimit (
                        0.0f, 1.0f, message.getFloatVelocity());
                    triggerPadOnAudioThread (
                        padIndex, velocity,
                        juce::jlimit (
                            0, juce::jmax (0, buffer.getNumSamples() - 1),
                            metadata.samplePosition));
                }
                else if (pad.loopEnabled.load())
                {
                    const int releaseSample = juce::jlimit (
                        0, juce::jmax (0, buffer.getNumSamples() - 1),
                        metadata.samplePosition);

                    for (auto& voice : pad.voices)
                    {
                        if (voice.active && voice.looping)
                        {
                            if (voice.releaseAtOutputSample < 0)
                                voice.releaseAtOutputSample = releaseSample;
                            else
                                voice.releaseAtOutputSample = juce::jmin (
                                    voice.releaseAtOutputSample, releaseSample);
                        }
                    }
                }
            }
        }
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

    if (browserPreviewTriggerPending.exchange (false))
        triggerBrowserPreviewOnAudioThread();

    renderBrowserPreview (buffer);
}

void SVDrummerAudioProcessor::triggerBrowserPreviewOnAudioThread()
{
    const auto sample = std::atomic_load_explicit (
        &browserPreviewSample, std::memory_order_acquire);

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
    {
        browserPreviewVoice.active = false;
        browserPreviewVoice.sample.reset();
        return;
    }

    browserPreviewVoice.sample = sample;
    browserPreviewVoice.position = 0.0;
    browserPreviewVoice.increment = sample->sourceSampleRate
                                  / juce::jmax (1.0, currentSampleRate);
    browserPreviewVoice.rangeStart = 0.0;
    browserPreviewVoice.rangeEnd = static_cast<double> (
        sample->audio.getNumSamples());
    browserPreviewVoice.velocity = 0.82f;
    browserPreviewVoice.delaySamples = 0;
    browserPreviewVoice.active = true;
}

void SVDrummerAudioProcessor::renderBrowserPreview (juce::AudioBuffer<float>& output)
{
    auto& voice = browserPreviewVoice;

    if (! voice.active || voice.sample == nullptr)
        return;

    const auto& source = voice.sample->audio;
    const int sourceChannels = source.getNumChannels();
    const int outputChannels = output.getNumChannels();

    for (int outputSample = 0; outputSample < output.getNumSamples(); ++outputSample)
    {
        if (voice.position < 0.0
            || voice.position >= static_cast<double> (source.getNumSamples()))
        {
            voice.active = false;
            voice.sample.reset();
            break;
        }

        const int firstIndex = juce::jlimit (
            0, source.getNumSamples() - 1,
            static_cast<int> (std::floor (voice.position)));
        const int secondIndex = juce::jmin (source.getNumSamples() - 1,
                                            firstIndex + 1);
        const float fraction = static_cast<float> (
            voice.position - static_cast<double> (firstIndex));
        const float left = juce::jmap (fraction,
                                       source.getSample (0, firstIndex),
                                       source.getSample (0, secondIndex));

        if (outputChannels == 1)
        {
            output.addSample (0, outputSample, left * voice.velocity);
        }
        else if (outputChannels > 1)
        {
            const int rightChannel = juce::jmin (1, sourceChannels - 1);
            const float right = juce::jmap (
                fraction,
                source.getSample (rightChannel, firstIndex),
                source.getSample (rightChannel, secondIndex));
            output.addSample (0, outputSample, left * voice.velocity);
            output.addSample (1, outputSample, right * voice.velocity);
        }

        voice.position += voice.increment;
    }
}

void SVDrummerAudioProcessor::triggerPadOnAudioThread (int padIndex,
                                                        float velocity,
                                                        int delaySamples,
                                                        bool triggeredBySequencer)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const auto sample = std::atomic_load_explicit (&pad.sample, std::memory_order_acquire);

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
        return;

    const bool looping = pad.loopEnabled.load();
    const int chokeGroup = pad.chokeGroup.load();

    if (chokeGroup > 0)
    {
        for (auto& candidatePad : pads)
        {
            if (candidatePad.chokeGroup.load() != chokeGroup)
                continue;

            for (auto& existingVoice : candidatePad.voices)
            {
                if (! existingVoice.active)
                    continue;

                if (existingVoice.chokeAtOutputSample < 0)
                    existingVoice.chokeAtOutputSample = delaySamples;
                else
                    existingVoice.chokeAtOutputSample = juce::jmin (
                        existingVoice.chokeAtOutputSample, delaySamples);
            }
        }
    }
    else if (looping)
    {
        for (auto& existingVoice : pad.voices)
        {
            if (! existingVoice.active)
                continue;

            if (existingVoice.chokeAtOutputSample < 0)
                existingVoice.chokeAtOutputSample = delaySamples;
            else
                existingVoice.chokeAtOutputSample = juce::jmin (
                    existingVoice.chokeAtOutputSample, delaySamples);
        }
    }

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
    const int sourceSamples = sample->audio.getNumSamples();
    const int rangeStart = juce::jlimit (
        0, sourceSamples - 1, pad.sampleStart.load());
    const int rangeEndSample = juce::jlimit (
        rangeStart, sourceSamples - 1, pad.sampleEnd.load());
    const int rangeEnd = rangeEndSample + 1;
    const int loopStart = juce::jlimit (
        rangeStart, rangeEndSample, pad.loopStart.load());
    const int loopEnd = juce::jlimit (
        loopStart, rangeEndSample, pad.loopEnd.load()) + 1;

    voice.sample = sample;
    voice.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    voice.delaySamples = juce::jmax (0, delaySamples);
    voice.chokeAtOutputSample = -1;
    voice.releaseAtOutputSample = -1;
    voice.increment = sourceRatio * tuneRatio * (reversed ? -1.0 : 1.0);
    voice.rangeStart = static_cast<double> (rangeStart);
    voice.rangeEnd = static_cast<double> (rangeEnd);
    voice.loopStart = static_cast<double> (loopStart);
    voice.loopEnd = static_cast<double> (loopEnd);
    voice.looping = looping && loopEnd > loopStart;
    voice.position = reversed ? voice.rangeEnd - 1.0 : voice.rangeStart;
    voice.sampleRevision = pad.sampleRevision.load();
    voice.sequencerTriggered = triggeredBySequencer;
    startVoiceEnvelope (voice, pad);
    voice.active = true;

    pad.activityCounter.fetch_add (1);
}

void SVDrummerAudioProcessor::startVoiceEnvelope (Voice& voice,
                                                   const PadState& pad)
{
    const auto millisecondsToSamples = [this] (float milliseconds)
    {
        return juce::jmax (
            0, juce::roundToInt (static_cast<double> (milliseconds)
                                 * juce::jmax (1.0, currentSampleRate) / 1000.0));
    };

    const int attackSamples = millisecondsToSamples (
        juce::jlimit (0.0f, 2000.0f, pad.ampAttackMs.load()));
    voice.envelopeDecaySamples = millisecondsToSamples (
        juce::jlimit (0.0f, 5000.0f, pad.ampDecayMs.load()));
    voice.envelopeReleaseSamples = millisecondsToSamples (
        juce::jlimit (0.0f, 5000.0f, pad.ampReleaseMs.load()));
    voice.envelopeSustain = juce::jlimit (
        0.0f, 1.0f, pad.ampSustain.load());
    voice.envelopeDelta = 0.0f;
    voice.envelopeSamplesRemaining = 0;

    if (attackSamples > 0)
    {
        voice.envelopeStage = EnvelopeStage::attack;
        voice.envelopeLevel = 0.0f;
        voice.envelopeSamplesRemaining = attackSamples;
        voice.envelopeDelta = 1.0f / static_cast<float> (attackSamples);
    }
    else
    {
        voice.envelopeLevel = 1.0f;
        startVoiceDecay (voice);
    }
}

void SVDrummerAudioProcessor::startVoiceDecay (Voice& voice)
{
    if (voice.envelopeDecaySamples > 0
        && std::abs (voice.envelopeSustain - 1.0f) > 1.0e-6f)
    {
        voice.envelopeStage = EnvelopeStage::decay;
        voice.envelopeLevel = 1.0f;
        voice.envelopeSamplesRemaining = voice.envelopeDecaySamples;
        voice.envelopeDelta = (voice.envelopeSustain - 1.0f)
                            / static_cast<float> (voice.envelopeDecaySamples);
        return;
    }

    voice.envelopeStage = EnvelopeStage::sustain;
    voice.envelopeLevel = voice.envelopeSustain;
    voice.envelopeSamplesRemaining = 0;
    voice.envelopeDelta = 0.0f;
}

void SVDrummerAudioProcessor::beginVoiceRelease (Voice& voice)
{
    voice.releaseAtOutputSample = -1;

    if (! voice.active || voice.envelopeStage == EnvelopeStage::release)
        return;

    if (voice.envelopeReleaseSamples <= 0 || voice.envelopeLevel <= 0.0f)
    {
        voice.active = false;
        voice.sample.reset();
        return;
    }

    voice.envelopeStage = EnvelopeStage::release;
    voice.envelopeSamplesRemaining = voice.envelopeReleaseSamples;
    voice.envelopeDelta = -voice.envelopeLevel
                        / static_cast<float> (voice.envelopeReleaseSamples);
}

void SVDrummerAudioProcessor::advanceVoiceEnvelope (Voice& voice)
{
    if (voice.envelopeStage == EnvelopeStage::sustain)
        return;

    voice.envelopeLevel += voice.envelopeDelta;

    if (--voice.envelopeSamplesRemaining > 0)
        return;

    if (voice.envelopeStage == EnvelopeStage::attack)
    {
        voice.envelopeLevel = 1.0f;
        startVoiceDecay (voice);
    }
    else if (voice.envelopeStage == EnvelopeStage::decay)
    {
        voice.envelopeStage = EnvelopeStage::sustain;
        voice.envelopeLevel = voice.envelopeSustain;
        voice.envelopeSamplesRemaining = 0;
        voice.envelopeDelta = 0.0f;
    }
    else
    {
        voice.envelopeLevel = 0.0f;
        voice.active = false;
        voice.sample.reset();
    }
}

void SVDrummerAudioProcessor::stopSequencerLoopVoicesOnAudioThread()
{
    for (auto& pad : pads)
    {
        for (auto& voice : pad.voices)
        {
            if (voice.active && voice.looping && voice.sequencerTriggered)
                beginVoiceRelease (voice);
        }
    }
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
        const int sourceChannels = source.getNumChannels();

        const int firstOutputSample = juce::jmin (outputSamples, voice.delaySamples);
        voice.delaySamples -= firstOutputSample;

        for (int outputSample = firstOutputSample; outputSample < outputSamples; ++outputSample)
        {
            if (voice.chokeAtOutputSample >= 0
                && outputSample >= voice.chokeAtOutputSample)
            {
                voice.active = false;
                voice.sample.reset();
                voice.chokeAtOutputSample = -1;
                break;
            }

            if (voice.releaseAtOutputSample >= 0
                && outputSample >= voice.releaseAtOutputSample)
            {
                beginVoiceRelease (voice);

                if (! voice.active)
                    break;
            }

            if (voice.looping && pad.loopEnabled.load())
            {
                const double loopLength = voice.loopEnd - voice.loopStart;

                if (loopLength > 0.0 && voice.increment >= 0.0
                    && voice.position >= voice.loopEnd)
                {
                    voice.position = voice.loopStart
                                   + std::fmod (voice.position - voice.loopStart,
                                                loopLength);
                }
                else if (loopLength > 0.0 && voice.increment < 0.0
                         && voice.position < voice.loopStart)
                {
                    const double overshoot = std::fmod (
                        voice.loopStart - voice.position, loopLength);
                    voice.position = voice.loopEnd - juce::jmax (overshoot, 1.0e-9);
                }
            }

            if (voice.position < voice.rangeStart || voice.position >= voice.rangeEnd)
            {
                voice.active = false;
                voice.sample.reset();
                break;
            }

            const int firstIndex = juce::jlimit (
                static_cast<int> (voice.rangeStart),
                static_cast<int> (voice.rangeEnd) - 1,
                static_cast<int> (std::floor (voice.position)));
            const int secondIndex = juce::jmin (
                static_cast<int> (voice.rangeEnd) - 1, firstIndex + 1);
            const float fraction = static_cast<float> (voice.position
                                                       - static_cast<double> (firstIndex));
            const float voiceGain = gain * voice.velocity
                                  * juce::jmax (0.0f, voice.envelopeLevel);

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
            advanceVoiceEnvelope (voice);

            if (! voice.active)
                break;
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
    const int lastSample = juce::jmax (0, newSample->audio.getNumSamples() - 1);
    pad.sampleRevision.fetch_add (1);
    pad.sampleStart.store (0);
    pad.sampleEnd.store (lastSample);
    pad.loopEnabled.store (false);
    pad.loopStart.store (0);
    pad.loopEnd.store (lastSample);
    std::atomic_store_explicit (&pad.sample, newSample, std::memory_order_release);

    {
        const juce::ScopedLock lock (stateLock);
        pad.samplePath = file.getFullPathName();
        pad.displayName = file.getFileNameWithoutExtension();
    }

    markPortableSettingsDirty();
    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::previewSampleFile (const juce::File& file)
{
    juce::String errorMessage;
    const auto newSample = createSampleData (file, errorMessage);

    if (newSample == nullptr)
        return juce::Result::fail (errorMessage);

    std::atomic_store_explicit (&browserPreviewSample, newSample,
                                std::memory_order_release);
    browserPreviewTriggerPending.store (true);
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

    const auto monoSampleAt = [&] (int sampleIndex)
    {
        float value = 0.0f;

        for (int channel = 0; channel < channelCount; ++channel)
            value += newSample->audio.getSample (channel, sampleIndex);

        return value / static_cast<float> (channelCount);
    };

    newSample->zeroCrossings.reserve (
        static_cast<std::size_t> (juce::jmax (16, sampleCount / 64)));
    float previousValue = monoSampleAt (0);

    if (previousValue == 0.0f)
        newSample->zeroCrossings.push_back (0);

    for (int sampleIndex = 1; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float currentValue = monoSampleAt (sampleIndex);
        int crossingSample = -1;

        if (currentValue == 0.0f && previousValue != 0.0f)
            crossingSample = sampleIndex;
        else if (currentValue != 0.0f && previousValue != 0.0f
                 && ((currentValue < 0.0f) != (previousValue < 0.0f)))
            crossingSample = std::abs (previousValue) <= std::abs (currentValue)
                               ? sampleIndex - 1 : sampleIndex;

        if (crossingSample >= 0
            && (newSample->zeroCrossings.empty()
                || newSample->zeroCrossings.back() != crossingSample))
            newSample->zeroCrossings.push_back (crossingSample);

        previousValue = currentValue;
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
    pad.sampleStart.store (0);
    pad.sampleEnd.store (0);
    pad.loopEnabled.store (false);
    pad.loopStart.store (0);
    pad.loopEnd.store (0);
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
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (-60.0f, 6.0f, decibels);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].volume,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].volumeDb.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadPan (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].pan.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadPan (int padIndex, float pan)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (-1.0f, 1.0f, pan);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].pan,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].pan.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadTuneSemitones (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].tuneSemitones.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadTuneSemitones (int padIndex, float semitones)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (-24.0f, 24.0f, semitones);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].tune,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].tuneSemitones.store (value);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPadChokeGroup (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].chokeGroup.load()
         : 0;
}

void SVDrummerAudioProcessor::setPadChokeGroup (int padIndex, int chokeGroup)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].chokeGroup.store (
            juce::jlimit (0, numberOfPads, chokeGroup));
        markPortableSettingsDirty();
    }
}

float SVDrummerAudioProcessor::getPadAmpAttackMs (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].ampAttackMs.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadAmpAttackMs (int padIndex,
                                                  float milliseconds)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].ampAttackMs.store (
            juce::jlimit (0.0f, 2000.0f, milliseconds));
        markPortableSettingsDirty();
    }
}

float SVDrummerAudioProcessor::getPadAmpDecayMs (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].ampDecayMs.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadAmpDecayMs (int padIndex,
                                                 float milliseconds)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].ampDecayMs.store (
            juce::jlimit (0.0f, 5000.0f, milliseconds));
        markPortableSettingsDirty();
    }
}

float SVDrummerAudioProcessor::getPadAmpSustain (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].ampSustain.load()
         : 1.0f;
}

void SVDrummerAudioProcessor::setPadAmpSustain (int padIndex, float level)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].ampSustain.store (
            juce::jlimit (0.0f, 1.0f, level));
        markPortableSettingsDirty();
    }
}

float SVDrummerAudioProcessor::getPadAmpReleaseMs (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].ampReleaseMs.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadAmpReleaseMs (int padIndex,
                                                   float milliseconds)
{
    if (isValidPadIndex (padIndex))
    {
        pads[static_cast<std::size_t> (padIndex)].ampReleaseMs.store (
            juce::jlimit (0.0f, 5000.0f, milliseconds));
        markPortableSettingsDirty();
    }
}

int SVDrummerAudioProcessor::getPadSampleLength (int padIndex) const
{
    const auto sample = getPadSample (padIndex);
    return sample != nullptr ? sample->audio.getNumSamples() : 0;
}

int SVDrummerAudioProcessor::getPadSampleStart (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].sampleStart.load()
         : 0;
}

void SVDrummerAudioProcessor::setPadSampleStart (int padIndex,
                                                  int samplePosition,
                                                  int snapDirection)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const int lastSample = juce::jmax (0, getPadSampleLength (padIndex) - 1);
    const int currentEnd = juce::jlimit (0, lastSample, pad.sampleEnd.load());
    const int maximumStart = currentEnd > 0 ? currentEnd - 1 : 0;
    const int newStart = snapMarkerPosition (
        padIndex, samplePosition, 0, maximumStart,
        pad.sampleStart.load(), snapDirection);
    pad.sampleStart.store (newStart);

    int adjustedLoopEnd = juce::jlimit (
        newStart, currentEnd, pad.loopEnd.load());

    if (currentEnd > newStart && adjustedLoopEnd <= newStart)
        adjustedLoopEnd = newStart + 1;

    pad.loopEnd.store (adjustedLoopEnd);
    pad.loopStart.store (juce::jlimit (
        newStart,
        adjustedLoopEnd > newStart ? adjustedLoopEnd - 1 : newStart,
        pad.loopStart.load()));
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPadSampleEnd (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].sampleEnd.load()
         : 0;
}

void SVDrummerAudioProcessor::setPadSampleEnd (int padIndex,
                                                int samplePosition,
                                                int snapDirection)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const int lastSample = juce::jmax (0, getPadSampleLength (padIndex) - 1);
    const int currentStart = juce::jlimit (
        0, lastSample, pad.sampleStart.load());
    const int minimumEnd = currentStart < lastSample ? currentStart + 1
                                                      : currentStart;
    const int newEnd = snapMarkerPosition (
        padIndex, samplePosition, minimumEnd, lastSample,
        pad.sampleEnd.load(), snapDirection);
    pad.sampleEnd.store (newEnd);

    const int adjustedLoopStart = juce::jlimit (
        currentStart, newEnd, pad.loopStart.load());
    pad.loopStart.store (adjustedLoopStart);
    pad.loopEnd.store (juce::jlimit (
        adjustedLoopStart < newEnd ? adjustedLoopStart + 1
                                   : adjustedLoopStart,
        newEnd,
        pad.loopEnd.load()));
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadLoopEnabled (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].loopEnabled.load();
}

void SVDrummerAudioProcessor::setPadLoopEnabled (int padIndex, bool shouldLoop)
{
    if (! isValidPadIndex (padIndex))
        return;

    pads[static_cast<std::size_t> (padIndex)].loopEnabled.store (shouldLoop);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPadLoopStart (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].loopStart.load()
         : 0;
}

void SVDrummerAudioProcessor::setPadLoopStart (int padIndex,
                                                int samplePosition,
                                                int snapDirection)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const int minimumStart = pad.sampleStart.load();
    const int currentEnd = pad.loopEnd.load();
    const int maximumStart = currentEnd > minimumStart ? currentEnd - 1
                                                        : minimumStart;
    pad.loopStart.store (snapMarkerPosition (
        padIndex, samplePosition, minimumStart, maximumStart,
        pad.loopStart.load(), snapDirection));
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPadLoopEnd (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].loopEnd.load()
         : 0;
}

void SVDrummerAudioProcessor::setPadLoopEnd (int padIndex,
                                              int samplePosition,
                                              int snapDirection)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const int currentStart = pad.loopStart.load();
    const int maximumEnd = pad.sampleEnd.load();
    const int minimumEnd = currentStart < maximumEnd ? currentStart + 1
                                                      : currentStart;
    pad.loopEnd.store (snapMarkerPosition (
        padIndex, samplePosition, minimumEnd, maximumEnd,
        pad.loopEnd.load(), snapDirection));
    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::resetPadSampleMarkers (int padIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const int lastSample = juce::jmax (0, getPadSampleLength (padIndex) - 1);
    pad.sampleStart.store (0);
    pad.sampleEnd.store (lastSample);
    pad.loopStart.store (0);
    pad.loopEnd.store (lastSample);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isSampleMarkerSnapEnabled() const
{
    return sampleMarkerSnapEnabled.load();
}

void SVDrummerAudioProcessor::setSampleMarkerSnapEnabled (bool shouldSnap)
{
    if (setHostParameterValue (
            sampleMarkerSnapParameter, shouldSnap ? 1.0f : 0.0f))
        return;

    sampleMarkerSnapEnabled.store (shouldSnap);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::snapMarkerPosition (
    int padIndex, int requestedPosition,
    int minimumPosition, int maximumPosition,
    int currentPosition, int direction) const
{
    const int minimum = juce::jmin (minimumPosition, maximumPosition);
    const int maximum = juce::jmax (minimumPosition, maximumPosition);
    const int requested = juce::jlimit (minimum, maximum, requestedPosition);

    if (! sampleMarkerSnapEnabled.load())
        return requested;

    const auto sample = getPadSample (padIndex);

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
        return juce::jlimit (minimum, maximum, currentPosition);

    const auto monoSampleAt = [&] (int sampleIndex)
    {
        float value = 0.0f;

        for (int channel = 0; channel < sample->audio.getNumChannels(); ++channel)
            value += sample->audio.getSample (channel, sampleIndex);

        return value / static_cast<float> (sample->audio.getNumChannels());
    };

    if (monoSampleAt (requested) == 0.0f)
        return requested;

    const auto& crossings = sample->zeroCrossings;

    if (crossings.empty())
        return juce::jlimit (minimum, maximum, currentPosition);

    if (direction > 0)
    {
        const auto next = std::upper_bound (
            crossings.begin(), crossings.end(), currentPosition);

        if (next != crossings.end() && *next >= minimum && *next <= maximum)
            return *next;

        return juce::jlimit (minimum, maximum, currentPosition);
    }

    if (direction < 0)
    {
        auto previous = std::lower_bound (
            crossings.begin(), crossings.end(), currentPosition);

        while (previous != crossings.begin())
        {
            --previous;

            if (*previous >= minimum && *previous <= maximum)
                return *previous;

            if (*previous < minimum)
                break;
        }

        return juce::jlimit (minimum, maximum, currentPosition);
    }

    auto after = std::lower_bound (crossings.begin(), crossings.end(), requested);
    int best = juce::jlimit (minimum, maximum, currentPosition);
    int bestDistance = (std::numeric_limits<int>::max)();

    const auto consider = [&] (int candidate)
    {
        if (candidate < minimum || candidate > maximum)
            return;

        const int distance = std::abs (candidate - requested);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    };

    if (after != crossings.end())
        consider (*after);

    if (after != crossings.begin())
        consider (*std::prev (after));

    return best;
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
    if (setHostParameterValue (
            sequencerEnabledParameter, shouldBeEnabled ? 1.0f : 0.0f))
        return;

    sequencerEnabled.store (shouldBeEnabled);
    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::stopPatternMidiPlayback()
{
    if (sequencerEnabledParameter != nullptr)
        sequencerEnabledParameter->setValueNotifyingHost (0.0f);
    else
        sequencerEnabled.store (false);

    activePatternGateNote.store (-1);
    pendingPatternGateStartNote.store (-1);
    patternGateActive.store (false);
    patternGateWaitingForSelection.store (false);
    patternGateRestartCounter.fetch_add (1);
}

SVDrummerAudioProcessor::PatternMidiMode
SVDrummerAudioProcessor::getPatternMidiMode() const
{
    return static_cast<PatternMidiMode> (juce::jlimit (
        static_cast<int> (PatternMidiMode::select),
        static_cast<int> (PatternMidiMode::hold),
        patternMidiMode.load()));
}

void SVDrummerAudioProcessor::setPatternMidiMode (PatternMidiMode newMode)
{
    const int mode = juce::jlimit (
        static_cast<int> (PatternMidiMode::select),
        static_cast<int> (PatternMidiMode::hold),
        static_cast<int> (newMode));

    if (setHostParameterValue (
            patternMidiModeParameter, static_cast<float> (mode)))
        return;

    patternMidiMode.store (juce::jlimit (
        static_cast<int> (PatternMidiMode::select),
        static_cast<int> (PatternMidiMode::hold),
        static_cast<int> (newMode)));
    patternGateActive.store (false);
    patternGateWaitingForSelection.store (false);
    activePatternGateNote.store (-1);
    pendingPatternGateStartNote.store (-1);
    sequencerEnabled.store (false);
    patternGateRestartCounter.fetch_add (1);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPatternBars() const
{
    return patternBars.load();
}

void SVDrummerAudioProcessor::setPatternBars (int bars)
{
    const int newBars = juce::jlimit (1, maximumPatternBars, bars);

    if (setHostParameterValue (
            patternBarsParameter, static_cast<float> (newBars)))
        return;

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
         : 4;
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

std::uint64_t SVDrummerAudioProcessor::getPatternChangeRevision() const
{
    return patternChangeCounter.load();
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
        initialisePatternSlot (newPatternIndex);

    currentPatternIndex.store (newPatternIndex);
    applyStoredPattern (newPatternIndex);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPatternAssigned (int patternIndex) const
{
    return isValidPatternIndex (patternIndex)
        && storedPatterns[static_cast<std::size_t> (patternIndex)].assigned;
}

bool SVDrummerAudioProcessor::patternHasSteps (int patternIndex) const
{
    if (! isValidPatternIndex (patternIndex))
        return false;

    if (patternIndex == currentPatternIndex.load())
    {
        for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
            for (int step = 0;
                 step < getLaneMaximumLoopLength (laneIndex);
                 ++step)
                if (getSequenceStepVelocity (laneIndex, step) > 0)
                    return true;

        return false;
    }

    const auto& stored = storedPatterns[static_cast<std::size_t> (patternIndex)];

    if (! stored.assigned)
        return false;

    for (const auto& lane : stored.lanes)
    {
        const int relevantSteps = juce::jlimit (
            1, maximumStepsPerLane,
            stored.bars * getSequencerStepsPerBar (lane.division));

        for (int step = 0; step < relevantSteps; ++step)
            if (lane.stepVelocities[static_cast<std::size_t> (step)] > 0)
                return true;
    }

    return false;
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
    const int requestedGateNote = pendingPatternGateStartNote.exchange (-1);

    if (isValidPatternIndex (requestedPattern))
        selectPattern (requestedPattern);

    if (getPatternMidiMode() != PatternMidiMode::select
        && requestedGateNote >= 0)
    {
        if (activePatternGateNote.load() == requestedGateNote)
        {
            patternGateWaitingForSelection.store (false);
            patternGateActive.store (true);
            sequencerEnabled.store (true);
            patternGateRestartCounter.fetch_add (1);
        }
        else
        {
            patternGateWaitingForSelection.store (false);
            patternGateActive.store (false);
            sequencerEnabled.store (false);
        }
    }
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

}

void SVDrummerAudioProcessor::capturePatternUndoState (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    undoPattern = storedPatterns[static_cast<std::size_t> (patternIndex)];
    undoPatternIndex.store (patternIndex);
}

void SVDrummerAudioProcessor::initialisePatternSlot (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    StoredPattern newPattern;
    newPattern.assigned = true;
    newPattern.name = "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2);

    const int sourceIndex = currentPatternIndex.load();

    if (isValidPatternIndex (sourceIndex)
        && storedPatterns[static_cast<std::size_t> (sourceIndex)].assigned)
    {
        const auto& source = storedPatterns[static_cast<std::size_t> (sourceIndex)];
        newPattern.bars = source.bars;

        for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
        {
            newPattern.lanes[static_cast<std::size_t> (laneIndex)].division
                = source.lanes[static_cast<std::size_t> (laneIndex)].division;
            newPattern.lanes[static_cast<std::size_t> (laneIndex)].loopLength
                = source.lanes[static_cast<std::size_t> (laneIndex)].loopLength;
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

    patternChangeCounter.fetch_add (1);
}

juce::String SVDrummerAudioProcessor::getSequencerDivisionName (int divisionIndex)
{
    static const std::array<juce::String, sequencerDivisionCount> names
    {
        "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32",
        "1/32T", "1/64"
    };

    return names[static_cast<std::size_t> (
        juce::jlimit (0, sequencerDivisionCount - 1, divisionIndex))];
}

int SVDrummerAudioProcessor::getSequencerStepsPerBar (int divisionIndex)
{
    static constexpr std::array<int, sequencerDivisionCount> steps
    {
        4, 6, 8, 12, 16, 24, 32, 48, 64
    };

    return steps[static_cast<std::size_t> (
        juce::jlimit (0, sequencerDivisionCount - 1, divisionIndex))];
}

double SVDrummerAudioProcessor::getSequencerQuarterNotesPerStep (int divisionIndex)
{
    static constexpr std::array<double, sequencerDivisionCount> lengths
    {
        1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125,
        1.0 / 12.0, 0.0625
    };

    return lengths[static_cast<std::size_t> (
        juce::jlimit (0, sequencerDivisionCount - 1, divisionIndex))];
}

void SVDrummerAudioProcessor::processSequencerTriggers (int numSamples)
{
    const auto currentPatternRevision = patternChangeCounter.load();
    const auto currentGateRestartRevision = patternGateRestartCounter.load();
    bool timelineNeedsReset = false;

    if (currentPatternRevision != lastPatternChangeCounter)
    {
        lastPatternChangeCounter = currentPatternRevision;
        timelineNeedsReset = true;
    }

    if (currentGateRestartRevision != lastPatternGateRestartCounter)
    {
        lastPatternGateRestartCounter = currentGateRestartRevision;
        timelineNeedsReset = true;
    }

    if (timelineNeedsReset && sequencerWasPlaying)
        stopSequencerLoopVoicesOnAudioThread();

    if (timelineNeedsReset)
        resetSequencerTimeline();

    auto playing = false;
    auto bpm = 120.0;
    auto blockStartPpq = fallbackSequencerPpq;
    auto hostPpqAvailable = false;
    const bool patternTriggerMode =
        getPatternMidiMode() != PatternMidiMode::select;

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

    if (patternTriggerMode)
    {
        blockStartPpq = fallbackSequencerPpq;
        hostPpqAvailable = false;
    }

    const bool gateReady = patternGateActive.load()
                        && ! patternGateWaitingForSelection.load();
    const bool playbackRequested = patternTriggerMode ? gateReady : playing;

    if (! sequencerEnabled.load() || ! playbackRequested || numSamples <= 0)
    {
        if (sequencerWasPlaying)
        {
            stopSequencerLoopVoicesOnAudioThread();
            resetSequencerTimeline();
        }

        for (auto& lane : sequenceLanes)
            lane.activeStep.store (-1);

        if (! playbackRequested)
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
                                         sample,
                                         true);
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

juce::Result SVDrummerAudioProcessor::addBrowserFolder (
    const juce::File& folder)
{
    if (! folder.isDirectory())
        return juce::Result::fail ("The selected sample folder does not exist.");

    const auto path = folder.getFullPathName();

    {
        const juce::ScopedLock lock (stateLock);

        for (const auto& existing : browserFolders)
            if (juce::File (existing) == folder)
                return juce::Result::ok();

        browserFolders.add (path);
    }

    return saveBrowserFoldersNow();
}

juce::Result SVDrummerAudioProcessor::removeBrowserFolder (
    const juce::File& folder)
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

    return changed ? saveBrowserFoldersNow() : juce::Result::ok();
}

int SVDrummerAudioProcessor::getEditorSelectedPad() const noexcept
{
    return juce::jlimit (0, numberOfPads - 1, editorSelectedPad.load());
}

void SVDrummerAudioProcessor::setEditorSelectedPad (int padIndex) noexcept
{
    editorSelectedPad.store (juce::jlimit (0, numberOfPads - 1, padIndex));
}

bool SVDrummerAudioProcessor::isEditorShowingPadSettings() const noexcept
{
    return editorShowingPadSettings.load();
}

void SVDrummerAudioProcessor::setEditorShowingPadSettings (bool shouldShow) noexcept
{
    editorShowingPadSettings.store (shouldShow);
}

SVDrummerAudioProcessor::BrowserMode
SVDrummerAudioProcessor::getEditorBrowserMode() const noexcept
{
    return static_cast<BrowserMode> (juce::jlimit (
        static_cast<int> (BrowserMode::samples),
        static_cast<int> (BrowserMode::projects),
        editorBrowserMode.load()));
}

void SVDrummerAudioProcessor::setEditorBrowserMode (BrowserMode mode) noexcept
{
    editorBrowserMode.store (static_cast<int> (mode));
}

juce::String SVDrummerAudioProcessor::getEditorBrowserTreeState (
    BrowserMode mode) const
{
    const juce::ScopedLock lock (stateLock);

    switch (mode)
    {
        case BrowserMode::kits:     return kitsBrowserTreeState;
        case BrowserMode::patterns: return patternsBrowserTreeState;
        case BrowserMode::projects: return projectsBrowserTreeState;
        case BrowserMode::samples:
        default:                    return samplesBrowserTreeState;
    }
}

void SVDrummerAudioProcessor::setEditorBrowserTreeState (
    BrowserMode mode, const juce::String& state)
{
    const juce::ScopedLock lock (stateLock);

    switch (mode)
    {
        case BrowserMode::kits:     kitsBrowserTreeState = state; break;
        case BrowserMode::patterns: patternsBrowserTreeState = state; break;
        case BrowserMode::projects: projectsBrowserTreeState = state; break;
        case BrowserMode::samples:
        default:                    samplesBrowserTreeState = state; break;
    }
}

juce::File SVDrummerAudioProcessor::getPortableDataDirectory() const
{
    return getThisModuleFile().getParentDirectory().getChildFile ("Data");
}

juce::File SVDrummerAudioProcessor::getPortableSamplesDirectory() const
{
    return getPortableDataDirectory().getChildFile ("Samples");
}

juce::File SVDrummerAudioProcessor::getPortableKitsDirectory() const
{
    return getPortableDataDirectory().getChildFile ("Kits");
}

juce::File SVDrummerAudioProcessor::getPortablePatternsDirectory() const
{
    return getPortableDataDirectory().getChildFile ("Patterns");
}

juce::File SVDrummerAudioProcessor::getPortableProjectsDirectory() const
{
    return getPortableDataDirectory().getChildFile ("Projects");
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

std::unique_ptr<juce::XmlElement> SVDrummerAudioProcessor::createKitXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("SVDRUMMER_KIT");
    xml->setAttribute ("version", "1.0");

    const juce::ScopedLock lock (stateLock);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& pad = pads[static_cast<std::size_t> (padIndex)];
        auto* padXml = xml->createNewChildElement ("PAD");
        padXml->setAttribute ("index", padIndex);
        padXml->setAttribute ("note", pad.midiNote.load());
        padXml->setAttribute ("mute", pad.muted.load());
        padXml->setAttribute ("solo", pad.soloed.load());
        padXml->setAttribute ("reverse", pad.reversed.load());
        padXml->setAttribute ("volumeDb", static_cast<double> (pad.volumeDb.load()));
        padXml->setAttribute ("pan", static_cast<double> (pad.pan.load()));
        padXml->setAttribute ("tune", static_cast<double> (pad.tuneSemitones.load()));
        padXml->setAttribute ("chokeGroup", pad.chokeGroup.load());
        padXml->setAttribute ("ampAttackMs", static_cast<double> (pad.ampAttackMs.load()));
        padXml->setAttribute ("ampDecayMs", static_cast<double> (pad.ampDecayMs.load()));
        padXml->setAttribute ("ampSustain", static_cast<double> (pad.ampSustain.load()));
        padXml->setAttribute ("ampReleaseMs", static_cast<double> (pad.ampReleaseMs.load()));
        padXml->setAttribute ("startSample", pad.sampleStart.load());
        padXml->setAttribute ("endSample", pad.sampleEnd.load());
        padXml->setAttribute ("loopEnabled", pad.loopEnabled.load());
        padXml->setAttribute ("loopStartSample", pad.loopStart.load());
        padXml->setAttribute ("loopEndSample", pad.loopEnd.load());
        padXml->setAttribute ("sample", pad.samplePath.isNotEmpty()
                                         ? makeStoredPath (juce::File (pad.samplePath))
                                         : juce::String());
    }

    return xml;
}

juce::Result SVDrummerAudioProcessor::loadKitXml (
    const juce::XmlElement& kitXml)
{
    if (! kitXml.hasTagName ("SVDRUMMER_KIT"))
        return juce::Result::fail ("This is not an SV-Drummer kit file.");

    std::array<KitPadState, numberOfPads> loaded;

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
        loaded[static_cast<std::size_t> (padIndex)].midiNote = 36 + padIndex;

    for (auto* item : kitXml.getChildWithTagNameIterator ("PAD"))
    {
        const int padIndex = item->getIntAttribute ("index", -1);

        if (! isValidPadIndex (padIndex))
            continue;

        auto& pad = loaded[static_cast<std::size_t> (padIndex)];
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
        pad.chokeGroup = juce::jlimit (
            0, numberOfPads, item->getIntAttribute ("chokeGroup", 0));
        pad.ampAttackMs = juce::jlimit (
            0.0f, 2000.0f,
            static_cast<float> (item->getDoubleAttribute ("ampAttackMs", 0.0)));
        pad.ampDecayMs = juce::jlimit (
            0.0f, 5000.0f,
            static_cast<float> (item->getDoubleAttribute ("ampDecayMs", 0.0)));
        pad.ampSustain = juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute ("ampSustain", 1.0)));
        pad.ampReleaseMs = juce::jlimit (
            0.0f, 5000.0f,
            static_cast<float> (item->getDoubleAttribute ("ampReleaseMs", 0.0)));
        pad.loopEnabled = item->getBoolAttribute ("loopEnabled", false);

        const bool hasSavedStart = item->hasAttribute ("startSample");
        const bool hasSavedEnd = item->hasAttribute ("endSample");
        const bool hasSavedLoopStart = item->hasAttribute ("loopStartSample");
        const bool hasSavedLoopEnd = item->hasAttribute ("loopEndSample");
        const auto storedPath = item->getStringAttribute ("sample");

        if (storedPath.isNotEmpty())
        {
            const auto sampleFile = resolveStoredPath (storedPath);
            pad.samplePath = sampleFile.getFullPathName();
            juce::String errorMessage;
            pad.sample = createSampleData (sampleFile, errorMessage);
            pad.displayName = sampleFile.getFileNameWithoutExtension();

            if (pad.sample == nullptr)
                pad.displayName += " (MISSING)";
        }

        const int explicitEnd = hasSavedEnd
                                  ? juce::jmax (
                                        0, item->getIntAttribute ("endSample", 0))
                                  : 0;
        const int explicitLoopEnd = hasSavedLoopEnd
                                      ? juce::jmax (
                                            0, item->getIntAttribute (
                                                   "loopEndSample", 0))
                                      : 0;
        const int lastSample = pad.sample != nullptr
                                 ? juce::jmax (
                                       0, pad.sample->audio.getNumSamples() - 1)
                                 : juce::jmax (explicitEnd, explicitLoopEnd);
        const int savedStart = hasSavedStart
                                 ? juce::jmax (
                                       0, item->getIntAttribute ("startSample", 0))
                                 : 0;
        const int savedEnd = hasSavedEnd
                               ? juce::jmax (savedStart, explicitEnd)
                               : lastSample;
        const int savedLoopStart = hasSavedLoopStart
                                     ? juce::jmax (
                                           savedStart,
                                           item->getIntAttribute (
                                               "loopStartSample", savedStart))
                                     : savedStart;
        const int savedLoopEnd = hasSavedLoopEnd
                                   ? juce::jmax (savedLoopStart, explicitLoopEnd)
                                   : savedEnd;
        pad.sampleStart = juce::jlimit (0, lastSample, savedStart);
        pad.sampleEnd = juce::jlimit (
            pad.sampleStart < lastSample ? pad.sampleStart + 1
                                         : pad.sampleStart,
            lastSample, savedEnd);
        pad.loopStart = juce::jlimit (
            pad.sampleStart,
            pad.sampleEnd > pad.sampleStart ? pad.sampleEnd - 1
                                             : pad.sampleStart,
            savedLoopStart);
        pad.loopEnd = juce::jlimit (
            pad.loopStart < pad.sampleEnd ? pad.loopStart + 1
                                          : pad.loopStart,
            pad.sampleEnd, savedLoopEnd);
    }

    const juce::ScopedLock lock (stateLock);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& sourcePad = loaded[static_cast<std::size_t> (padIndex)];
        auto& targetPad = pads[static_cast<std::size_t> (padIndex)];
        targetPad.sampleRevision.fetch_add (1);
        std::atomic_store_explicit (&targetPad.sample, sourcePad.sample,
                                    std::memory_order_release);
        targetPad.samplePath = sourcePad.samplePath;
        targetPad.displayName = sourcePad.displayName;
        targetPad.midiNote.store (sourcePad.midiNote);
        targetPad.muted.store (sourcePad.muted);
        targetPad.soloed.store (sourcePad.soloed);
        targetPad.reversed.store (sourcePad.reversed);
        targetPad.volumeDb.store (sourcePad.volumeDb);
        targetPad.pan.store (sourcePad.pan);
        targetPad.tuneSemitones.store (sourcePad.tuneSemitones);
        targetPad.chokeGroup.store (sourcePad.chokeGroup);
        targetPad.ampAttackMs.store (sourcePad.ampAttackMs);
        targetPad.ampDecayMs.store (sourcePad.ampDecayMs);
        targetPad.ampSustain.store (sourcePad.ampSustain);
        targetPad.ampReleaseMs.store (sourcePad.ampReleaseMs);
        targetPad.sampleStart.store (sourcePad.sampleStart);
        targetPad.sampleEnd.store (sourcePad.sampleEnd);
        targetPad.loopEnabled.store (sourcePad.loopEnabled);
        targetPad.loopStart.store (sourcePad.loopStart);
        targetPad.loopEnd.store (sourcePad.loopEnd);
    }

    patternChangeCounter.fetch_add (1);
    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::saveKitToFile (
    const juce::File& file) const
{
    const auto xml = createKitXml();

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The kit folder could not be created.");

    if (! file.replaceWithText (xml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The kit file could not be written.");

    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::loadKitFromFile (
    const juce::File& file)
{
    if (! isSupportedKitFile (file) || ! file.existsAsFile())
        return juce::Result::fail ("Select an SV-Drummer .svkit file.");

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return juce::Result::fail ("The kit file could not be read.");

    const auto result = loadKitXml (*xml);

    if (result.wasOk())
    {
        relinkMissingSamplesFromFolder (file.getParentDirectory(), false);
        markPortableSettingsDirty();
    }

    return result;
}

bool SVDrummerAudioProcessor::kitHasSamples() const
{
    const juce::ScopedLock lock (stateLock);

    for (const auto& pad : pads)
        if (pad.samplePath.isNotEmpty())
            return true;

    return false;
}

std::unique_ptr<juce::XmlElement>
SVDrummerAudioProcessor::createPatternXml (int patternIndex) const
{
    if (! isValidPatternIndex (patternIndex))
        return {};

    const auto& pattern = storedPatterns[static_cast<std::size_t> (patternIndex)];
    auto xml = std::make_unique<juce::XmlElement> ("SVDRUMMER_PATTERN");
    xml->setAttribute ("version", "1.0");
    xml->setAttribute ("type", "pattern");
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

    return xml;
}

std::unique_ptr<juce::XmlElement>
SVDrummerAudioProcessor::createPatternSetXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("SVDRUMMER_PATTERN_SET");
    xml->setAttribute ("version", "1.0");
    xml->setAttribute ("type", "pattern-set");
    xml->setAttribute ("current", getCurrentPatternIndex());

    for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
    {
        auto* slotXml = xml->createNewChildElement ("SLOT");
        slotXml->setAttribute ("index", patternIndex);
        slotXml->setAttribute ("assigned", isPatternAssigned (patternIndex));
        slotXml->setAttribute ("midiNote", getPatternMidiNote (patternIndex));

        if (isPatternAssigned (patternIndex))
            if (auto patternXml = createPatternXml (patternIndex))
                slotXml->addChildElement (patternXml.release());
    }

    return xml;
}

juce::Result SVDrummerAudioProcessor::parsePatternXml (
    int patternIndex, const juce::XmlElement& patternXml,
    StoredPattern& destination) const
{
    if (! isValidPatternIndex (patternIndex))
        return juce::Result::fail ("Invalid pattern slot.");

    if (! patternXml.hasTagName ("SVDRUMMER_PATTERN")
        || patternXml.getStringAttribute ("type") != "pattern")
        return juce::Result::fail ("This is not an SV-Drummer pattern file.");

    StoredPattern loaded;
    loaded.assigned = true;
    loaded.name = patternXml.getStringAttribute (
        "name", "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2));
    loaded.bars = juce::jlimit (
        1, maximumPatternBars, patternXml.getIntAttribute ("bars", 1));

    for (auto* item : patternXml.getChildWithTagNameIterator ("LANE"))
    {
        const int laneIndex = item->getIntAttribute ("index", -1);

        if (! isValidPadIndex (laneIndex))
            continue;

        auto& lane = loaded.lanes[static_cast<std::size_t> (laneIndex)];
        lane.division = juce::jlimit (
            0, sequencerDivisionCount - 1, item->getIntAttribute ("division", 4));
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

    destination = std::move (loaded);
    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::loadPatternXmlIntoSlot (
    int patternIndex, const juce::XmlElement& patternXml)
{
    StoredPattern loaded;
    const auto result = parsePatternXml (patternIndex, patternXml, loaded);

    if (result.failed())
        return result;

    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (loaded);
    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::loadPatternSetXml (
    const juce::XmlElement& patternSetXml)
{
    if (! patternSetXml.hasTagName ("SVDRUMMER_PATTERN_SET")
        || patternSetXml.getStringAttribute ("type") != "pattern-set")
        return juce::Result::fail ("This is not an SV-Drummer pattern-set file.");

    std::array<StoredPattern, numberOfPatterns> loadedPatterns;
    std::array<int, numberOfPatterns> loadedMidiNotes;
    loadedMidiNotes.fill (-1);

    for (int index = 0; index < numberOfPatterns; ++index)
        loadedPatterns[static_cast<std::size_t> (index)].name
            = "Pattern " + juce::String (index + 1).paddedLeft ('0', 2);

    for (auto* slotXml : patternSetXml.getChildWithTagNameIterator ("SLOT"))
    {
        const int patternIndex = slotXml->getIntAttribute ("index", -1);

        if (! isValidPatternIndex (patternIndex))
            continue;

        loadedMidiNotes[static_cast<std::size_t> (patternIndex)] = juce::jlimit (
            -1, 127, slotXml->getIntAttribute ("midiNote", -1));

        if (! slotXml->getBoolAttribute ("assigned", false))
            continue;

        auto* patternXml = slotXml->getChildByName ("SVDRUMMER_PATTERN");

        if (patternXml == nullptr)
            return juce::Result::fail (
                "The Pattern Set contains an invalid Pattern slot.");

        const auto result = parsePatternXml (
            patternIndex, *patternXml,
            loadedPatterns[static_cast<std::size_t> (patternIndex)]);

        if (result.failed())
            return result;
    }

    storedPatterns = std::move (loadedPatterns);
    undoPatternIndex.store (-1);

    for (int index = 0; index < numberOfPatterns; ++index)
        patternMidiNotes[static_cast<std::size_t> (index)].store (
            loadedMidiNotes[static_cast<std::size_t> (index)]);

    int restoredPattern = juce::jlimit (
        0, numberOfPatterns - 1,
        patternSetXml.getIntAttribute ("current", 0));

    if (! isPatternAssigned (restoredPattern))
    {
        restoredPattern = 0;

        while (restoredPattern < numberOfPatterns
               && ! isPatternAssigned (restoredPattern))
            ++restoredPattern;
    }

    if (restoredPattern >= numberOfPatterns)
    {
        restoredPattern = 0;
        initialisePatternSlot (restoredPattern);
    }

    currentPatternIndex.store (restoredPattern);
    applyStoredPattern (restoredPattern);
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

    StoredPattern loaded;
    const auto result = parsePatternXml (patternIndex, *xml, loaded);

    if (result.failed())
        return result;

    captureCurrentPattern();
    capturePatternUndoState (patternIndex);
    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (loaded);

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

void SVDrummerAudioProcessor::copyPatternSlot (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    if (patternIndex == currentPatternIndex.load())
        captureCurrentPattern();

    copiedPattern = storedPatterns[static_cast<std::size_t> (patternIndex)];
    copiedPatternAvailable.store (true);
}

bool SVDrummerAudioProcessor::canPastePatternSlot() const noexcept
{
    return copiedPatternAvailable.load();
}

void SVDrummerAudioProcessor::pastePatternSlot (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex) || ! canPastePatternSlot())
        return;

    captureCurrentPattern();
    capturePatternUndoState (patternIndex);
    auto pasted = copiedPattern;
    pasted.assigned = true;
    pasted.name = "Pattern "
                + juce::String (patternIndex + 1).paddedLeft ('0', 2);
    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (pasted);

    if (patternIndex == currentPatternIndex.load())
        applyStoredPattern (patternIndex);

    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::clearPatternSlot (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    captureCurrentPattern();
    capturePatternUndoState (patternIndex);
    auto cleared = storedPatterns[static_cast<std::size_t> (patternIndex)];
    cleared.assigned = true;

    if (cleared.name.isEmpty())
        cleared.name = "Pattern "
                     + juce::String (patternIndex + 1).paddedLeft ('0', 2);

    for (auto& lane : cleared.lanes)
        lane.stepVelocities.fill (0);

    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (cleared);

    if (patternIndex == currentPatternIndex.load())
        applyStoredPattern (patternIndex);

    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::randomisePatternSlot (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    captureCurrentPattern();
    capturePatternUndoState (patternIndex);

    if (! storedPatterns[static_cast<std::size_t> (patternIndex)].assigned)
        initialisePatternSlot (patternIndex);

    auto randomised = storedPatterns[static_cast<std::size_t> (patternIndex)];
    randomised.assigned = true;

    if (randomised.name.isEmpty())
        randomised.name = "Pattern "
                        + juce::String (patternIndex + 1).paddedLeft ('0', 2);

    auto& random = juce::Random::getSystemRandom();

    for (auto& lane : randomised.lanes)
    {
        lane.stepVelocities.fill (0);
        const int steps = juce::jlimit (
            1, maximumStepsPerLane, lane.loopLength);
        bool addedStep = false;

        for (int step = 0; step < steps; ++step)
        {
            if (random.nextFloat() < 0.25f)
            {
                lane.stepVelocities[static_cast<std::size_t> (step)]
                    = static_cast<std::uint8_t> (72 + random.nextInt (56));
                addedStep = true;
            }
        }

        if (! addedStep)
        {
            const int step = random.nextInt (steps);
            lane.stepVelocities[static_cast<std::size_t> (step)]
                = static_cast<std::uint8_t> (72 + random.nextInt (56));
        }
    }

    storedPatterns[static_cast<std::size_t> (patternIndex)]
        = std::move (randomised);

    if (patternIndex == currentPatternIndex.load())
        applyStoredPattern (patternIndex);

    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::canUndoPatternOperation (
    int patternIndex) const noexcept
{
    return isValidPatternIndex (patternIndex)
        && undoPatternIndex.load() == patternIndex;
}

void SVDrummerAudioProcessor::undoPatternOperation (int patternIndex)
{
    if (! canUndoPatternOperation (patternIndex))
        return;

    storedPatterns[static_cast<std::size_t> (patternIndex)] = undoPattern;
    undoPatternIndex.store (-1);

    if (patternIndex == currentPatternIndex.load())
        applyStoredPattern (patternIndex);

    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::patternSetHasSteps() const
{
    for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
        if (patternHasSteps (patternIndex))
            return true;

    return false;
}

juce::Result SVDrummerAudioProcessor::savePatternSetToFile (
    const juce::File& file)
{
    captureCurrentPattern();
    const auto xml = createPatternSetXml();

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The Pattern Set folder could not be created.");

    if (! file.replaceWithText (xml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The Pattern Set file could not be written.");

    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::loadPatternSetFromFile (
    const juce::File& file)
{
    if (! isSupportedPatternSetFile (file) || ! file.existsAsFile())
        return juce::Result::fail ("Select an SV-Drummer .svpatternset file.");

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return juce::Result::fail ("The Pattern Set file could not be read.");

    const auto result = loadPatternSetXml (*xml);

    if (result.wasOk())
        markPortableSettingsDirty();

    return result;
}

juce::Result SVDrummerAudioProcessor::saveProjectToFile (
    const juce::File& file)
{
    captureCurrentPattern();
    auto projectXml = std::make_unique<juce::XmlElement> ("SVDRUMMER_PROJECT");
    projectXml->setAttribute ("version", "1.0");
    projectXml->setAttribute ("type", "project");
    const auto midiMode = getPatternMidiMode();
    projectXml->setAttribute ("midiMode", patternMidiModeToString (midiMode));
    projectXml->setAttribute (
        "sequencerEnabled",
        midiMode == PatternMidiMode::select && isSequencerEnabled());
    projectXml->setAttribute ("markerSnap", isSampleMarkerSnapEnabled());

    if (auto kitXml = createKitXml())
        projectXml->addChildElement (kitXml.release());

    if (auto patternSetXml = createPatternSetXml())
        projectXml->addChildElement (patternSetXml.release());

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The Projects folder could not be created.");

    if (! file.replaceWithText (projectXml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The Project file could not be written.");

    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::loadProjectFromFile (
    const juce::File& file)
{
    if (! isSupportedProjectFile (file) || ! file.existsAsFile())
        return juce::Result::fail ("Select an SV-Drummer .svproject file.");

    const auto projectXml = juce::XmlDocument::parse (file);

    if (projectXml == nullptr
        || ! projectXml->hasTagName ("SVDRUMMER_PROJECT")
        || projectXml->getStringAttribute ("type") != "project")
        return juce::Result::fail ("This is not an SV-Drummer Project file.");

    auto* kitXml = projectXml->getChildByName ("SVDRUMMER_KIT");
    auto* patternSetXml = projectXml->getChildByName ("SVDRUMMER_PATTERN_SET");

    if (kitXml == nullptr || patternSetXml == nullptr)
        return juce::Result::fail (
            "The Project does not contain both a Kit and a Pattern Set.");

    if (patternSetXml->getStringAttribute ("type") != "pattern-set")
        return juce::Result::fail ("The Project contains an invalid Pattern Set.");

    const auto patternResult = loadPatternSetXml (*patternSetXml);

    if (patternResult.failed())
        return patternResult;

    const auto kitResult = loadKitXml (*kitXml);

    if (kitResult.failed())
        return kitResult;

    // A Project and its samples are often moved together. Try the Project
    // folder first so those samples can be restored without prompting.
    relinkMissingSamplesFromFolder (file.getParentDirectory(), false);

    const auto midiMode = patternMidiModeFromString (
        projectXml->getStringAttribute ("midiMode", "Select"));
    setPatternMidiMode (midiMode);
    setSequencerEnabled (
        midiMode == PatternMidiMode::select
        && projectXml->getBoolAttribute ("sequencerEnabled", false));
    setSampleMarkerSnapEnabled (
        projectXml->getBoolAttribute ("markerSnap", false));
    markPortableSettingsDirty();
    return juce::Result::ok();
}

juce::StringArray SVDrummerAudioProcessor::getMissingSampleDescriptions() const
{
    juce::StringArray descriptions;
    const juce::ScopedLock lock (stateLock);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& pad = pads[static_cast<std::size_t> (padIndex)];

        if (pad.samplePath.isEmpty()
            || std::atomic_load_explicit (&pad.sample,
                                          std::memory_order_acquire) != nullptr)
            continue;

        const auto fileName = juce::File (pad.samplePath).getFileName();
        descriptions.add ("Pad " + juce::String (padIndex + 1) + ": "
                          + (fileName.isNotEmpty() ? fileName
                                                   : pad.samplePath));
    }

    return descriptions;
}

int SVDrummerAudioProcessor::relinkMissingSamplesFromFolder (
    const juce::File& folder, bool searchSubfolders)
{
    if (! folder.isDirectory())
        return 0;

    juce::Array<juce::File> candidates;
    folder.findChildFiles (candidates, juce::File::findFiles, searchSubfolders,
                           "*.wav;*.mp3;*.ogg;*.flac;*.aif;*.aiff");

    int relinkedCount = 0;

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        auto& pad = pads[static_cast<std::size_t> (padIndex)];

        if (std::atomic_load_explicit (&pad.sample,
                                      std::memory_order_acquire) != nullptr)
            continue;

        juce::String missingPath;

        {
            const juce::ScopedLock lock (stateLock);
            missingPath = pad.samplePath;
        }

        if (missingPath.isEmpty())
            continue;

        const auto missingName = juce::File (missingPath).getFileName();
        juce::File replacement;

        for (const auto& candidate : candidates)
        {
            if (candidate.getFileName().equalsIgnoreCase (missingName))
            {
                replacement = candidate;
                break;
            }
        }

        if (replacement == juce::File())
            continue;

        juce::String errorMessage;
        const auto newSample = createSampleData (replacement, errorMessage);

        if (newSample == nullptr)
            continue;

        const int lastSample = juce::jmax (
            0, newSample->audio.getNumSamples() - 1);
        const int savedStart = juce::jlimit (
            0, lastSample, pad.sampleStart.load());
        const int savedEnd = juce::jlimit (
            savedStart < lastSample ? savedStart + 1 : savedStart,
            lastSample, pad.sampleEnd.load());
        const int savedLoopStart = juce::jlimit (
            savedStart,
            savedEnd > savedStart ? savedEnd - 1 : savedStart,
            pad.loopStart.load());
        const int savedLoopEnd = juce::jlimit (
            savedLoopStart < savedEnd ? savedLoopStart + 1 : savedLoopStart,
            savedEnd, pad.loopEnd.load());

        pad.sampleRevision.fetch_add (1);
        pad.sampleStart.store (savedStart);
        pad.sampleEnd.store (savedEnd);
        pad.loopStart.store (savedLoopStart);
        pad.loopEnd.store (savedLoopEnd);
        std::atomic_store_explicit (&pad.sample, newSample,
                                    std::memory_order_release);

        {
            const juce::ScopedLock lock (stateLock);
            pad.samplePath = replacement.getFullPathName();
            pad.displayName = replacement.getFileNameWithoutExtension();
        }

        ++relinkedCount;
    }

    if (relinkedCount > 0)
    {
        patternChangeCounter.fetch_add (1);
        markPortableSettingsDirty();
    }

    return relinkedCount;
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

bool SVDrummerAudioProcessor::setHostParameterValue (
    juce::RangedAudioParameter* parameter, float denormalisedValue)
{
    if (parameter == nullptr)
        return false;

    const float normalisedValue = parameter->convertTo0to1 (denormalisedValue);

    if (! juce::approximatelyEqual (parameter->getValue(), normalisedValue))
        parameter->setValueNotifyingHost (normalisedValue);

    return true;
}

void SVDrummerAudioProcessor::markHostParameterStateChanged() noexcept
{
    if (! restoringHostState.load())
        portableSettingsDirty.store (true);
}

void SVDrummerAudioProcessor::markPortableSettingsDirty() noexcept
{
    portableSettingsDirty.store (true);

    if (restoringHostState.load())
        return;

    if (stateRevisionParameter != nullptr)
    {
        float revision = hostStateRevision.load() + (1.0f / 1024.0f);

        if (revision >= 1.0f)
            revision = 0.0f;

        stateRevisionParameter->setValueNotifyingHost (revision);
    }

    updateHostDisplay (
        juce::AudioProcessorListener::ChangeDetails()
            .withNonParameterStateChanged (true));
}

juce::Result SVDrummerAudioProcessor::savePortableSettingsNow()
{
    return savePortableSettings();
}

int SVDrummerAudioProcessor::getEditorZoomPercent() const noexcept
{
    return juce::jlimit (75, 200, editorZoomPercent.load());
}

void SVDrummerAudioProcessor::setEditorZoomPercent (int zoomPercent) noexcept
{
    editorZoomPercent.store (juce::jlimit (75, 200, zoomPercent));
}

juce::Result SVDrummerAudioProcessor::saveEditorZoomNow()
{
    return savePortableSettings();
}

juce::Result SVDrummerAudioProcessor::saveBrowserFoldersNow()
{
    return savePortableSettings();
}

bool SVDrummerAudioProcessor::hasUnsavedPortableChanges() const noexcept
{
    return portableSettingsDirty.load();
}

juce::Result SVDrummerAudioProcessor::savePortableSettings()
{
    const auto settingsFile = getPortableSettingsFile();

    if (settingsFile.getParentDirectory().createDirectory().failed())
        return juce::Result::fail (
            "The portable Settings folder could not be created.");

    juce::StringArray lines;
    lines.add ("; SV-Drummer portable settings");
    lines.add ("; Musical session data is stored by the host or in SV-Drummer library files.");
    lines.add ("; Paths under the portable Data folder are stored relative to the plug-in.");
    lines.add (juce::String());
    lines.add ("[GUI]");
    lines.add ("ZoomPercent=" + juce::String (getEditorZoomPercent()));
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

    if (! settingsFile.replaceWithText (
            lines.joinIntoString ("\r\n") + "\r\n", false, false))
        return juce::Result::fail (
            "Data\\Settings\\SV-Drummer.ini could not be written.");

    return juce::Result::ok();
}

void SVDrummerAudioProcessor::loadPortableSettings()
{
    const auto settingsFile = getPortableSettingsFile();

    juce::StringPairArray guiValues;
    juce::StringPairArray browserValues;
    juce::StringArray lines;

    if (settingsFile.existsAsFile())
        lines.addLines (settingsFile.loadFileAsString());

    juce::String section;

    for (auto line : lines)
    {
        line = line.trim();

        if (line.isEmpty() || line.startsWithChar (';'))
            continue;

        if (line.startsWithChar ('[') && line.endsWithChar (']'))
        {
            section = line.substring (1, line.length() - 1).trim();
            continue;
        }

        const int equals = line.indexOfChar ('=');

        if (equals > 0)
        {
            const auto key = line.substring (0, equals).trim();
            const auto value = line.substring (equals + 1).trim();

            if (section.equalsIgnoreCase ("GUI"))
                guiValues.set (key, value);
            else if (section.equalsIgnoreCase ("Browser"))
                browserValues.set (key, value);
        }
    }

    const auto storedZoom = guiValues.getValue ("ZoomPercent", {});

    editorZoomPercent.store (juce::jlimit (
        75, 200, storedZoom.isNotEmpty() ? storedZoom.getIntValue() : 100));

    const int folderCount = juce::jlimit (
        0, 1024, browserValues.getValue ("FolderCount", "0").getIntValue());

    {
        const juce::ScopedLock lock (stateLock);
        browserFolders.clear();

        for (int index = 0; index < folderCount; ++index)
        {
            const auto stored = browserValues.getValue (
                "Folder" + juce::String (index + 1), {});

            if (stored.isNotEmpty())
                browserFolders.addIfNotAlreadyThere (resolveStoredPath (stored).getFullPathName());
        }
    }

    portableSettingsDirty.store (false);
}

void SVDrummerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    captureCurrentPattern();
    juce::XmlElement state ("SVDRUMMER_STATE");
    state.setAttribute ("version", "5.0.0");
    state.setAttribute ("markerSnap", isSampleMarkerSnapEnabled());

    auto* sequencer = state.createNewChildElement ("SEQUENCER");
    const auto midiMode = getPatternMidiMode();
    sequencer->setAttribute ("midiMode", patternMidiModeToString (midiMode));
    sequencer->setAttribute ("enabled",
                             midiMode == PatternMidiMode::select
                                 && isSequencerEnabled());
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
        item->setAttribute ("chokeGroup", getPadChokeGroup (padIndex));
        item->setAttribute ("ampAttackMs", static_cast<double> (getPadAmpAttackMs (padIndex)));
        item->setAttribute ("ampDecayMs", static_cast<double> (getPadAmpDecayMs (padIndex)));
        item->setAttribute ("ampSustain", static_cast<double> (getPadAmpSustain (padIndex)));
        item->setAttribute ("ampReleaseMs", static_cast<double> (getPadAmpReleaseMs (padIndex)));
        item->setAttribute ("startSample", getPadSampleStart (padIndex));
        item->setAttribute ("endSample", getPadSampleEnd (padIndex));
        item->setAttribute ("loopEnabled", isPadLoopEnabled (padIndex));
        item->setAttribute ("loopStartSample", getPadLoopStart (padIndex));
        item->setAttribute ("loopEndSample", getPadLoopEnd (padIndex));

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

    restoringHostState.store (true);

    sampleMarkerSnapEnabled.store (
        state->getBoolAttribute ("markerSnap", false));

    if (auto* sequencer = state->getChildByName ("SEQUENCER"))
    {
        patternMidiMode.store (static_cast<int> (patternMidiModeFromString (
            sequencer->getStringAttribute ("midiMode", "Select"))));
        sequencerEnabled.store (
            getPatternMidiMode() == PatternMidiMode::select
            && sequencer->getBoolAttribute ("enabled", false));
        patternGateActive.store (false);
        patternGateWaitingForSelection.store (false);
        activePatternGateNote.store (-1);
        pendingPatternGateStartNote.store (-1);
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
                item->getIntAttribute ("division", 4)));
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
        pad.chokeGroup.store (juce::jlimit (
            0, numberOfPads, item->getIntAttribute ("chokeGroup", 0)));
        pad.ampAttackMs.store (juce::jlimit (
            0.0f, 2000.0f,
            static_cast<float> (item->getDoubleAttribute ("ampAttackMs", 0.0))));
        pad.ampDecayMs.store (juce::jlimit (
            0.0f, 5000.0f,
            static_cast<float> (item->getDoubleAttribute ("ampDecayMs", 0.0))));
        pad.ampSustain.store (juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute ("ampSustain", 1.0))));
        pad.ampReleaseMs.store (juce::jlimit (
            0.0f, 5000.0f,
            static_cast<float> (item->getDoubleAttribute ("ampReleaseMs", 0.0))));
        const bool savedLoopEnabled = item->getBoolAttribute ("loopEnabled", false);
        const bool hasSavedStart = item->hasAttribute ("startSample");
        const bool hasSavedEnd = item->hasAttribute ("endSample");
        const bool hasSavedLoopStart = item->hasAttribute ("loopStartSample");
        const bool hasSavedLoopEnd = item->hasAttribute ("loopEndSample");

        const auto storedSample = item->getStringAttribute ("sample");

        if (storedSample.isEmpty())
        {
            clearPadSample (padIndex);
        }
        else
        {
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

        const auto loadedSample = std::atomic_load_explicit (
            &pad.sample, std::memory_order_acquire);
        const int explicitEnd = hasSavedEnd
                                  ? juce::jmax (
                                        0, item->getIntAttribute ("endSample", 0))
                                  : 0;
        const int explicitLoopEnd = hasSavedLoopEnd
                                      ? juce::jmax (
                                            0, item->getIntAttribute (
                                                   "loopEndSample", 0))
                                      : 0;
        const int lastSample = loadedSample != nullptr
                                 ? juce::jmax (
                                       0, loadedSample->audio.getNumSamples() - 1)
                                 : juce::jmax (explicitEnd, explicitLoopEnd);
        const int savedStart = hasSavedStart
                                 ? juce::jmax (
                                       0, item->getIntAttribute ("startSample", 0))
                                 : 0;
        const int savedEnd = hasSavedEnd
                               ? juce::jmax (savedStart, explicitEnd)
                               : lastSample;
        const int savedLoopStart = hasSavedLoopStart
                                     ? juce::jmax (
                                           savedStart,
                                           item->getIntAttribute (
                                               "loopStartSample", savedStart))
                                     : savedStart;
        const int savedLoopEnd = hasSavedLoopEnd
                                   ? juce::jmax (savedLoopStart, explicitLoopEnd)
                                   : savedEnd;
        const bool hasCollapsedDefaultRange = loadedSample != nullptr
                                           && lastSample > 1
                                           && savedStart == 0
                                           && savedEnd <= 1
                                           && savedLoopStart == 0
                                           && savedLoopEnd <= 1;
        const int effectiveEnd = hasCollapsedDefaultRange ? lastSample
                                                          : savedEnd;
        const int effectiveLoopEnd = hasCollapsedDefaultRange ? lastSample
                                                              : savedLoopEnd;
        const int restoredStart = juce::jlimit (0, lastSample, savedStart);
        const int restoredEnd = juce::jlimit (
            restoredStart < lastSample ? restoredStart + 1 : restoredStart,
            lastSample, effectiveEnd);
        const int restoredLoopStart = juce::jlimit (
            restoredStart,
            restoredEnd > restoredStart ? restoredEnd - 1 : restoredStart,
            savedLoopStart);
        pad.sampleStart.store (restoredStart);
        pad.sampleEnd.store (restoredEnd);
        pad.loopEnabled.store (savedLoopEnabled);
        pad.loopStart.store (restoredLoopStart);
        pad.loopEnd.store (juce::jlimit (
            restoredLoopStart < restoredEnd ? restoredLoopStart + 1
                                            : restoredLoopStart,
            restoredEnd, effectiveLoopEnd));
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
            {
                if (auto* patternXml = slotXml->getChildByName (
                        "SVDRUMMER_PATTERN"))
                    loadPatternXmlIntoSlot (patternIndex, *patternXml);
            }
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

    portableSettingsDirty.store (false);
    restoringHostState.store (false);
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

bool SVDrummerAudioProcessor::isSupportedKitFile (const juce::File& file)
{
    return file.hasFileExtension ("svkit");
}

bool SVDrummerAudioProcessor::isSupportedPatternFile (const juce::File& file)
{
    return file.hasFileExtension ("svpattern");
}

bool SVDrummerAudioProcessor::isSupportedPatternSetFile (const juce::File& file)
{
    return file.hasFileExtension ("svpatternset");
}

bool SVDrummerAudioProcessor::isSupportedProjectFile (const juce::File& file)
{
    return file.hasFileExtension ("svproject");
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SVDrummerAudioProcessor();
}
