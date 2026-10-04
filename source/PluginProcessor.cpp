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

float shapeAttackPhase (float phase, float curve) noexcept
{
    const float linear = juce::jlimit (0.0f, 1.0f, phase);
    const float amount = juce::jlimit (-1.0f, 1.0f, curve);

    if (amount < 0.0f)
    {
        const float exponent = std::pow (4.0f, -amount);
        return 1.0f - std::pow (1.0f - linear, exponent);
    }

    return std::pow (linear, std::pow (4.0f, amount));
}

struct BiquadCoefficients
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

BiquadCoefficients makeFilterCoefficients (
    SVDrummerAudioProcessor::PadFilterType type,
    float cutoffHz,
    float resonance,
    double sampleRate)
{
    BiquadCoefficients coefficients;

    if (type == SVDrummerAudioProcessor::PadFilterType::off
        || type == SVDrummerAudioProcessor::PadFilterType::comb
        || type == SVDrummerAudioProcessor::PadFilterType::formant
        || type == SVDrummerAudioProcessor::PadFilterType::ladder)
        return coefficients;

    const double safeSampleRate = juce::jmax (1.0, sampleRate);
    const double cutoff = juce::jlimit (
        20.0, safeSampleRate * 0.45, static_cast<double> (cutoffHz));
    const double q = 0.5 + 11.5 * juce::jlimit (
        0.0, 1.0, static_cast<double> (resonance));
    const double omega = juce::MathConstants<double>::twoPi
                       * cutoff / safeSampleRate;
    const double sine = std::sin (omega);
    const double cosine = std::cos (omega);
    const double alpha = sine / (2.0 * q);
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosine;
    const double a2 = 1.0 - alpha;

    switch (type)
    {
        case SVDrummerAudioProcessor::PadFilterType::lowPass:
            b0 = (1.0 - cosine) * 0.5;
            b1 = 1.0 - cosine;
            b2 = b0;
            break;
        case SVDrummerAudioProcessor::PadFilterType::bandPass:
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            break;
        case SVDrummerAudioProcessor::PadFilterType::highPass:
            b0 = (1.0 + cosine) * 0.5;
            b1 = -(1.0 + cosine);
            b2 = b0;
            break;
        case SVDrummerAudioProcessor::PadFilterType::notch:
            b0 = 1.0;
            b1 = -2.0 * cosine;
            b2 = 1.0;
            break;
        case SVDrummerAudioProcessor::PadFilterType::off:
        case SVDrummerAudioProcessor::PadFilterType::comb:
        case SVDrummerAudioProcessor::PadFilterType::formant:
        case SVDrummerAudioProcessor::PadFilterType::ladder:
        default:
            break;
    }

    coefficients.b0 = static_cast<float> (b0 / a0);
    coefficients.b1 = static_cast<float> (b1 / a0);
    coefficients.b2 = static_cast<float> (b2 / a0);
    coefficients.a1 = static_cast<float> (a1 / a0);
    coefficients.a2 = static_cast<float> (a2 / a0);
    return coefficients;
}

float processBiquadSample (float input,
                            const BiquadCoefficients& coefficients,
                            float& z1,
                            float& z2) noexcept
{
    const float output = coefficients.b0 * input + z1;
    z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
    z2 = coefficients.b2 * input - coefficients.a2 * output;
    return output;
}

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
        default: return "Manual";
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

juce::String patternSyncModeToString (
    SVDrummerAudioProcessor::PatternSyncMode mode)
{
    switch (mode)
    {
        case SVDrummerAudioProcessor::PatternSyncMode::bar:  return "Bar";
        case SVDrummerAudioProcessor::PatternSyncMode::beat: return "Beat";
        case SVDrummerAudioProcessor::PatternSyncMode::played:
        default:                                              return "Played";
    }
}

SVDrummerAudioProcessor::PatternSyncMode patternSyncModeFromString (
    const juce::String& text)
{
    if (text.equalsIgnoreCase ("Bar"))
        return SVDrummerAudioProcessor::PatternSyncMode::bar;

    if (text.equalsIgnoreCase ("Beat"))
        return SVDrummerAudioProcessor::PatternSyncMode::beat;

    return SVDrummerAudioProcessor::PatternSyncMode::played;
}

#if JUCE_WINDOWS
int moduleLocationAnchor = 0;
#endif
}

SVDrummerAudioProcessor::SVDrummerAudioProcessor()
    : AudioProcessor ([]
      {
          BusesProperties buses;
          buses.addBus (false, "MAIN", juce::AudioChannelSet::stereo(), true);

          for (int output = 1; output <= numberOfPadOutputBuses; ++output)
              buses.addBus (false, "AUX " + juce::String (output),
                            juce::AudioChannelSet::stereo(), false);

          return buses;
      }())
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
        patternPlaybackPatternBars[static_cast<std::size_t> (index)].store (0);
    }

    for (auto& step : patternPlaybackSteps)
        step.store (-1);

    undoPatternPlaybackSteps.fill (-1);

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
                queuedSyncedPatternSelection.store (-1);
                queuedSyncedPatternGateNote.store (-1);
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
                    default: return juce::String ("MANUAL");
                }
            }));

    patternSyncModeParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "pattern_sync_mode", 1 },
            "Pattern Sync Mode",
            juce::NormalisableRange<float> { 0.0f, 2.0f, 1.0f },
            0.0f,
            juce::String(),
            [this] { return static_cast<float> (patternSyncMode.load()); },
            [this] (float value)
            {
                const int next = juce::jlimit (0, 2, juce::roundToInt (value));

                if (patternSyncMode.exchange (next) == next)
                    return;

                if (queuedSyncedPatternSelection.load() >= 0)
                {
                    if (next == static_cast<int> (PatternSyncMode::played))
                    {
                        dispatchQueuedPatternSelection();
                    }
                    else
                    {
                        queuedSyncedPatternBoundaryPpq.store (
                            getNextPatternSyncBoundary (
                                currentHostPpqPosition.load()));
                    }
                }

                markHostParameterStateChanged();
            },
            3,
            false,
            [] (float value, int)
            {
                switch (juce::jlimit (0, 2, juce::roundToInt (value)))
                {
                    case 1:  return juce::String ("BAR");
                    case 2:  return juce::String ("BEAT");
                    case 0:
                    default: return juce::String ("PLAYED");
                }
            }));

    patternPlaybackChainEnabledParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "pattern_chain_enabled", 1 },
            "Pattern Chain Enabled",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
            1.0f,
            juce::String(),
            [this]
            {
                return patternPlaybackChainEnabled.load() ? 1.0f : 0.0f;
            },
            [this] (float value)
            {
                const bool next = value >= 0.5f;

                if (patternPlaybackChainEnabled.exchange (next) != next)
                {
                    activePatternPlaybackStep.store (-1);
                    patternTimelineResetCounter.fetch_add (1);
                    markHostParameterStateChanged();
                }
            },
            2,
            true,
            booleanText));

    patternPlaybackLoopParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "pattern_chain_loop", 1 },
            "Pattern Chain Loop",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
            0.0f,
            juce::String(),
            [this]
            {
                return patternPlaybackLoopEnabled.load() ? 1.0f : 0.0f;
            },
            [this] (float value)
            {
                const bool next = value >= 0.5f;

                if (patternPlaybackLoopEnabled.exchange (next) != next)
                    markHostParameterStateChanged();
            },
            2,
            true,
            booleanText));

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

                refreshPatternPlaybackBars();
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

    // Keep the original v0.7.12 parameters first and append the second
    // automation stage so their established host order remains unchanged.
    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";

        padHostParameters[index].midiNote = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_midi_note", 1 },
                namePrefix + "MIDI Note",
                juce::NormalisableRange<float> { 0.0f, 127.0f, 1.0f },
                static_cast<float> (36 + padIndex),
                juce::String(),
                [this, index] {
                    return static_cast<float> (pads[index].midiNote.load());
                },
                [this, index] (float value)
                {
                    const int next = juce::jlimit (
                        0, 127, juce::roundToInt (value));

                    if (pads[index].midiNote.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                128,
                false,
                [] (float value, int)
                {
                    return juce::MidiMessage::getMidiNoteName (
                        juce::jlimit (0, 127, juce::roundToInt (value)),
                        true, true, 4);
                }));

        const auto addBooleanPadParameter = [&] (
            juce::RangedAudioParameter*& destination,
            const juce::String& idSuffix,
            const juce::String& nameSuffix,
            std::atomic<bool>& state)
        {
            auto* const statePointer = &state;
            destination = addStateParameter (
                std::make_unique<StateBackedParameter> (
                    juce::ParameterID { idPrefix + idSuffix, 1 },
                    namePrefix + nameSuffix,
                    juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
                    0.0f,
                    juce::String(),
                    [statePointer] {
                        return statePointer->load() ? 1.0f : 0.0f;
                    },
                    [this, statePointer] (float value)
                    {
                        const bool next = value >= 0.5f;

                        if (statePointer->exchange (next) != next)
                            markHostParameterStateChanged();
                    },
                    2,
                    true,
                    booleanText));
        };

        addBooleanPadParameter (padHostParameters[index].mute,
                                "_mute", "Mute", pads[index].muted);
        addBooleanPadParameter (padHostParameters[index].solo,
                                "_solo", "Solo", pads[index].soloed);
        addBooleanPadParameter (padHostParameters[index].reverse,
                                "_reverse", "Reverse", pads[index].reversed);

        padHostParameters[index].chokeGroup = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_choke_group", 1 },
                namePrefix + "Choke Group",
                juce::NormalisableRange<float> {
                    0.0f, static_cast<float> (numberOfPads), 1.0f },
                0.0f,
                juce::String(),
                [this, index] {
                    return static_cast<float> (pads[index].chokeGroup.load());
                },
                [this, index] (float value)
                {
                    const int next = juce::jlimit (
                        0, numberOfPads, juce::roundToInt (value));

                    if (pads[index].chokeGroup.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                numberOfPads + 1,
                false,
                [] (float value, int)
                {
                    const int group = juce::roundToInt (value);
                    return group <= 0 ? juce::String ("Off")
                                      : juce::String (group);
                }));

        const auto envelopeTimeText = [] (float value, int)
        {
            if (value >= 1000.0f)
                return juce::String (value / 1000.0f, 2) + " s";

            return juce::String (juce::roundToInt (value)) + " ms";
        };

        padHostParameters[index].ampCurve = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_amp_curve", 1 },
                namePrefix + "AMP Curve",
                juce::NormalisableRange<float> { -1.0f, 1.0f, 0.01f },
                0.0f,
                juce::String(),
                [this, index] { return pads[index].ampCurve.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (-1.0f, 1.0f, value);

                    if (std::abs (pads[index].ampCurve.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                201,
                false,
                [] (float value, int)
                {
                    if (std::abs (value) < 0.005f)
                        return juce::String ("0.00");

                    return (value > 0.0f ? juce::String ("+")
                                          : juce::String())
                         + juce::String (value, 2);
                }));

        padHostParameters[index].ampAttack = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_amp_attack_ms", 1 },
                namePrefix + "Attack",
                juce::NormalisableRange<float> { 0.0f, 2000.0f, 1.0f },
                0.0f,
                "ms",
                [this, index] { return pads[index].ampAttackMs.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 2000.0f, value);

                    if (std::abs (pads[index].ampAttackMs.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                envelopeTimeText));

        padHostParameters[index].ampDecay = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_amp_decay_ms", 1 },
                namePrefix + "Decay",
                juce::NormalisableRange<float> { 0.0f, 5000.0f, 1.0f },
                0.0f,
                "ms",
                [this, index] { return pads[index].ampDecayMs.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 5000.0f, value);

                    if (std::abs (pads[index].ampDecayMs.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                envelopeTimeText));

        padHostParameters[index].ampSustain = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_amp_sustain", 1 },
                namePrefix + "Sustain",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
                1.0f,
                "%",
                [this, index] { return pads[index].ampSustain.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 1.0f, value);

                    if (std::abs (pads[index].ampSustain.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                101,
                false,
                [] (float value, int)
                {
                    return juce::String (juce::roundToInt (value * 100.0f))
                         + "%";
                }));

        padHostParameters[index].ampRelease = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_amp_release_ms", 1 },
                namePrefix + "Release",
                juce::NormalisableRange<float> { 0.0f, 5000.0f, 1.0f },
                0.0f,
                "ms",
                [this, index] { return pads[index].ampReleaseMs.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 5000.0f, value);

                    if (std::abs (
                            pads[index].ampReleaseMs.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                envelopeTimeText));

        addBooleanPadParameter (padHostParameters[index].loopEnabled,
                                "_loop", "Loop", pads[index].loopEnabled);
    }

    for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
    {
        const auto index = static_cast<std::size_t> (laneIndex);
        const auto idPrefix = "lane_"
                            + juce::String (laneIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Lane " + juce::String (laneIndex + 1) + " ";

        laneHostParameters[index].division = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_division", 1 },
                namePrefix + "Division",
                juce::NormalisableRange<float> {
                    0.0f, static_cast<float> (sequencerDivisionCount - 1), 1.0f },
                4.0f,
                juce::String(),
                [this, index] {
                    return static_cast<float> (
                        sequenceLanes[index].division.load());
                },
                [this, index] (float value)
                {
                    const int next = juce::jlimit (
                        0, sequencerDivisionCount - 1,
                        juce::roundToInt (value));
                    auto& lane = sequenceLanes[index];

                    if (lane.division.exchange (next) == next)
                        return;

                    lane.loopLength.store (juce::jlimit (
                        1,
                        getLaneMaximumLoopLength (static_cast<int> (index)),
                        lane.loopLength.load()));
                    markHostParameterStateChanged();
                },
                sequencerDivisionCount,
                false,
                [] (float value, int)
                {
                    return SVDrummerAudioProcessor::getSequencerDivisionName (
                        juce::roundToInt (value));
                }));

        laneHostParameters[index].loopLength = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_loop_length_steps", 1 },
                namePrefix + "Loop Length",
                juce::NormalisableRange<float> {
                    1.0f, static_cast<float> (maximumStepsPerLane), 1.0f },
                16.0f,
                "Steps",
                [this, index] {
                    return static_cast<float> (
                        sequenceLanes[index].loopLength.load());
                },
                [this, index] (float value)
                {
                    const int laneIndexValue = static_cast<int> (index);
                    const int next = juce::jlimit (
                        1,
                        getLaneMaximumLoopLength (laneIndexValue),
                        juce::roundToInt (value));

                    if (sequenceLanes[index].loopLength.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                maximumStepsPerLane,
                false,
                [] (float value, int)
                {
                    const int steps = juce::roundToInt (value);
                    return juce::String (steps)
                         + (steps == 1 ? " Step" : " Steps");
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

    const auto frequencyText = [] (float value, int)
    {
        if (value < 1.0f)
            return juce::String ("Off");

        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, value >= 10000.0f ? 1 : 2)
                 + " kHz";

        return juce::String (juce::roundToInt (value)) + " Hz";
    };

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";

        auto cutoffRange = juce::NormalisableRange<float> {
            20.0f, 20000.0f, 1.0f };
        cutoffRange.setSkewForCentre (1000.0f);
        padHostParameters[index].filterCutoff = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_filter_cutoff_hz", 1 },
                namePrefix + "Filter Cutoff",
                cutoffRange,
                20000.0f,
                "Hz",
                [this, index] { return pads[index].filterCutoffHz.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (20.0f, 20000.0f, value);

                    if (std::abs (
                            pads[index].filterCutoffHz.exchange (next) - next)
                        > 0.01f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                frequencyText));

        padHostParameters[index].filterResonance = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_filter_resonance", 1 },
                namePrefix + "Filter Resonance",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
                0.0f,
                "%",
                [this, index] { return pads[index].filterResonance.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 1.0f, value);

                    if (std::abs (
                            pads[index].filterResonance.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                101,
                false,
                [] (float value, int)
                {
                    return juce::String (juce::roundToInt (value * 100.0f))
                         + "%";
                }));

        padHostParameters[index].filterDrive = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_filter_drive_db", 1 },
                namePrefix + "Filter Drive",
                juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f },
                0.0f,
                "dB",
                [this, index] { return pads[index].filterDriveDb.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 24.0f, value);

                    if (std::abs (pads[index].filterDriveDb.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return juce::String (value, 1) + " dB";
                }));

        padHostParameters[index].filterType = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_filter_type", 1 },
                namePrefix + "Filter Type",
                juce::NormalisableRange<float> { 1.0f, 7.0f, 1.0f },
                1.0f,
                juce::String(),
                [this, index] {
                    return static_cast<float> (pads[index].filterType.load());
                },
                [this, index] (float value)
                {
                    const int next = juce::jlimit (
                        1, 7, juce::roundToInt (value));

                    if (pads[index].filterType.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                7,
                false,
                [] (float value, int)
                {
                    return SVDrummerAudioProcessor::getPadFilterTypeName (
                        static_cast<SVDrummerAudioProcessor::PadFilterType> (
                            juce::jlimit (1, 7, juce::roundToInt (value))));
                }));

        padHostParameters[index].filterSlope = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_filter_slope", 1 },
                namePrefix + "Filter Slope",
                juce::NormalisableRange<float> { 0.0f, 3.0f, 1.0f },
                1.0f,
                juce::String(),
                [this, index] {
                    return static_cast<float> (pads[index].filterSlope.load());
                },
                [this, index] (float value)
                {
                    const int next = juce::jlimit (
                        0, 3, juce::roundToInt (value));

                    if (pads[index].filterSlope.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                4,
                false,
                [] (float value, int)
                {
                    return SVDrummerAudioProcessor::getPadFilterSlopeName (
                        juce::roundToInt (value));
                }));

        auto highPassRange = juce::NormalisableRange<float> {
            0.0f, 2000.0f, 1.0f };
        highPassRange.setSkewForCentre (180.0f);
        padHostParameters[index].highPassCutoff = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_high_pass_cutoff_hz", 1 },
                namePrefix + "High-Pass Cutoff",
                highPassRange,
                0.0f,
                "Hz",
                [this, index] { return pads[index].highPassCutoffHz.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 2000.0f, value);

                    if (std::abs (
                            pads[index].highPassCutoffHz.exchange (next) - next)
                        > 0.01f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                frequencyText));
    }

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";

        padHostParameters[index].compressorEnabled = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_compressor_enabled", 1 },
                namePrefix + "Compressor",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
                0.0f,
                juce::String(),
                [this, index] {
                    return pads[index].compressorEnabled.load() ? 1.0f : 0.0f;
                },
                [this, index] (float value)
                {
                    const bool next = value >= 0.5f;

                    if (pads[index].compressorEnabled.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                2,
                true,
                booleanText));

        padHostParameters[index].compressorThreshold = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_compressor_threshold_db", 1 },
                namePrefix + "Compressor Threshold",
                juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f },
                -18.0f,
                "dB",
                [this, index] {
                    return pads[index].compressorThresholdDb.load();
                },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (-60.0f, 0.0f, value);

                    if (std::abs (
                            pads[index].compressorThresholdDb.exchange (next)
                            - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return juce::String (value, 1) + " dB";
                }));

        auto ratioRange = juce::NormalisableRange<float> {
            1.0f, 20.0f, 0.1f };
        ratioRange.setSkewForCentre (4.0f);
        padHostParameters[index].compressorRatio = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_compressor_ratio", 1 },
                namePrefix + "Compressor Ratio",
                ratioRange,
                4.0f,
                ":1",
                [this, index] { return pads[index].compressorRatio.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (1.0f, 20.0f, value);

                    if (std::abs (pads[index].compressorRatio.exchange (next)
                                  - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return juce::String (value, 1) + ":1";
                }));

        auto attackRange = juce::NormalisableRange<float> {
            0.1f, 100.0f, 0.1f };
        attackRange.setSkewForCentre (10.0f);
        padHostParameters[index].compressorAttack = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_compressor_attack_ms", 1 },
                namePrefix + "Compressor Attack",
                attackRange,
                10.0f,
                "ms",
                [this, index] {
                    return pads[index].compressorAttackMs.load();
                },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.1f, 100.0f, value);

                    if (std::abs (pads[index].compressorAttackMs.exchange (next)
                                  - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
                }));

        auto releaseRange = juce::NormalisableRange<float> {
            10.0f, 1000.0f, 1.0f };
        releaseRange.setSkewForCentre (100.0f);
        padHostParameters[index].compressorRelease = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_compressor_release_ms", 1 },
                namePrefix + "Compressor Release",
                releaseRange,
                100.0f,
                "ms",
                [this, index] {
                    return pads[index].compressorReleaseMs.load();
                },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (10.0f, 1000.0f, value);

                    if (std::abs (pads[index].compressorReleaseMs.exchange (next)
                                  - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return juce::String (juce::roundToInt (value)) + " ms";
                }));

        padHostParameters[index].compressorKnee = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_compressor_knee_db", 1 },
                namePrefix + "Compressor Knee",
                juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f },
                6.0f,
                "dB",
                [this, index] { return pads[index].compressorKneeDb.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 24.0f, value);

                    if (std::abs (pads[index].compressorKneeDb.exchange (next)
                                  - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return juce::String (value, 1) + " dB";
                }));

        padHostParameters[index].compressorGain = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_compressor_gain_db", 1 },
                namePrefix + "Compressor Gain",
                juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f },
                0.0f,
                "dB",
                [this, index] { return pads[index].compressorGainDb.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (-24.0f, 24.0f, value);

                    if (std::abs (pads[index].compressorGainDb.exchange (next)
                                  - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                0,
                false,
                [] (float value, int)
                {
                    return (value > 0.0f ? juce::String ("+")
                                          : juce::String())
                         + juce::String (value, 1) + " dB";
                }));
    }

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";

        padHostParameters[index].saturationAmount = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_saturation_amount", 1 },
                namePrefix + "Saturation",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
                0.0f,
                "%",
                [this, index] { return pads[index].saturationAmount.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 1.0f, value);

                    if (std::abs (pads[index].saturationAmount.exchange (next)
                                  - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                101,
                false,
                [] (float value, int)
                {
                    return juce::String (juce::roundToInt (value * 100.0f))
                         + "%";
                }));

        padHostParameters[index].saturationHardClip = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_saturation_hard_clip", 1 },
                namePrefix + "Saturation Hard Clip",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
                0.0f,
                "%",
                [this, index] {
                    return pads[index].saturationHardClipAmount.load();
                },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 1.0f, value);

                    if (std::abs (
                            pads[index].saturationHardClipAmount.exchange (next)
                            - next) > 0.0001f)
                        markHostParameterStateChanged();
                },
                101,
                false,
                [] (float value, int)
                {
                    return juce::String (juce::roundToInt (value * 100.0f))
                         + "%";
                }));
    }

    // Append the new section-bypass parameters after every existing pad and
    // lane parameter so established host parameter indices remain stable.
    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";

        const auto addSectionEnable = [&] (
            juce::RangedAudioParameter*& destination,
            const juce::String& idSuffix,
            const juce::String& nameSuffix,
            std::atomic<bool>& state)
        {
            auto* const statePointer = &state;
            destination = addStateParameter (
                std::make_unique<StateBackedParameter> (
                    juce::ParameterID { idPrefix + idSuffix, 1 },
                    namePrefix + nameSuffix,
                    juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
                    0.0f,
                    juce::String(),
                    [statePointer] {
                        return statePointer->load() ? 1.0f : 0.0f;
                    },
                    [this, statePointer] (float value)
                    {
                        const bool next = value >= 0.5f;

                        if (statePointer->exchange (next) != next)
                            markHostParameterStateChanged();
                    },
                    2,
                    true,
                    booleanText));
        };

        addSectionEnable (padHostParameters[index].filterEnabled,
                          "_filter_enabled", "Filter",
                          pads[index].filterEnabled);
        addSectionEnable (padHostParameters[index].saturationEnabled,
                          "_saturation_enabled", "Saturation",
                          pads[index].saturationEnabled);
    }

    const auto addGlobalBoolean = [&] (
        juce::RangedAudioParameter*& destination,
        const juce::String& id,
        const juce::String& name,
        std::atomic<bool>& state)
    {
        auto* const statePointer = &state;
        destination = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { id, 1 }, name,
                juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
                0.0f, juce::String(),
                [statePointer] {
                    return statePointer->load() ? 1.0f : 0.0f;
                },
                [this, statePointer] (float value)
                {
                    const bool next = value >= 0.5f;

                    if (statePointer->exchange (next) != next)
                        markHostParameterStateChanged();
                },
                2, true, booleanText));
    };

    const auto addGlobalFloat = [&] (
        juce::RangedAudioParameter*& destination,
        const juce::String& id,
        const juce::String& name,
        juce::NormalisableRange<float> range,
        float defaultValue,
        const juce::String& suffix,
        std::atomic<float>& state,
        StateBackedParameter::Formatter formatter)
    {
        auto* const statePointer = &state;
        destination = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { id, 1 }, name, range, defaultValue,
                suffix,
                [statePointer] { return statePointer->load(); },
                [this, statePointer, range] (float value)
                {
                    const float next = range.snapToLegalValue (
                        juce::jlimit (range.start, range.end, value));

                    if (std::abs (statePointer->exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0, false, std::move (formatter)));
    };

    addGlobalBoolean (globalDelayEnabledParameter,
                      "global_delay_enabled", "Global Delay",
                      globalDelayEnabled);
    addGlobalFloat (
        globalDelayTimeParameter, "global_delay_time_ms", "Global Delay Time",
        juce::NormalisableRange<float> { 1.0f, 2000.0f, 1.0f },
        250.0f, "ms", globalDelayTimeMs,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value)) + " ms";
        });
    addGlobalFloat (
        globalDelayFeedbackParameter, "global_delay_feedback",
        "Global Delay Feedback",
        juce::NormalisableRange<float> { 0.0f, 0.95f, 0.01f },
        0.35f, "%", globalDelayFeedback,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });
    addGlobalFloat (
        globalDelayMixParameter, "global_delay_mix", "Global Delay Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.25f, "%", globalDelayMix,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });
    addGlobalBoolean (globalReverbEnabledParameter,
                      "global_reverb_enabled", "Global Reverb",
                      globalReverbEnabled);
    addGlobalFloat (
        globalReverbSizeParameter, "global_reverb_size", "Global Reverb Size",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.50f, "%", globalReverbSize,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });
    addGlobalFloat (
        globalReverbDampingParameter, "global_reverb_damping",
        "Global Reverb Damping",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.50f, "%", globalReverbDamping,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });
    addGlobalFloat (
        globalReverbWidthParameter, "global_reverb_width", "Global Reverb Width",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        1.0f, "%", globalReverbWidth,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });
    addGlobalFloat (
        globalReverbMixParameter, "global_reverb_mix", "Global Reverb Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.20f, "%", globalReverbMix,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });

    // Appended after the v0.8.2 global FX parameters so their host indices
    // stay stable.
    addGlobalBoolean (globalDelaySyncEnabledParameter,
                      "global_delay_sync_enabled", "Global Delay Sync",
                      globalDelaySyncEnabled);
    globalDelaySyncDivisionParameter = addStateParameter (
        std::make_unique<StateBackedParameter> (
            juce::ParameterID { "global_delay_sync_division", 1 },
            "Global Delay Sync Division",
            juce::NormalisableRange<float> { 0.0f, 11.0f, 1.0f },
            5.0f, juce::String(),
            [this] {
                return static_cast<float> (globalDelaySyncDivision.load());
            },
            [this] (float value)
            {
                const int next = juce::jlimit (
                    0, 11, juce::roundToInt (value));

                if (globalDelaySyncDivision.exchange (next) != next)
                    markHostParameterStateChanged();
            },
            12, false,
            [] (float value, int)
            {
                return SVDrummerAudioProcessor::getDelaySyncDivisionName (
                    juce::roundToInt (value));
            }));

    // New parameters are appended so the indices of the existing PHI/DAW
    // mappings remain unchanged.
    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";
        padHostParameters[index].loopMode = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_loop_mode", 1 },
                namePrefix + "Loop Mode",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
                0.0f,
                juce::String(),
                [this, index] {
                    return static_cast<float> (pads[index].loopMode.load());
                },
                [this, index] (float value)
                {
                    const int next = juce::jlimit (
                        0, 1, juce::roundToInt (value));

                    if (pads[index].loopMode.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                2,
                false,
                [] (float value, int)
                {
                    return SVDrummerAudioProcessor::getPadLoopModeName (
                        static_cast<SVDrummerAudioProcessor::PadLoopMode> (
                            juce::jlimit (0, 1, juce::roundToInt (value))));
                }));
    }

    // Output routing was added in v0.9.0. Keep it at the end so every existing
    // PHI/DAW parameter index remains unchanged.
    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";
        padHostParameters[index].outputBus = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_output_bus", 1 },
                namePrefix + "Output",
                juce::NormalisableRange<float> {
                    0.0f, static_cast<float> (numberOfPadOutputBuses), 1.0f },
                0.0f,
                juce::String(),
                [this, index] {
                    return static_cast<float> (pads[index].outputBus.load());
                },
                [this, index] (float value)
                {
                    const int next = juce::jlimit (
                        0, numberOfPadOutputBuses,
                        juce::roundToInt (value));

                    if (pads[index].outputBus.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                numberOfOutputBuses,
                false,
                [] (float value, int)
                {
                    return SVDrummerAudioProcessor::getPadOutputBusName (
                        juce::roundToInt (value));
                }));
    }

    // Per-pad sequencer playback mode was added in v0.9.7. Append it so the
    // indices of all existing PHI/DAW parameters remain unchanged.
    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";
        padHostParameters[index].sequencerGated = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_sequencer_gated", 1 },
                namePrefix + "Sequencer Playback",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 1.0f },
                0.0f,
                juce::String(),
                [this, index] {
                    return pads[index].sequencerGated.load() ? 1.0f : 0.0f;
                },
                [this, index] (float value)
                {
                    const bool next = value >= 0.5f;

                    if (pads[index].sequencerGated.exchange (next) != next)
                        markHostParameterStateChanged();
                },
                2,
                false,
                [] (float value, int)
                {
                    return value >= 0.5f ? juce::String ("Gated")
                                         : juce::String ("Trigger");
                }));
    }

    // Mixer sends were added in v0.10.0. Keep them last so all earlier host
    // parameter indices remain stable in PHI and DAW projects.
    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto index = static_cast<std::size_t> (padIndex);
        const auto idPrefix = "pad_"
                            + juce::String (padIndex + 1).paddedLeft ('0', 2);
        const auto namePrefix = "Pad " + juce::String (padIndex + 1) + " ";
        const auto percentageText = [] (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        };

        padHostParameters[index].delaySend = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_delay_send", 1 },
                namePrefix + "Delay Send",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
                0.0f, "%",
                [this, index] { return pads[index].delaySend.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 1.0f, value);

                    if (std::abs (pads[index].delaySend.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0, false, percentageText));

        padHostParameters[index].reverbSend = addStateParameter (
            std::make_unique<StateBackedParameter> (
                juce::ParameterID { idPrefix + "_reverb_send", 1 },
                namePrefix + "Reverb Send",
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
                0.0f, "%",
                [this, index] { return pads[index].reverbSend.load(); },
                [this, index] (float value)
                {
                    const float next = juce::jlimit (0.0f, 1.0f, value);

                    if (std::abs (pads[index].reverbSend.exchange (next) - next)
                        > 0.0001f)
                        markHostParameterStateChanged();
                },
                0, false, percentageText));
    }

    // Global ducking was added in v0.11.0. These parameters stay at the end
    // so every existing PHI/DAW parameter index remains unchanged.
    addGlobalFloat (
        globalDelayDuckParameter, "global_delay_duck", "Global Delay Duck",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.0f, "%", globalDelayDuck,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });
    addGlobalFloat (
        globalDelayDuckAttackParameter, "global_delay_duck_attack_ms",
        "Global Delay Duck Attack",
        juce::NormalisableRange<float> { 0.1f, 250.0f, 0.1f },
        10.0f, "ms", globalDelayDuckAttackMs,
        [] (float value, int) {
            return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
        });
    addGlobalFloat (
        globalDelayDuckReleaseParameter, "global_delay_duck_release_ms",
        "Global Delay Duck Release",
        juce::NormalisableRange<float> { 10.0f, 2000.0f, 1.0f },
        250.0f, "ms", globalDelayDuckReleaseMs,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value)) + " ms";
        });
    addGlobalFloat (
        globalReverbDuckParameter, "global_reverb_duck",
        "Global Reverb Duck",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.0f, "%", globalReverbDuck,
        [] (float value, int) {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        });

    // The global master was added in v0.11.3 and remains last to preserve all
    // established PHI/DAW parameter indices.
    addGlobalFloat (
        masterVolumeParameter, "master_volume_db", "Master Volume",
        juce::NormalisableRange<float> { -60.0f, 6.0f, 0.1f },
        0.0f, "dB", masterVolumeDb,
        [] (float value, int) {
            return value <= -59.95f
                     ? juce::String ("-INF")
                     : juce::String (value, 1) + " dB";
        });
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

void SVDrummerAudioProcessor::prepareToPlay (double sampleRate,
                                              int maximumBlockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentMasterGain = juce::Decibels::decibelsToGain (
        masterVolumeDb.load(), -60.0f);
    preparePadFilterDsp (maximumBlockSize);
    prepareGlobalEffects();
    lastPatternTimelineResetCounter = patternTimelineResetCounter.load();
    lastPatternPlaybackSwitchCounter = patternPlaybackSwitchCounter.load();
    lastPatternGateRestartCounter = patternGateRestartCounter.load();
    patternGateActive.store (false);
    patternGateWaitingForSelection.store (false);
    activePatternGateNote.store (-1);
    pendingPatternGateStartNote.store (-1);
    queuedSyncedPatternSelection.store (-1);
    queuedSyncedPatternGateNote.store (-1);
    currentHostTransportPlaying.store (false);
    currentHostPpqAvailable.store (false);

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
    if (! layouts.inputBuses.isEmpty()
        || layouts.outputBuses.size() != numberOfOutputBuses
        || layouts.getMainOutputChannelSet()
             != juce::AudioChannelSet::stereo())
    {
        return false;
    }

    for (int busIndex = 1; busIndex < numberOfOutputBuses; ++busIndex)
    {
        const auto layout = layouts.getChannelSet (false, busIndex);

        if (! layout.isDisabled()
            && layout != juce::AudioChannelSet::stereo())
        {
            return false;
        }
    }

    return true;
}

void SVDrummerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    auto mainOutput = getBusBuffer (buffer, false, 0);

    bool blockHostPlaying = false;
    bool blockHostPpqAvailable = false;
    const bool previousHostPpqAvailable = currentHostPpqAvailable.load();
    const double previousHostPpq = currentHostPpqPosition.load();
    double blockStartPpq = previousHostPpq;
    double blockBpm = currentHostTempoBpm.load();

    if (auto* currentPlayHead = getPlayHead())
        if (const auto position = currentPlayHead->getPosition())
        {
            blockHostPlaying = position->getIsPlaying();

            if (const auto bpm = position->getBpm())
                blockBpm = juce::jmax (1.0, *bpm);

            if (const auto ppq = position->getPpqPosition())
            {
                blockStartPpq = *ppq;
                blockHostPpqAvailable = true;
            }
        }

    if (queuedSyncedPatternSelection.load() >= 0
        && blockHostPlaying
        && blockHostPpqAvailable
        && previousHostPpqAvailable
        && blockStartPpq + 1.0e-6 < previousHostPpq)
    {
        queuedSyncedPatternBoundaryPpq.store (
            getNextPatternSyncBoundary (blockStartPpq));
    }

    currentHostTempoBpm.store (blockBpm);
    currentHostPpqPosition.store (blockStartPpq);
    currentHostTransportPlaying.store (blockHostPlaying);
    currentHostPpqAvailable.store (blockHostPpqAvailable);
    serviceQueuedPatternSelection (blockStartPpq);

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
                        const bool quantiseSelection =
                            getPatternSyncMode() != PatternSyncMode::played
                            && blockHostPlaying
                            && blockHostPpqAvailable;
                        const bool preserveCurrentPatternUntilBoundary =
                            quantiseSelection
                            && midiMode != PatternMidiMode::select
                            && sequencerEnabled.load()
                            && patternGateActive.load()
                            && ! patternGateWaitingForSelection.load();

                        if (midiMode != PatternMidiMode::select
                            && ! preserveCurrentPatternUntilBoundary)
                        {
                            activePatternGateNote.store (note);
                            patternGateActive.store (false);
                            patternGateWaitingForSelection.store (true);
                            sequencerEnabled.store (true);
                        }

                        if (quantiseSelection)
                        {
                            const double quarterNotesPerSample =
                                blockBpm
                                / (60.0 * juce::jmax (
                                      1.0, currentSampleRate));
                            const double eventPpq = blockStartPpq
                                + static_cast<double> (
                                      juce::jmax (0, metadata.samplePosition))
                                      * quarterNotesPerSample;
                            queueSyncedPatternSelection (
                                patternIndex,
                                midiMode == PatternMidiMode::select ? -1
                                                                    : note,
                                eventPpq);
                        }
                        else
                        {
                            pendingPatternSelection.store (patternIndex);

                            if (midiMode != PatternMidiMode::select)
                                pendingPatternGateStartNote.store (note);

                            triggerAsyncUpdate();
                        }
                    }
                }
                else if (midiMode == PatternMidiMode::gate
                         && queuedSyncedPatternGateNote.load() == note)
                {
                    queuedSyncedPatternSelection.store (-1);
                    queuedSyncedPatternGateNote.store (-1);
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
                0, false, true);

    const auto pendingReleases = pendingInterfaceReleases.exchange (0);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        if ((pendingReleases
             & (std::uint32_t { 1 } << static_cast<unsigned int> (padIndex))) == 0)
            continue;

        for (auto& voice : pads[static_cast<std::size_t> (padIndex)].voices)
            if (voice.active && voice.looping && voice.interfaceTriggered)
                voice.releaseAtOutputSample = 0;
    }

    processSequencerTriggers (buffer.getNumSamples());

    const bool hasSolo = anyPadIsSoloed();
    delaySendBuffer.setSize (2, buffer.getNumSamples(), false, false, true);
    reverbSendBuffer.setSize (2, buffer.getNumSamples(), false, false, true);
    delaySendBuffer.clear();
    reverbSendBuffer.clear();

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& pad = pads[static_cast<std::size_t> (padIndex)];
        const bool outputEnabled = hasSolo ? pad.soloed.load()
                                           : ! pad.muted.load();

        padRenderBuffer.setSize (2, buffer.getNumSamples(),
                                 false, false, true);
        padRenderBuffer.clear();
        renderPadVoices (padIndex, padRenderBuffer, outputEnabled);

        auto& filterState = padFilterDspStates[
            static_cast<std::size_t> (padIndex)];

        if (! outputEnabled)
        {
            if (filterState.outputWasEnabled)
            {
                resetPadFilterDspState (padIndex);
                resetPadCompressorDspState (padIndex);
            }

            filterState.wetMix = 0.0f;
            filterState.wasEnabled = false;
            padSaturationDspStates[static_cast<std::size_t> (padIndex)] = {};
            filterState.outputWasEnabled = false;
            continue;
        }

        if (! filterState.outputWasEnabled)
        {
            resetPadFilterDspState (padIndex);
            resetPadCompressorDspState (padIndex);
            filterState.wetMix = 0.0f;
            filterState.wasEnabled = false;
            padSaturationDspStates[static_cast<std::size_t> (padIndex)] = {};
        }

        filterState.outputWasEnabled = true;
        processPadFilter (padIndex, padRenderBuffer);
        processPadCompressor (padIndex, padRenderBuffer);
        processPadSaturation (padIndex, padRenderBuffer);

        const float delaySend = juce::jlimit (
            0.0f, 1.0f, pad.delaySend.load());
        const float reverbSend = juce::jlimit (
            0.0f, 1.0f, pad.reverbSend.load());

        for (int channel = 0; channel < padRenderBuffer.getNumChannels(); ++channel)
        {
            if (delaySend > 0.0f)
                delaySendBuffer.addFrom (
                    channel, 0, padRenderBuffer, channel, 0,
                    buffer.getNumSamples(), delaySend);

            if (reverbSend > 0.0f)
                reverbSendBuffer.addFrom (
                    channel, 0, padRenderBuffer, channel, 0,
                    buffer.getNumSamples(), reverbSend);
        }

        const int outputBus = juce::jlimit (
            0, numberOfPadOutputBuses, pad.outputBus.load());
        auto destination = getBusBuffer (buffer, false, outputBus);
        const int channelsToCopy = juce::jmin (
            destination.getNumChannels(), padRenderBuffer.getNumChannels());

        for (int channel = 0; channel < channelsToCopy; ++channel)
            destination.addFrom (channel, 0, padRenderBuffer, channel, 0,
                                 buffer.getNumSamples());
    }

    // The global effects act as independent stereo aux returns on MAIN.
    // Per-pad routing remains dry, while every pad may feed either return.
    processGlobalDelay (delaySendBuffer);
    processGlobalReverb (reverbSendBuffer);

    const int returnChannels = juce::jmin (
        mainOutput.getNumChannels(), delaySendBuffer.getNumChannels());

    for (int channel = 0; channel < returnChannels; ++channel)
    {
        mainOutput.addFrom (channel, 0, delaySendBuffer, channel, 0,
                            buffer.getNumSamples());
        mainOutput.addFrom (channel, 0, reverbSendBuffer, channel, 0,
                            buffer.getNumSamples());
    }

    if (browserPreviewTriggerPending.exchange (false))
        triggerBrowserPreviewOnAudioThread();

    renderBrowserPreview (mainOutput);

    const float targetMasterGain = juce::Decibels::decibelsToGain (
        masterVolumeDb.load(), -60.0f);
    const bool rampMaster = std::abs (targetMasterGain - currentMasterGain)
                            > 0.000001f;

    for (int busIndex = 0; busIndex < numberOfOutputBuses; ++busIndex)
    {
        auto output = getBusBuffer (buffer, false, busIndex);

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
        {
            if (rampMaster)
                output.applyGainRamp (channel, 0, output.getNumSamples(),
                                      currentMasterGain, targetMasterGain);
            else
                output.applyGain (channel, 0, output.getNumSamples(),
                                  targetMasterGain);
        }
    }

    currentMasterGain = targetMasterGain;
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
                                                        bool triggeredBySequencer,
                                                        bool triggeredByInterface)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    const auto sample = std::atomic_load_explicit (&pad.sample, std::memory_order_acquire);

    if (sample == nullptr || sample->audio.getNumSamples() <= 0)
        return;

    const bool looping = pad.loopEnabled.load();
    const bool gatedSequencerTrigger = triggeredBySequencer
                                    && pad.sequencerGated.load();
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
    else if (looping && ! gatedSequencerTrigger)
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
    voice.pingPongLoop = voice.looping
                      && getPadLoopMode (padIndex) == PadLoopMode::pingPong;
    voice.position = reversed ? voice.rangeEnd - 1.0 : voice.rangeStart;
    voice.sampleRevision = pad.sampleRevision.load();
    voice.interfaceTriggered = triggeredByInterface;
    voice.sequencerTriggered = triggeredBySequencer;
    voice.sequencerGated = gatedSequencerTrigger;
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
    voice.envelopeAttackCurve = juce::jlimit (
        -1.0f, 1.0f, pad.ampCurve.load());
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

void SVDrummerAudioProcessor::releaseSequencerVoicesOnAudioThread()
{
    for (auto& pad : pads)
    {
        for (auto& voice : pad.voices)
        {
            if (voice.active && voice.sequencerTriggered
                && (voice.looping || voice.sequencerGated))
                beginVoiceRelease (voice);
        }
    }
}

void SVDrummerAudioProcessor::schedulePadSequencerGateRelease (
    int padIndex, int outputSample)
{
    if (! isValidPadIndex (padIndex))
        return;

    for (auto& voice : pads[static_cast<std::size_t> (padIndex)].voices)
    {
        if (! voice.active || ! voice.sequencerTriggered)
            continue;

        if (voice.releaseAtOutputSample < 0)
            voice.releaseAtOutputSample = outputSample;
        else
            voice.releaseAtOutputSample = juce::jmin (
                voice.releaseAtOutputSample, outputSample);
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

                if (loopLength > 0.0 && voice.pingPongLoop
                    && ((voice.increment >= 0.0
                         && voice.position >= voice.loopEnd)
                        || (voice.increment < 0.0
                            && voice.position < voice.loopStart)))
                {
                    const double period = loopLength * 2.0;
                    const double originalIncrement = voice.increment;
                    double phase = std::fmod (
                        voice.position - voice.loopStart, period);

                    if (phase < 0.0)
                        phase += period;

                    if (phase < loopLength)
                    {
                        voice.position = voice.loopStart + phase;
                        voice.increment = originalIncrement;
                    }
                    else
                    {
                        voice.position = voice.loopStart
                                       + (period - phase);
                        voice.increment = -originalIncrement;
                    }

                    voice.position = juce::jlimit (
                        voice.loopStart,
                        voice.loopEnd - 1.0e-9,
                        voice.position);
                }
                else if (loopLength > 0.0 && voice.increment >= 0.0
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
            float shapedEnvelope = juce::jmax (0.0f, voice.envelopeLevel);

            if (voice.envelopeStage == EnvelopeStage::attack
                && std::abs (voice.envelopeAttackCurve) > 0.0001f)
            {
                shapedEnvelope = shapeAttackPhase (
                    shapedEnvelope, voice.envelopeAttackCurve);
            }

            const float voiceGain = gain * voice.velocity * shapedEnvelope;

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

void SVDrummerAudioProcessor::preparePadFilterDsp (int maximumBlockSize)
{
    padRenderBuffer.setSize (2, juce::jmax (1, maximumBlockSize),
                             false, true);

    const int maximumCombDelay = juce::jmax (
        2, juce::roundToInt (currentSampleRate / 20.0) + 2);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        auto& state = padFilterDspStates[static_cast<std::size_t> (padIndex)];

        for (auto& delay : state.combDelay)
            delay.assign (static_cast<std::size_t> (maximumCombDelay), 0.0f);

        state.wetMix = 0.0f;
        state.wasEnabled = false;
        state.outputWasEnabled = false;
        resetPadFilterDspState (padIndex);
        resetPadCompressorDspState (padIndex);
        padSaturationDspStates[static_cast<std::size_t> (padIndex)] = {};
    }
}

void SVDrummerAudioProcessor::resetPadFilterDspState (int padIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& state = padFilterDspStates[static_cast<std::size_t> (padIndex)];
    for (auto& channel : state.mainZ1)
        channel.fill (0.0f);
    for (auto& channel : state.mainZ2)
        channel.fill (0.0f);
    state.mainOnePole.fill (0.0f);
    for (auto& channel : state.formantAZ1)
        channel.fill (0.0f);
    for (auto& channel : state.formantAZ2)
        channel.fill (0.0f);
    for (auto& channel : state.formantBZ1)
        channel.fill (0.0f);
    for (auto& channel : state.formantBZ2)
        channel.fill (0.0f);
    for (auto& channel : state.ladderStages)
        channel.fill (0.0f);
    state.highPassZ1.fill (0.0f);
    state.highPassZ2.fill (0.0f);

    for (auto& delay : state.combDelay)
        std::fill (delay.begin(), delay.end(), 0.0f);

    state.combWritePosition = 0;
    state.lastFilterType = -1;
}

void SVDrummerAudioProcessor::processPadFilter (
    int padIndex, juce::AudioBuffer<float>& buffer)
{
    if (! isValidPadIndex (padIndex)
        || buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto& pad = pads[static_cast<std::size_t> (padIndex)];
    auto& state = padFilterDspStates[static_cast<std::size_t> (padIndex)];
    const auto type = static_cast<PadFilterType> (juce::jlimit (
        1, 7, pad.filterType.load()));
    const int typeValue = static_cast<int> (type);
    const int slopeIndex = juce::jlimit (0, 3, pad.filterSlope.load());
    const bool mainFilterEnabled = pad.filterEnabled.load();
    const float targetWetMix = mainFilterEnabled ? 1.0f : 0.0f;

    if (! mainFilterEnabled && state.wetMix <= 0.0f)
    {
        if (state.wasEnabled)
            resetPadFilterDspState (padIndex);

        state.wasEnabled = false;
        return;
    }

    if (mainFilterEnabled && ! state.wasEnabled)
    {
        resetPadFilterDspState (padIndex);
        state.wasEnabled = true;
    }

    const int activeTypeValue = typeValue * 10 + slopeIndex;

    if (state.lastFilterType != activeTypeValue)
    {
        resetPadFilterDspState (padIndex);
        state.lastFilterType = activeTypeValue;
    }

    const float cutoff = juce::jlimit (
        20.0f, 20000.0f, pad.filterCutoffHz.load());
    const float resonance = juce::jlimit (
        0.0f, 1.0f, pad.filterResonance.load());
    const float driveDb = juce::jlimit (
        0.0f, 24.0f, pad.filterDriveDb.load());
    const float highPassCutoff = juce::jlimit (
        0.0f, 2000.0f, pad.highPassCutoffHz.load());
    static constexpr std::array<int, 4> cascadedBiquadStages { 1, 1, 2, 4 };
    static constexpr std::array<int, 4> ladderPoleCounts { 1, 2, 4, 8 };
    const int biquadStages = cascadedBiquadStages[
        static_cast<std::size_t> (slopeIndex)];
    const int ladderPoles = ladderPoleCounts[
        static_cast<std::size_t> (slopeIndex)];
    const float stageResonance = resonance
        / std::sqrt (static_cast<float> (juce::jmax (1, biquadStages)));
    const auto mainCoefficients = makeFilterCoefficients (
        type, cutoff, stageResonance, currentSampleRate);
    const auto formantACoefficients = makeFilterCoefficients (
        PadFilterType::bandPass,
        juce::jlimit (120.0f, 6500.0f, cutoff * 0.55f),
        juce::jlimit (0.08f, 0.82f,
                      0.18f + stageResonance * 0.58f),
        currentSampleRate);
    const auto formantBCoefficients = makeFilterCoefficients (
        PadFilterType::bandPass,
        juce::jlimit (280.0f, 12000.0f, cutoff * 1.55f),
        juce::jlimit (0.04f, 0.72f,
                      0.12f + stageResonance * 0.48f),
        currentSampleRate);
    const auto highPassCoefficients = makeFilterCoefficients (
        PadFilterType::highPass,
        juce::jmax (20.0f, highPassCutoff), 0.018f,
        currentSampleRate);
    const float driveAmount = driveDb / 24.0f;
    const float driveGain = juce::Decibels::decibelsToGain (driveDb);
    const float driveNormaliser = driveDb > 0.0f
                                    ? 1.0f / std::tanh (driveGain)
                                    : 1.0f;
    const int channelCount = juce::jmin (2, buffer.getNumChannels());
    const bool useComb = type == PadFilterType::comb
                      && ! state.combDelay[0].empty();
    const bool useFormant = type == PadFilterType::formant;
    const bool useLadder = type == PadFilterType::ladder;
    const bool useOnePole = slopeIndex == 0
                         && (type == PadFilterType::lowPass
                             || type == PadFilterType::highPass);
    const float onePoleCoefficient = juce::jlimit (
        0.0001f, 0.995f,
        1.0f - std::exp (
            -juce::MathConstants<float>::twoPi * cutoff
            / static_cast<float> (juce::jmax (1.0, currentSampleRate))));
    const float ladderFeedback = resonance * 3.72f;
    const int combBufferSize = useComb
                                 ? static_cast<int> (state.combDelay[0].size())
                                 : 0;
    const int combDelaySamples = useComb
        ? juce::jlimit (
              1, combBufferSize - 1,
              juce::roundToInt (currentSampleRate
                                / juce::jmax (20.0f, cutoff)))
        : 1;
    const float combFeedback = resonance * 0.92f;
    const float bypassRampStep = 1.0f / static_cast<float> (
        juce::jmax (1.0, currentSampleRate * 0.005));

    if (highPassCutoff < 20.0f)
    {
        state.highPassZ1.fill (0.0f);
        state.highPassZ2.fill (0.0f);
    }

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        state.wetMix += juce::jlimit (
            -bypassRampStep, bypassRampStep,
            targetWetMix - state.wetMix);
        const float wetMix = state.wetMix;
        const int combReadPosition = useComb
            ? (state.combWritePosition - combDelaySamples + combBufferSize)
                % combBufferSize
            : 0;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            float value = buffer.getSample (channel, sample);
            const float dryValue = value;

            if (driveDb > 0.0f)
            {
                const float saturated = std::tanh (value * driveGain)
                                      * driveNormaliser;
                value += (saturated - value) * driveAmount;
            }

            if (useComb)
            {
                auto& delay = state.combDelay[static_cast<std::size_t> (channel)];
                const float delayed = delay[static_cast<std::size_t> (
                    combReadPosition)];
                delay[static_cast<std::size_t> (state.combWritePosition)]
                    = value + delayed * combFeedback;
                value = value * 0.65f + delayed * 0.35f;
            }
            else if (useFormant)
            {
                float formantA = value;
                float formantB = value;
                auto& aZ1 = state.formantAZ1[static_cast<std::size_t> (channel)];
                auto& aZ2 = state.formantAZ2[static_cast<std::size_t> (channel)];
                auto& bZ1 = state.formantBZ1[static_cast<std::size_t> (channel)];
                auto& bZ2 = state.formantBZ2[static_cast<std::size_t> (channel)];

                for (int stage = 0; stage < biquadStages; ++stage)
                {
                    const auto stageIndex = static_cast<std::size_t> (stage);
                    formantA = processBiquadSample (
                        formantA, formantACoefficients,
                        aZ1[stageIndex], aZ2[stageIndex]);
                    formantB = processBiquadSample (
                        formantB, formantBCoefficients,
                        bZ1[stageIndex], bZ2[stageIndex]);
                }

                value = (formantA + formantB) * 0.78f;
            }
            else if (useLadder)
            {
                auto& stages = state.ladderStages[
                    static_cast<std::size_t> (channel)];
                float poleInput = std::tanh (
                    value - ladderFeedback
                                * stages[static_cast<std::size_t> (
                                    ladderPoles - 1)]);

                for (int pole = 0; pole < ladderPoles; ++pole)
                {
                    auto& stage = stages[static_cast<std::size_t> (pole)];
                    stage += onePoleCoefficient
                           * (std::tanh (poleInput) - std::tanh (stage));
                    poleInput = stage;
                }

                value = poleInput;
            }
            else if (useOnePole)
            {
                auto& onePole = state.mainOnePole[
                    static_cast<std::size_t> (channel)];
                onePole += onePoleCoefficient * (value - onePole);
                value = type == PadFilterType::highPass
                      ? value - onePole : onePole;
            }
            else
            {
                auto& mainZ1 = state.mainZ1[
                    static_cast<std::size_t> (channel)];
                auto& mainZ2 = state.mainZ2[
                    static_cast<std::size_t> (channel)];

                for (int stage = 0; stage < biquadStages; ++stage)
                {
                    const auto stageIndex = static_cast<std::size_t> (stage);
                    value = processBiquadSample (
                        value, mainCoefficients,
                        mainZ1[stageIndex], mainZ2[stageIndex]);
                }
            }

            if (highPassCutoff >= 20.0f)
            {
                value = processBiquadSample (
                    value, highPassCoefficients,
                    state.highPassZ1[static_cast<std::size_t> (channel)],
                    state.highPassZ2[static_cast<std::size_t> (channel)]);
            }

            buffer.setSample (
                channel, sample,
                dryValue + (value - dryValue) * wetMix);
        }

        if (useComb)
            state.combWritePosition = (state.combWritePosition + 1)
                                    % combBufferSize;
    }

    if (! mainFilterEnabled && state.wetMix <= 0.0f)
    {
        resetPadFilterDspState (padIndex);
        state.wasEnabled = false;
    }
}

void SVDrummerAudioProcessor::resetPadCompressorDspState (int padIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& state = padCompressorDspStates[static_cast<std::size_t> (padIndex)];
    state.gain = 1.0f;
    state.outputGain = 1.0f;
    state.wetMix = 0.0f;
    state.wasEnabled = false;
}

void SVDrummerAudioProcessor::processPadCompressor (
    int padIndex, juce::AudioBuffer<float>& buffer)
{
    if (! isValidPadIndex (padIndex)
        || buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto& pad = pads[static_cast<std::size_t> (padIndex)];
    auto& state = padCompressorDspStates[static_cast<std::size_t> (padIndex)];
    const bool enabled = pad.compressorEnabled.load();
    const float targetWetMix = enabled ? 1.0f : 0.0f;

    if (! enabled && state.wetMix <= 0.0f)
    {
        if (state.wasEnabled)
            resetPadCompressorDspState (padIndex);

        return;
    }

    const bool starting = ! state.wasEnabled;

    if (starting)
    {
        state.gain = 1.0f;
        state.wasEnabled = true;
    }

    const float thresholdDb = juce::jlimit (
        -60.0f, 0.0f, pad.compressorThresholdDb.load());
    const float ratio = juce::jlimit (
        1.0f, 20.0f, pad.compressorRatio.load());
    const float attackMs = juce::jlimit (
        0.1f, 100.0f, pad.compressorAttackMs.load());
    const float releaseMs = juce::jlimit (
        10.0f, 1000.0f, pad.compressorReleaseMs.load());
    const float kneeDb = juce::jlimit (
        0.0f, 24.0f, pad.compressorKneeDb.load());
    const float targetOutputGain = juce::Decibels::decibelsToGain (
        juce::jlimit (-24.0f, 24.0f, pad.compressorGainDb.load()));
    const double safeSampleRate = juce::jmax (1.0, currentSampleRate);
    const float attackCoefficient = static_cast<float> (std::exp (
        -1.0 / (0.001 * static_cast<double> (attackMs) * safeSampleRate)));
    const float releaseCoefficient = static_cast<float> (std::exp (
        -1.0 / (0.001 * static_cast<double> (releaseMs) * safeSampleRate)));
    const float compressionSlope = 1.0f - 1.0f / ratio;
    const float bypassRampStep = 1.0f / static_cast<float> (
        juce::jmax (1.0, safeSampleRate * 0.005));
    const float outputGainCoefficient = static_cast<float> (std::exp (
        -1.0 / (0.010 * safeSampleRate)));

    if (starting)
        state.outputGain = targetOutputGain;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        state.wetMix += juce::jlimit (
            -bypassRampStep, bypassRampStep,
            targetWetMix - state.wetMix);
        const float wetMix = state.wetMix;
        float detector = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            detector = juce::jmax (
                detector, std::abs (buffer.getSample (channel, sample)));

        const float levelDb = juce::Decibels::gainToDecibels (
            detector, -120.0f);
        const float overThreshold = levelDb - thresholdDb;
        float gainReductionDb = 0.0f;

        if (kneeDb > 0.0f)
        {
            const float halfKnee = kneeDb * 0.5f;

            if (overThreshold > -halfKnee)
            {
                if (overThreshold >= halfKnee)
                {
                    gainReductionDb = compressionSlope * overThreshold;
                }
                else
                {
                    const float kneePosition = overThreshold + halfKnee;
                    gainReductionDb = compressionSlope
                                    * kneePosition * kneePosition
                                    / (2.0f * kneeDb);
                }
            }
        }
        else if (overThreshold > 0.0f)
        {
            gainReductionDb = compressionSlope * overThreshold;
        }

        const float targetGain = juce::Decibels::decibelsToGain (
            -gainReductionDb);
        const float coefficient = targetGain < state.gain
                                    ? attackCoefficient
                                    : releaseCoefficient;
        state.gain = targetGain + coefficient * (state.gain - targetGain);
        state.outputGain = targetOutputGain
                         + outputGainCoefficient
                             * (state.outputGain - targetOutputGain);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float dryValue = buffer.getSample (channel, sample);
            const float wetValue = dryValue * state.gain * state.outputGain;
            buffer.setSample (channel, sample,
                              dryValue + (wetValue - dryValue) * wetMix);
        }
    }

    if (! enabled && state.wetMix <= 0.0f)
    {
        resetPadCompressorDspState (padIndex);
    }
}

void SVDrummerAudioProcessor::processPadSaturation (
    int padIndex, juce::AudioBuffer<float>& buffer)
{
    if (! isValidPadIndex (padIndex)
        || buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return;

    const auto& pad = pads[static_cast<std::size_t> (padIndex)];
    auto& state = padSaturationDspStates[static_cast<std::size_t> (padIndex)];
    const bool enabled = pad.saturationEnabled.load();
    const float targetWetMix = enabled ? 1.0f : 0.0f;

    if (! enabled && state.wetMix <= 0.0f)
    {
        state.wasEnabled = false;
        return;
    }

    if (enabled && ! state.wasEnabled)
        state.wasEnabled = true;

    const float softAmount = juce::jlimit (
        0.0f, 1.0f, pad.saturationAmount.load());
    const float hardClipAmount = juce::jlimit (
        0.0f, 1.0f, pad.saturationHardClipAmount.load());

    // Pad volume and constant-power pan are output controls, so remove their
    // attenuation while shaping the waveform and restore it afterwards. This
    // keeps both nonlinear controls effective on quiet pads without changing
    // the pad's final volume or stereo position.
    const float padGain = juce::Decibels::decibelsToGain (
        pad.volumeDb.load(), -80.0f);
    const float pan = juce::jlimit (-1.0f, 1.0f, pad.pan.load());
    const float panAngle = (pan + 1.0f)
                         * juce::MathConstants<float>::pi * 0.25f;
    const float leftPan = std::cos (panAngle);
    const float rightPan = std::sin (panAngle);

    // tanh (shape * x) / tanh (shape) approaches x as shape approaches zero,
    // giving SAT a continuous identity-to-soft-clipping transfer rather than
    // behaving mainly as an input gain control. The small ceiling reduction
    // matches the rounded peak seen in the reference saturation recording.
    const float softShape = softAmount * 7.0f;
    const float softNormaliser = softShape > 0.0001f
                                   ? 1.0f / std::tanh (softShape)
                                   : 1.0f;
    const float softCeiling = 1.0f - softAmount * 0.016f;

    // HARD CLIP is a true variable-threshold clipper. It is intentionally not
    // mixed with the dry signal: samples over the threshold must acquire flat
    // tops, as they do in the supplied TAL Drum reference sweep.
    const float hardDriveGain = juce::Decibels::decibelsToGain (
        hardClipAmount * 30.0f);
    const float bypassRampStep = 1.0f / static_cast<float> (
        juce::jmax (1.0, currentSampleRate * 0.005));

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        state.wetMix += juce::jlimit (
            -bypassRampStep, bypassRampStep,
            targetWetMix - state.wetMix);
        const float wetMix = state.wetMix;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float dryValue = buffer.getSample (channel, sample);
            const float channelScale = padGain * (
                buffer.getNumChannels() <= 1 ? 1.0f
              : channel == 0 ? leftPan
              : channel == 1 ? rightPan
                             : 1.0f);

            if (channelScale <= 1.0e-7f)
                continue;

            float value = dryValue / channelScale;

            if (softAmount > 0.0f)
                value = std::tanh (value * softShape)
                      * softNormaliser * softCeiling;

            if (hardClipAmount > 0.0f)
                value = juce::jlimit (
                    -1.0f, 1.0f, value * hardDriveGain);

            const float wetValue = value * channelScale;
            buffer.setSample (channel, sample,
                              dryValue + (wetValue - dryValue) * wetMix);
        }
    }

    if (! enabled && state.wetMix <= 0.0f)
        state.wasEnabled = false;
}

void SVDrummerAudioProcessor::prepareGlobalEffects()
{
    const int maximumDelaySamples = juce::jmax (
        4, juce::roundToInt (juce::jmax (1.0, currentSampleRate) * 8.0) + 2);

    for (auto& channel : globalDelayBuffer)
        channel.assign (static_cast<std::size_t> (maximumDelaySamples), 0.0f);

    globalDelayWritePosition = 0;
    globalDelayCurrentSamples = 0.0f;
    globalDelayFeedbackLowPass.fill (0.0f);
    globalDelayBypassMix = 0.0f;
    globalDelayDuckEnvelope = 0.0f;
    globalDelayWasEnabled = false;
    delaySendBuffer.setSize (
        2, juce::jmax (1, padRenderBuffer.getNumSamples()), false, true);
    reverbSendBuffer.setSize (
        2, juce::jmax (1, padRenderBuffer.getNumSamples()), false, true);
    globalReverbDuckGain.assign (
        static_cast<std::size_t> (
            juce::jmax (1, padRenderBuffer.getNumSamples())),
        1.0f);
    globalReverb.setSampleRate (juce::jmax (1.0, currentSampleRate));
    globalReverb.reset();
    globalReverbBypassMix = 0.0f;
    globalReverbDuckEnvelope = 0.0f;
    globalReverbWasEnabled = false;
}

void SVDrummerAudioProcessor::processGlobalDelay (
    juce::AudioBuffer<float>& buffer)
{
    const bool enabled = globalDelayEnabled.load();
    const float targetBypassMix = enabled ? 1.0f : 0.0f;
    const auto resetDelay = [this]
    {
        for (auto& channel : globalDelayBuffer)
            std::fill (channel.begin(), channel.end(), 0.0f);

        globalDelayWritePosition = 0;
        globalDelayCurrentSamples = 0.0f;
        globalDelayFeedbackLowPass.fill (0.0f);
        globalDelayDuckEnvelope = 0.0f;
    };

    if (! enabled && globalDelayBypassMix <= 0.0f)
    {
        if (globalDelayWasEnabled)
            resetDelay();

        globalDelayWasEnabled = false;
        buffer.clear();
        return;
    }

    if (globalDelayBuffer[0].size() < 4)
        prepareGlobalEffects();

    const int bufferSize = static_cast<int> (globalDelayBuffer[0].size());
    const double sampleRate = juce::jmax (1.0, currentSampleRate);
    float targetMilliseconds = globalDelayTimeMs.load();

    if (globalDelaySyncEnabled.load())
    {
        static constexpr std::array<double, 12> quarterNoteLengths
        {
            4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5,
            1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625
        };
        const int division = juce::jlimit (
            0, static_cast<int> (quarterNoteLengths.size()) - 1,
            globalDelaySyncDivision.load());
        const double bpm = juce::jmax (1.0, currentHostTempoBpm.load());
        targetMilliseconds = static_cast<float> (
            60000.0 / bpm
            * quarterNoteLengths[static_cast<std::size_t> (division)]);
    }

    const float targetDelaySamples = juce::jlimit (
        1.0f, static_cast<float> (bufferSize - 2),
        targetMilliseconds * static_cast<float> (sampleRate) / 1000.0f);

    if (! globalDelayWasEnabled || globalDelayCurrentSamples <= 0.0f)
        globalDelayCurrentSamples = targetDelaySamples;

    globalDelayWasEnabled = true;
    const float feedback = juce::jlimit (
        0.0f, 0.95f, globalDelayFeedback.load());
    const float mix = juce::jlimit (0.0f, 1.0f, globalDelayMix.load());
    const int channelCount = juce::jmin (2, buffer.getNumChannels());
    const float delaySmoothing = static_cast<float> (
        1.0 - std::exp (-1.0 / (0.025 * sampleRate)));
    const double feedbackCutoff = juce::jmin (9000.0, sampleRate * 0.45);
    const float feedbackSmoothing = static_cast<float> (
        std::exp (-2.0 * juce::MathConstants<double>::pi
                  * feedbackCutoff / sampleRate));
    const float bypassRampStep = 1.0f / static_cast<float> (
        juce::jmax (1.0, sampleRate * 0.005));
    const float duckAmount = juce::jlimit (
        0.0f, 1.0f, globalDelayDuck.load());
    const float duckAttackSeconds = juce::jmax (
        0.0001f, globalDelayDuckAttackMs.load() * 0.001f);
    const float duckReleaseSeconds = juce::jmax (
        0.001f, globalDelayDuckReleaseMs.load() * 0.001f);
    const float duckAttackCoefficient = static_cast<float> (
        1.0 - std::exp (-1.0 / (duckAttackSeconds * sampleRate)));
    const float duckReleaseCoefficient = static_cast<float> (
        1.0 - std::exp (-1.0 / (duckReleaseSeconds * sampleRate)));

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float duckGain = 1.0f;

        if (duckAmount > 0.0f)
        {
            float detector = 0.0f;

            for (int channel = 0; channel < channelCount; ++channel)
                detector = juce::jmax (
                    detector, std::abs (buffer.getSample (channel, sample)));

            detector = std::sqrt (juce::jlimit (
                0.0f, 1.0f, detector * 8.0f));
            const float coefficient = detector > globalDelayDuckEnvelope
                                        ? duckAttackCoefficient
                                        : duckReleaseCoefficient;
            globalDelayDuckEnvelope += coefficient
                * (detector - globalDelayDuckEnvelope);
            constexpr float maximumDuckNaturalLog = 5.526204f; // 48 dB
            duckGain = std::exp (
                -maximumDuckNaturalLog * duckAmount
                * juce::jlimit (0.0f, 1.0f,
                                globalDelayDuckEnvelope));
        }
        else
        {
            globalDelayDuckEnvelope = 0.0f;
        }

        globalDelayBypassMix += juce::jlimit (
            -bypassRampStep, bypassRampStep,
            targetBypassMix - globalDelayBypassMix);
        const float returnLevel = mix * globalDelayBypassMix * duckGain;
        globalDelayCurrentSamples += delaySmoothing
            * (targetDelaySamples - globalDelayCurrentSamples);
        float readPosition = static_cast<float> (globalDelayWritePosition)
                           - globalDelayCurrentSamples;

        while (readPosition < 0.0f)
            readPosition += static_cast<float> (bufferSize);

        const int first = static_cast<int> (std::floor (readPosition))
                        % bufferSize;
        const int second = (first + 1) % bufferSize;
        const float fraction = readPosition - std::floor (readPosition);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& delay = globalDelayBuffer[static_cast<std::size_t> (channel)];
            const float delayed = juce::jmap (
                fraction, delay[static_cast<std::size_t> (first)],
                delay[static_cast<std::size_t> (second)]);
            auto& filtered = globalDelayFeedbackLowPass[
                static_cast<std::size_t> (channel)];
            filtered = delayed + feedbackSmoothing * (filtered - delayed);
            const float input = buffer.getSample (channel, sample);
            delay[static_cast<std::size_t> (globalDelayWritePosition)]
                = input + std::tanh (filtered) * feedback;
            buffer.setSample (channel, sample, filtered * returnLevel);
        }

        globalDelayWritePosition = (globalDelayWritePosition + 1) % bufferSize;
    }

    if (! enabled && globalDelayBypassMix <= 0.0f)
    {
        resetDelay();
        globalDelayWasEnabled = false;
    }
}

void SVDrummerAudioProcessor::processGlobalReverb (
    juce::AudioBuffer<float>& buffer)
{
    const bool enabled = globalReverbEnabled.load();
    const float targetBypassMix = enabled ? 1.0f : 0.0f;

    if (! enabled && globalReverbBypassMix <= 0.0f)
    {
        if (globalReverbWasEnabled)
            globalReverb.reset();

        globalReverbWasEnabled = false;
        globalReverbDuckEnvelope = 0.0f;
        buffer.clear();
        return;
    }

    if (enabled && ! globalReverbWasEnabled)
    {
        globalReverb.reset();
        globalReverbDuckEnvelope = 0.0f;
        globalReverbWasEnabled = true;
    }

    const int numSamples = buffer.getNumSamples();
    const int channelCount = juce::jmin (2, buffer.getNumChannels());

    if (globalReverbDuckGain.size()
        < static_cast<std::size_t> (numSamples))
        globalReverbDuckGain.resize (
            static_cast<std::size_t> (numSamples), 1.0f);

    const float duckAmount = juce::jlimit (
        0.0f, 1.0f, globalReverbDuck.load());
    const double sampleRate = juce::jmax (1.0, currentSampleRate);
    constexpr float duckAttackSeconds = 0.010f;
    constexpr float duckReleaseSeconds = 0.250f;
    const float duckAttackCoefficient = static_cast<float> (
        1.0 - std::exp (-1.0 / (duckAttackSeconds * sampleRate)));
    const float duckReleaseCoefficient = static_cast<float> (
        1.0 - std::exp (-1.0 / (duckReleaseSeconds * sampleRate)));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float duckGain = 1.0f;

        if (duckAmount > 0.0f)
        {
            float detector = 0.0f;

            for (int channel = 0; channel < channelCount; ++channel)
                detector = juce::jmax (
                    detector, std::abs (buffer.getSample (channel, sample)));

            detector = std::sqrt (juce::jlimit (
                0.0f, 1.0f, detector * 8.0f));
            const float coefficient = detector > globalReverbDuckEnvelope
                                        ? duckAttackCoefficient
                                        : duckReleaseCoefficient;
            globalReverbDuckEnvelope += coefficient
                * (detector - globalReverbDuckEnvelope);
            constexpr float maximumDuckNaturalLog = 5.526204f; // 48 dB
            duckGain = std::exp (
                -maximumDuckNaturalLog * duckAmount
                * juce::jlimit (0.0f, 1.0f,
                                globalReverbDuckEnvelope));
        }
        else
        {
            globalReverbDuckEnvelope = 0.0f;
        }

        globalReverbDuckGain[static_cast<std::size_t> (sample)] = duckGain;
    }

    juce::Reverb::Parameters parameters;
    parameters.roomSize = juce::jlimit (0.0f, 1.0f, globalReverbSize.load());
    parameters.damping = juce::jlimit (0.0f, 1.0f, globalReverbDamping.load());
    parameters.width = juce::jlimit (0.0f, 1.0f, globalReverbWidth.load());
    parameters.wetLevel = 1.0f;
    parameters.dryLevel = 0.0f;
    parameters.freezeMode = 0.0f;
    globalReverb.setParameters (parameters);

    if (buffer.getNumChannels() >= 2)
        globalReverb.processStereo (buffer.getWritePointer (0),
                                    buffer.getWritePointer (1),
                                    numSamples);
    else
        globalReverb.processMono (buffer.getWritePointer (0),
                                  numSamples);

    const float bypassRampStep = 1.0f / static_cast<float> (
        juce::jmax (1.0, currentSampleRate * 0.005));
    const float returnLevel = juce::jlimit (
        0.0f, 1.0f, globalReverbMix.load());

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        globalReverbBypassMix += juce::jlimit (
            -bypassRampStep, bypassRampStep,
            targetBypassMix - globalReverbBypassMix);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float wet = buffer.getSample (channel, sample);
            buffer.setSample (channel, sample,
                              wet * returnLevel * globalReverbBypassMix
                                  * globalReverbDuckGain[
                                      static_cast<std::size_t> (sample)]);
        }
    }

    if (! enabled && globalReverbBypassMix <= 0.0f)
    {
        globalReverb.reset();
        globalReverbDuckEnvelope = 0.0f;
        globalReverbWasEnabled = false;
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

SVDrummerAudioProcessor::KitPadState
SVDrummerAudioProcessor::capturePadState (int padIndex) const
{
    KitPadState state;

    if (! isValidPadIndex (padIndex))
        return state;

    const auto& pad = pads[static_cast<std::size_t> (padIndex)];
    state.sample = std::atomic_load_explicit (
        &pad.sample, std::memory_order_acquire);

    {
        const juce::ScopedLock lock (stateLock);
        state.samplePath = pad.samplePath;
        state.displayName = pad.displayName;
    }

    state.midiNote = pad.midiNote.load();
    state.muted = pad.muted.load();
    state.soloed = pad.soloed.load();
    state.reversed = pad.reversed.load();
    state.volumeDb = pad.volumeDb.load();
    state.pan = pad.pan.load();
    state.tuneSemitones = pad.tuneSemitones.load();
    state.outputBus = pad.outputBus.load();
    state.delaySend = pad.delaySend.load();
    state.reverbSend = pad.reverbSend.load();
    state.chokeGroup = pad.chokeGroup.load();
    state.ampCurve = pad.ampCurve.load();
    state.ampAttackMs = pad.ampAttackMs.load();
    state.ampDecayMs = pad.ampDecayMs.load();
    state.ampSustain = pad.ampSustain.load();
    state.ampReleaseMs = pad.ampReleaseMs.load();
    state.filterCutoffHz = pad.filterCutoffHz.load();
    state.filterResonance = pad.filterResonance.load();
    state.filterDriveDb = pad.filterDriveDb.load();
    state.filterEnabled = pad.filterEnabled.load();
    state.filterType = pad.filterType.load();
    state.filterSlope = pad.filterSlope.load();
    state.highPassCutoffHz = pad.highPassCutoffHz.load();
    state.compressorEnabled = pad.compressorEnabled.load();
    state.compressorThresholdDb = pad.compressorThresholdDb.load();
    state.compressorRatio = pad.compressorRatio.load();
    state.compressorAttackMs = pad.compressorAttackMs.load();
    state.compressorReleaseMs = pad.compressorReleaseMs.load();
    state.compressorKneeDb = pad.compressorKneeDb.load();
    state.compressorGainDb = pad.compressorGainDb.load();
    state.saturationAmount = pad.saturationAmount.load();
    state.saturationHardClipAmount = pad.saturationHardClipAmount.load();
    state.saturationEnabled = pad.saturationEnabled.load();
    state.sampleStart = pad.sampleStart.load();
    state.sampleEnd = pad.sampleEnd.load();
    state.loopEnabled = pad.loopEnabled.load();
    state.loopMode = pad.loopMode.load();
    state.sequencerGated = pad.sequencerGated.load();
    state.loopStart = pad.loopStart.load();
    state.loopEnd = pad.loopEnd.load();
    return state;
}

void SVDrummerAudioProcessor::applyPadState (
    int padIndex, const KitPadState& state)
{
    if (! isValidPadIndex (padIndex))
        return;

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    pad.sampleRevision.fetch_add (1);
    std::atomic_store_explicit (&pad.sample, state.sample,
                                std::memory_order_release);

    {
        const juce::ScopedLock lock (stateLock);
        pad.samplePath = state.samplePath;
        pad.displayName = state.displayName;
    }

    pad.sampleStart.store (state.sampleStart);
    pad.sampleEnd.store (state.sampleEnd);
    pad.loopStart.store (state.loopStart);
    pad.loopEnd.store (state.loopEnd);

    setPadMidiNote (padIndex, state.midiNote);
    setPadMuted (padIndex, state.muted);
    setPadSoloed (padIndex, state.soloed);
    setPadReversed (padIndex, state.reversed);
    setPadVolumeDb (padIndex, state.volumeDb);
    setPadPan (padIndex, state.pan);
    setPadTuneSemitones (padIndex, state.tuneSemitones);
    setPadOutputBus (padIndex, state.outputBus);
    setPadDelaySend (padIndex, state.delaySend);
    setPadReverbSend (padIndex, state.reverbSend);
    setPadChokeGroup (padIndex, state.chokeGroup);
    setPadAmpCurve (padIndex, state.ampCurve);
    setPadAmpAttackMs (padIndex, state.ampAttackMs);
    setPadAmpDecayMs (padIndex, state.ampDecayMs);
    setPadAmpSustain (padIndex, state.ampSustain);
    setPadAmpReleaseMs (padIndex, state.ampReleaseMs);
    setPadFilterCutoffHz (padIndex, state.filterCutoffHz);
    setPadFilterResonance (padIndex, state.filterResonance);
    setPadFilterDriveDb (padIndex, state.filterDriveDb);
    setPadFilterEnabled (padIndex, state.filterEnabled);
    setPadFilterType (
        padIndex,
        static_cast<PadFilterType> (juce::jlimit (1, 7, state.filterType)));
    setPadFilterSlopeIndex (padIndex, state.filterSlope);
    setPadHighPassCutoffHz (padIndex, state.highPassCutoffHz);
    setPadCompressorEnabled (padIndex, state.compressorEnabled);
    setPadCompressorThresholdDb (padIndex, state.compressorThresholdDb);
    setPadCompressorRatio (padIndex, state.compressorRatio);
    setPadCompressorAttackMs (padIndex, state.compressorAttackMs);
    setPadCompressorReleaseMs (padIndex, state.compressorReleaseMs);
    setPadCompressorKneeDb (padIndex, state.compressorKneeDb);
    setPadCompressorGainDb (padIndex, state.compressorGainDb);
    setPadSaturationAmount (padIndex, state.saturationAmount);
    setPadSaturationHardClipAmount (
        padIndex, state.saturationHardClipAmount);
    setPadSaturationEnabled (padIndex, state.saturationEnabled);
    setPadLoopEnabled (padIndex, state.loopEnabled);
    setPadLoopMode (
        padIndex,
        static_cast<PadLoopMode> (juce::jlimit (0, 1, state.loopMode)));
    setPadSequencerGated (padIndex, state.sequencerGated);
    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::resetPadToDefault (int padIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    KitPadState defaults;
    defaults.midiNote = 36 + padIndex;
    applyPadState (padIndex, defaults);
}

void SVDrummerAudioProcessor::copyPad (int padIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    const auto state = capturePadState (padIndex);

    {
        const juce::ScopedLock lock (stateLock);
        copiedPad = state;
    }

    copiedPadAvailable.store (true);
}

bool SVDrummerAudioProcessor::canPastePad() const noexcept
{
    return copiedPadAvailable.load();
}

void SVDrummerAudioProcessor::pastePad (int padIndex)
{
    if (! isValidPadIndex (padIndex) || ! copiedPadAvailable.load())
        return;

    KitPadState state;

    {
        const juce::ScopedLock lock (stateLock);
        state = copiedPad;
    }

    applyPadState (padIndex, state);
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

    const int value = juce::jlimit (0, 127, midiNote);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].midiNote,
            static_cast<float> (value)))
        return;

    pads[static_cast<std::size_t> (padIndex)].midiNote.store (value);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadMuted (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].muted.load();
}

void SVDrummerAudioProcessor::setPadMuted (int padIndex, bool shouldBeMuted)
{
    if (! isValidPadIndex (padIndex))
        return;

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].mute,
            shouldBeMuted ? 1.0f : 0.0f))
        return;

    pads[static_cast<std::size_t> (padIndex)].muted.store (shouldBeMuted);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadSoloed (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].soloed.load();
}

void SVDrummerAudioProcessor::setPadSoloed (int padIndex, bool shouldBeSoloed)
{
    if (! isValidPadIndex (padIndex))
        return;

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].solo,
            shouldBeSoloed ? 1.0f : 0.0f))
        return;

    pads[static_cast<std::size_t> (padIndex)].soloed.store (shouldBeSoloed);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadReversed (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].reversed.load();
}

void SVDrummerAudioProcessor::setPadReversed (int padIndex, bool shouldBeReversed)
{
    if (! isValidPadIndex (padIndex))
        return;

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].reverse,
            shouldBeReversed ? 1.0f : 0.0f))
        return;

    pads[static_cast<std::size_t> (padIndex)].reversed.store (shouldBeReversed);
    markPortableSettingsDirty();
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

int SVDrummerAudioProcessor::getPadOutputBus (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? juce::jlimit (
               0, numberOfPadOutputBuses,
               pads[static_cast<std::size_t> (padIndex)].outputBus.load())
         : 0;
}

void SVDrummerAudioProcessor::setPadOutputBus (int padIndex, int outputBus)
{
    if (! isValidPadIndex (padIndex))
        return;

    const int value = juce::jlimit (0, numberOfPadOutputBuses, outputBus);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].outputBus,
            static_cast<float> (value)))
        return;

    pads[static_cast<std::size_t> (padIndex)].outputBus.store (value);
    markPortableSettingsDirty();
}

juce::String SVDrummerAudioProcessor::getPadOutputBusName (int outputBus)
{
    const int value = juce::jlimit (0, numberOfPadOutputBuses, outputBus);
    return value == 0 ? juce::String ("MAIN")
                      : "AUX " + juce::String (value);
}

float SVDrummerAudioProcessor::getPadDelaySend (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].delaySend.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadDelaySend (int padIndex, float amount)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].delaySend,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].delaySend.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadReverbSend (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].reverbSend.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadReverbSend (int padIndex, float amount)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].reverbSend,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].reverbSend.store (value);
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
    if (! isValidPadIndex (padIndex))
        return;

    const int value = juce::jlimit (0, numberOfPads, chokeGroup);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].chokeGroup,
            static_cast<float> (value)))
        return;

    pads[static_cast<std::size_t> (padIndex)].chokeGroup.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadAmpCurve (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].ampCurve.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadAmpCurve (int padIndex, float curve)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (-1.0f, 1.0f, curve);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].ampCurve,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].ampCurve.store (value);
    markPortableSettingsDirty();
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
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 2000.0f, milliseconds);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].ampAttack,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].ampAttackMs.store (value);
    markPortableSettingsDirty();
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
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 5000.0f, milliseconds);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].ampDecay,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].ampDecayMs.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadAmpSustain (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].ampSustain.load()
         : 1.0f;
}

void SVDrummerAudioProcessor::setPadAmpSustain (int padIndex, float level)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 1.0f, level);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].ampSustain,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].ampSustain.store (value);
    markPortableSettingsDirty();
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
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 5000.0f, milliseconds);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].ampRelease,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].ampReleaseMs.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadFilterCutoffHz (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].filterCutoffHz.load()
         : 20000.0f;
}

void SVDrummerAudioProcessor::setPadFilterCutoffHz (int padIndex,
                                                     float frequencyHz)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (20.0f, 20000.0f, frequencyHz);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].filterCutoff,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].filterCutoffHz.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadFilterResonance (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].filterResonance.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadFilterResonance (int padIndex, float amount)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].filterResonance,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].filterResonance.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadFilterDriveDb (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].filterDriveDb.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadFilterDriveDb (int padIndex, float decibels)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 24.0f, decibels);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].filterDrive,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].filterDriveDb.store (value);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadFilterEnabled (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].filterEnabled.load();
}

void SVDrummerAudioProcessor::setPadFilterEnabled (int padIndex,
                                                    bool shouldBeEnabled)
{
    if (! isValidPadIndex (padIndex))
        return;

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .filterEnabled,
            shouldBeEnabled ? 1.0f : 0.0f))
        return;

    pads[static_cast<std::size_t> (padIndex)].filterEnabled.store (
        shouldBeEnabled);
    markPortableSettingsDirty();
}

SVDrummerAudioProcessor::PadFilterType
SVDrummerAudioProcessor::getPadFilterType (int padIndex) const
{
    const int value = isValidPadIndex (padIndex)
                        ? pads[static_cast<std::size_t> (padIndex)]
                              .filterType.load()
                        : static_cast<int> (PadFilterType::lowPass);
    return static_cast<PadFilterType> (juce::jlimit (1, 7, value));
}

void SVDrummerAudioProcessor::setPadFilterType (int padIndex,
                                                 PadFilterType type)
{
    if (! isValidPadIndex (padIndex))
        return;

    const int value = juce::jlimit (1, 7, static_cast<int> (type));

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].filterType,
            static_cast<float> (value)))
        return;

    pads[static_cast<std::size_t> (padIndex)].filterType.store (value);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPadFilterSlopeIndex (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? juce::jlimit (0, 3,
               pads[static_cast<std::size_t> (padIndex)].filterSlope.load())
         : 1;
}

void SVDrummerAudioProcessor::setPadFilterSlopeIndex (int padIndex,
                                                       int slopeIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    const int value = juce::jlimit (0, 3, slopeIndex);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].filterSlope,
            static_cast<float> (value)))
        return;

    pads[static_cast<std::size_t> (padIndex)].filterSlope.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadHighPassCutoffHz (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].highPassCutoffHz.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadHighPassCutoffHz (int padIndex,
                                                       float frequencyHz)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 2000.0f, frequencyHz);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].highPassCutoff,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].highPassCutoffHz.store (value);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadCompressorEnabled (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].compressorEnabled.load();
}

void SVDrummerAudioProcessor::setPadCompressorEnabled (int padIndex,
                                                        bool shouldBeEnabled)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = shouldBeEnabled ? 1.0f : 0.0f;

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .compressorEnabled,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].compressorEnabled.store (
        shouldBeEnabled);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadCompressorThresholdDb (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)]
               .compressorThresholdDb.load()
         : -18.0f;
}

void SVDrummerAudioProcessor::setPadCompressorThresholdDb (
    int padIndex, float decibels)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (-60.0f, 0.0f, decibels);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .compressorThreshold,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].compressorThresholdDb.store (
        value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadCompressorRatio (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].compressorRatio.load()
         : 4.0f;
}

void SVDrummerAudioProcessor::setPadCompressorRatio (int padIndex, float ratio)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (1.0f, 20.0f, ratio);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .compressorRatio,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].compressorRatio.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadCompressorAttackMs (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].compressorAttackMs.load()
         : 10.0f;
}

void SVDrummerAudioProcessor::setPadCompressorAttackMs (
    int padIndex, float milliseconds)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.1f, 100.0f, milliseconds);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .compressorAttack,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].compressorAttackMs.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadCompressorReleaseMs (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].compressorReleaseMs.load()
         : 100.0f;
}

void SVDrummerAudioProcessor::setPadCompressorReleaseMs (
    int padIndex, float milliseconds)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (10.0f, 1000.0f, milliseconds);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .compressorRelease,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].compressorReleaseMs.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadCompressorKneeDb (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].compressorKneeDb.load()
         : 6.0f;
}

void SVDrummerAudioProcessor::setPadCompressorKneeDb (int padIndex,
                                                       float decibels)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 24.0f, decibels);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .compressorKnee,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].compressorKneeDb.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadCompressorGainDb (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].compressorGainDb.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadCompressorGainDb (int padIndex,
                                                       float decibels)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (-24.0f, 24.0f, decibels);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .compressorGain,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].compressorGainDb.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadSaturationAmount (int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)].saturationAmount.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadSaturationAmount (int padIndex,
                                                       float amount)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .saturationAmount,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].saturationAmount.store (value);
    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getPadSaturationHardClipAmount (
    int padIndex) const
{
    return isValidPadIndex (padIndex)
         ? pads[static_cast<std::size_t> (padIndex)]
               .saturationHardClipAmount.load()
         : 0.0f;
}

void SVDrummerAudioProcessor::setPadSaturationHardClipAmount (
    int padIndex, float amount)
{
    if (! isValidPadIndex (padIndex))
        return;

    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .saturationHardClip,
            value))
        return;

    pads[static_cast<std::size_t> (padIndex)].saturationHardClipAmount.store (
        value);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadSaturationEnabled (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].saturationEnabled.load();
}

void SVDrummerAudioProcessor::setPadSaturationEnabled (
    int padIndex, bool shouldBeEnabled)
{
    if (! isValidPadIndex (padIndex))
        return;

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .saturationEnabled,
            shouldBeEnabled ? 1.0f : 0.0f))
        return;

    pads[static_cast<std::size_t> (padIndex)].saturationEnabled.store (
        shouldBeEnabled);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isGlobalDelayEnabled() const noexcept
{
    return globalDelayEnabled.load();
}

void SVDrummerAudioProcessor::setGlobalDelayEnabled (bool shouldBeEnabled)
{
    if (! setHostParameterValue (globalDelayEnabledParameter,
                                 shouldBeEnabled ? 1.0f : 0.0f))
        globalDelayEnabled.store (shouldBeEnabled);

    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isGlobalDelaySyncEnabled() const noexcept
{
    return globalDelaySyncEnabled.load();
}

void SVDrummerAudioProcessor::setGlobalDelaySyncEnabled (
    bool shouldBeEnabled)
{
    if (! setHostParameterValue (globalDelaySyncEnabledParameter,
                                 shouldBeEnabled ? 1.0f : 0.0f))
        globalDelaySyncEnabled.store (shouldBeEnabled);

    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getGlobalDelaySyncDivision() const noexcept
{
    return juce::jlimit (0, 11, globalDelaySyncDivision.load());
}

void SVDrummerAudioProcessor::setGlobalDelaySyncDivision (int divisionIndex)
{
    const int value = juce::jlimit (0, 11, divisionIndex);

    if (! setHostParameterValue (globalDelaySyncDivisionParameter,
                                 static_cast<float> (value)))
        globalDelaySyncDivision.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalDelayTimeMs() const noexcept
{
    return globalDelayTimeMs.load();
}

void SVDrummerAudioProcessor::setGlobalDelayTimeMs (float milliseconds)
{
    const float value = juce::jlimit (1.0f, 2000.0f, milliseconds);

    if (! setHostParameterValue (globalDelayTimeParameter, value))
        globalDelayTimeMs.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalDelayFeedback() const noexcept
{
    return globalDelayFeedback.load();
}

void SVDrummerAudioProcessor::setGlobalDelayFeedback (float amount)
{
    const float value = juce::jlimit (0.0f, 0.95f, amount);

    if (! setHostParameterValue (globalDelayFeedbackParameter, value))
        globalDelayFeedback.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalDelayMix() const noexcept
{
    return globalDelayMix.load();
}

void SVDrummerAudioProcessor::setGlobalDelayMix (float amount)
{
    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (! setHostParameterValue (globalDelayMixParameter, value))
        globalDelayMix.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalDelayDuck() const noexcept
{
    return globalDelayDuck.load();
}

void SVDrummerAudioProcessor::setGlobalDelayDuck (float amount)
{
    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (! setHostParameterValue (globalDelayDuckParameter, value))
        globalDelayDuck.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalDelayDuckAttackMs() const noexcept
{
    return globalDelayDuckAttackMs.load();
}

void SVDrummerAudioProcessor::setGlobalDelayDuckAttackMs (
    float milliseconds)
{
    const float value = juce::jlimit (0.1f, 250.0f, milliseconds);

    if (! setHostParameterValue (globalDelayDuckAttackParameter, value))
        globalDelayDuckAttackMs.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalDelayDuckReleaseMs() const noexcept
{
    return globalDelayDuckReleaseMs.load();
}

void SVDrummerAudioProcessor::setGlobalDelayDuckReleaseMs (
    float milliseconds)
{
    const float value = juce::jlimit (10.0f, 2000.0f, milliseconds);

    if (! setHostParameterValue (globalDelayDuckReleaseParameter, value))
        globalDelayDuckReleaseMs.store (value);

    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isGlobalReverbEnabled() const noexcept
{
    return globalReverbEnabled.load();
}

void SVDrummerAudioProcessor::setGlobalReverbEnabled (bool shouldBeEnabled)
{
    if (! setHostParameterValue (globalReverbEnabledParameter,
                                 shouldBeEnabled ? 1.0f : 0.0f))
        globalReverbEnabled.store (shouldBeEnabled);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalReverbSize() const noexcept
{
    return globalReverbSize.load();
}

void SVDrummerAudioProcessor::setGlobalReverbSize (float amount)
{
    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (! setHostParameterValue (globalReverbSizeParameter, value))
        globalReverbSize.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalReverbDamping() const noexcept
{
    return globalReverbDamping.load();
}

void SVDrummerAudioProcessor::setGlobalReverbDamping (float amount)
{
    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (! setHostParameterValue (globalReverbDampingParameter, value))
        globalReverbDamping.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalReverbWidth() const noexcept
{
    return globalReverbWidth.load();
}

void SVDrummerAudioProcessor::setGlobalReverbWidth (float amount)
{
    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (! setHostParameterValue (globalReverbWidthParameter, value))
        globalReverbWidth.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalReverbMix() const noexcept
{
    return globalReverbMix.load();
}

void SVDrummerAudioProcessor::setGlobalReverbMix (float amount)
{
    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (! setHostParameterValue (globalReverbMixParameter, value))
        globalReverbMix.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getGlobalReverbDuck() const noexcept
{
    return globalReverbDuck.load();
}

void SVDrummerAudioProcessor::setGlobalReverbDuck (float amount)
{
    const float value = juce::jlimit (0.0f, 1.0f, amount);

    if (! setHostParameterValue (globalReverbDuckParameter, value))
        globalReverbDuck.store (value);

    markPortableSettingsDirty();
}

float SVDrummerAudioProcessor::getMasterVolumeDb() const noexcept
{
    return masterVolumeDb.load();
}

void SVDrummerAudioProcessor::setMasterVolumeDb (float decibels)
{
    const float value = juce::jlimit (-60.0f, 6.0f, decibels);

    if (! setHostParameterValue (masterVolumeParameter, value))
        masterVolumeDb.store (value);

    markPortableSettingsDirty();
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

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].loopEnabled,
            shouldLoop ? 1.0f : 0.0f))
        return;

    pads[static_cast<std::size_t> (padIndex)].loopEnabled.store (shouldLoop);
    markPortableSettingsDirty();
}

SVDrummerAudioProcessor::PadLoopMode
SVDrummerAudioProcessor::getPadLoopMode (int padIndex) const
{
    const int value = isValidPadIndex (padIndex)
                        ? pads[static_cast<std::size_t> (padIndex)]
                              .loopMode.load()
                        : static_cast<int> (PadLoopMode::normal);
    return static_cast<PadLoopMode> (juce::jlimit (0, 1, value));
}

void SVDrummerAudioProcessor::setPadLoopMode (int padIndex, PadLoopMode mode)
{
    if (! isValidPadIndex (padIndex))
        return;

    const int value = juce::jlimit (0, 1, static_cast<int> (mode));

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)].loopMode,
            static_cast<float> (value)))
        return;

    pads[static_cast<std::size_t> (padIndex)].loopMode.store (value);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::isPadSequencerGated (int padIndex) const
{
    return isValidPadIndex (padIndex)
        && pads[static_cast<std::size_t> (padIndex)].sequencerGated.load();
}

void SVDrummerAudioProcessor::setPadSequencerGated (
    int padIndex, bool shouldBeGated)
{
    if (! isValidPadIndex (padIndex))
        return;

    if (setHostParameterValue (
            padHostParameters[static_cast<std::size_t> (padIndex)]
                .sequencerGated,
            shouldBeGated ? 1.0f : 0.0f))
        return;

    pads[static_cast<std::size_t> (padIndex)].sequencerGated.store (
        shouldBeGated);
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
    const auto padBit =
        std::uint32_t { 1 } << static_cast<unsigned int> (padIndex);
    pendingInterfaceReleases.fetch_and (~padBit);
    pendingInterfaceTriggers.fetch_or (padBit);
}

void SVDrummerAudioProcessor::releasePadFromInterface (int padIndex)
{
    if (! isValidPadIndex (padIndex))
        return;

    pendingInterfaceReleases.fetch_or (
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
    pendingPatternSelection.store (-1);
    pendingPatternGateStartNote.store (-1);
    queuedSyncedPatternSelection.store (-1);
    queuedSyncedPatternGateNote.store (-1);
    pendingPatternPlaybackSelection.store (-1);
    patternPlaybackSwitchPending.store (false);
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
    queuedSyncedPatternSelection.store (-1);
    queuedSyncedPatternGateNote.store (-1);
    sequencerEnabled.store (false);
    patternGateRestartCounter.fetch_add (1);
    markPortableSettingsDirty();
}

SVDrummerAudioProcessor::PatternSyncMode
SVDrummerAudioProcessor::getPatternSyncMode() const noexcept
{
    return static_cast<PatternSyncMode> (juce::jlimit (
        static_cast<int> (PatternSyncMode::played),
        static_cast<int> (PatternSyncMode::beat),
        patternSyncMode.load()));
}

void SVDrummerAudioProcessor::setPatternSyncMode (PatternSyncMode newMode)
{
    const int mode = juce::jlimit (
        static_cast<int> (PatternSyncMode::played),
        static_cast<int> (PatternSyncMode::beat),
        static_cast<int> (newMode));

    if (setHostParameterValue (
            patternSyncModeParameter, static_cast<float> (mode)))
        return;

    if (patternSyncMode.exchange (mode) == mode)
        return;

    if (queuedSyncedPatternSelection.load() >= 0)
    {
        if (mode == static_cast<int> (PatternSyncMode::played))
            dispatchQueuedPatternSelection();
        else
            queuedSyncedPatternBoundaryPpq.store (
                getNextPatternSyncBoundary (currentHostPpqPosition.load()));
    }

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

    refreshPatternPlaybackBars();
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

    const int value = juce::jlimit (
        0, sequencerDivisionCount - 1, divisionIndex);

    if (setHostParameterValue (
            laneHostParameters[static_cast<std::size_t> (laneIndex)].division,
            static_cast<float> (value)))
        return;

    auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
    lane.division.store (value);
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

    const int value = juce::jlimit (
        1, getLaneMaximumLoopLength (laneIndex), lengthInSteps);

    if (setHostParameterValue (
            laneHostParameters[static_cast<std::size_t> (laneIndex)].loopLength,
            static_cast<float> (value)))
        return;

    sequenceLanes[static_cast<std::size_t> (laneIndex)].loopLength.store (value);
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

int SVDrummerAudioProcessor::getPatternPlaybackStep (int stepIndex) const
{
    if (stepIndex < 0 || stepIndex >= maximumPatternPlaybackSteps)
        return -1;

    return juce::jlimit (
        -1, numberOfPatterns - 1,
        patternPlaybackSteps[static_cast<std::size_t> (stepIndex)].load());
}

void SVDrummerAudioProcessor::setPatternPlaybackStep (
    int stepIndex, int patternIndex)
{
    if (stepIndex < 0 || stepIndex >= maximumPatternPlaybackSteps)
        return;

    patternPlaybackSteps[static_cast<std::size_t> (stepIndex)].store (
        juce::jlimit (-1, numberOfPatterns - 1, patternIndex));

    if (activePatternPlaybackStep.load() == stepIndex)
        patternTimelineResetCounter.fetch_add (1);

    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getActivePatternPlaybackStep() const noexcept
{
    return activePatternPlaybackStep.load();
}

int SVDrummerAudioProcessor::getPatternPlaybackBars() const noexcept
{
    return juce::jlimit (1, maximumPatternBars, patternPlaybackBars.load());
}

bool SVDrummerAudioProcessor::isPatternPlaybackChainEnabled() const noexcept
{
    return patternPlaybackChainEnabled.load();
}

void SVDrummerAudioProcessor::setPatternPlaybackChainEnabled (
    bool shouldBeEnabled)
{
    if (setHostParameterValue (
            patternPlaybackChainEnabledParameter,
            shouldBeEnabled ? 1.0f : 0.0f))
        return;

    if (patternPlaybackChainEnabled.exchange (shouldBeEnabled)
        != shouldBeEnabled)
    {
        activePatternPlaybackStep.store (-1);
        patternTimelineResetCounter.fetch_add (1);
        markPortableSettingsDirty();
    }
}

bool SVDrummerAudioProcessor::isPatternPlaybackLoopEnabled() const noexcept
{
    return patternPlaybackLoopEnabled.load();
}

void SVDrummerAudioProcessor::setPatternPlaybackLoopEnabled (bool shouldLoop)
{
    if (setHostParameterValue (
            patternPlaybackLoopParameter, shouldLoop ? 1.0f : 0.0f))
        return;

    if (patternPlaybackLoopEnabled.exchange (shouldLoop) != shouldLoop)
        markPortableSettingsDirty();
}

SVDrummerAudioProcessor::PatternLaneState
SVDrummerAudioProcessor::captureSequenceLaneState (int laneIndex) const
{
    PatternLaneState state;

    if (! isValidPadIndex (laneIndex))
        return state;

    state.division = getLaneDivision (laneIndex);
    state.loopLength = getLaneLoopLength (laneIndex);

    for (int step = 0; step < maximumStepsPerLane; ++step)
        state.stepVelocities[static_cast<std::size_t> (step)] =
            static_cast<std::uint8_t> (
                getSequenceStepVelocity (laneIndex, step));

    return state;
}

void SVDrummerAudioProcessor::applySequenceLaneState (
    int laneIndex, const PatternLaneState& state)
{
    if (! isValidPadIndex (laneIndex))
        return;

    auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
    lane.division.store (juce::jlimit (
        0, sequencerDivisionCount - 1, state.division));
    lane.loopLength.store (juce::jlimit (
        1, getLaneMaximumLoopLength (laneIndex), state.loopLength));

    for (int step = 0; step < maximumStepsPerLane; ++step)
        lane.stepVelocities[static_cast<std::size_t> (step)].store (
            state.stepVelocities[static_cast<std::size_t> (step)]);

    lane.activeStep.store (-1);
    patternChangeCounter.fetch_add (1);
    patternTimelineResetCounter.fetch_add (1);
    markPortableSettingsDirty();
}

void SVDrummerAudioProcessor::captureSequenceLaneUndoState (int laneIndex)
{
    if (! isValidPadIndex (laneIndex))
        return;

    undoSequenceLane = captureSequenceLaneState (laneIndex);
    undoSequenceLaneIndex.store (laneIndex);
}

void SVDrummerAudioProcessor::copySequenceLane (int laneIndex)
{
    if (! isValidPadIndex (laneIndex))
        return;

    copiedSequenceLane = captureSequenceLaneState (laneIndex);
    copiedSequenceLaneAvailable.store (true);
}

bool SVDrummerAudioProcessor::canPasteSequenceLane() const noexcept
{
    return copiedSequenceLaneAvailable.load();
}

void SVDrummerAudioProcessor::pasteSequenceLane (int laneIndex)
{
    if (! isValidPadIndex (laneIndex) || ! canPasteSequenceLane())
        return;

    captureSequenceLaneUndoState (laneIndex);
    applySequenceLaneState (laneIndex, copiedSequenceLane);
}

void SVDrummerAudioProcessor::randomiseSequenceLane (int laneIndex)
{
    if (! isValidPadIndex (laneIndex))
        return;

    captureSequenceLaneUndoState (laneIndex);
    auto randomised = captureSequenceLaneState (laneIndex);
    randomised.stepVelocities.fill (0);
    const int steps = juce::jlimit (
        1, maximumStepsPerLane, randomised.loopLength);
    auto& random = juce::Random::getSystemRandom();
    bool addedStep = false;

    for (int step = 0; step < steps; ++step)
    {
        if (random.nextFloat() < 0.25f)
        {
            randomised.stepVelocities[static_cast<std::size_t> (step)] =
                static_cast<std::uint8_t> (72 + random.nextInt (56));
            addedStep = true;
        }
    }

    if (! addedStep)
    {
        const int step = random.nextInt (steps);
        randomised.stepVelocities[static_cast<std::size_t> (step)] =
            static_cast<std::uint8_t> (72 + random.nextInt (56));
    }

    applySequenceLaneState (laneIndex, randomised);
}

void SVDrummerAudioProcessor::clearSequenceLane (int laneIndex)
{
    if (! isValidPadIndex (laneIndex))
        return;

    captureSequenceLaneUndoState (laneIndex);
    auto cleared = captureSequenceLaneState (laneIndex);
    cleared.stepVelocities.fill (0);
    applySequenceLaneState (laneIndex, cleared);
}

void SVDrummerAudioProcessor::nudgeSequenceLane (int laneIndex, int direction)
{
    if (! isValidPadIndex (laneIndex) || direction == 0)
        return;

    captureSequenceLaneUndoState (laneIndex);
    const auto original = captureSequenceLaneState (laneIndex);
    auto shifted = original;
    const int steps = juce::jlimit (
        1, maximumStepsPerLane, original.loopLength);
    const int movement = direction < 0 ? -1 : 1;

    for (int sourceStep = 0; sourceStep < steps; ++sourceStep)
    {
        const int destinationStep =
            (sourceStep + movement + steps) % steps;
        shifted.stepVelocities[static_cast<std::size_t> (destinationStep)] =
            original.stepVelocities[static_cast<std::size_t> (sourceStep)];
    }

    applySequenceLaneState (laneIndex, shifted);
}

bool SVDrummerAudioProcessor::canUndoSequenceLaneOperation (
    int laneIndex) const noexcept
{
    return isValidPadIndex (laneIndex)
        && undoSequenceLaneIndex.load() == laneIndex;
}

void SVDrummerAudioProcessor::undoSequenceLaneOperation (int laneIndex)
{
    if (! canUndoSequenceLaneOperation (laneIndex))
        return;

    const auto state = undoSequenceLane;
    undoSequenceLaneIndex.store (-1);
    applySequenceLaneState (laneIndex, state);
}

void SVDrummerAudioProcessor::capturePatternPlaybackUndoState()
{
    for (int step = 0; step < maximumPatternPlaybackSteps; ++step)
        undoPatternPlaybackSteps[static_cast<std::size_t> (step)] =
            getPatternPlaybackStep (step);

    undoPatternPlaybackAvailable.store (true);
}

void SVDrummerAudioProcessor::clearPatternPlaybackChain()
{
    capturePatternPlaybackUndoState();

    for (auto& step : patternPlaybackSteps)
        step.store (-1);

    pendingPatternPlaybackSelection.store (-1);
    patternPlaybackSwitchPending.store (false);
    activePatternPlaybackStep.store (-1);
    patternTimelineResetCounter.fetch_add (1);
    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::canUndoPatternPlaybackChain() const noexcept
{
    return undoPatternPlaybackAvailable.load();
}

void SVDrummerAudioProcessor::undoPatternPlaybackChain()
{
    if (! canUndoPatternPlaybackChain())
        return;

    for (int step = 0; step < maximumPatternPlaybackSteps; ++step)
        patternPlaybackSteps[static_cast<std::size_t> (step)].store (
            undoPatternPlaybackSteps[static_cast<std::size_t> (step)]);

    pendingPatternPlaybackSelection.store (-1);
    patternPlaybackSwitchPending.store (false);
    undoPatternPlaybackAvailable.store (false);
    activePatternPlaybackStep.store (-1);
    patternTimelineResetCounter.fetch_add (1);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getCurrentPatternIndex() const
{
    return currentPatternIndex.load();
}

std::uint64_t SVDrummerAudioProcessor::getPatternChangeRevision() const
{
    return patternChangeCounter.load();
}

double SVDrummerAudioProcessor::getNextPatternSyncBoundary (
    double requestPpq) const noexcept
{
    const auto mode = getPatternSyncMode();

    if (mode == PatternSyncMode::played)
        return requestPpq;

    const double quantum = mode == PatternSyncMode::bar ? 4.0 : 1.0;
    return std::ceil (requestPpq / quantum - 1.0e-9) * quantum;
}

void SVDrummerAudioProcessor::queueSyncedPatternSelection (
    int patternIndex, int gateNote, double requestPpq)
{
    if (! isValidPatternIndex (patternIndex))
        return;

    queuedSyncedPatternGateNote.store (gateNote);
    queuedSyncedPatternBoundaryPpq.store (
        getNextPatternSyncBoundary (requestPpq));
    queuedSyncedPatternSelection.store (patternIndex);
}

void SVDrummerAudioProcessor::dispatchQueuedPatternSelection()
{
    const int patternIndex = queuedSyncedPatternSelection.exchange (-1);
    const int gateNote = queuedSyncedPatternGateNote.exchange (-1);

    if (! isValidPatternIndex (patternIndex))
        return;

    pendingPatternSelection.store (patternIndex);

    if (gateNote >= 0)
    {
        activePatternGateNote.store (gateNote);
        pendingPatternGateStartNote.store (gateNote);
    }

    triggerAsyncUpdate();
}

void SVDrummerAudioProcessor::serviceQueuedPatternSelection (
    double blockStartPpq)
{
    if (queuedSyncedPatternSelection.load() < 0)
        return;

    if (! currentHostTransportPlaying.load()
        || ! currentHostPpqAvailable.load()
        || blockStartPpq + 1.0e-9
               >= queuedSyncedPatternBoundaryPpq.load())
        dispatchQueuedPatternSelection();
}

void SVDrummerAudioProcessor::selectPattern (int newPatternIndex)
{
    if (! isValidPatternIndex (newPatternIndex))
        return;

    if (newPatternIndex == currentPatternIndex.load())
    {
        queuedSyncedPatternSelection.store (-1);
        queuedSyncedPatternGateNote.store (-1);
        return;
    }

    if (getPatternSyncMode() != PatternSyncMode::played
        && currentHostTransportPlaying.load()
        && currentHostPpqAvailable.load())
    {
        queueSyncedPatternSelection (
            newPatternIndex, -1, currentHostPpqPosition.load());
        return;
    }

    selectPatternImmediately (newPatternIndex);
}

void SVDrummerAudioProcessor::selectPatternImmediately (int newPatternIndex)
{
    if (! isValidPatternIndex (newPatternIndex))
        return;

    pendingPatternPlaybackSelection.store (-1);
    patternPlaybackSwitchPending.store (false);

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

juce::File SVDrummerAudioProcessor::getPatternFile (int patternIndex) const
{
    if (! isValidPatternIndex (patternIndex))
        return {};

    const juce::ScopedLock lock (stateLock);
    const auto& filePath =
        storedPatterns[static_cast<std::size_t> (patternIndex)].filePath;
    return filePath.isNotEmpty() ? juce::File (filePath) : juce::File();
}

juce::String SVDrummerAudioProcessor::getCurrentKitName() const
{
    const juce::ScopedLock lock (stateLock);
    return currentKitName;
}

juce::File SVDrummerAudioProcessor::getCurrentKitFile() const
{
    const juce::ScopedLock lock (stateLock);
    return currentKitFilePath.isNotEmpty()
             ? juce::File (currentKitFilePath)
             : juce::File();
}

juce::String SVDrummerAudioProcessor::getCurrentPatternSetName() const
{
    const juce::ScopedLock lock (stateLock);
    return currentPatternSetName;
}

juce::File SVDrummerAudioProcessor::getCurrentPatternSetFile() const
{
    const juce::ScopedLock lock (stateLock);
    return currentPatternSetFilePath.isNotEmpty()
             ? juce::File (currentPatternSetFilePath)
             : juce::File();
}

juce::String SVDrummerAudioProcessor::getCurrentProjectName() const
{
    const juce::ScopedLock lock (stateLock);
    return currentProjectName;
}

juce::File SVDrummerAudioProcessor::getCurrentProjectFile() const
{
    const juce::ScopedLock lock (stateLock);
    return currentProjectFilePath.isNotEmpty()
             ? juce::File (currentProjectFilePath)
             : juce::File();
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
    const int requestedPlaybackPattern =
        pendingPatternPlaybackSelection.exchange (-1);
    const int requestedGateNote = pendingPatternGateStartNote.exchange (-1);

    if (isValidPatternIndex (requestedPattern))
    {
        patternPlaybackSwitchPending.store (false);
        selectPatternImmediately (requestedPattern);
    }
    else if (isValidPatternIndex (requestedPlaybackPattern))
    {
        selectPatternFromPlaybackLane (requestedPlaybackPattern);
        patternPlaybackSwitchPending.store (false);
    }
    else
    {
        patternPlaybackSwitchPending.store (false);
    }

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

    refreshPatternPlaybackBars();
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
    refreshPatternPlaybackBars();
}

void SVDrummerAudioProcessor::applyStoredPattern (
    int patternIndex, bool restartTimeline)
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

    refreshPatternPlaybackBars();
    patternChangeCounter.fetch_add (1);

    if (restartTimeline)
        patternTimelineResetCounter.fetch_add (1);
    else
        patternPlaybackSwitchCounter.fetch_add (1);
}

void SVDrummerAudioProcessor::selectPatternFromPlaybackLane (int patternIndex)
{
    if (! isValidPatternIndex (patternIndex)
        || ! sequencerEnabled.load()
        || patternIndex == currentPatternIndex.load())
        return;

    captureCurrentPattern();

    if (! storedPatterns[static_cast<std::size_t> (patternIndex)].assigned)
        initialisePatternSlot (patternIndex);

    currentPatternIndex.store (patternIndex);
    applyStoredPattern (patternIndex, false);
    markPortableSettingsDirty();
}

int SVDrummerAudioProcessor::getPatternBarsForPlayback (
    int patternIndex) const noexcept
{
    if (! isValidPatternIndex (patternIndex))
        return 1;

    if (patternIndex == currentPatternIndex.load())
        return juce::jlimit (1, maximumPatternBars, getPatternBars());

    const int storedBars = patternPlaybackPatternBars[
        static_cast<std::size_t> (patternIndex)].load();
    return storedBars > 0
             ? juce::jlimit (1, maximumPatternBars, storedBars)
             : juce::jlimit (1, maximumPatternBars, getPatternBars());
}

void SVDrummerAudioProcessor::refreshPatternPlaybackBars()
{
    int longest = juce::jlimit (1, maximumPatternBars, getPatternBars());
    const int current = currentPatternIndex.load();

    for (int index = 0; index < numberOfPatterns; ++index)
    {
        if (index == current)
        {
            patternPlaybackPatternBars[static_cast<std::size_t> (index)].store (
                juce::jlimit (1, maximumPatternBars, getPatternBars()));
            continue;
        }

        const auto& stored = storedPatterns[static_cast<std::size_t> (index)];

        if (stored.assigned)
        {
            const int bars = juce::jlimit (
                1, maximumPatternBars, stored.bars);
            patternPlaybackPatternBars[static_cast<std::size_t> (index)].store (
                bars);
            longest = juce::jmax (
                longest, bars);
        }
        else
        {
            patternPlaybackPatternBars[static_cast<std::size_t> (index)].store (
                0);
        }
    }

    patternPlaybackBars.store (longest);
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

juce::String SVDrummerAudioProcessor::getDelaySyncDivisionName (
    int divisionIndex)
{
    static const std::array<juce::String, 12> names
    {
        "1", "1/2", "1/3", "1/4", "1/4T", "1/8", "1/8T",
        "1/16", "1/16T", "1/32", "1/32T", "1/64"
    };

    return names[static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (names.size()) - 1,
                      divisionIndex))];
}

juce::String SVDrummerAudioProcessor::getPadFilterTypeName (PadFilterType type)
{
    switch (type)
    {
        case PadFilterType::lowPass:  return "LPF";
        case PadFilterType::bandPass: return "BPF";
        case PadFilterType::highPass: return "HPF";
        case PadFilterType::comb:     return "COMB";
        case PadFilterType::formant:  return "FORMANT";
        case PadFilterType::ladder:   return "LADDER";
        case PadFilterType::notch:    return "NOTCH";
        case PadFilterType::off:
        default:                      return "OFF";
    }
}

juce::String SVDrummerAudioProcessor::getPadFilterSlopeName (int slopeIndex)
{
    static const std::array<juce::String, 4> names
    {
        "6 dB", "12 dB", "24 dB", "48 dB"
    };

    return names[static_cast<std::size_t> (juce::jlimit (0, 3, slopeIndex))];
}

juce::String SVDrummerAudioProcessor::getPadLoopModeName (PadLoopMode mode)
{
    return mode == PadLoopMode::pingPong ? "PING-PONG" : "NORMAL";
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
    const auto currentTimelineResetRevision =
        patternTimelineResetCounter.load();
    const auto currentPlaybackSwitchRevision =
        patternPlaybackSwitchCounter.load();
    const auto currentGateRestartRevision = patternGateRestartCounter.load();
    bool timelineNeedsReset = false;
    bool patternPlaybackSwitched = false;

    if (currentTimelineResetRevision != lastPatternTimelineResetCounter)
    {
        lastPatternTimelineResetCounter = currentTimelineResetRevision;
        timelineNeedsReset = true;
    }

    if (currentPlaybackSwitchRevision != lastPatternPlaybackSwitchCounter)
    {
        lastPatternPlaybackSwitchCounter = currentPlaybackSwitchRevision;
        patternPlaybackSwitched = true;
        lastSequenceAbsoluteSteps.fill (
            (std::numeric_limits<juce::int64>::min)());

        if (patternPlaybackChainSlot >= 0)
            patternPlaybackChainLengthQuarterNotes =
                static_cast<double> (getPatternBars()) * 4.0;
    }

    if (currentGateRestartRevision != lastPatternGateRestartCounter)
    {
        lastPatternGateRestartCounter = currentGateRestartRevision;
        timelineNeedsReset = true;
    }

    if (timelineNeedsReset && sequencerWasPlaying)
        releaseSequencerVoicesOnAudioThread();

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
            releaseSequencerVoicesOnAudioThread();
            resetSequencerTimeline();
        }

        for (auto& lane : sequenceLanes)
            lane.activeStep.store (-1);

        activePatternPlaybackStep.store (-1);

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
    const bool patternChainConfigured =
        isPatternPlaybackChainEnabled()
        && isValidPatternIndex (getPatternPlaybackStep (0));
    bool patternChainStopped = false;

    if (! patternChainConfigured)
    {
        patternPlaybackChainSlot = -1;
        patternPlaybackChainPositionQuarterNotes = 0.0;
        patternPlaybackChainLengthQuarterNotes = 0.0;
        activePatternPlaybackStep.store (-1);
    }

    const auto beginPatternChainSlot = [this] (int slot)
    {
        const int requestedPattern = getPatternPlaybackStep (slot);

        if (! isValidPatternIndex (requestedPattern))
            return false;

        patternPlaybackChainSlot = slot;
        patternPlaybackChainPositionQuarterNotes = 0.0;
        patternPlaybackChainLengthQuarterNotes =
            static_cast<double> (
                getPatternBarsForPlayback (requestedPattern)) * 4.0;
        activePatternPlaybackStep.store (slot);
        lastSequenceAbsoluteSteps.fill (
            (std::numeric_limits<juce::int64>::min)());
        releaseSequencerVoicesOnAudioThread();

        if (requestedPattern != currentPatternIndex.load())
        {
            pendingPatternPlaybackSelection.store (requestedPattern);
            patternPlaybackSwitchPending.store (true);
            triggerAsyncUpdate();
        }

        return true;
    };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double hostPpq = blockStartPpq
                             + static_cast<double> (sample)
                                   * quarterNotesPerSample;
        double sequencePpq = hostPpq;
        bool forceLaneTrigger =
            (forceInitialTrigger || patternPlaybackSwitched) && sample == 0;

        if (patternChainConfigured)
        {
            bool beganNewSlot = false;

            if (patternPlaybackChainSlot < 0)
            {
                beganNewSlot = beginPatternChainSlot (0);
            }
            else if (! patternPlaybackSwitchPending.load()
                     && patternPlaybackChainPositionQuarterNotes + 1.0e-10
                            >= patternPlaybackChainLengthQuarterNotes)
            {
                int nextSlot = patternPlaybackChainSlot + 1;
                const bool reachedChainEnd =
                    nextSlot >= maximumPatternPlaybackSteps
                    || ! isValidPatternIndex (
                        getPatternPlaybackStep (nextSlot));

                if (reachedChainEnd
                    && isPatternPlaybackLoopEnabled()
                    && isValidPatternIndex (getPatternPlaybackStep (0)))
                {
                    nextSlot = 0;
                }
                else if (reachedChainEnd)
                {
                    sequencerEnabled.store (false);
                    releaseSequencerVoicesOnAudioThread();

                    for (auto& lane : sequenceLanes)
                        lane.activeStep.store (-1);

                    activePatternPlaybackStep.store (-1);
                    patternPlaybackChainSlot = -1;
                    patternPlaybackChainPositionQuarterNotes = 0.0;
                    patternPlaybackChainLengthQuarterNotes = 0.0;
                    sequencerPatternPositionQuarterNotes.store (0.0);
                    patternChainStopped = true;
                    markPortableSettingsDirty();
                    break;
                }

                beganNewSlot = beginPatternChainSlot (nextSlot);
            }

            forceLaneTrigger = forceLaneTrigger || beganNewSlot;

            if (patternPlaybackSwitchPending.load())
                continue;

            sequencePpq = patternPlaybackChainPositionQuarterNotes;
        }

        if (patternPlaybackSwitchPending.load())
            continue;

        for (int laneIndex = 0; laneIndex < numberOfPads; ++laneIndex)
        {
            auto& lane = sequenceLanes[static_cast<std::size_t> (laneIndex)];
            const double stepLength = getSequencerQuarterNotesPerStep (lane.division.load());
            const auto absoluteStep = static_cast<juce::int64> (
                std::floor ((sequencePpq + 1.0e-10) / stepLength));
            auto& lastStep = lastSequenceAbsoluteSteps[static_cast<std::size_t> (laneIndex)];

            if (! forceLaneTrigger && absoluteStep == lastStep)
                continue;

            const bool hadPreviousStep = lastStep
                != (std::numeric_limits<juce::int64>::min)();

            if (hadPreviousStep
                && pads[static_cast<std::size_t> (laneIndex)]
                       .sequencerGated.load())
            {
                schedulePadSequencerGateRelease (laneIndex, sample);
            }

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

        if (patternChainConfigured)
            patternPlaybackChainPositionQuarterNotes += quarterNotesPerSample;
    }

    if (patternChainStopped)
    {
        sequencerWasPlaying = false;
        return;
    }

    const double blockEndPpq = blockStartPpq
                             + static_cast<double> (numSamples) * quarterNotesPerSample;
    fallbackSequencerPpq = hostPpqAvailable ? blockEndPpq : fallbackSequencerPpq
                                                              + static_cast<double> (numSamples)
                                                                    * quarterNotesPerSample;

    double patternPosition = patternPlaybackChainPositionQuarterNotes;

    if (! patternChainConfigured)
    {
        const double patternLength =
            static_cast<double> (getPatternBars()) * 4.0;
        patternPosition = std::fmod (blockEndPpq, patternLength);

        if (patternPosition < 0.0)
            patternPosition += patternLength;
    }

    sequencerPatternPositionQuarterNotes.store (patternPosition);
    sequencerWasPlaying = true;
}

void SVDrummerAudioProcessor::resetSequencerTimeline()
{
    lastSequenceAbsoluteSteps.fill ((std::numeric_limits<juce::int64>::min)());
    patternPlaybackChainSlot = -1;
    patternPlaybackChainPositionQuarterNotes = 0.0;
    patternPlaybackChainLengthQuarterNotes = 0.0;
    fallbackSequencerPpq = 0.0;
    sequencerWasPlaying = false;

    for (auto& lane : sequenceLanes)
        lane.activeStep.store (-1);

    activePatternPlaybackStep.store (-1);
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
    return getEditorViewIndex() == 1;
}

void SVDrummerAudioProcessor::setEditorShowingPadSettings (bool shouldShow) noexcept
{
    setEditorViewIndex (shouldShow ? 1 : 0);
}

int SVDrummerAudioProcessor::getEditorViewIndex() const noexcept
{
    return juce::jlimit (0, 2, editorViewIndex.load());
}

void SVDrummerAudioProcessor::setEditorViewIndex (int viewIndex) noexcept
{
    editorViewIndex.store (juce::jlimit (0, 2, viewIndex));
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

std::unique_ptr<juce::XmlElement>
SVDrummerAudioProcessor::createGlobalFxXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("GLOBAL_FX");
    xml->setAttribute ("delayEnabled", isGlobalDelayEnabled());
    xml->setAttribute ("delaySyncEnabled", isGlobalDelaySyncEnabled());
    xml->setAttribute ("delaySyncDivision", getGlobalDelaySyncDivision());
    xml->setAttribute ("delayTimeMs",
                       static_cast<double> (getGlobalDelayTimeMs()));
    xml->setAttribute ("delayFeedback",
                       static_cast<double> (getGlobalDelayFeedback()));
    xml->setAttribute ("delayMix",
                       static_cast<double> (getGlobalDelayMix()));
    xml->setAttribute ("delayDuck",
                       static_cast<double> (getGlobalDelayDuck()));
    xml->setAttribute ("delayDuckAttackMs",
                       static_cast<double> (getGlobalDelayDuckAttackMs()));
    xml->setAttribute ("delayDuckReleaseMs",
                       static_cast<double> (getGlobalDelayDuckReleaseMs()));
    xml->setAttribute ("reverbEnabled", isGlobalReverbEnabled());
    xml->setAttribute ("reverbSize",
                       static_cast<double> (getGlobalReverbSize()));
    xml->setAttribute ("reverbDamping",
                       static_cast<double> (getGlobalReverbDamping()));
    xml->setAttribute ("reverbWidth",
                       static_cast<double> (getGlobalReverbWidth()));
    xml->setAttribute ("reverbMix",
                       static_cast<double> (getGlobalReverbMix()));
    xml->setAttribute ("reverbDuck",
                       static_cast<double> (getGlobalReverbDuck()));
    xml->setAttribute ("masterVolumeDb",
                       static_cast<double> (getMasterVolumeDb()));
    return xml;
}

void SVDrummerAudioProcessor::loadGlobalFxXml (
    const juce::XmlElement* globalFxXml)
{
    if (globalFxXml == nullptr || ! globalFxXml->hasTagName ("GLOBAL_FX"))
    {
        setGlobalDelayEnabled (false);
        setGlobalDelaySyncEnabled (false);
        setGlobalDelaySyncDivision (5);
        setGlobalDelayTimeMs (250.0f);
        setGlobalDelayFeedback (0.35f);
        setGlobalDelayMix (0.25f);
        setGlobalDelayDuck (0.0f);
        setGlobalDelayDuckAttackMs (10.0f);
        setGlobalDelayDuckReleaseMs (250.0f);
        setGlobalReverbEnabled (false);
        setGlobalReverbSize (0.50f);
        setGlobalReverbDamping (0.50f);
        setGlobalReverbWidth (1.0f);
        setGlobalReverbMix (0.20f);
        setGlobalReverbDuck (0.0f);
        setMasterVolumeDb (0.0f);
        return;
    }

    setGlobalDelayEnabled (
        globalFxXml->getBoolAttribute ("delayEnabled", false));
    setGlobalDelaySyncEnabled (
        globalFxXml->getBoolAttribute ("delaySyncEnabled", false));
    setGlobalDelaySyncDivision (
        globalFxXml->getIntAttribute ("delaySyncDivision", 5));
    setGlobalDelayTimeMs (static_cast<float> (
        globalFxXml->getDoubleAttribute ("delayTimeMs", 250.0)));
    setGlobalDelayFeedback (static_cast<float> (
        globalFxXml->getDoubleAttribute ("delayFeedback", 0.35)));
    setGlobalDelayMix (static_cast<float> (
        globalFxXml->getDoubleAttribute ("delayMix", 0.25)));
    setGlobalDelayDuck (static_cast<float> (
        globalFxXml->getDoubleAttribute ("delayDuck", 0.0)));
    setGlobalDelayDuckAttackMs (static_cast<float> (
        globalFxXml->getDoubleAttribute ("delayDuckAttackMs", 10.0)));
    setGlobalDelayDuckReleaseMs (static_cast<float> (
        globalFxXml->getDoubleAttribute ("delayDuckReleaseMs", 250.0)));
    setGlobalReverbEnabled (
        globalFxXml->getBoolAttribute ("reverbEnabled", false));
    setGlobalReverbSize (static_cast<float> (
        globalFxXml->getDoubleAttribute ("reverbSize", 0.50)));
    setGlobalReverbDamping (static_cast<float> (
        globalFxXml->getDoubleAttribute ("reverbDamping", 0.50)));
    setGlobalReverbWidth (static_cast<float> (
        globalFxXml->getDoubleAttribute ("reverbWidth", 1.0)));
    setGlobalReverbMix (static_cast<float> (
        globalFxXml->getDoubleAttribute ("reverbMix", 0.20)));
    setGlobalReverbDuck (static_cast<float> (
        globalFxXml->getDoubleAttribute ("reverbDuck", 0.0)));
    setMasterVolumeDb (static_cast<float> (
        globalFxXml->getDoubleAttribute ("masterVolumeDb", 0.0)));
}

std::unique_ptr<juce::XmlElement> SVDrummerAudioProcessor::createKitXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("SVDRUMMER_KIT");
    xml->setAttribute ("version", "1.0");

    const juce::ScopedLock lock (stateLock);
    xml->setAttribute ("name", currentKitName);

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
        padXml->setAttribute ("outputBus", pad.outputBus.load());
        padXml->setAttribute ("delaySend", static_cast<double> (
            pad.delaySend.load()));
        padXml->setAttribute ("reverbSend", static_cast<double> (
            pad.reverbSend.load()));
        padXml->setAttribute ("chokeGroup", pad.chokeGroup.load());
        padXml->setAttribute ("ampCurve", static_cast<double> (
            pad.ampCurve.load()));
        padXml->setAttribute ("ampAttackMs", static_cast<double> (pad.ampAttackMs.load()));
        padXml->setAttribute ("ampDecayMs", static_cast<double> (pad.ampDecayMs.load()));
        padXml->setAttribute ("ampSustain", static_cast<double> (pad.ampSustain.load()));
        padXml->setAttribute ("ampReleaseMs", static_cast<double> (pad.ampReleaseMs.load()));
        padXml->setAttribute ("filterCutoffHz", static_cast<double> (
            pad.filterCutoffHz.load()));
        padXml->setAttribute ("filterResonance", static_cast<double> (
            pad.filterResonance.load()));
        padXml->setAttribute ("filterDriveDb", static_cast<double> (
            pad.filterDriveDb.load()));
        padXml->setAttribute ("filterEnabled", pad.filterEnabled.load());
        padXml->setAttribute ("filterType", pad.filterType.load());
        padXml->setAttribute ("filterSlope", pad.filterSlope.load());
        padXml->setAttribute ("highPassCutoffHz", static_cast<double> (
            pad.highPassCutoffHz.load()));
        padXml->setAttribute ("compressorEnabled",
                              pad.compressorEnabled.load());
        padXml->setAttribute ("compressorThresholdDb", static_cast<double> (
            pad.compressorThresholdDb.load()));
        padXml->setAttribute ("compressorRatio", static_cast<double> (
            pad.compressorRatio.load()));
        padXml->setAttribute ("compressorAttackMs", static_cast<double> (
            pad.compressorAttackMs.load()));
        padXml->setAttribute ("compressorReleaseMs", static_cast<double> (
            pad.compressorReleaseMs.load()));
        padXml->setAttribute ("compressorKneeDb", static_cast<double> (
            pad.compressorKneeDb.load()));
        padXml->setAttribute ("compressorGainDb", static_cast<double> (
            pad.compressorGainDb.load()));
        padXml->setAttribute ("saturationAmount", static_cast<double> (
            pad.saturationAmount.load()));
        padXml->setAttribute ("saturationHardClip", static_cast<double> (
            pad.saturationHardClipAmount.load()));
        padXml->setAttribute ("saturationEnabled",
                              pad.saturationEnabled.load());
        padXml->setAttribute ("startSample", pad.sampleStart.load());
        padXml->setAttribute ("endSample", pad.sampleEnd.load());
        padXml->setAttribute ("loopEnabled", pad.loopEnabled.load());
        padXml->setAttribute ("loopMode", pad.loopMode.load());
        padXml->setAttribute ("sequencerGated",
                              pad.sequencerGated.load());
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
        pad.outputBus = juce::jlimit (
            0, numberOfPadOutputBuses,
            item->getIntAttribute ("outputBus", 0));
        pad.delaySend = juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "delaySend", 0.0)));
        pad.reverbSend = juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "reverbSend", 0.0)));
        pad.chokeGroup = juce::jlimit (
            0, numberOfPads, item->getIntAttribute ("chokeGroup", 0));
        pad.ampCurve = juce::jlimit (
            -1.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute ("ampCurve", 0.0)));
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
        pad.filterCutoffHz = juce::jlimit (
            20.0f, 20000.0f,
            static_cast<float> (item->getDoubleAttribute (
                "filterCutoffHz", 20000.0)));
        pad.filterResonance = juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "filterResonance", 0.0)));
        pad.filterDriveDb = juce::jlimit (
            0.0f, 24.0f,
            static_cast<float> (item->getDoubleAttribute (
                "filterDriveDb", 0.0)));
        pad.filterEnabled = item->getBoolAttribute (
            "filterEnabled", false);
        pad.filterType = juce::jlimit (
            1, 7, item->getIntAttribute ("filterType", 1));
        pad.filterSlope = juce::jlimit (
            0, 3, item->getIntAttribute ("filterSlope", 1));
        pad.highPassCutoffHz = juce::jlimit (
            0.0f, 2000.0f,
            static_cast<float> (item->getDoubleAttribute (
                "highPassCutoffHz", 0.0)));
        pad.compressorEnabled = item->getBoolAttribute (
            "compressorEnabled", false);
        pad.compressorThresholdDb = juce::jlimit (
            -60.0f, 0.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorThresholdDb", -18.0)));
        pad.compressorRatio = juce::jlimit (
            1.0f, 20.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorRatio", 4.0)));
        pad.compressorAttackMs = juce::jlimit (
            0.1f, 100.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorAttackMs", 10.0)));
        pad.compressorReleaseMs = juce::jlimit (
            10.0f, 1000.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorReleaseMs", 100.0)));
        pad.compressorKneeDb = juce::jlimit (
            0.0f, 24.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorKneeDb", 6.0)));
        pad.compressorGainDb = juce::jlimit (
            -24.0f, 24.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorGainDb", 0.0)));
        pad.saturationAmount = juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "saturationAmount", 0.0)));
        pad.saturationHardClipAmount = juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "saturationHardClip", 0.0)));
        pad.saturationEnabled = item->getBoolAttribute (
            "saturationEnabled", false);
        pad.loopEnabled = item->getBoolAttribute ("loopEnabled", false);
        pad.loopMode = juce::jlimit (
            0, 1, item->getIntAttribute ("loopMode", 0));
        pad.sequencerGated = item->getBoolAttribute (
            "sequencerGated", false);

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

    currentKitName = kitXml.getStringAttribute ("name", "New Kit");
    currentKitFilePath.clear();

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
        targetPad.outputBus.store (sourcePad.outputBus);
        targetPad.delaySend.store (sourcePad.delaySend);
        targetPad.reverbSend.store (sourcePad.reverbSend);
        targetPad.chokeGroup.store (sourcePad.chokeGroup);
        targetPad.ampCurve.store (sourcePad.ampCurve);
        targetPad.ampAttackMs.store (sourcePad.ampAttackMs);
        targetPad.ampDecayMs.store (sourcePad.ampDecayMs);
        targetPad.ampSustain.store (sourcePad.ampSustain);
        targetPad.ampReleaseMs.store (sourcePad.ampReleaseMs);
        targetPad.filterCutoffHz.store (sourcePad.filterCutoffHz);
        targetPad.filterResonance.store (sourcePad.filterResonance);
        targetPad.filterDriveDb.store (sourcePad.filterDriveDb);
        targetPad.filterEnabled.store (sourcePad.filterEnabled);
        targetPad.filterType.store (sourcePad.filterType);
        targetPad.filterSlope.store (sourcePad.filterSlope);
        targetPad.highPassCutoffHz.store (sourcePad.highPassCutoffHz);
        targetPad.compressorEnabled.store (sourcePad.compressorEnabled);
        targetPad.compressorThresholdDb.store (sourcePad.compressorThresholdDb);
        targetPad.compressorRatio.store (sourcePad.compressorRatio);
        targetPad.compressorAttackMs.store (sourcePad.compressorAttackMs);
        targetPad.compressorReleaseMs.store (sourcePad.compressorReleaseMs);
        targetPad.compressorKneeDb.store (sourcePad.compressorKneeDb);
        targetPad.compressorGainDb.store (sourcePad.compressorGainDb);
        targetPad.saturationAmount.store (sourcePad.saturationAmount);
        targetPad.saturationHardClipAmount.store (
            sourcePad.saturationHardClipAmount);
        targetPad.saturationEnabled.store (sourcePad.saturationEnabled);
        targetPad.sampleStart.store (sourcePad.sampleStart);
        targetPad.sampleEnd.store (sourcePad.sampleEnd);
        targetPad.loopEnabled.store (sourcePad.loopEnabled);
        targetPad.loopMode.store (sourcePad.loopMode);
        targetPad.sequencerGated.store (sourcePad.sequencerGated);
        targetPad.loopStart.store (sourcePad.loopStart);
        targetPad.loopEnd.store (sourcePad.loopEnd);
    }

    patternChangeCounter.fetch_add (1);
    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::saveKitToFile (
    const juce::File& file)
{
    const auto xml = createKitXml();
    const auto savedName = file.getFileNameWithoutExtension();
    xml->setAttribute ("name", savedName);

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The kit folder could not be created.");

    if (! file.replaceWithText (xml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The kit file could not be written.");

    {
        const juce::ScopedLock lock (stateLock);
        currentKitName = savedName;
        currentKitFilePath = file.getFullPathName();
    }

    markPortableSettingsDirty();
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
        {
            const juce::ScopedLock lock (stateLock);
            currentKitName = file.getFileNameWithoutExtension();
            currentKitFilePath = file.getFullPathName();
        }

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
    xml->setAttribute ("version", "1.3");
    xml->setAttribute ("type", "pattern-set");
    xml->setAttribute ("current", getCurrentPatternIndex());

    {
        const juce::ScopedLock lock (stateLock);
        xml->setAttribute ("name", currentPatternSetName);
    }

    auto* playbackXml = xml->createNewChildElement ("PATTERN_PLAYBACK");
    playbackXml->setAttribute ("steps", encodePatternPlaybackSteps());
    playbackXml->setAttribute (
        "enabled", isPatternPlaybackChainEnabled());
    playbackXml->setAttribute (
        "loop", isPatternPlaybackLoopEnabled());

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

    const auto loadedPatternSetName = patternSetXml.getStringAttribute (
        "name", "New Pattern Set");

    pendingPatternPlaybackSelection.store (-1);
    patternPlaybackSwitchPending.store (false);

    std::array<StoredPattern, numberOfPatterns> loadedPatterns;
    std::array<int, numberOfPatterns> loadedMidiNotes;
    loadedMidiNotes.fill (-1);
    const auto encodedPlaybackSteps =
        patternSetXml.getChildByName ("PATTERN_PLAYBACK") != nullptr
            ? patternSetXml.getChildByName ("PATTERN_PLAYBACK")
                  ->getStringAttribute ("steps")
            : juce::String();
    const bool loadedPlaybackLoop =
        patternSetXml.getChildByName ("PATTERN_PLAYBACK") != nullptr
            && patternSetXml.getChildByName ("PATTERN_PLAYBACK")
                   ->getBoolAttribute ("loop", false);
    const bool loadedPlaybackEnabled =
        patternSetXml.getChildByName ("PATTERN_PLAYBACK") == nullptr
            || patternSetXml.getChildByName ("PATTERN_PLAYBACK")
                   ->getBoolAttribute ("enabled", true);

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
    decodePatternPlaybackSteps (encodedPlaybackSteps);
    patternPlaybackChainEnabled.store (loadedPlaybackEnabled);
    patternPlaybackLoopEnabled.store (loadedPlaybackLoop);

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
    refreshPatternPlaybackBars();

    {
        const juce::ScopedLock lock (stateLock);
        currentPatternSetName = loadedPatternSetName;
        currentPatternSetFilePath.clear();
    }

    return juce::Result::ok();
}

juce::Result SVDrummerAudioProcessor::saveStoredPatternToFile (
    int patternIndex, const juce::File& file)
{
    const auto xml = createPatternXml (patternIndex);

    if (xml == nullptr)
        return juce::Result::fail ("Invalid pattern slot.");

    const auto savedName = file.getFileNameWithoutExtension();
    xml->setAttribute ("name", savedName);

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The pattern folder could not be created.");

    if (! file.replaceWithText (xml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The pattern file could not be written.");

    {
        const juce::ScopedLock lock (stateLock);
        auto& stored = storedPatterns[static_cast<std::size_t> (patternIndex)];
        stored.name = savedName;
        stored.filePath = file.getFullPathName();
    }

    markPortableSettingsDirty();
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

    loaded.name = file.getFileNameWithoutExtension();
    loaded.filePath = file.getFullPathName();

    captureCurrentPattern();
    capturePatternUndoState (patternIndex);
    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (loaded);

    if (patternIndex == currentPatternIndex.load())
        applyStoredPattern (patternIndex);
    else
        refreshPatternPlaybackBars();

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
    pasted.filePath.clear();
    storedPatterns[static_cast<std::size_t> (patternIndex)] = std::move (pasted);

    if (patternIndex == currentPatternIndex.load())
        applyStoredPattern (patternIndex);
    else
        refreshPatternPlaybackBars();

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
    else
        refreshPatternPlaybackBars();

    markPortableSettingsDirty();
}

bool SVDrummerAudioProcessor::patternSetHasSteps() const
{
    for (int step = 0; step < maximumPatternPlaybackSteps; ++step)
        if (getPatternPlaybackStep (step) >= 0)
            return true;

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
    const auto savedName = file.getFileNameWithoutExtension();
    xml->setAttribute ("name", savedName);

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The Pattern Set folder could not be created.");

    if (! file.replaceWithText (xml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The Pattern Set file could not be written.");

    {
        const juce::ScopedLock lock (stateLock);
        currentPatternSetName = savedName;
        currentPatternSetFilePath = file.getFullPathName();
    }

    markPortableSettingsDirty();
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
    {
        {
            const juce::ScopedLock lock (stateLock);
            currentPatternSetName = file.getFileNameWithoutExtension();
            currentPatternSetFilePath = file.getFullPathName();
        }

        // Loading a library Pattern Set must always leave transport stopped.
        // This also clears any pending GATE or HOLD trigger state.
        stopPatternMidiPlayback();
        markPortableSettingsDirty();
    }

    return result;
}

juce::Result SVDrummerAudioProcessor::saveProjectToFile (
    const juce::File& file)
{
    captureCurrentPattern();
    auto projectXml = std::make_unique<juce::XmlElement> ("SVDRUMMER_PROJECT");
    const auto savedName = file.getFileNameWithoutExtension();
    projectXml->setAttribute ("version", "1.0");
    projectXml->setAttribute ("type", "project");
    projectXml->setAttribute ("name", savedName);
    const auto midiMode = getPatternMidiMode();
    projectXml->setAttribute ("midiMode", patternMidiModeToString (midiMode));
    projectXml->setAttribute (
        "syncMode", patternSyncModeToString (getPatternSyncMode()));
    // Project files store the setup, not a command to begin playback.
    projectXml->setAttribute ("sequencerEnabled", false);
    projectXml->setAttribute ("markerSnap", isSampleMarkerSnapEnabled());

    if (auto kitXml = createKitXml())
        projectXml->addChildElement (kitXml.release());

    if (auto patternSetXml = createPatternSetXml())
        projectXml->addChildElement (patternSetXml.release());

    if (auto globalFxXml = createGlobalFxXml())
        projectXml->addChildElement (globalFxXml.release());

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("The Projects folder could not be created.");

    if (! file.replaceWithText (projectXml->toString(), false, false, "\r\n"))
        return juce::Result::fail ("The Project file could not be written.");

    {
        const juce::ScopedLock lock (stateLock);
        currentProjectName = savedName;
        currentProjectFilePath = file.getFullPathName();
    }

    markPortableSettingsDirty();
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
    auto* globalFxXml = projectXml->getChildByName ("GLOBAL_FX");

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

    loadGlobalFxXml (globalFxXml);

    // A Project and its samples are often moved together. Try the Project
    // folder first so those samples can be restored without prompting.
    relinkMissingSamplesFromFolder (file.getParentDirectory(), false);

    const auto midiMode = patternMidiModeFromString (
        projectXml->getStringAttribute ("midiMode", "Manual"));
    setPatternMidiMode (midiMode);
    setPatternSyncMode (patternSyncModeFromString (
        projectXml->getStringAttribute ("syncMode", "Played")));
    // Deliberately ignore the legacy sequencerEnabled value. Loading any
    // Project must leave transport stopped, including a file saved with "1".
    stopPatternMidiPlayback();
    setSampleMarkerSnapEnabled (
        projectXml->getBoolAttribute ("markerSnap", false));

    {
        const juce::ScopedLock lock (stateLock);
        currentProjectName = file.getFileNameWithoutExtension();
        currentProjectFilePath = file.getFullPathName();
    }

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

juce::String SVDrummerAudioProcessor::encodePatternPlaybackSteps() const
{
    juce::String encoded;

    for (int step = 0; step < maximumPatternPlaybackSteps; ++step)
    {
        const int patternIndex = getPatternPlaybackStep (step);

        if (! isValidPatternIndex (patternIndex))
            continue;

        if (encoded.isNotEmpty())
            encoded << ";";

        encoded << step << ":" << patternIndex + 1;
    }

    return encoded;
}

void SVDrummerAudioProcessor::decodePatternPlaybackSteps (
    const juce::String& encodedSteps)
{
    for (auto& step : patternPlaybackSteps)
        step.store (-1);

    juce::StringArray entries;
    entries.addTokens (encodedSteps, ";", {});

    for (const auto& entry : entries)
    {
        const int separator = entry.indexOfChar (':');

        if (separator <= 0)
            continue;

        const int step = entry.substring (0, separator).getIntValue();
        const int patternIndex =
            entry.substring (separator + 1).getIntValue() - 1;

        if (step >= 0 && step < maximumPatternPlaybackSteps
            && isValidPatternIndex (patternIndex))
            patternPlaybackSteps[static_cast<std::size_t> (step)].store (
                patternIndex);
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
    state.setAttribute ("version", "13.0.0");
    state.setAttribute ("markerSnap", isSampleMarkerSnapEnabled());

    {
        const juce::ScopedLock lock (stateLock);
        state.setAttribute ("kitName", currentKitName);
        state.setAttribute (
            "kitFile", currentKitFilePath.isNotEmpty()
                           ? makeStoredPath (juce::File (currentKitFilePath))
                           : juce::String());
        state.setAttribute ("patternSetName", currentPatternSetName);
        state.setAttribute (
            "patternSetFile", currentPatternSetFilePath.isNotEmpty()
                                  ? makeStoredPath (
                                        juce::File (currentPatternSetFilePath))
                                  : juce::String());
        state.setAttribute ("projectName", currentProjectName);
        state.setAttribute (
            "projectFile", currentProjectFilePath.isNotEmpty()
                               ? makeStoredPath (
                                     juce::File (currentProjectFilePath))
                               : juce::String());
    }

    auto* sequencer = state.createNewChildElement ("SEQUENCER");
    const auto midiMode = getPatternMidiMode();
    sequencer->setAttribute ("midiMode", patternMidiModeToString (midiMode));
    sequencer->setAttribute (
        "syncMode", patternSyncModeToString (getPatternSyncMode()));
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
        item->setAttribute ("outputBus", getPadOutputBus (padIndex));
        item->setAttribute ("delaySend", static_cast<double> (
            getPadDelaySend (padIndex)));
        item->setAttribute ("reverbSend", static_cast<double> (
            getPadReverbSend (padIndex)));
        item->setAttribute ("chokeGroup", getPadChokeGroup (padIndex));
        item->setAttribute ("ampCurve", static_cast<double> (
            getPadAmpCurve (padIndex)));
        item->setAttribute ("ampAttackMs", static_cast<double> (getPadAmpAttackMs (padIndex)));
        item->setAttribute ("ampDecayMs", static_cast<double> (getPadAmpDecayMs (padIndex)));
        item->setAttribute ("ampSustain", static_cast<double> (getPadAmpSustain (padIndex)));
        item->setAttribute ("ampReleaseMs", static_cast<double> (getPadAmpReleaseMs (padIndex)));
        item->setAttribute ("filterCutoffHz", static_cast<double> (
            getPadFilterCutoffHz (padIndex)));
        item->setAttribute ("filterResonance", static_cast<double> (
            getPadFilterResonance (padIndex)));
        item->setAttribute ("filterDriveDb", static_cast<double> (
            getPadFilterDriveDb (padIndex)));
        item->setAttribute ("filterEnabled", isPadFilterEnabled (padIndex));
        item->setAttribute ("filterType", static_cast<int> (
            getPadFilterType (padIndex)));
        item->setAttribute ("filterSlope",
                            getPadFilterSlopeIndex (padIndex));
        item->setAttribute ("highPassCutoffHz", static_cast<double> (
            getPadHighPassCutoffHz (padIndex)));
        item->setAttribute ("compressorEnabled",
                            isPadCompressorEnabled (padIndex));
        item->setAttribute ("compressorThresholdDb", static_cast<double> (
            getPadCompressorThresholdDb (padIndex)));
        item->setAttribute ("compressorRatio", static_cast<double> (
            getPadCompressorRatio (padIndex)));
        item->setAttribute ("compressorAttackMs", static_cast<double> (
            getPadCompressorAttackMs (padIndex)));
        item->setAttribute ("compressorReleaseMs", static_cast<double> (
            getPadCompressorReleaseMs (padIndex)));
        item->setAttribute ("compressorKneeDb", static_cast<double> (
            getPadCompressorKneeDb (padIndex)));
        item->setAttribute ("compressorGainDb", static_cast<double> (
            getPadCompressorGainDb (padIndex)));
        item->setAttribute ("saturationAmount", static_cast<double> (
            getPadSaturationAmount (padIndex)));
        item->setAttribute ("saturationHardClip", static_cast<double> (
            getPadSaturationHardClipAmount (padIndex)));
        item->setAttribute ("saturationEnabled",
                            isPadSaturationEnabled (padIndex));
        item->setAttribute ("startSample", getPadSampleStart (padIndex));
        item->setAttribute ("endSample", getPadSampleEnd (padIndex));
        item->setAttribute ("loopEnabled", isPadLoopEnabled (padIndex));
        item->setAttribute (
            "loopMode", static_cast<int> (getPadLoopMode (padIndex)));
        item->setAttribute ("sequencerGated",
                            isPadSequencerGated (padIndex));
        item->setAttribute ("loopStartSample", getPadLoopStart (padIndex));
        item->setAttribute ("loopEndSample", getPadLoopEnd (padIndex));

        const auto path = getPadSamplePath (padIndex);
        item->setAttribute ("sample", path.isNotEmpty()
                                       ? makeStoredPath (juce::File (path))
                                       : juce::String());
    }

    auto* patternsXml = state.createNewChildElement ("PATTERNS");
    patternsXml->setAttribute ("current", getCurrentPatternIndex());
    auto* playbackXml = patternsXml->createNewChildElement (
        "PATTERN_PLAYBACK");
    playbackXml->setAttribute ("steps", encodePatternPlaybackSteps());
    playbackXml->setAttribute (
        "enabled", isPatternPlaybackChainEnabled());
    playbackXml->setAttribute (
        "loop", isPatternPlaybackLoopEnabled());

    for (int patternIndex = 0; patternIndex < numberOfPatterns; ++patternIndex)
    {
        auto* slotXml = patternsXml->createNewChildElement ("SLOT");
        slotXml->setAttribute ("index", patternIndex);
        slotXml->setAttribute ("assigned", isPatternAssigned (patternIndex));
        slotXml->setAttribute ("midiNote", getPatternMidiNote (patternIndex));
        const auto patternFile = getPatternFile (patternIndex);
        slotXml->setAttribute (
            "file", patternFile != juce::File()
                        ? makeStoredPath (patternFile)
                        : juce::String());

        if (isPatternAssigned (patternIndex))
            if (auto patternXml = createPatternXml (patternIndex))
                slotXml->addChildElement (patternXml.release());
    }

    if (auto globalFxXml = createGlobalFxXml())
        state.addChildElement (globalFxXml.release());

    copyXmlToBinary (state, destData);
}

void SVDrummerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = getXmlFromBinary (data, sizeInBytes);

    if (state == nullptr || ! state->hasTagName ("SVDRUMMER_STATE"))
        return;

    restoringHostState.store (true);
    pendingPatternSelection.store (-1);
    queuedSyncedPatternSelection.store (-1);
    queuedSyncedPatternGateNote.store (-1);
    pendingPatternPlaybackSelection.store (-1);
    patternPlaybackSwitchPending.store (false);

    sampleMarkerSnapEnabled.store (
        state->getBoolAttribute ("markerSnap", false));

    {
        const juce::ScopedLock lock (stateLock);
        currentKitName = state->getStringAttribute ("kitName", "New Kit");
        const auto storedKitFile = state->getStringAttribute ("kitFile");
        currentKitFilePath = storedKitFile.isNotEmpty()
                               ? resolveStoredPath (storedKitFile).getFullPathName()
                               : juce::String();
        currentPatternSetName = state->getStringAttribute (
            "patternSetName", "New Pattern Set");
        const auto storedPatternSetFile = state->getStringAttribute (
            "patternSetFile");
        currentPatternSetFilePath = storedPatternSetFile.isNotEmpty()
                                      ? resolveStoredPath (
                                            storedPatternSetFile).getFullPathName()
                                      : juce::String();
        currentProjectName = state->getStringAttribute (
            "projectName", "New Project");
        const auto storedProjectFile = state->getStringAttribute (
            "projectFile");
        currentProjectFilePath = storedProjectFile.isNotEmpty()
                                   ? resolveStoredPath (
                                         storedProjectFile).getFullPathName()
                                   : juce::String();
    }

    if (auto* sequencer = state->getChildByName ("SEQUENCER"))
    {
        patternMidiMode.store (static_cast<int> (patternMidiModeFromString (
            sequencer->getStringAttribute ("midiMode", "Manual"))));
        patternSyncMode.store (static_cast<int> (patternSyncModeFromString (
            sequencer->getStringAttribute ("syncMode", "Played"))));
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
        pad.outputBus.store (juce::jlimit (
            0, numberOfPadOutputBuses,
            item->getIntAttribute ("outputBus", 0)));
        pad.delaySend.store (juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "delaySend", 0.0))));
        pad.reverbSend.store (juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "reverbSend", 0.0))));
        pad.chokeGroup.store (juce::jlimit (
            0, numberOfPads, item->getIntAttribute ("chokeGroup", 0)));
        pad.ampCurve.store (juce::jlimit (
            -1.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute ("ampCurve", 0.0))));
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
        pad.filterCutoffHz.store (juce::jlimit (
            20.0f, 20000.0f,
            static_cast<float> (item->getDoubleAttribute (
                "filterCutoffHz", 20000.0))));
        pad.filterResonance.store (juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "filterResonance", 0.0))));
        pad.filterDriveDb.store (juce::jlimit (
            0.0f, 24.0f,
            static_cast<float> (item->getDoubleAttribute (
                "filterDriveDb", 0.0))));
        pad.filterEnabled.store (item->getBoolAttribute (
            "filterEnabled", false));
        pad.filterType.store (juce::jlimit (
            1, 7, item->getIntAttribute ("filterType", 1)));
        pad.filterSlope.store (juce::jlimit (
            0, 3, item->getIntAttribute ("filterSlope", 1)));
        pad.highPassCutoffHz.store (juce::jlimit (
            0.0f, 2000.0f,
            static_cast<float> (item->getDoubleAttribute (
                "highPassCutoffHz", 0.0))));
        pad.compressorEnabled.store (item->getBoolAttribute (
            "compressorEnabled", false));
        pad.compressorThresholdDb.store (juce::jlimit (
            -60.0f, 0.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorThresholdDb", -18.0))));
        pad.compressorRatio.store (juce::jlimit (
            1.0f, 20.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorRatio", 4.0))));
        pad.compressorAttackMs.store (juce::jlimit (
            0.1f, 100.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorAttackMs", 10.0))));
        pad.compressorReleaseMs.store (juce::jlimit (
            10.0f, 1000.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorReleaseMs", 100.0))));
        pad.compressorKneeDb.store (juce::jlimit (
            0.0f, 24.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorKneeDb", 6.0))));
        pad.compressorGainDb.store (juce::jlimit (
            -24.0f, 24.0f,
            static_cast<float> (item->getDoubleAttribute (
                "compressorGainDb", 0.0))));
        pad.saturationAmount.store (juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "saturationAmount", 0.0))));
        pad.saturationHardClipAmount.store (juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (item->getDoubleAttribute (
                "saturationHardClip", 0.0))));
        pad.saturationEnabled.store (item->getBoolAttribute (
            "saturationEnabled", false));
        const bool savedLoopEnabled = item->getBoolAttribute ("loopEnabled", false);
        pad.loopMode.store (juce::jlimit (
            0, 1, item->getIntAttribute ("loopMode", 0)));
        pad.sequencerGated.store (item->getBoolAttribute (
            "sequencerGated", false));
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
        for (auto& step : patternPlaybackSteps)
            step.store (-1);

        patternPlaybackChainEnabled.store (true);
        patternPlaybackLoopEnabled.store (false);

        if (auto* playbackXml = patternsXml->getChildByName (
                "PATTERN_PLAYBACK"))
        {
            decodePatternPlaybackSteps (
                playbackXml->getStringAttribute ("steps"));
            patternPlaybackChainEnabled.store (
                playbackXml->getBoolAttribute ("enabled", true));
            patternPlaybackLoopEnabled.store (
                playbackXml->getBoolAttribute ("loop", false));
        }

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

            const auto storedPatternFile =
                slotXml->getStringAttribute ("file");

            if (storedPatternFile.isNotEmpty())
                storedPatterns[static_cast<std::size_t> (patternIndex)].filePath
                    = resolveStoredPath (
                          storedPatternFile).getFullPathName();
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
        refreshPatternPlaybackBars();
    }

    else
    {
        for (auto& step : patternPlaybackSteps)
            step.store (-1);

        patternPlaybackChainEnabled.store (true);
        patternPlaybackLoopEnabled.store (false);

        currentPatternIndex.store (0);
        captureCurrentPattern();
    }

    loadGlobalFxXml (state->getChildByName ("GLOBAL_FX"));

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
