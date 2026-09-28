# SV-Drummer Stage 3.1

Native Windows x64 VST3 drum sample player and host-synchronised sequencer.

## Permanent identity

- Product: `SV-Drummer`
- Manufacturer: `sl23`
- Manufacturer code: `sl23`
- Plug-in code: `svd1`
- Bundle ID: `com.sl23.svdrummer`
- Version: `1.0.0`

## Current features

- Sixteen coloured sample pads in two rows of eight.
- Drag supported audio files from the internal browser or Windows Explorer.
- Multiple sample-library folders, displayed as a lazy-loading folder tree.
- WAV, MP3, OGG, FLAC and AIFF sample loading through JUCE audio formats.
- Waveform and sample name on every pad.
- MIDI notes 36–51 by default; click or mouse-wheel the note field to change it.
- Mouse and MIDI pad triggering with MIDI velocity.
- Per-pad mute, solo, volume, pan, tune and reverse controls.
- Portable settings under `dist\Data\Settings`.
- Reserved portable folders for samples and future patterns.
- Samples/Patterns browser tabs and Sequencer/Pad Settings views.
- Sixteen-lane host-synchronised step sequencer with sample-accurate triggering.
- An independent timing division and loop length for every lane.
- Per-step velocity, adjusted with the mouse wheel over an active step.
- Pattern length from 1 to 16 bars, with switchable 1-, 2- or 4-bar views.
- Horizontal bar scrolling for longer patterns.
- Shorter lane loops repeat as dimmed ghost steps to distinguish them from the
  original loop pass.
- A single full-height playhead rectangle replaces sixteen separate lane boxes.
- Sixteen working pattern slots, each storing its sequence and complete pad kit.
- Pattern slots can be selected manually or by an optional custom MIDI note.
- Portable `.svpattern` files with browser-to-slot drag assignment.
- Sequencer data saved in both plug-in state and portable settings.
- Sample-library roots reopen collapsed for faster browsing.
- ASCII-only header status text for reliable display on Windows hosts.

The advanced waveform editor, amp/filter/LFO controls, sample looping and choke
groups remain reserved for later stages.

Stage 1.1 corrects the Windows `max` macro collision reported by the first
Visual Studio 2026 build.

Stage 1.2 enables JUCE's self-contained MP3 decoder and explicitly enables
WAV, OGG and FLAC support. SF2 SoundFont loading is reserved for a later
sampler-engine stage because it requires preset, key-zone and velocity-zone
handling rather than ordinary audio-file decoding.

Stage 2.0 activates the first sequencer engine and editable grid.

Stage 2.1 separates step drawing from velocity editing. Left-click toggles a
step; left-drag paints or erases steps while remaining locked to the starting
lane. Use the mouse wheel over an active step to change its velocity, or
right-drag to clear steps. The build now preserves and ignores extra release
files such as RAR archives placed in `dist`.

Click the Play icon and start the host transport to play the pattern.

Stage 3.0 activates the sixteen pattern slots. Clicking a slot stores the current
pattern before loading the selected slot. A new empty slot copies the current pad
kit and lane setup while starting with an empty sequence. Slot state includes
pattern length, lane divisions, lane loop lengths, velocities, samples and pad
settings.

Pattern-selection MIDI notes default to OFF so they cannot take notes away from
the drum pads. Mouse-wheel over a pattern slot to assign or change its MIDI note;
right-click provides MIDI Off, default note and Save to Pattern Library commands.
Saved patterns appear under the browser's PATTERNS tab and can be dragged onto any
slot. Working slot files are stored under `Data\Patterns\Slots`.

The sequencer transport is now a Play/Stop icon and BARS is renamed to LENGTH.
MIDI pad event offsets are constrained to the current audio block so notes from
hosts that supply oversized offsets still trigger the assigned pads correctly.
Note names use the PHI/DAW convention where MIDI note 60 is C4, correcting the
earlier one-octave display mismatch.

Stage 3.1 changes LENGTH, VIEW, DIV and LOOP into direct-drag rotary controls.
Their labels are positioned above the knobs and their read-only values are shown
underneath without value boxes. The knobs also respond to the mouse wheel.

## Build

In the Project Root, double-click `- Build.bat`.

The script closes `PolyHostInterface.exe`, builds the Release VST3 and creates:

`dist\SV-Drummer.vst3`

Build output is also saved to `Results.log`.
