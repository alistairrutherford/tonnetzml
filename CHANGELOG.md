# Changelog

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
