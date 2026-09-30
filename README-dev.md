# SV-Drummer v0.7.12

Native Windows x64 VST3 drum sample player and host-synchronised sequencer.

## Permanent identity

- Product: `SV-Drummer`
- Manufacturer: `sl23`
- Manufacturer code: `sl23`
- Plug-in code: `svd1`
- Bundle ID: `com.sl23.svdrummer`
- Version: `0.7.12`

## Current features

- Sixteen coloured sample pads in two rows of eight.
- Drag supported audio files from the internal browser or Windows Explorer.
- Multiple sample-library folders, displayed as a lazy-loading folder tree.
- WAV, MP3, OGG, FLAC and AIFF sample loading through JUCE audio formats.
- Waveform and sample name on every pad.
- Numbered pad circles act as global selectors and selection indicators.
- MIDI notes 36–51 by default; click or mouse-wheel the note field to change it.
- Mouse and MIDI pad triggering with MIDI velocity.
- Per-pad mute, solo, volume, pan, tune and reverse controls.
- Per-pad amplitude envelope with Attack, Decay, Sustain and Release.
- Sixteen per-pad choke groups; pads sharing a non-zero group cut each other off.
- High-detail per-pad waveform editor with draggable playback START and END markers.
- Per-pad sample looping with a LOOP switch and draggable LS/LE waveform markers.
- Exact sample-frame START, END, L-START and L-END positions with wheel editing.
- Optional zero-crossing SNAP for click-resistant trim and loop boundaries.
- Continuous, time-ordered waveform tracing through every zero crossing.
- Cursor-centred mouse-wheel waveform zoom for precise START and END editing.
- Draggable full-sample overview bar for scrolling the zoomed waveform.
- Browser Preview button for auditioning a selected sample without loading a pad.
- Portable settings under `dist\Data\Settings`.
- Portable GUI zoom restored from `dist\Data\Settings\SV-Drummer.ini` and
  saved when the editor closes.
- Portable Samples, Kits, Patterns and Projects folders.
- Icon tabs for Samples, Kits, Patterns and Projects, plus one shared Browser
  Refresh button.
- Every Browser mode has an Open Folder button for its active library location.
- Sixteen-lane host-synchronised step sequencer with sample-accurate triggering.
- An independent timing division and loop length for every lane.
- Per-step velocity, adjusted with the mouse wheel over an active step.
- Pattern length from 1 to 16 bars, with switchable 1-, 2- or 4-bar views.
- Horizontal bar scrolling for longer patterns.
- Shorter lane loops repeat as dimmed ghost steps to distinguish them from the
  original loop pass.
- A single full-height playhead rectangle replaces sixteen separate lane boxes.
- Sixteen working Pattern buttons, each storing sixteen Sequences: one per lane.
- Pattern menus can generate an undoable random rhythm across all sixteen lanes.
- Pattern slots can be selected manually or by an optional custom MIDI note.
- Pattern MIDI notes support SELECT, momentary GATE and latched HOLD modes.
- Portable `.svpattern` files containing one Pattern button's sixteen Sequences.
- Portable `.svpatternset` files containing the complete set of sixteen Patterns.
- Portable `.svkit` files containing all sixteen pads and their settings.
- Portable `.svproject` files containing the complete Kit and Pattern Set.
- Missing Project or Kit samples can be relinked from a chosen folder without
  changing any saved pad settings.
- Kit, Pattern Set and sequencer data saved in the host-managed plug-in state.
- Browser folders, selection and scroll position restore when the editor reopens.
- ASCII-only header status text for reliable display on Windows hosts.

Filter and LFO controls remain reserved for later stages.

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

Stage 3.0 activates the sixteen Pattern buttons. Clicking a button stores the current
Pattern before loading the selected button. A new empty button copies the current
lane timing setup while starting with no steps. As of v0.7.3, slot state contains
only pattern length, lane divisions, lane loop lengths and velocities; the Kit
remains global.

Pattern-selection MIDI notes default to OFF so they cannot take notes away from
the drum pads. Mouse-wheel over a pattern slot to assign or change its MIDI note;
right-click provides MIDI Off, default note and Save Pattern commands.
Saved Patterns appear under the browser's PATTERNS tab and can be dragged onto
any button. Working Pattern files are stored under `Data\Patterns\Patterns\Slots`.

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
scale is restored from `Data\Settings\SV-Drummer.ini`. Version 0.7.7 restores
saving this GUI preference when the editor closes.

The PAD SETTINGS view now includes the first functional sample-editing stage: a
large waveform with draggable START and END markers. The selected range controls
real playback in normal and reverse modes. Double-click the waveform to reset to
the complete sample. START and END are preserved in host state and `.svkit`
files. The loop controls planned here are completed in
Stage 5.0.

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
that colour when the pad is selected.

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
MIDI MODE is saved in host state and complete `.svproject` files.

Stage 4.7 adds the third MIDI MODE, `HOLD`. Pressing an assigned pattern note
selects that pattern, restarts it at Step 1 and leaves it playing after note-off.
Pressing other assigned notes switches and restarts their patterns; pressing the
currently playing pattern note again stops the sequencer.

All rotary controls now support double-click reset. LENGTH, DIV, LOOP and the
per-pad VOLUME, PAN and TUNE knobs return to the values captured when the current
pattern or host preset was loaded. VIEW returns to its normal `1 Bar` default.

Stage 4.8 makes each numbered pad circle a global selector and indicator. In
Sequencer view, selecting a lane fills its matching pad circle, while clicking a
circle selects that lane for editing. In Pad Settings, clicking a circle selects
that pad for editing just like its cog button. The selected pad and sequencer
lane remain linked when switching views, and circle clicks no longer audition
the sample.

Stage 5.0 allows the circular Play/Stop button to stop MIDI-triggered playback
in both GATE and HOLD modes. It remains intentionally unable to start those
modes without their assigned pattern note.

PAD SETTINGS now adds functional per-pad sample looping. Enable `LOOP`, then
drag the `LS` and `LE` flags at the top of the waveform to set the repeat range;
the `S` and `E` trim flags remain at the bottom. The top half of an overlapping
marker selects the loop point and the bottom half selects the trim point.
Double-click resets all four markers and the waveform view. MIDI note-off stops
a held loop, a new trigger restarts it, and stopping the sequencer ends loops
started by sequencer steps. Loop enable and positions are preserved in host
state and `.svkit` files.

Stage 5.1 replaces the separate positive and negative waveform envelopes with
one continuous time-ordered signal path. The main waveform and overview now
cross zero without doubling back, matching conventional audio editors while
remaining filled to the zero line. Trim `S`/`E` marker lines and flags are red;
loop `LS`/`LE` marker lines and flags are aqua. The complete visible flag area
is now a drag target, as well as the marker line itself.

Stage 5.2 replaces percentage marker positions with exact sample-frame indices.
The larger `START`, `END`, `L-START` and `L-END` value boxes show the precise
playback boundary and can each be adjusted with the mouse wheel. With `SNAP`
off, each wheel movement changes the boundary by one sample; with `SNAP` on it
moves to the next or previous zero crossing. Marker dragging also selects only
the nearest zero crossing while SNAP is enabled.

Zero crossings are analysed once when a sample is loaded, keeping dragging
responsive. Exact positions control playback directly and are preserved in
host state and `.svkit` files. SNAP is saved in host state and complete
`.svproject` files.

Stage 6.0 makes the active states of `REVERSE` and `LOOP` immediately visible
with strong filled button colours. Loop marker lines and flags use a darker aqua
for clearer white `LS`/`LE` text.

The PAD SETTINGS controls now include a functional `CHOKE` group knob. `OFF`
leaves the pad polyphonic; values 1–16 assign a group. Triggering any pad in a
non-zero group cuts voices already playing in the same group, including an
earlier voice from that pad. Chokes honour the trigger's sample offset within
the current audio block. Choke assignments are stored with host state and
`.svkit` files, and double-click returns the knob to the value
captured with the current Kit or host preset.

Stage 6.1 preserves the selected Sequencer/Pad Settings view, selected pad/lane,
browser Samples/Kits/Patterns mode, expanded folders, selected browser item and
browser scroll position while PHI changes focus or recreates the plug-in editor.

The Browser uses a smaller, smoother row font. The Sequencer scrollbar now
matches the Browser scrollbar and begins beside the first step rather than under
the lane labels. The LANE button aligns vertically with MIDI MODE; two divider
lines isolate the BAR ruler from the editing controls and step grid. The ruler's
LANE heading is centred over the lane column, while each lane number has its own
50%-black frame and its DIV value is centred separately. Pattern slots containing
steps use a larger uniform pastel-red indicator dot. SNAP's inactive styling now
matches the inactive REVERSE and LOOP buttons.

Stage 6.2 makes the Sequencer scrollbar thinner and lifts the top controls away
from the ruler divider. MIDI MODE now uses grey for SELECT, blue for GATE and a
distinct violet for HOLD. Empty pads retain their normal text while their border,
footer and selection colour are subtly darkened.

The Browser toolbar now uses vector icons for Add Folder, Remove, Refresh,
Preview and Save, with wider gaps between the buttons. Removing a browser folder
or clearing a pad sample requires confirmation; neither operation deletes the
original folder or audio file.

Kit, Pattern and Pattern Set library saving is manual. GUI zoom is treated as an
editor preference and is written separately when the editor closes. Switching
Patterns keeps edits safely in the running plug-in instance without writing a
library file. DAW project state remains host-managed so normal DAW Save/Load
continues to work.

The supplied transparent logo is embedded from `Resources\logo.png` and replaces
the drawn SV-DRUMMER title at a fixed 193×41 design size. A higher-resolution
replacement can use the same filename and will retain the same displayed size
after rebuilding.

Stage 6.3 darkens empty pad colours a little further while leaving their text
unchanged. Browser toolbar tooltips now have an editor-level tooltip window, and
the Refresh artwork uses two clear circular arrows. Knob values use the same
bold 9.5-point font as their labels with a softer grey colour. The visible
version number shares the graphical logo's text baseline.

Stage 7.0 adds a functional per-pad amplitude envelope to PAD SETTINGS. ATTACK
rises from silence to full level, DECAY falls to SUSTAIN, and SUSTAIN controls
the level maintained for the rest of a one-shot or while a loop is held.
RELEASE controls the selected pad voice's fade when it is released; for a
looped voice, release occurs on MIDI note-off or when playback is stopped.
Defaults of 0 ms Attack, 0 ms Decay, 100% Sustain and 0 ms Release preserve the
sound and stopping behaviour of earlier projects.

Envelope settings are retained independently for every pad and are included in
DAW project state, `.svproject` files and `.svkit` files. Double-clicking an
envelope knob returns it to the value captured when the current Kit or host
preset was loaded.

The amplitude envelope is part of each pad's sample playback, not a Sequencer
setting. A live envelope line is drawn over the selected pad's waveform, with
the current Attack, Decay, Sustain and Release values shown on the waveform.

VOLUME, PAN and TUNE now appear in the Sequencer control bar as selected-pad
controls. Selecting a pad or lane loads only that pad's values, and the three
knobs and labels adopt its colour. CHOKE and the amplitude-envelope controls
remain in PAD SETTINGS alongside the waveform they affect.

Only Browser controls retain tooltips. Empty-pad colours are
reduced while all pad text retains its normal colour. The header shows the
unified pre-release version beside the logo; the separate top-right product and
Stage text has been removed.

The preceding selected-pad/envelope UI update is v0.7.1. Version 0.7.2 reduces
empty-pad colours from approximately 59% to approximately 45% of their normal
brightness. The four amplitude-envelope knobs now use longer full-range drag
distances, and their mouse wheels move in fine fixed steps: 5 ms for Attack,
10 ms for Decay and Release, and 1% for Sustain. The envelope line remains live
over the waveform, but its value-summary bar has been removed to leave the
sample unobscured.

Version 0.7.3 separates the Kit from the sixteen Pattern slots. Selecting a
Pattern button now changes only its pattern length, lane timing and step data;
it never changes loaded samples or pad settings. Each lane is a Sequence; one
Pattern contains all sixteen lane Sequences. Individual Patterns now use
`.svpattern` files under `Data\Patterns\Patterns`.

The Browser now has `SAMPLES`, `KITS` and `PATTERNS` tabs. The KITS Save button
asks for a filename and location, defaulting to `Data\Kits`, and writes a
portable `.svkit` containing all sixteen sample assignments and pad settings.
Drag a saved Kit onto any pad to load the whole Kit. If any current pad contains
a sample, SV-Drummer asks for confirmation before replacing the sixteen pads.

Version 0.7.4 makes individual Pattern saving explicit. `Save Pattern` and the
PATTERNS-tab Save icon both open a filename/location dialog, defaulting to
`Pattern ##.svpattern` under `Data\Patterns\Patterns`. The redundant
Save icon has been removed from the SAMPLES tab.

Version 0.7.5 removes the tick from the Pattern-button save command. The same
menu adds `Copy` and `Paste` for transferring a complete Pattern between Pattern
buttons; replacing a populated destination asks first. Pad
Volume, Pan and Tune values are now drawn directly at the same 9.5-point bold
size as the other Sequencer-row values, avoiding JUCE's automatic fitted-text
reduction.

The centered `PATTERNS` heading now has a `MENU` button beneath it. `Save Pattern
Set` saves all sixteen Patterns and their MIDI-note assignments as one
`.svpatternset` file, defaulting to `Data\Patterns\Pattern Sets`. The PATTERNS
Browser contains separate `Patterns` and `Pattern Sets` folders. Dragging a
`.svpatternset` onto any Pattern button loads the complete sixteen-Pattern Set
and asks for confirmation when the current set contains steps.

Version 0.7.6 keeps every Pattern button whose MIDI assignment is `OFF` dark,
including an empty slot that has merely been selected. The blue outline still
identifies the selected slot, and a populated OFF slot retains its red data dot.
PAD VOL, PAD PAN and PAD TUNE values use an 11.5-point bold font so their
rendered size matches the other Sequencer-row values more closely.

Version 0.7.7 standardises the terminology: a Sequence is one sequencer lane, a
Pattern is one Pattern button containing all sixteen Sequences, and a Pattern Set
contains all sixteen Pattern buttons. Pre-release backwards compatibility with
the former `.svseq` and `.svpattern` meanings is intentionally not retained. It
also reconnects GUI zoom persistence so a resized editor writes its zoom setting
when closed and restores that value next time.

Version 0.7.8 adds the Projects Browser tab and portable `.svproject` files. A
Project restores the complete Kit, all sample paths and pad settings, the full
sixteen-Pattern Set, Pattern MIDI-note assignments, MIDI mode, transport state
and SNAP setting. Projects default to `Data\Projects` and can be dragged onto
any pad or Pattern button, with confirmation before replacing existing content.

The four Browser modes now use icon tabs. A single Refresh button in the
Browser's top-right corner refreshes whichever mode is selected; the former
per-mode Refresh placement has been removed. Envelope time values now initialise
as `0 ms` instead of `0`. PAD VOL, PAD PAN and PAD TUNE remain Kit settings and
are not written into Pattern files. Pattern menus now include `Clear` and a
one-level `Undo Last Operation` for Clear, Paste and Pattern-file replacement.

Version 0.7.9 adds missing-sample recovery for Projects and Kits. When loading,
SV-Drummer first checks the folder containing the Project or Kit for moved
samples. If any remain missing, it lists the affected pads and offers a
`Locate Folder` option. The selected folder and its subfolders are searched by
the original audio filename. Relinking changes only the audio-file location;
all pad volume, pan, tune, envelope, loop, marker, MIDI and choke settings remain
unchanged. Any files that still cannot be found remain clearly listed.

Version 0.7.10 fixes Sample Browser folder persistence. The supplied settings
file confirmed that folder paths were being written correctly; the problem was
that a DAW or PHI project state could later replace the global list with its own
older copy. Browser folders are now global portable preferences read only from
`Data\Settings\SV-Drummer.ini`. Adding or removing a Sample folder immediately
updates only the INI file's `[Browser]` section. It does not require a Save
button and does not save Kits, Patterns, pads or other musical content.

GUI zoom has also moved into that same INI file. The former
`Data\Settings.ini` value is migrated when present and the obsolete file is
removed after the combined settings file is successfully written. Saving zoom
on editor close updates only the zoom entry; it does not automatically save
changed Kits, Patterns or other musical content.

Every Browser mode now has an Open Folder button. In Samples it opens the
selected sample or library folder, falling back to `Data\Samples`; the other
tabs open `Data\Kits`, `Data\Patterns` or `Data\Projects`. Pattern-button menus
now include `Random`, which replaces all sixteen lane rhythms using the existing
lane divisions and loop lengths. Random is included in the existing one-level
`Undo Last Operation` system.

Version 0.7.11 separates global preferences from musical session state. The
portable `Data\Settings\SV-Drummer.ini` file is read only for GUI zoom and
Sample Browser folders; any older Pad, Sequencer or Pattern sections are
ignored. Saving either preference rewrites the INI with only `[GUI]` and
`[Browser]` sections. No legacy conversion is performed.

Pads, samples, Kit settings, all sixteen Patterns, MIDI assignments, sequencer
state and SNAP now restore exclusively from the host's plug-in state (or from a
manually loaded SV-Drummer library file). Every musical edit and Project load
also reports a non-parameter state change to the host, allowing PHI to update
its saved VST3 state even though SV-Drummer does not yet expose host-automation
parameters. PHI's empty Macro Mapping view is therefore separate from MIDI-note
input and session persistence.

Version 0.7.12 adds the first stable host-automation layer. PHI and other hosts
now see 52 automatable SV-Drummer parameters: Sequencer Play, MIDI Mode,
Pattern Length, Sample Marker Snap, and Volume, Pan and Tune for each of the
sixteen pads. The parameter IDs are fixed independently of their visible names.

SV-Drummer also exposes one non-automatable internal state-revision parameter.
It is deliberately hidden from PHI's Macro Mapping list, but it changes whenever
samples, Kits, Patterns, Pattern Sets or Projects alter musical state. This gives
PHI a normal parameter-change callback as well as the VST3 non-parameter-state
notification, ensuring that its next saved or recalled session contains the
current SV-Drummer state. Host parameters are state-backed, so restoring the
existing SV-Drummer state chunk immediately restores their displayed values.

## Pattern buttons

- The highlighted button is the current pattern being edited.
- A slot with its MIDI note set to `OFF` stays dark. Selection is shown by the
  blue outline, while the red dot continues to identify a slot containing steps.
- Clicking another button retains the Pattern you are leaving in the running
  plug-in instance and loads the selected slot without changing the Kit.
- Selecting an unused slot copies the current lane timing setup but starts with
  an empty Pattern.
- An assigned slot restores only its pattern length, lane divisions, independent
  lane loop lengths and step velocities.
- Pattern-selection MIDI notes default to `OFF`. Mouse-wheel over a pattern button
  to assign or change its note. Right-click offers `MIDI Note Off` and
  `Use Default MIDI Note`; default notes start at MIDI 60/C4 for Pattern 1.
- A MIDI note assigned to pattern selection is used for switching patterns and
  therefore will not also trigger a drum pad.
- Right-click a Pattern button and choose `Save Pattern` to create a portable
  `.svpattern` file under `Data\Patterns\Patterns`.
- The same right-click menu can `Copy` one Pattern and `Paste` it into
  another slot without changing the Kit or the destination MIDI-note assignment.
- `Random` replaces all sixteen lane rhythms at roughly 25% density, assigns
  varied audible velocities and guarantees at least one step in every lane.
  Existing lane divisions and loop lengths are preserved.
- `Clear` removes all sixteen lane Sequences while preserving lane timing and
  the Pattern MIDI-note assignment. `Undo Last Operation` restores the last
  Pattern replaced by Clear, Paste or a dragged Pattern file.
- Open the browser's `PATTERNS` tab, then drag a `.svpattern` file onto any
  Pattern button to assign that Pattern to the slot.
- The PATTERNS-tab floppy icon saves the currently selected Pattern through the
  same filename/location dialog.
- Click the centered `PATTERNS` section's `MENU`, then `Save Pattern Set`, to
  save all sixteen slots as one `.svpatternset` file. Drag that file onto any
  Pattern button to replace the complete sixteen-Pattern Set.

## Build

In the Project Root, double-click `- Build.bat`.

The script closes `PolyHostInterface.exe`, builds the Release VST3 and creates:

`dist\SV-Drummer.vst3`

Build output is also saved to `Results.log`.
