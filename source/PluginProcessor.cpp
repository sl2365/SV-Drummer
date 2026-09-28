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

    loadPortableSettings();

    if (getBrowserFolders().isEmpty())
    {
        const auto portableSamples = getPortableSamplesDirectory();

        if (portableSamples.createDirectory().wasOk())
            addBrowserFolder (portableSamples);
    }
}

SVDrummerAudioProcessor::~SVDrummerAudioProcessor()
{
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

        for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
            if (pads[static_cast<std::size_t> (padIndex)].midiNote.load() == note)
                triggerPadOnAudioThread (padIndex, velocity);
    }

    const auto pending = pendingInterfaceTriggers.exchange (0);

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
        if ((pending & (std::uint32_t { 1 } << static_cast<unsigned int> (padIndex))) != 0)
            triggerPadOnAudioThread (
                padIndex,
                pendingInterfaceVelocities[static_cast<std::size_t> (padIndex)].load());

    const bool hasSolo = anyPadIsSoloed();

    for (int padIndex = 0; padIndex < numberOfPads; ++padIndex)
    {
        const auto& pad = pads[static_cast<std::size_t> (padIndex)];
        const bool outputEnabled = ! pad.muted.load()
                                && (! hasSolo || pad.soloed.load());

        renderPadVoices (padIndex, buffer, outputEnabled);
    }
}

void SVDrummerAudioProcessor::triggerPadOnAudioThread (int padIndex, float velocity)
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

        for (int outputSample = 0; outputSample < outputSamples; ++outputSample)
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

    if (! file.existsAsFile())
        return juce::Result::fail ("The sample file does not exist.");

    if (! isSupportedAudioFile (file))
        return juce::Result::fail ("Supported formats are WAV, MP3, OGG, FLAC and AIFF.");

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return juce::Result::fail ("The sample could not be read.");

    if (reader->lengthInSamples > static_cast<juce::int64> ((std::numeric_limits<int>::max)()))
        return juce::Result::fail ("The sample is too long to load into memory.");

    auto newSample = std::make_shared<SampleData>();
    const int sampleCount = static_cast<int> (reader->lengthInSamples);
    const int channelCount = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    newSample->audio.setSize (channelCount, sampleCount);
    newSample->audio.clear();

    if (! reader->read (&newSample->audio, 0, sampleCount, 0, true, true))
        return juce::Result::fail ("The sample data could not be decoded.");

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

    auto& pad = pads[static_cast<std::size_t> (padIndex)];
    pad.sampleRevision.fetch_add (1);
    std::atomic_store_explicit (&pad.sample,
                                std::shared_ptr<const SampleData> (std::move (newSample)),
                                std::memory_order_release);

    {
        const juce::ScopedLock lock (stateLock);
        pad.samplePath = file.getFullPathName();
        pad.displayName = file.getFileNameWithoutExtension();
    }

    markPortableSettingsDirty();
    return juce::Result::ok();
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

void SVDrummerAudioProcessor::markPortableSettingsDirty() noexcept
{
    portableSettingsDirty.store (true);
}

void SVDrummerAudioProcessor::flushPortableSettingsIfNeeded()
{
    if (portableSettingsDirty.exchange (false))
        savePortableSettings();
}

void SVDrummerAudioProcessor::savePortableSettings() const
{
    const auto settingsFile = getPortableSettingsFile();
    settingsFile.getParentDirectory().createDirectory();
    getPortableSamplesDirectory().createDirectory();
    getPortableDataDirectory().getChildFile ("Patterns").createDirectory();

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

    portableSettingsDirty.store (false);
}

void SVDrummerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("SVDRUMMER_STATE");
    state.setAttribute ("version", "1.0.0");

    auto* browser = state.createNewChildElement ("BROWSER");

    for (const auto& folder : getBrowserFolders())
    {
        auto* item = browser->createNewChildElement ("FOLDER");
        item->setAttribute ("path", makeStoredPath (juce::File (folder)));
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

bool SVDrummerAudioProcessor::isSupportedAudioFile (const juce::File& file)
{
    return file.hasFileExtension ("wav;mp3;ogg;flac;aif;aiff");
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SVDrummerAudioProcessor();
}
