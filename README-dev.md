# SV-Drummer v1.14.2

Native Windows x64 VST3 drum sample player and host-synchronised sequencer.

Version 0.10.0 adds a sixteen-channel per-pad mixer to the FX + Mixer tab. Each strip
provides Volume, Pan, Output, Mute, Solo, Delay Send and Reverb Send controls in
two rows of eight. The mixer mirrors the existing pad controls, and its new
post-fader sends feed independent global Delay and Reverb returns on MAIN.

Version 0.10.1 restores the complete Mute button letter in every compact mixer
strip, makes the PAN fader fill outward from its centre position, and renames
the main tab to `FX + Mixer`.

Version 0.10.2 restores the PAN fader's complete background track while
retaining its centre-origin bipolar fill. When any pad is soloed, Solo now
temporarily overrides that pad's stored Mute state; removing Solo restores the
Mute behaviour. Drum-pad and sequencer-lane dimming follows the same rule.

Version 0.10.3 extends the PAN handle and bipolar fill to the normal full fader
travel. At maximum Left or Right, its handle centre now aligns with the Volume,
Delay Send and Reverb Send handles at their corresponding extremes.

Version 0.10.4 gives all four controls in every mixer strip an identical-width
fader cell. This removes the final-cell rounding difference so the Reverb Send
stalk and endpoint positions exactly match Delay Send.

Version 0.10.5 standardises every Pad Settings, Delay and Reverb rotary control
against the Sequencer DIV/LOOP typography. Knob labels now share the same font,
size and muted colour, while value fields independently share the same value
font, size and lighter colour.

Version 0.10.6 slightly enlarges the shared knob-label font and applies it to
the Filter Type and Slope labels as well. Pad Settings knob indicators are now
section-coloured without colouring their labels, bodies or numeric values:
CURVE uses dark aqua, SAT/HARD CLIP use yellow, Filter knobs use red and
Compressor knobs use green.

Version 0.10.7 brightens the CURVE knob's aqua indicator while retaining a
moderate tone that remains balanced with the other Pad Settings colours.

Version 0.11.0 adds independent wet-return ducking to Delay and Reverb. Delay
also provides adjustable Duck Attack and Duck Release envelope times, while
Reverb uses a smooth fixed duck envelope. Every rotary label and value now
uses the same explicit font face as the Sequencer PAD VOL, PAN and TUNE knobs,
and the shared knob-value font is enlarged from 11.5 to 12.5 pixels.

Version 0.11.1 explicitly reapplies that shared bold value font whenever a
rotary control adopts or changes its look-and-feel. Delay's six knobs and
Reverb's five knobs now remain on single rows, the compact SYNC label and amber
LED sit immediately beside TIME, and the Mixer is restored to its exact
pre-ducking height and fader dimensions. LED off is FREE timing; LED on is host
SYNC.

Version 0.11.2 places the SYNC label above its LED on the same baseline as the
knob labels. Delay and Reverb now use the same compact title-to-control spacing
as the Pad Settings AMP, FILTER, COMPRESSOR and SATURATION sections. Their
panels are correspondingly shorter, giving the recovered height to both Mixer
rows and their faders while preserving every other channel control.

Version 0.11.3 vertically centres the SYNC LED with the Delay knob bodies and
moves both MIX knobs to the far right of their effect rows. Rotary values now
use the exact original Sequencer value-font construction in bold. Ducking uses
a more sensitive detector and an exponential response offering up to 48 dB of
wet-return reduction, making its action clearly audible. A new saved and
automatable VOLUME control in the top-right header applies click-free gain to
MAIN and all sixteen AUX outputs.

Version 0.11.4 explicitly renders the LENGTH, VIEW, DIV and LOOP value readouts
with the same bold value font used by PAD VOL, PAN and TUNE, bypassing JUCE's
slider text-box font handling while preserving the existing knob layout. The
CURVE indicator is also brighter and shifted toward a cleaner aqua hue. Filter
CUT, Filter HPF, Compressor THRESH and RELEASE now retain their intended font
width at every value. The wider shared readout sizing applies to every Pad
Settings, Delay and Reverb knob so longer formatted values are not horizontally
compressed elsewhere either. Those shared knob readouts, including CHOKE, are
now explicitly painted with the common bold value font rather than depending
on the embedded JUCE label to preserve its font weight. Delay TIME, Delay Duck
RELEASE and Compressor RELEASE switch to compact seconds above 999 ms, keeping
their full values readable within the existing knob cells.

Version 0.11.5 moves CHOKE onto the same explicit bold value renderer as every
other Pad Settings knob and brightens CURVE to a clearer aqua. Sequencer lane
headers are slightly wider and now show both their timing division and compact
loop length in bars. New left/right NUDGE buttons shift only the currently
selected lane by one step with wraparound and integrate with lane Undo. The
per-pad Compressor gains a saved, automatable -24 to +24 dB output GAIN with
click-free smoothing. Mixer channels whose pads contain no sample are dimmed
without disabling their controls or reacting to Mute/Solo state.

Version 0.11.6 brightens the CURVE accent to a stronger aqua and increases the
Sequencer transport diameter by exactly five pixels. Its stopped appearance is
unchanged, while the playing state is now dark green. Every Mixer fader retains
its existing colour, length, handle and alignment while using the same four-pixel
coloured stalk thickness as PAN. Delay knob indicators are now olive and Reverb
knob indicators are lilac.

Version 0.11.7 makes the DIV, LOOP and NUDGE labels follow the selected pad
colour without recolouring their Sequencer knobs or buttons. Scrolling down on
the LANE indicator now advances to the next lane, while scrolling up returns to
the previous lane. All Mixer faders are rendered through the same explicit
four-pixel track path as PAN while retaining their existing colours, lengths,
alignment and handle styles.

Version 0.11.8 changes the active Sequencer Play button from green to the exact
blue used by the LENGTH and VIEW knobs. The Delay and Reverb titles, enable LEDs
and control rows move down by two pixels, while only the Delay SYNC label and LED
move six pixels right toward TIME; the Delay knob positions remain unchanged.

Version 0.11.9 changes the active Sequencer Play button to the more subdued
scrollbar blue while retaining its existing stopped appearance and hover states.

Version 0.11.10 gives the AMP envelope, PAN, TUNE and CHOKE knobs a restrained
blue-violet accent derived from Pad 9 but shifted slightly further toward blue.
The CURVE knob retains its existing bright aqua accent.

Version 0.12.0 adds a live loaded-item strip above the Browser tree. The Kits
and Projects tabs show the current Kit or Project, while the Patterns tab shows
both the selected Pattern slot and its current Pattern Set. The display follows
loads, saves, Pattern selection changes and host-restored state automatically.

Version 0.12.1 replaces the per-tab loaded-item strip with a permanent,
separately framed CURRENTLY LOADED panel below the Browser. It shows the Kit,
selected Pattern, Pattern Set and Project together on every Browser tab. Its
labels use ASCII colons instead of the unsupported bullet character, preventing
the garbled text seen in some Windows hosts.

Version 0.12.2 increases the CURRENTLY LOADED heading to 12 px and its four
information rows from 10.5 px to 12 px without changing the panel layout.

Version 0.12.3 gives the CURRENTLY LOADED heading the exact 14 px plain font
style and colour used by the SAMPLE LIBRARIES, KIT LIBRARY, PATTERN LIBRARY and
PROJECT LIBRARY headings. The four information rows remain 12 px bold.

Version 0.12.4 renames the Browser information panel heading from CURRENTLY
LOADED to PROJECT INFO while retaining its matched Browser-title styling.

Version 0.13.0 gives Kits, individual Patterns, Pattern Sets and Projects the
standard Save/Save As workflow. Save now writes straight back to the exact
associated file, opening the file chooser only for a new item that has never
been saved. Save As always opens the chooser in the relevant SV-Drummer library
folder with the current filename prefilled so it can be renamed or copied
elsewhere. Pattern, Pattern Set and Project file associations are now retained
in plug-in host state, just as Kit associations already were.

Version 0.13.1 refines the Pad Settings and FX + Mixer headings. AMP, FILTER,
COMPRESSOR and SATURATION move up two pixels; their amber enable LEDs remain
vertically centred with the titles. The Delay SYNC label moves down three
pixels, and all seven section headings use a slightly brighter form of the
shared knob-label colour. Double-clicking the Sequencer Play/Stop button now
stops the sequencer and immediately silences every playing sample, Browser
preview and remaining global-effect tail through an audio-thread-safe request.

Version 0.13.2 vertically centres the complete waveform-header control row by
moving REVERSE, LOOP, NORMAL/PING-PONG, SNAP and all four marker value controls
up three pixels together. The Delay SYNC label moves down a further four pixels,
and the Master Volume slider now uses an exact four-pixel track thickness.

Version 0.13.3 standardises every Sequencer-header control label on the shared
fixed grey knob-label colour. DIV and LOOP now join PAD VOL, PAD PAN and PAD
TUNE in using the selected pad colour for their knob indicators. The two NUDGE
buttons retain neutral borders but use clean, geometrically centred chevrons in
the selected pad colour.

Version 0.13.4 restores the two centred NUDGE chevrons to their original neutral
grey and moves the selected-pad colour to the two button borders. Their borders
continue to brighten on hover and press while following the selected lane.

Version 0.13.5 redraws Browser folders with wider, conventional file-manager
proportions while retaining a compact tab. The Open Folder toolbar icon now
uses the same familiar left-tab orientation instead of the reversed silhouette.

Version 0.13.6 increases the height of both Browser-tree and Open Folder button
icons slightly, retaining their wider bodies and compact left-side tabs.

Version 0.13.7 gives Browser files an icon appropriate to their active library
tab. Samples retain the waveform, Kits use the four-pad grid, Patterns and
Pattern Sets use sequencer steps, and Projects use the folded-page document.
Folder icons remain consistent across every tab.

Version 0.13.8 increases the Browser-tab height from 27 to 31 pixels and scales
their icons from 22 x 16 to 25 x 18 pixels. Each tab's toolbar buttons increase
proportionally from 34 x 26 to 38 x 29 pixels, giving their icons more room
without changing the button aspect ratio or spacing style.

Version 0.13.9 constrains the Save and Save As floppy-disk artwork to a centred
square inside the wider Browser buttons. The Save As badge now uses the same
restrained green as SNAP while retaining its existing light plus symbol.

Version 0.13.10 enlarges the green Save As badge from 9 to 11 pixels and scales
up its light plus symbol proportionally, without altering the floppy disk.

Version 0.14.0 replaces the vertical mixer-like Pattern symbols in both the
Browser tab and its files with a four-lane horizontal step pattern. Short,
thick rounded dashes are staggered across five positions so the icon reads as a
sideways sequencer while retaining the visual weight of the previous design.

Version 0.14.1 lengthens every Pattern-icon step horizontally while retaining
its original height and corner treatment, so the marks read as dashes without
making the icon thinner or lighter.

Version 1.14.2 promotes SV-Drummer to its version-1 release numbering. The
build metadata, displayed GUI version and saved-state version marker now all
use the same `1.14.2` value; subsequent releases continue from this series.

Version 0.9.12 makes the sequencer LANE indicator mouse-wheel selectable,
brightens the relevant idle button borders and hover states, and adds distinct
Save As actions for Kits, Patterns, Pattern Sets and Projects. Normal Save now
prefills the active item's existing name, while Save As uses the original
generic name.

Version 0.9.13 retains the exact loaded Kit file for subsequent normal saves,
including Kits created before names were embedded in the file. Sequencer lanes
now follow the same effective mute/solo display rule as the drum pads: muted
lanes and every non-soloed lane are darkened whenever Solo is active.

## Permanent identity

- Product: `SV-Drummer`
- Manufacturer: `sl23`
- Manufacturer code: `sl23`
- Plug-in code: `svd1`
- Bundle ID: `com.sl23.svdrummer`
- Version: `1.14.2`

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
- Mouse-wheel pad Volume adjustment with a four-pixel level bar inside the
  right side of every pad; the existing Volume knobs remain available.
- MAIN stereo output plus sixteen optional stereo pad outputs. Every pad defaults
  to MAIN and has a visible OUTPUT selector in the Pad Settings title row.
- Per-pad amplitude envelope with Attack, Decay, Sustain and Release.
- Switchable per-pad multimode filter with LPF, BPF, HPF, Notch, Comb, Formant
  and Ladder modes, plus resonance and drive. Filter type and 6/12/24/48 dB
  slope are selected from scrollable drop-downs.
- Independent per-pad low-cut HPF for removing unwanted low end.
- Switchable stereo-linked compressor on every pad, with Threshold, Ratio,
  Attack, Release and Knee controls.
- Switchable soft-saturation and hard-clipping intensity controls on every pad.
- Sixteen-channel per-pad mixer with Volume, Pan, Output, Mute, Solo, Delay Send
  and Reverb Send; the two effect sends are stored with Kits and host state.
- Global Delay and Reverb returns on MAIN, with independent wet-return ducking;
  Delay also has adjustable duck Attack and Release. All settings are stored
  with Projects and host state.
- Global -60 to +6 dB master Volume in the top-right header, applied to MAIN
  and every AUX output with click-free gain smoothing.
- Sixteen per-pad choke groups; pads sharing a non-zero group cut each other off.
- High-detail per-pad waveform editor with draggable playback START and END markers.
- Per-pad sample looping with a LOOP switch, NORMAL/PING-PONG playback and
  draggable LS/LE waveform markers.
- Exact sample-frame START, END, L-START and L-END positions with wheel editing.
- Optional zero-crossing SNAP for click-resistant trim and loop boundaries.
- Continuous, time-ordered waveform tracing through every zero crossing.
- Cursor-centred mouse-wheel waveform zoom for precise START and END editing.
- Horizontal mouse-wheel tilt nudges the visible range of a zoomed waveform.
- Draggable full-sample overview bar for scrolling the zoomed waveform.
- Browser Preview button for auditioning a selected sample without loading a pad.
- Right-click an audio file in the Browser tree to preview it immediately.
- Portable settings under `dist\Data\Settings`.
- Portable GUI zoom restored from `dist\Data\Settings\SV-Drummer.ini` and
  saved when the editor closes.
- Portable Samples, Kits, Patterns and Projects folders.
- Icon tabs for Samples, Kits, Patterns and Projects, plus one shared Browser
  Refresh button.
- Every Browser mode has an Open Folder button for its active library location.
- Sixteen-lane host-synchronised step sequencer with sample-accurate triggering.
- Global Pattern playback lane beneath Lane 16, with sixteen Pattern-selection
  cells per bar and `OFF`/Pattern 1–16 mouse-wheel editing.
- Saved amber Pattern-chain enable LED in the lane header; disabling the chain
  preserves its cells while allowing the currently selected Pattern to repeat.
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
- Pattern MIDI notes support MANUAL, momentary GATE and latched HOLD modes.
- Portable `.svpattern` files containing one Pattern button's sixteen Sequences.
- Portable `.svpatternset` files containing the complete set of sixteen Patterns.
- Portable `.svkit` files containing all sixteen pads and their settings.
- Portable `.svproject` files containing the complete Kit and Pattern Set.
- Browser file choosers for loading Kits and individual Patterns from anywhere
  on disk, plus a PATTERNS menu command for loading complete Pattern Sets.
- Drum-pad Clear, Copy and Paste context commands for complete pad assignments
  and settings.
- Missing Project or Kit samples can be relinked from a chosen folder without
  changing any saved pad settings.
- Kit, Pattern Set and sequencer data saved in the host-managed plug-in state.
- Browser folders, selection and scroll position restore when the editor reopens.
- ASCII-only header status text for reliable display on Windows hosts.

LFO controls remain reserved for a later stage.

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
`MANUAL` preserves the original behaviour: assigned MIDI notes select patterns
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
from the ruler divider. MIDI MODE now uses grey for MANUAL, blue for GATE and a
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
sixteen-Pattern Set, Pattern MIDI-note assignments, MIDI mode and SNAP setting.
Projects default to `Data\Projects` and can be dragged onto
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

Version 0.7.13 makes Pattern Set loading transport-safe. Loading a
`.svpatternset` always leaves the Sequencer stopped and clears any pending GATE
or HOLD latch; it never starts playback merely because a set was loaded.

The second host-automation stage adds each pad's MIDI note, Mute, Solo, Reverse,
Choke Group, Attack, Decay, Sustain, Release and Loop controls. Together with
the v0.7.12 controls, PHI and other hosts now see 212 automatable parameters.
The earlier parameter IDs and ordering remain unchanged, and sample marker
positions remain internal because their valid ranges depend on the loaded audio
file.

Version 0.7.14 separates manual Project loading from transport state. Newly
saved `.svproject` files write `sequencerEnabled="0"`, and loading a Project
always stops the Sequencer even when an older file contains
`sequencerEnabled="1"`. MIDI Mode is still restored, but playback remains under
the user's control.

Browser root folders are now ordered automatically. Built-in portable folders
remain pinned at the top in their intended order, while user-added sample
folders are sorted alphabetically by folder name. Subfolders and files retain
their existing alphabetical sorting, and saved tree openness continues to use
full paths so sorting does not lose the focused Browser location.

Version 0.7.15 adds the third host-automation stage. Every Sequencer lane now
exposes its Division and Loop Length as independent PHI/DAW parameters, adding
32 controls and bringing the visible automation total to 244. Division values
use the same musical names as the interface, while Loop Length is constrained
to the current Pattern length and that lane's Division. Existing parameter IDs
and ordering remain unchanged because these controls are appended after the
v0.7.13 parameter set.

Pattern selection is deliberately not automated in this stage because Pattern
replacement must be marshalled safely away from the real-time audio callback.
Sample markers also remain internal because their valid ranges depend on the
length of the loaded sample.

Version 0.7.16 adds a complete per-pad filter stage to PAD SETTINGS. Each pad
has its own 20 Hz–20 kHz Cutoff, Resonance, 0–24 dB Drive and selectable Off,
LPF, BPF, HPF or Comb Type. A separate Off–2 kHz HPF is available as a simple
low-cut control, independently of the selected main-filter Type.

All five controls are Kit data. They are preserved in `.svkit` and `.svproject`
files and in the host-managed plug-in state, without becoming part of Pattern
or Pattern Set files. They also add 80 stable PHI/DAW automation parameters,
bringing the visible total to 324 while retaining all earlier parameter IDs and
ordering.

REVERSE, LOOP and SNAP now occupy the left side of the waveform's header row.
START, END, L-START and L-END remain together on the right, leaving the lower
Pad Settings row available for the five new filter controls.

Version 0.7.17 adds a switchable compressor to every pad after its filter stage.
The compressor uses stereo-linked detection to preserve the pad's left/right
balance and provides Threshold (-60–0 dB), Ratio (1:1–20:1), Attack
(0.1–100 ms), Release (10–1000 ms) and Knee (0–24 dB). `COMP OFF` is a true
bypass and changes to `COMP ON` when enabled.

Compressor settings are Kit data and are preserved in `.svkit`, `.svproject`
and host session state, while remaining independent of Patterns and Pattern
Sets. The six controls per pad add 96 PHI/DAW automation parameters, bringing
the visible automation total to 420 without changing earlier parameter IDs or
ordering.

Version 0.7.18 reorganises PAD SETTINGS into two control rows with left-aligned
AMP, FILTER, COMPRESSOR and SATURATION headings and vertical section dividers.
The instruction line beneath the controls has been removed and the panel's
vertical margins tightened, keeping the waveform useful without increasing the
overall interface size.

Volume, Pan and Tune have moved from the Sequencer back into the AMP section and
now use the standard knob colour. The AMP section is arranged as A/D/S/R above
Volume/Pan/Tune/Choke. Filter Drive now remains active when Filter Type is OFF,
allowing it to colour a pad independently of the main filter shape.

SATURATION adds separate 0–100% `SAT` soft-saturation and `HARD CLIP` intensity
knobs, following the compressor in each pad's processing chain. Both are Kit
settings stored in `.svkit`, `.svproject` and host session state, not Patterns.
The two controls per pad add 32 PHI/DAW automation parameters, bringing the
visible total to 452 while preserving all earlier parameter IDs and ordering.

Version 0.7.19 increases the base interface height so both PAD SETTINGS control
rows can use larger knobs with a clearer gap between them. The extra height is
also applied to the Sequencer panel without changing or rearranging any of its
controls. PAD SETTINGS reserves a taller control area while leaving more height
for the waveform editor than the previous two-row layout.

SAT now uses a continuous soft-clipping transfer based on the supplied TAL Drum
reference sweep. HARD CLIP is a true variable-threshold clipper with no clean
signal mixed back in, producing clearly flat waveform peaks at stronger values.
Both nonlinear stages compensate for Pad Volume and constant-power Pan before
processing and restore them afterwards, so quiet or centred pads are distorted
properly rather than merely becoming louder.

Version 0.7.20 reduces the two PAD SETTINGS knob rows to the same 66-pixel row
height used by the Sequencer's top-bar knobs. The overall interface and tab
panel are reduced by the same 38 pixels, preserving the expanded waveform
editor height and the existing ten-pixel gap between the two control rows.

Projects in the Browser now load by double-click instead of being dragged from
the Project tree. An empty current Project is replaced immediately; when the
current Kit contains samples or the Pattern Set contains steps, SV-Drummer asks
for confirmation first. Pattern and Pattern Set Browser behaviour is unchanged.

Version 0.8.0 is Stage 8. Empty pads now use 35% of their original colour
brightness, down from 45%, while leaving their text unchanged. SAT's soft-clip
shape is approximately 27% stronger; the HARD CLIP transfer is unchanged.

Every drum pad now has a right-click menu. `Clear` confirms before restoring the
pad to its default empty state, including its default MIDI note and every pad
setting. `Copy` and `Paste` transfer the sample assignment, sample path, trim and
loop markers, MIDI assignment, AMP, filter, compressor and saturation settings.
The clipboard lasts for the current plug-in instance.

Right-clicking an audio file in the Samples Browser now previews it immediately,
using the same audition function as the Browser's Preview button.
Dedicated Load buttons in the Kits and Patterns tabs open file choosers for an
arbitrary `.svkit` or `.svpattern`; an occupied destination is confirmed before
replacement. `PATTERNS` > `MENU` > `Load Pattern Set` similarly browses for an
arbitrary `.svpatternset` and confirms before replacing a non-empty set.

Version 0.8.1 makes the CHOKE control display `OFF` immediately when its loaded
value is zero. It also adds a global seventeenth Sequencer row labelled
`PATTERN`. The row always contains sixteen fixed 1/16-note cells per bar;
mouse-wheel over a cell selects `OFF` or Pattern 1–16. A numbered cell switches
to that Pattern at its playback boundary, while `OFF` marks the end of the
configured chain.

The Pattern playback row is a sixteen-slot playback chain. Each slot selects one
of the sixteen Patterns and remains highlighted while that Pattern plays for its
full saved bar length. Playback then advances to the next chain slot; an `OFF`
slot either stops the SV-Drummer sequencer or returns it to slot 1 when chain
Loop is enabled. An untouched row whose first slot is `OFF` leaves normal
single-Pattern looping unchanged. The chain belongs to the
complete Pattern Set, so it is stored in `.svpatternset`, `.svproject` and host
session state, but not in individual `.svpattern` or `.svkit` files.

Version 0.8.2 restores PAD VOL, PAD PAN and PAD TUNE to the Sequencer top bar
while retaining the same controls in Pad Settings. Both sets edit the same
per-pad Kit values; the Sequencer copies follow the selected lane and adopt its
pad colour.

The Pad Settings Filter Type control is now a mouse-wheel-enabled drop-down
containing LPF, BPF, HPF and COMB. Filter, Compressor and Saturation use compact
clickable amber status LEDs aligned with their section headings. Switching the
Filter LED off bypasses the filter type, Drive and the independent low-cut HPF
as one complete section.

A third main tab, FX, contains separately bordered global Delay and Reverb
panels. Delay provides Time, Feedback and Mix; Reverb provides Size, Damping,
Width and Mix. Each effect has its own amber enable LED. Global FX process the
complete drum mix in this original implementation, remain outside Kit and
Pattern data, and are saved in Projects and host session state. Version 0.10.0
replaces that insert routing with the per-pad sends described below.

Version 0.8.3 standardises the FX controls to the same knob dimensions, label
font, value font and styling used elsewhere in the interface. The only accented
variants remain the Sequencer's PAD VOL, PAD PAN and PAD TUNE controls, whose
colour follows the selected pad. The SEQUENCER, PAD SETTINGS and FX tab buttons
now also have identical widths.

Delay TIME can switch between FREE milliseconds and host-synchronised timing.
SYNC offers `1`, `1/2`, `1/3`, `1/4`, `1/4T`, `1/8`, `1/8T`, `1/16`, `1/16T`,
`1/32`, `1/32T` and `1/64`. The feedback path now uses smoothly changing delay
time, bandwidth limiting and soft saturation to avoid zipper noise and harsh
digital feedback buildup. FREE/SYNC state and the selected division are saved
with Projects and host session state.

Version 0.8.4 changes the Pattern row from a bar-following step lane into the
sixteen-slot Pattern chain described above. Its active slot is independent of
the bar playhead and stays selected for the complete length of the Pattern it
launches. Right-clicking a pad-lane header now provides Copy, Paste, Random,
Clear and one-level Undo; the Pattern-chain header provides Clear and Undo.
The Delay FREE/SYNC control now sits beside its knobs, and disabling the Filter
section bypasses Drive and low-cut HPF as well as the selected filter type.

Version 0.8.5 adds a saved Loop setting to the Pattern-chain header menu. With
Loop off, the chain stops at its first `OFF` slot or after slot 16. With Loop on,
that boundary returns to slot 1 and the Pattern row changes to dark aqua. The
new Sequencer SYNC MODE cycles through PLAYED, BAR and BEAT: PLAYED switches
immediately, while BAR and BEAT defer MIDI-triggered and clicked Pattern changes
to the next matching host boundary. Sync Mode is exposed as a host parameter
and is saved in Projects and host session state. The Delay FREE/SYNC button now
sits to the left of TIME, and the Pad Settings section titles are larger with
the Saturation LED moved into the right margin.

Version 0.8.6 adds a saved amber enable LED to the Pattern-chain header. When
disabled, the complete chain remains stored and editable but playback ignores
it, allowing the selected Pattern to repeat and MIDI/manual Pattern changes to
remain dynamic. HOLD mode now keeps the current Pattern playing while a BAR- or
BEAT-synchronised MIDI selection waits for its boundary. Per-pad filters add
Formant and nonlinear Ladder types plus a separate scrollable 6, 12, 24 or
48 dB slope selector. Filter slope is included in Kits, Projects, pad copy/paste
and host session state.

Version 0.8.7 adds a Notch filter and corrects the filter Type/Slope combo-box
interaction. Popup choices are no longer overwritten by the 20 Hz UI refresh,
wheel movement is accumulated instead of skipping entries, and smaller arrows
and fitted text keep the selected value readable. MIDI MODE `SELECT` is renamed
to `MANUAL`. Each pad now stores a NORMAL or PING-PONG loop mode; Ping-Pong
reflects playback between LS and LE. Mouse pad and waveform audition also send
note release when the mouse is released, using the existing AMP Release time.

Version 0.8.8 makes each Filter Type/Slope wheel event move exactly one entry,
removing multi-item jumps and stale accumulated movement. Their selected-value
font and control height are larger while retaining the compact arrow. Tilting a
mouse wheel left or right over a zoomed waveform now nudges its visible range
horizontally, while the existing vertical wheel action continues to zoom.

Version 0.9.0 adds a fixed multi-output VST3 layout: stereo MAIN plus sixteen
optional stereo buses now named AUX 1 through AUX 16. All pads default to MAIN, so
ordinary stereo operation is unchanged. The Pad Settings title row contains a
visible OUTPUT selector for the selected pad. A pad routed to an individual
output is removed from MAIN, preventing doubled audio. Auxiliary outputs are
dry for host processing; the global Delay and Reverb, and Browser preview,
remain on MAIN. Hosts must enable or connect the optional output buses before
they can be heard.

These names identify logical stereo plug-in buses rather than fixed sound-card
connectors. If a host flattens every enabled bus into one channel list, MAIN is
channels 1+2, AUX 1 is 3+4, AUX 2 is 5+6, and so on. A DAW or host can instead
route any of those buses to mixer tracks, MAIN, or any available hardware
output pair.

Output assignments are per-pad Kit settings. They are preserved in `.svkit`,
`.svproject`, pad Copy/Paste and host session state, but never in Patterns or
Pattern Sets. Sixteen appended `Pad N Output` host parameters expose the
assignments without changing any established parameter IDs or indices.

Version 0.9.1 prevents paired Windows wheel callbacks from skipping entries in
the Filter Type and Slope selectors. A short same-direction debounce makes one
physical wheel movement advance exactly one item. Each drum pad also gains a
duplicate Volume workflow for evaluation: scrolling anywhere over the pad
except its MIDI-note field adjusts the existing per-pad Volume in 0.5 dB steps.
A four-pixel vertical track on the pad's inside-right edge shows the complete
-60 to +6 dB range in the background and the current pad-coloured level in the
foreground. The AMP and Sequencer Volume knobs remain unchanged in this test.

Version 0.9.2 fills the complete pad-volume reference track with the same grey
previously used for its outline. Scrolling a pad now also displays the resulting
Volume value in a temporary tooltip for two seconds; continued scrolling resets
the two-second display time.

Version 0.9.3 restores the dark hollow pad-volume track and makes its outline
more obvious by increasing it from 24% to 48% opacity and from 0.75 to 1 pixel.
The wheel-adjustment value now uses an independent popup which remains visible
continuously and closes two seconds after the final wheel movement.

Version 0.9.4 moves the pad-volume popup to the left side of the cursor so it
does not cover the volume bar. The smaller popup now shows only the value and
unit, for example `-2.9 dB`.

Version 0.9.5 replaces the AMP section's duplicate Volume knob with a per-pad
bipolar `CURVE` control. `0.00` preserves the previous linear attack, negative
values produce a faster concave rise and positive values produce a slower
convex rise. The audible AMP attack and live waveform envelope use the same
curve. CURVE is Kit data and is included in `.svkit`, `.svproject`, pad
Copy/Paste and host session state. Pad-wheel Volume and the Sequencer's Pad
Volume knob remain available.

The OUTPUT selector is moved up two pixels to centre it in the PAD SETTINGS
header. Filter, Compressor, Saturation, global Delay and global Reverb now use
five-millisecond dry/wet bypass ramps, preventing abrupt On/Off discontinuities
during live playback.

Version 0.9.6 changes the negative AMP CURVE to the true mirrored counterpart
of the existing positive curve. Fully left now produces a smooth concave attack
instead of the previous near-vertical initial rise, while the positive curve is
unchanged. Every rotary control is four pixels larger in diameter and its
indicator ring is two pixels thinner. Symmetrical bipolar controls such as
CURVE, PAN and TUNE now draw their coloured indicator arc outward from the
centre/zero position.

Version 0.9.7 adds a per-pad `TRIGGER`/`GATED` sequencer playback button to the
waveform header. TRIGGER retains the existing one-shot behaviour. GATED treats
each active sequencer step as a note: the step start triggers the pad and the
next step boundary releases it through that pad's AMP Release setting. This
also gives looped samples a natural sequencer-controlled ending without an
abrupt cut. The mode is Kit data, is included in Projects, pad Copy/Paste and
host session state, and is exposed as a PHI/DAW parameter.

Version 0.9.8 moves the per-pad `TRIGGER`/`GATED` control from Pad Settings to
the Sequencer top bar beside PAD VOL, PAD PAN and PAD TUNE. Its button and
`PAD MODE` label always use the selected pad's colour; changing the mode now
changes only the button text.

Version 0.9.9 renames the sixteen logical auxiliary outputs from `OUT 1–16` to
`AUX 1–16` in both the VST3 bus names and the per-pad OUTPUT selector. MAIN and
every routing index remain unchanged.

Version 0.9.10 gives SNAP a distinct green active colour and draws a short
border-coloured link between LOOP and NORMAL/PING-PONG to show that the loop
mode belongs to the LOOP function. When one or more pads are soloed, every
non-soloed pad now receives the same visual darkening as a muted pad. Their Mute
indicators remain unchanged because those pads are only being silenced by the
Solo state.

Version 0.10.0 adds the MIXER below Delay and Reverb in the FX tab. Sixteen
compact channel strips are arranged as Pads 1–8 and 9–16. Volume, Pan, Output,
Mute and Solo mirror the same pad parameters shown elsewhere. Delay Send and
Reverb Send are independent post-fader, post-pan and post-pad-effect levels;
they respect Mute/Solo and may feed the MAIN effect returns even when the pad's
dry signal is assigned to an AUX output. Send levels default to zero, are Kit
data, copy and paste with a pad, save in Projects and host state, and are
available as host automation parameters. The global Delay/Reverb Mix controls
now set their return levels.

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
