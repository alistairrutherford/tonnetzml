# Changelog

## Unreleased

### Changed

- **Bigger text throughout:** the header (modes, key, the sentence), the settings panel, the word
  bar's chips and the op palette, the capture strip, the keyboard strip (its bar and the operator
  keys' labels), the lattice's hints, tooltip and op labels, and the Info View are all larger
  again (controls 15.5 pt, captions 14). Note names and ghost-op chips on the lattice have larger
  minimum sizes, so they stay readable zoomed out. The strips are a few pixels taller to fit.

### Fixed

- **A crash opening the editor (1.0.2 and 1.0.3):** the settings panel asked the editor to lay
  itself out while the editor was still being built, and the editor's guard against that could
  read as set before it was initialised. It happened only now and then (it depends on what was
  in memory), but could take the host down with it. The guard is initialised first, and the
  panel no longer asks for a layout while it is constructed.
- **Notes the editor forgot:** a chord clicked on the lattice or keyboard strip in the same audio
  block as a reset (the host's first block, or a restored state) or a transport stop kept
  sounding while the editor forgot it, so it couldn't be let go from the editor. Each editor note
  is now numbered, and a release tells the editor which ones it actually ended; the rest stay
  held.

## 1.0.3 (2026-11-19)

### Fixed

- **Escape left the red trail behind:** Escape (and the lattice's *release*) let go of the chord
  but left the trail, the fading red line through the last chords, on the grid. Both now clear it;
  a chord still sounding (from a keyboard, say) starts a new trail, with no line back to the old
  chords.
- After a reset, the trail now starts again from the chord sounding at once, not only when the
  chord next changes.

## 1.0.2 (2026-11-19)

### Added

- **Settings panel:** the settings that used to float over the lattice's top-right corner are now
  a panel of their own, docked to the right of the lattice from the header down to the capture
  strip: a *settings* title line with *reset* and a fold button (›), then one line per group with
  its caption on the left. Fold it (›) to a slim edge to give the lattice the room; ‹ opens it
  again (saved with the set, as before). The lattice and its word bar now end where the panel begins.
- **Reset capture:** a *reset* button at the right of the capture strip forgets the chords captured,
  the take included, and clears the lattice's trail (the fading red line through the last chords),
  keeping only the chord made now so the next move starts a new trail. An armed take carries on,
  empty; notes still sounding stay in the capture from the reset. Dimmed while nothing is captured.
- **Reset settings:** a *reset* button on the settings panel's title line puts the panel's 18
  settings back to their defaults (Out, the channels, voicing, gather, Orbit, Walk, latch, the operator keys, the held key, In key, Preview tone, the test rig). Mode, Key, Slot,
  Enable, the capture toggles, the slots' words and `W` are left alone.
- Both reset buttons ask first: a click shows *sure?* in vermilion, and a second click within 3
  seconds acts. Both have Info View entries.

### Changed

- **Bigger, clearer labels:** the settings, the header's controls, the word bar, the
  capture strip, the keyboard strip and the lattice's hints are about 1.5 points larger, and the
  faint caption ink is darker in both themes. The capture strip and the keyboard strip's bar are a
  few pixels taller, so the lattice gives up 10 px.

### Fixed

- With an empty word, "none: the chord as played" was drawn over the word bar's ⌫ and ⌂ buttons.

## 1.0.1 (2026-11-18)

### Added

- **Info View:** a strip along the bottom of the editor, under the keyboard, like Live's own Info
  View. Point at any control, button or part of the window (the lattice, the word bar, the capture
  strip, the keyboard) and it shows what it does and what its settings mean; elsewhere, a short
  hint. Drawn in the editor's own ink, in both themes. The smallest window is still 760 × 520; the
  lattice gives up 42 px to make room.
- The README's screenshot shows the editor with the Info View.

## 1.0.0 (2026-11-11)

The first release. TonnetzML is a VST3 and Standalone MIDI plug-in for macOS that places the chord
you play on the Tonnetz, transforms it with a word of lattice operations, and sends the new chord
out as MIDI, voiced close to the one you played. [PLAN.md](PLAN.md) §12 has the full record of
each phase.

### What's in it

- **Lattice core (Phase 1):** the operations P, L, R, N, S, H, translations (+5 −5 +M3 −M3 +m3 −m3
  and any lattice vector), Flip, Pivot, Grow and Shrink, on triads and on other chords; words of
  up to 32 ops with a text form (`P L +5`) and reduction; the factory cycles PL, PR, LR and PLR;
  chord naming and spelling.
- **Mirror (Phase 2):** every chord comes out transformed by one of eight stored words (slots
  A–H). Held keys are gathered into a chord (20 ms window by default, 0 for clips), voiced with
  only the moved tones changing, and sent on their own channel (Mix: Generated, Source +
  Generated, or Source only). Non-note MIDI passes through.
- **Walk, Orbit and key (Phase 3):** an octave of operator keys (P L R N S H +5 −5 +M3 −M3 Undo
  Home) walks a held chord across the lattice; Orbit steps a cycle in time with the host (1/4 bar
  to 4 bars); latch; a key listener (Temperley's profiles) with Listen, Hold and Off, and *In key*
  for the moves that stay in it.
- **Lattice editor (Phase 4):** the editor is the lattice: click triangles to play, ghost
  neighbours to add ops (Explore), the word bar to edit `W`, the key band and Roman numerals, a
  keyboard strip with the operator zone, paper and night themes.
- **Capture and export (Phase 5):** the last 64 bars (or an armed take) as chord glyphs; drag a
  `.mid` out to a track or the Finder (*As played* or *Chords*, with the key signature), or save one.
- **Finish (Phase 6):** Preview tone (a small built-in voice, on by default in the Standalone) and
  hover audition; Pivot, Flip and Grow straight from the lattice; the editor's view saved with
  the set.
- **Tooling (Phases 0–6):** a file-driven test rig (`~/Music/TonnetzML/TestRig`), JUCE-free core
  tests (`Tests/run_tests.sh`), the headless snapshot app (`Tests/run_snapshot.sh`), pluginval at
  strictness 10, scripted Ableton Live checks for each phase (`LiveBridge/tests`),
  `Tools/install.sh` and `Tools/release.sh`.

### Requirements

- macOS 12 or later, Apple silicon or Intel (universal binary).
- A VST3 host that routes MIDI out of an instrument plug-in (tested in Ableton Live 12.4), or the
  Standalone.

### Known limits

- **The capture is not saved with the set:** reopening a set starts with an empty capture strip.
  Drag or save the MIDI before closing (PLAN.md §9, question 3).
- **Major and minor only** for the key listener; other modes are not guessed (§9, question 5).
- **Equal temperament only:** the just-intonation lattice is out of scope for 1.0 (§9, question 4).
- **The twelve operator keys are fixed** to their ops (the zone can move, the mapping can't).
- **The audio output is silent** unless Preview tone is on: TonnetzML is a MIDI effect shaped as
  an instrument, because Live only routes MIDI out of instrument-type VST3s.
- **Live was checked at one buffer size (1024) and one sample rate (44.1 kHz)**; the engine is
  swept at 32–2048 samples offline. No hardware-keyboard check of the operator keys was automated.
- **Not notarized yet** unless the zip's name lacks `-adhoc`: an ad hoc signed build runs on the
  Mac it was built on; elsewhere Gatekeeper blocks it until the quarantine is cleared
  (`xattr -dr com.apple.quarantine TonnetzML.vst3 TonnetzML.app`).
