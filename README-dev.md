# SV-Drummer Stage 4.6

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
- High-detail per-pad waveform editor with draggable playback START and END markers.
- Cursor-centred mouse-wheel waveform zoom for precise START and END editing.
- Draggable full-sample overview bar for scrolling the zoomed waveform.
- Browser Preview button for auditioning a selected sample without loading a pad.
- Portable settings under `dist\Data\Settings`.
- Portable GUI zoom saved as `dist\Data\Settings.ini`.
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
- Pattern MIDI notes support SELECT and momentary GATE playback modes.
- Portable `.svpattern` files with browser-to-slot drag assignment.
- Sequencer data saved in both plug-in state and portable settings.
- Sample-library roots reopen collapsed for faster browsing.
- ASCII-only header status text for reliable display on Windows hosts.

Amp/filter/LFO controls, sample looping and choke groups remain reserved for
later stages.

Stage 1.1 corrects the Windows `max` macro collision reported by the first
Visual Studio 2026 build.

Stage 1.2 enables JUCE's self-contained MP3 decoder and explicitly enables
WAV, OGG and FLAC support. SF2 SoundFont loading is reserved for a later
sampler-engine stage because it requires preset, key-zone and velocity-zone
handling rather than ordinary audio-file decoding.

Stage 2.0 activates the first sequencer engine and editable grid.

Stage 2.1 separates step drawing from velocity editing. Use the mouse wheel over
an active step to change its velocity. The build preserves and ignores extra
release files such as RAR archives placed in `dist`.

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

Stage 3.2 makes grid editing directional: left-click and left-drag only add
steps, while right-click and right-drag only delete them. Left-clicking an
existing step selects its lane without changing or deleting that step. The
SEQUENCER and PAD SETTINGS selectors are now joined tabs. The redundant heading
and instruction line inside the sequencer were removed, giving their vertical
space back to the sixteen step lanes.

Stage 3.3 completes the tab treatment. The inactive tab is darker with muted
text and a complete outline. The selected tab matches the panel surface and has
no bottom border, allowing its side outline to flow directly into the active
panel and making the selected view immediately clear.

Stage 3.4 removes leading zeros from the visible pad, lane and pattern-slot
numbers. Internal portable-settings keys and working-slot filenames retain their
zero padding for compatibility. Blank areas of the sequencer now use the normal
arrow cursor; the hand cursor is limited to genuinely clickable components.

Stage 4.0 enlarges the `BAR #` ruler labels and improves the size and contrast of
the MIDI-note/`OFF` text on the pattern buttons. The editor's 75–200% window
scale is restored from `Data\Settings.ini`; changes are written after resizing
settles and again when the editor closes.

The PAD SETTINGS view now includes the first functional sample-editing stage: a
large waveform with draggable START and END markers. The selected range controls
real playback in normal and reverse modes. Double-click the waveform to reset to
the complete sample. START and END are preserved in host state, portable pad
settings and `.svpattern` files. Loop controls follow in the next stage.

Stage 4.1 compacts the PAD SETTINGS knobs and buttons to the Sequencer control
size, places the sample path on the pad title row and gives the reclaimed height
to the waveform. Click away from the S/E markers to audition the selected pad;
drag directly on a marker to trim it. The marker lines now end in labelled S and
E flags at the bottom of the waveform.

Stage 4.2 changes resizing to uniform whole-interface scaling from 75–200% and
uses a slightly taller 1280×800 design canvas to increase all sixteen sequencer
lane heights. VIEW now shows `1` on first load instead of its internal index `0`.
Quarter-note beat guides run behind the cells independently of lane division,
with stronger guides at bar boundaries. Browser rows, empty-pad text, pad footer
controls, pattern slots and lane headers are larger for readability.

The sample browser now has a PREVIEW button that auditions the selected audio
file without assigning it to a pad. Waveform analysis has increased from 160 to
2048 envelope points and both pad displays render efficiently at their available
pixel width. In PAD SETTINGS, Clear Sample is now the compact `X` at the end of
the sample path.

Stage 4.3 adds cursor-centred mouse-wheel zoom to the PAD SETTINGS waveform,
down to 1% of the complete sample. START and END dragging remains mapped to the
correct sample position while zoomed; double-click resets both the playback
range and waveform view. Each pad now has the numbered circle shown in the
reference mockup. The circle uses a pad-colour outline normally and fills with
that colour only while the pad is open for editing.

The pad name and number now share one vertically aligned title row. The
Sequencer transport has a circular Play/Stop button, and DIV adds `1/4T` and
`1/64` in direct musical order. New lanes continue to default to `1/16`.

Stage 4.4 adds a TAL-style full-sample overview beneath the PAD SETTINGS
waveform. The highlighted viewport can be dragged to scroll a zoomed sample,
and clicking elsewhere in the overview moves the viewport there. Both the main
waveform and overview now use continuous point-to-point outlines filled to the
zero line instead of disconnected vertical peak bars. Pad-number circles and
their numbers are also larger.

Stage 4.5 redraws the zero-amplitude centre line after the filled waveform and
viewport shading. It therefore remains continuously visible across both the
main waveform and the full-sample overview.

Stage 4.6 adds a MIDI MODE control beside the enlarged transport button.
`SELECT` preserves the original behaviour: assigned MIDI notes select patterns
while the transport remains manually controlled and follows the host timeline.
`GATE` makes each assigned pattern note momentary: note-on selects the pattern,
turns Play on and restarts at Step 1 using the host BPM; note-off turns Play off
immediately. The Play triangle is geometrically centred.

LENGTH and VIEW now display `Bar`/`Bars`, LOOP displays `Step`/`Steps`, and DIV
explicitly refreshes its musical value after settings or pattern restoration.
MIDI MODE is saved in both portable settings and host state.

## Pattern buttons

- The highlighted button is the current pattern being edited.
- Clicking another button automatically stores the pattern you are leaving and
  loads the selected slot.
- Selecting an unused slot copies the current drum kit and lane setup, but starts
  with an empty sequence.
- An assigned slot restores its pattern length, lane divisions, independent lane
  loop lengths, step velocities, loaded samples and pad settings.
- Pattern-selection MIDI notes default to `OFF`. Mouse-wheel over a pattern button
  to assign or change its note. Right-click offers `MIDI Note Off` and
  `Use Default MIDI Note`; default notes start at MIDI 60/C4 for Pattern 1.
- A MIDI note assigned to pattern selection is used for switching patterns and
  therefore will not also trigger a drum pad.
- Right-click a pattern button and choose `Save to Pattern Library` to create a
  portable `.svpattern` file under `Data\Patterns`.
- Open the browser's `PATTERNS` tab, then drag a `.svpattern` file onto any
  pattern button to assign it to that slot.
- Automatic working copies of assigned slots are kept under
  `Data\Patterns\Slots` and are restored when the plug-in is reopened.

## Build

In the Project Root, double-click `- Build.bat`.

The script closes `PolyHostInterface.exe`, builds the Release VST3 and creates:

`dist\SV-Drummer.vst3`

Build output is also saved to `Results.log`.
