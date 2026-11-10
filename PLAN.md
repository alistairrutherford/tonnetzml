# TonnetzML: Project Plan

*Tonnetz / Musical Lattices: a VST3 that generates new chords from played ones by moving them on
the chord lattice.* Written 2026-11-10. Working name **TonnetzML** (the folder's name; see §11).

A chord played on a keyboard, or arriving from a MIDI track, is placed on the Tonnetz. Lattice
operations (flips, slides, translations, growth) turn it into a new chord, voiced close to the
original, and sent out as MIDI in real time. What comes out can be recorded by another track or
captured inside the plug-in and dragged out as a MIDI file. The editor is the lattice itself, with
a keyboard along the bottom.

---

## 1. Goals

| # | Goal | Done when |
|---|------|-----------|
| G1 | Loads in Live 12 as a VST3 and sends MIDI to another track | `pluginval` strictness 10 passes. An instrument track with *MIDI From: TonnetzML track → TonnetzML* plays and records the output. |
| G2 | **Lattice model:** any chord is a point set on the Tonnetz; the operations of §3.2 are exact | Core tests agree with an independent reference for every operation × every chord type × 12 roots. |
| G3 | **Real time:** a chord from a keyboard or a clip comes out transformed, with no stuck notes | Clip chords transform on the same sample; hand-played chords within the gather window (§3.3). Fuzz test leaves nothing sounding. |
| G4 | **Walk:** operator keys, lattice clicks or a tempo-synced cycle move the chord step by step | A recorded take of a cycle (e.g. `PL` at 1 bar) equals the offline render, every note on the grid. |
| G5 | **Voicing:** the output keeps common tones in place and moves the others by the smallest step | For P, L, R on a triad exactly one voice moves, by 1 or 2 semitones, in every inversion and register. |
| G6 | **Capture and export:** the generated chords are kept and can be dragged out as a .mid | The dragged file equals the take Live recorded from the same performance (pitch, velocity, start, length to 0.001 beat). |
| G7 | **A new interface:** the lattice shows played and generated notes live; the keyboard tries out chords | Snapshot tests of the lattice for known chords; played by mouse in Live (computer use). |
| G8 | Tested in Live automatically through the Ableton MCP plug-in at every phase | Each phase in §8 ends with a scripted Live check whose result is written into §12. |
| G9 | **Key guess:** listens to the source notes and guesses their key, as ChordMonkey does; the lattice shows it | ChordMonkey's key corpus passes unchanged; a clip in a known key played in Live shows that key, and the guess is unmoved by the generated chords. |

**Not goals (v1):** audio processing; a chord-pattern workstation (that is ChordMonkey); a scale that
constrains the notes (the guessed key informs, it does not force); just intonation or microtuning (the lattice is the 12-tone equal-tempered torus); AU, AAX, Windows.

---

## 2. The theory, as the plug-in uses it

**The lattice.** A node is an integer vector `(a, b)`; its pitch class is `7a + 4b mod 12`. One step
right is a perfect fifth (+7), up-right a major third (+4), down-right a minor third (+3). Drawn as a
triangular grid, every upward triangle `(a,b) (a+1,b) (a,b+1)` is a major triad and every downward
triangle `(a,b) (a+1,b) (a+1,b−1)` a minor triad.

```
        E ─── B ─── F♯── C♯          → perfect fifth (+7)
       ╱ ╲   ╱ ╲   ╱ ╲   ╱           ↗ major third  (+4)
      C ─── G ─── D ─── A            ↘ minor third  (+3)
     ╱ ╲   ╱ ╲   ╱ ╲   ╱
    A♭── E♭── B♭── F             △ C E G = C major    ▽ C E♭ G = C minor
```

In equal temperament the plane wraps into a torus: `(0,3)` and `(4,−1)` are both pitch class 0, so
there are 12 nodes and 24 triangles. The editor draws the unrolled plane (it repeats) and the
engine works on pitch classes, so the two never disagree.

**Spelling comes free.** A node's place on the line of fifths is `a + 4b`, which fixes its letter
and accidentals (C G D A E B F♯ … to the right, F B♭ E♭ … to the left). The lattice is labelled from
that, re-anchored around the camera so nothing needs more than a double accidental.

**A chord is a set of vectors.** Any pitch-class set has an embedding: for each tone pick the copy
of its node that makes the shape most compact (smallest sum of pairwise lattice distances, ties to
the copy nearest the previous chord). Triads are one triangle; maj7 and m7 are two triangles
sharing an edge; dom7, sus, dim and aug are looser shapes but still valid point sets.

**Operations are affine maps of that vector space,** which is what lets them apply to any chord and
not only triads:

| Op | Geometry | On pitch classes | C major → |
|----|----------|------------------|-----------|
| **P** | point-reflect through the midpoint of the chord's **fifth** edge | `x → p + q − x`, `{p,q}` the fifth | C minor |
| **R** | … of its **major-third** edge | same, `{p,q}` the major third | A minor |
| **L** | … of its **minor-third** edge | same, `{p,q}` the minor third | E minor |
| **N** | `R L P` (in that order) | | F minor |
| **S** | `L P R` (slide) | | C♯ minor |
| **H** | `L P L` (hexatonic pole) | | A♭ minor |
| **T(v)** | translate by a lattice vector | `x → x + 7a + 4b` | ±fifth: G, F; ±M3: E, A♭; ±m3: E♭, A |
| **Flip(p,q)** | reflect through the midpoint of any two chord tones | `x → p + q − x` | the general case of P, L, R |
| **Pivot(p)** | reflect through one chord tone | `x → 2p − x` | keeps one common tone |
| **Grow(e)** / **Shrink** | add the vertex across edge `e` / remove the last added | adds one tone | across E–G: Cmaj7; across C–E: Am7 |

- P, L, R on a triad are the neo-Riemannian operations exactly (checked in G2). On other chords they
  are the same reflection applied to every tone: C7 under P is C–E♭–G–A (dominant ↔ half-diminished,
  the classic seventh-chord parsimony).
- **Choosing the edge (as built in Phase 1).** A chord carries an *anchor* (one of its tones: the
  root when it comes in) and a *parity* (0 when it comes in). If the chord has several edges of
  the needed interval (Cmaj7 has two fifths), the one used is the first by: it contains the anchor;
  its midpoint is nearest the anchor around the pitch-class circle; then `(p − anchor) mod 12`
  smallest at parity 0, `(anchor − p − k) mod 12` at parity 1. Every reflection maps the anchor
  with the other tones and flips the parity; a translation moves the anchor. This makes the
  choice commute with every op, which is what keeps P, L and R involutions on *every* chord (the
  first plan, "the edge containing the root, then the lowest", broke `P P` on Cmaj7: after P the
  namer's root is A♭, whose fifth edge is the other one). For triads the anchor never matters.
  Flip, Pivot and Grow take their tones as semitones above the anchor, so a stored word works on
  any chord. If the chord has no such edge (Csus4 has no thirds), the op is unavailable:
  greyed on the lattice, and a no-op from an operator key.
- Every op is a bijection on the chord's tones (Grow apart), so each output voice has one source
  voice. Voicing (§3.4) and note-offs (§3.3) rest on that.
- Cycles fall out as words: `PL` walks a hexatonic cycle (6 chords), `PR` an octatonic one (8), `LR`
  all 24 triads. These are the factory cycles for Orbit (§3.1).

---

## 3. Functional design

### 3.1 One state, four ways to drive it

The engine holds a **word** `W`: a short list of ops (up to 32). The output is always
`W(input chord)`. The ways of playing differ only in who edits `W`:

| Mode | Who edits `W` | Use |
|------|---------------|-----|
| **Mirror** | nobody: `W` is one of eight stored slots (parameter `slot`, A–H) | every chord from the keyboard or clip comes out transformed the same way |
| **Walk** | **operator keys**: an octave of keys (default C0–B0: P, L, R, N, S, H, ±fifth, ±M3, Undo, Home) each append one op and restrike; a new input chord keeps `W` (Relative) or clears it (Reset) | right hand holds a chord, left hand walks it across the lattice |
| **Orbit** | the host clock: appends the next op of a cycle word every Rate (1/4 … 4 bars) while a chord is held or latched | a held chord becomes a progression, locked to the song |
| **Explore** | the editor: clicking a ghost neighbour appends its op; clicking any triangle or node set plays it outright | finding chords by mouse, with no input at all |

Adjacent identical involutions cancel (`P P` = nothing), so Undo is just "append the inverse".
**Latch** keeps the last input chord as the source after its keys are released.

### 3.2 Input: finding the chord

- Notes on the operator-key zone never sound. Everything else is the source chord.
- **Gather window** (0–80 ms, default 20): note-ons within the window of the first are one chord.
  At 0 only notes on the same sample group, which is exactly right for a quantized clip and adds no
  delay. Hand-played chords need the window; the output chord sounds when it closes.
- A note added to a held chord after the window re-evaluates the chord and **retunes**: only voices
  whose pitch changes get a note-off and note-on.
- A single note, or two, is still a point set: ops that need a missing edge are unavailable,
  translations and Pivot always work.

### 3.3 Output

- Each output voice is bound to its source voice, so releasing a source key releases its image and
  velocities carry across. Stop, bypass, mode changes and deleting the device release everything
  (one `VoiceTracker`, as in the sibling plug-ins).
- **Mix:** Generated only / Source + Generated / Source only (bypass with the lattice still live).
  Source and Generated go out on separate MIDI channels (defaults 1 and 2) so a recording track can
  take either.
- Timing is sample-accurate inside the block; Orbit steps are placed from the host's ppq, including
  across loops, locates and tempo changes.

### 3.4 Voicing

Applied op by op, so a long word still leads smoothly:

- A reflected voice goes to the nearest pitch of its new pitch class (at most 6 semitones; a tie
  goes toward the chord's centre). Common tones do not move.
- A translation moves every voice by the same interval, up or down by whichever keeps the chord's
  centre nearest the source's (**Register: Stay**), or literally (**Register: Follow**, the chord
  climbs as the walk goes on).
- **Bass:** Off / Root below (adds the new root one or two octaves under the chord).
- Output is clamped to 0–127 by octave shifts of the whole chord, never by dropping a voice.

### 3.5 Capture and export

- **To a recording track:** nothing to do in the plug-in beyond G1. In Live: an armed MIDI track
  with *MIDI From: TonnetzML track → TonnetzML*.
- **Capture:** the audio thread writes every generated note (and, optionally, source note) to a
  fixed lock-free queue stamped in beats; the message thread keeps the last 64 bars
  (retrospective, always on) and, when **Capture** is armed, an open-ended take. Beats come from
  the host's ppq while it plays, else from a free-running clock at the host's tempo.
- **Export:** *Drag MIDI* (drag out of the editor into Live or Finder, via
  `performExternalDragDropOfFiles`) and *Save .mid…*. Type 1 file, 960 ppq, tempo, meter and track
  names written. Two renderings: **As played** (exact) and **Chords** (one block chord per chord
  change, snapped to the grid, for editing).
- The capture strip in the editor (§5) is the same data: one glyph per chord, in order.

### 3.6 Key guessing

ChordMonkey's listener, copied (`KeyListener.h`, `KeyProfiles.h`): a pitch-class histogram of what
is played (onsets plus held time, velocity-weighted, halving every 12 s of listening), matched
every 50 ms against Temperley's major and minor profiles over the 24 keys, with its hysteresis (a
first guess after 2 s as the best; a change only when another key wins by 0.05 for 1.5 s, or at all
for 4 s) and its "no guess" for too few notes or a poor fit.

What differs here, because TonnetzML has no scale that drives its notes:

- **It listens while you play.** ChordMonkey's Analyse stops chord generation while it listens;
  here the key changes nothing that sounds, so `key` = **Listen** runs alongside every mode.
- **Only source notes are heard.** Generated chords are chromatic by design (one `PL` step leaves
  the key) and would drag the guess around, so they never reach the listener. Operator keys
  don't either.
- **`key`:** Off / Listen / Hold. *Hold* freezes the current guess (the equivalent of ChordMonkey's
  *Use guess*) into `keyTonic` and `keyMinor`, which can also be set by hand. Leaving Hold for
  Listen starts a fresh analysis.

What the key is used for:

- **On the lattice:** the key's seven notes are a compact band of nodes (for C major: F C G D on
  one row, A E B above); it is tinted, the tonic marked, and a tentative guess drawn fainter than a
  confident one. A walk visibly leaves and re-enters the key.
- **Spelling:** note and chord names are spelled in the key (G♭ major shows C♭, not B).
- **Numerals:** the header and capture strip add the chord's numeral in the key
  (`Cmaj7 (I) ──P·L──► A♭maj7 (♭VI)`).
- **In key (optional, `inKey`):** ghost neighbours whose result is diatonic are drawn solid and
  the rest faint; nothing is blocked. In Orbit, *In key* skips cycle steps that land outside it.
- **Export:** the .mid carries the key signature.

### 3.7 Parameters (host-visible; target ≤ 28)

`enable`, `mode` (4), `slot` (A–H), `latch`, `walkInput` (Relative/Reset), `orbitCycle` (PL, PR, LR,
PLR, slot's word), `orbitRate`, `gatherMs`, `mix`, `sourceChannel`, `genChannel`, `register`,
`bass`, `octave` (−2…+2), `opZoneStart`, `opZoneOn`, `capture`, `captureSource`, `key` (Off/Listen/Hold),
`keyTonic`, `keyMinor`, `inKey`, `previewTone`, `testRig` (§7). The eight slot words, the current `W`, Latch's chord and the lattice camera are
plug-in state, not parameters.

**Preview tone:** a small built-in polyphonic voice (off by default in a host, on in the
Standalone) so the keyboard and lattice can be tried with nothing routed.

---

## 4. Architecture

```
 MIDI in ─► ZoneRouter ─┬─► ChordGatherer ─► LatticeEngine ─► Voicer ─► VoiceTracker ─► EventQueue ─► MIDI out
 (host, editor keys,    │      (window)        W(chord)                                    │
  lattice clicks,       └─► operator keys ──► WordEditor ◄── OrbitClock (host ppq)         └─► CaptureQueue ─► CaptureModel ─► .mid
  test rig)             └─► KeyListener (source notes only) ─► KeyGuess (atomic) ─► editor
                                                   ▲                                                        (message thread)
                                                   └── editor (lock-free command FIFO)
 audio thread ──── SnapshotExchange ───► editor: source set, generated set, W, embedding, available ops
```

### 4.1 Modules

| Path | Contents | JUCE? |
|------|----------|-------|
| `Source/core/lattice/` | `Lattice.h` (vectors, torus, pitch class, spelling), `Embedding.h` (compact embedding), `Ops.h` (the table in §2), `Word.h` (parse, print, reduce, apply), `Cycles.h` | no |
| `Source/core/engine/` | `ZoneRouter.h`, `ChordGatherer.h`, `Voicer.h`, `OrbitClock.h`, `LatticeEngine.h` (the block processor), plus `VoiceTracker.h`, `EventQueue.h`, `MidiEvents.h`, `Transport.h`, `SnapshotExchange.h`, `KeyListener.h` copied from ChordMonkey | no |
| `Source/core/theory/` | `ChordNamer.h`, `KeyProfiles.h`, `RomanNumeral.h` and the major/minor part of `Scales.h` (copied from ChordMonkey, for labels, root finding and the key) | no |
| `Source/core/midiio/` | `CaptureQueue.h`, `CaptureModel.h`, `MidiFile.h` (copied) | no |
| `Source/plugin/` | `TonnetzProcessor`, `Parameters`, state, preview voice | yes |
| `Source/ui/` | all new (§5): `LatticeView`, `LatticeGeometry`, `KeyboardStrip`, `WordBar`, `CaptureStrip`, `Hud`, `Ink` (colours, fonts) | yes |

Rules carried over from the sibling projects because they paid off: the core is header-only with no
JUCE dependency and compiles with plain `clang++`; no allocation, locks or strings on the audio
thread (fixed arrays; checked with a malloc hook); the editor reads snapshots and writes commands,
never engine state.

**Reused from the siblings: engine plumbing, test rigs and build tooling. Not reused: anything in
`Source/ui`** (`Theme`, `ParamControl`, `Panels`, `Keyboards`, `PresetBar`): see §5.

### 4.2 Identity and build

Projucer project `TonnetzML.jucer`, JUCE 8.0.14 at `~/Development/JUCE`, C++17, VST3 + Standalone,
universal, macOS 12+. `pluginIsSynth, pluginWantsMidiIn, pluginProducesMidiOut` (Live only routes
MIDI out of instrument-type VST3s; the audio output is silent unless Preview tone is on).
Manufacturer `ARut`, plug-in code `Tnzl`, bundle ID `com.alistairrutherford.tonnetzml`,
VST3 category `Instrument,Tools`. `Tools/install.sh` (build, ad-hoc sign, install for this user,
`--validate` runs pluginval), as ChordMonkey's.

---

## 5. UI

The sibling plug-ins share one look: a dark page of boxed panels, combo boxes and bar sliders with
an amber accent. TonnetzML uses none of that. **The lattice is the whole window;** controls float
over it and there are no panels, combo-box rows or sliders.

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ TonnetzML  ◐ Mirror Walk Orbit Explore   ◉ C major 91%   Cmaj7 ──P·L──► A♭maj7  ⦿ Cap │
│                                                                                      │
│            E ─────── B ─────── F♯────── C♯────── G♯                                  │
│           ╱ ╲       ╱ ╲       ╱ ╲       ╱ ╲     ╱                                    │
│          ╱   ╲  L  ╱   ╲     ╱   ╲     ╱   ╲   ╱        ░ source chord (teal fill)   │
│         C ─────── G ─────── D ─────── A ─────── E       ▓ generated chord (vermilion)│
│        ╱ ╲░░░░░░░╱ ╲       ╱ ╲       ╱ ╲       ╱        ◎ common tone (both rings)   │
│       ╱ R ╲░░░░░╱ P ╲     ╱   ╲     ╱   ╲     ╱         ┄ trail of the walk          │
│      A♭────── E♭────── B♭────── F ─────── C             P L R  ghost neighbours      │
│        ┄┄┄┄┄┄▓▓▓▓▓▓                                                                  │
│                                                                                      │
│  word  [ P ][ L ][ + ]   ⌫  Home      slots  A B C D E F G H      Rate ♩ 1 bar       │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ capture  ▽ △ △▽ ▽ △ △ ▽ △▽ …                                   ⇩ Drag MIDI   Save…   │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ ▌▌ ▌▌▌ ▌▌ ▌▌▌ ▌▌ ▌▌▌   keyboard: op zone tinted, source keys teal, generated vermilion│
└──────────────────────────────────────────────────────────────────────────────────────┘
```

**Lattice view**
- An endless plane: drag to pan, scroll or pinch to zoom, double-click to recentre. The camera
  eases to keep the source and generated chords in view (Follow, on by default).
- Node discs carry spelled note names. A sounding node pulses with its velocity; a chord is the
  filled hull of its nodes (triangles for triads, rhombi for sevenths, a looser outline otherwise).
- **Two inks:** source in teal, generated in vermilion, common tones ringed in both. On a light
  "paper" ground with ink-weight lines by default, with a dark variant; either is unlike the
  siblings' panels.
- **Ghost neighbours:** the chords one op away from the generated chord are outlined and labelled
  with their op. Hover names the result ("L → E minor, 1 voice moves 1 semitone") and auditions it
  if Preview tone is on; click appends the op (Explore and Walk).
- **Direct play:** click a triangle to play its triad, drag across triangles to glide through
  them, shift-click nodes to build any set. These are source chords like any other.
- **Trail:** a fading line through the centres of the last chords, so a walk is visible as a path.
  A cycle draws its closed loop.

**Key.** The key's band of nodes is tinted on the lattice with the tonic marked (§3.6). The
header shows the guess and its fit (`◉ C major 91%`, hollow while tentative); click it to cycle
Listen / Hold / Off, right-click to set the key by hand or toggle *In key*.

**Word bar.** The word is a row of chips, not a menu: click `+` for an op palette laid out as the
lattice directions, drag chips to reorder, drop one off the bar to delete. Slots A–H store words;
the header shows the result as a sentence (`Cmaj7 ──P·L──► A♭maj7`).

**Capture strip.** One small glyph per captured chord (its lattice shape, in its ink), scrolling
left. Click a glyph to recall that chord as the source; drag the handle out for the MIDI file
(Alt: Chords rendering).

**Keyboard strip.** Full width, 88 keys, scrollable. Shows source and generated notes in the two
inks and tints the operator zone, with each operator key labelled. Click to play; **Sticky**
(default on for the mouse) holds clicked keys so a chord can be built one key at a time, with a
*Release* button; the computer keyboard plays a two-octave window.

**Mechanics.** Plain `juce::Graphics` vector drawing, repainting only the lattice layer that
changed, on a 60 Hz timer fed by the snapshot. Resizable from 760 × 520 with no fixed layout
grid: the lattice takes whatever the three bottom strips leave. Every host parameter still has an
on-screen control (checked by the snapshot test), but as HUD toggles, chips and click-to-cycle
labels rather than generated rows.

---

## 6. Milestones

Each phase ends with its core tests, the snapshot tests, `pluginval` 10 and its Live check (§7.4).

| Phase | Builds | Live check (Ableton MCP) |
|-------|--------|--------------------------|
| **0. Skeleton** | `.jucer`, processor passing MIDI through, empty editor, `install.sh`, test scripts, test rig (§7.3). Probe the bridge: `bridge_command list_commands`, and whether it can create and launch a clip. | Appears under VST3 › Alistair Rutherford; loads; MIDI through to a second track records the test rig's notes exactly. |
| **1. Lattice core** | `core/lattice` complete with the reference fixture (§7.1). No plug-in change. | none (core only) |
| **2. Mirror** | Router, gatherer, engine, voicer, mix and channels, parameters. Editor still a stub with a word text field. | Rig plays I–V–vi–IV; takes with `slot` = P, L, R, N, `PL` equal the offline render at two tempos and two buffer sizes; meters at 0 after stop, bypass, delete. |
| **3. Walk, Orbit and key** | Operator keys, word editing, latch, orbit clock. `KeyListener` on the source notes, the `key` parameters, the guess written to the rig's status file. | Rig holds one chord and presses operator keys; Orbit `PL` and `LR` at 1 bar and 1/4 over a loop (session clip); every note on the grid. Rig plays a tune in D major and one in F minor with `slot` = `PL`: the status file shows each key, confident, with the generated chords sounding throughout. |
| **4. Lattice editor** | `LatticeView`, keyboard strip, word bar, HUD, both inks, trail, ghosts, direct play, the key band, spelling and numerals in the key, *In key*. | Computer use on the editor in Live: click a triangle, a ghost, build a chord on the sticky keyboard; recorded notes match what was clicked. Screenshot kept in `docs/`. |
| **5. Capture and export** | Capture queue and model, strip, both renderings, drag-out, save. | Record a walk on track 2 while capturing; drag the capture onto track 3; `get_clip_notes` of both are equal (G6). |
| **6. Finish** | Preview tone, Grow/Shrink, Flip/Pivot from the lattice (click an edge or node), state save and restore, README. | Save the set, restart Live, reopen: slots, word, latch chord and camera are back and the same take records again. |

P1 is phases 0–5. Phase 6's Grow/Shrink and free Flip/Pivot are P2 and can slip without hurting the rest.

---

## 7. Testing

### 7.1 Core (no JUCE): `Tests/run_tests.sh`

- **Reference fixture:** `Tools/gen_lattice_fixture.py`, an independent brute-force implementation
  in plain Python (pitch-class arithmetic only, no lattice code), writes
  `Tests/fixtures/lattice_reference.json`: every op × 30 chord types × 12 roots, the PLR group's
  24 elements, cycle lengths. Committed, so tests need no Python.
- `test_lattice`: torus identities, spelling of every node in a 9 × 7 window, embeddings (triads
  are triangles, maj7/m7 are rhombi, embeddings are transposition-invariant).
- `test_ops`: against the fixture; P, L, R are involutions; `N = RLP`, `S = LPR`, `H = LPL`;
  unavailable ops; word reduction never changes the result.
- `test_voicing`: G5 over every triad inversion and register; no voice ever moves more than 6
  semitones per reflection; clamping at both ends of the keyboard.
- `test_engine`: gather window (same-sample, 5–80 ms spreads, late notes retuning), per-voice
  release, mix and channels, operator zone, latch, swept over 44.1/48/96 kHz × 32–2048-sample
  blocks.
- `test_orbit`: steps on the grid across loops, locates, tempo changes, block boundaries.
- `test_keys`: ChordMonkey's test and corpus, unchanged (the listener must score what it scores
  there); plus, through the engine: the guess is the same with Mix on Generated or Source +
  Generated and any word, operator keys are not heard, Hold freezes, Listen after Hold starts
  empty, *In key* marks exactly the diatonic neighbours for all 24 keys.
- `test_capture`: queue overflow, beat stamps stopped and playing, both renderings, .mid round trip.
- `test_edge` **fuzz:** random blocks of notes, operator keys, mode and parameter changes, stops and
  resets; nothing left sounding, no allocation.

### 7.2 Snapshot and host

- `Tests/run_snapshot.sh` (a small JUCE app around the real processor and editor, as the siblings'):
  reference images of the lattice for fixed chords and words in both themes and at three sizes;
  hit-testing (a click at a triangle's centre plays that triad, for all 24); every parameter has a
  control; state round trip; malloc hook during playback.
- `Tests/run_validation.sh`: `pluginval` strictness 10. `Tests/run_bench.sh`: block time.

### 7.3 Test rig

The bridge cannot create a clip with notes or launch one (checked again in Phase 0, 2026-11-10: §12), so,
as in ChordMonkey, the plug-in carries a rig: with `testRig` on it reads
`~/Music/TonnetzML/TestRig/Keys.txt` (`beat note velocity length` per line, up to 512; `#` comments)
and plays those keys as if they came from the host while the transport runs (`core/engine/TestRig.h`).
Operator keys are just notes in the file. Keys are placed from the host's ppq, so start them at
beat 4 or later: an arrangement take starts at beat 0 after Live's count-in, and nothing before the
start position is played. While the rig is on the plug-in also writes `Status.txt` beside it (Phase 0: keys loaded, rejected
lines, playing, note-ons and note-offs sent, keys held; later phases add the key guess with its fit
and state, the current word, the source and generated chord names), so a Live check can read what
the editor would show.

**Clip-fed checks without clip creation (found in Phase 0).** A take recorded from the rig is an
arrangement clip, and arrangement clips play with the transport. So "fed from a MIDI track" is
checked in two takes: (1) rig on, record the plug-in's pass-through (Mix = Source only from Phase 2) on **TZ Clip**; (2) rig
off, TZ Clip disarmed with its input on *No Input*, TZ Src *MIDI From: TZ Clip* with Monitor *In*,
record TZ Rec. The rig stays the main feed for determinism; session-clip (loop) checks still record
*to* a session slot, which the bridge can do.

### 7.4 Live in the loop (Ableton MCP plug-in, ClaudeLiveBridge on port 9873)

Standard set, built by `create_project` per phase (`~/Music/Ableton/Projects/TonnetzML Phase N`):
**TZ Src** (TonnetzML, input No Input or a clip track), **TZ Rec** (Drift, *MIDI From: TZ Src →
TonnetzML*, armed), optionally **TZ Clip** (source chords) and **TZ Drop** (for dragged files).

Each check is: write `Keys.txt` → `set_parameter` (mode, slot, …) → `start_recording` →
`stop_recording` → `get_clip_notes` on TZ Rec → compare with the same scenario rendered offline by
the core (`Tests/render_scenario`, which prints the expected notes as JSON) → `get_meters` at 0.
Tolerance 0.001 beat.

Known bridge behaviour to script around (from ChordMonkey's log, and Phase 0 here): loading a
plug-in arms its track and exclusive arm disarms the recorder (re-arm it); so does setting a
track's Monitor to *In*; a track whose input is *No Input* cannot be armed or disarmed (the bridge
raises "cannot be armed": set arm before the routing); choice parameters are set as 0–1 values;
`start_recording`'s `length_beats` did not stop the take (call `stop_recording`); Live counts in
(one bar, plus about a second before the transport moves: wait the take's length + 4 s before
`stop_recording`); the results of `start_recording` and `stop_recording` are read before Live acts
(`record_mode`, `is_playing` and `current_song_time` in them are stale: read `get_state` after);
arrangement takes over a loop make take lanes, so loop checks record to a session clip; a leftover
later clip is what `get_clip_notes` returns unless `clip_index` is given (or
`clear_arrangement_clips` first); Live caches a VST3's parameter list in a saved set, so reload the
device after adding parameters. **A newly installed plug-in is not in Live's browser until Live
rescans** (*Settings → Plug-Ins → Rescan*, or a restart); the bridge cannot trigger that, and
`refresh_index` only re-reads the browser. An updated build of a known plug-in needs no rescan, only
the device reloaded. Notes come back within about 0.00005 beat (one sample at 120 BPM); the
tolerance stays 0.001.

Editor checks in Live (phases 4–6) use computer use on the plug-in window with the bridge reading
back the recorded notes. Synthetic modifier keys did not reach JUCE before, so Alt-drag is covered
by the snapshot app and a manual check.

**State (2026-11-10, Phase 0):** Live 12.4.16 with the bridge up; the probe and the first Live
check are in §12.

**Manual checklist:** a hardware keyboard on the operator zone; drag-out landing at the drop
point; pan and zoom on a trackpad; resizing.

---

## 8. Risks

| Risk | Mitigation |
|------|------------|
| The gather window delays hand-played chords | 20 ms default, 0 for clips; alternative "sound the source at once, retune when the window closes" behind a switch if 20 ms feels late. Decided by playing it in Phase 2. |
| Ops on non-triads surprise the ear (loose shapes, unavailable ops) | The lattice always shows which ops are available and what they give before they are chosen; fixture covers 30 chord types; Mirror on an unavailable op passes the chord through rather than dropping it. |
| Live does not route VST3 MIDI out as expected | Proven by three sibling plug-ins with the same configuration; rechecked in Phase 0. |
| Drag-out of a file from a VST3 window into Live | Proven in Chordism and ChordMonkey; Phase 5 Live check. |
| Lattice repaint cost at large sizes | Layered painting with a cached grid image; benchmark in the snapshot app; cap at 60 Hz. |
| Too many ops for an octave of operator keys | Fixed twelve in v1 (§3.1); remapping is state, not parameters, if wanted later. |
| Automated editor tests in Live are slow and brittle | Keep them few (one per phase); the snapshot app carries the weight of UI testing. |

---

## 9. Open questions

1. **Name:** TonnetzML, or something else before the plug-in code and bundle ID are fixed in Phase 0?
2. **Theme:** light "paper and ink" by default with a dark variant, as planned, or dark first?
3. **Capture in the saved set:** the plan saves words, slots and the latch chord but not the capture
   buffer. Should the last capture survive a set reload?
4. **Just-intonation lattice** (the 5-limit plane without the torus wrap, via MPE or pitch bend):
   out of scope for v1; worth a v2?
5. **Modes beyond major and minor:** ChordMonkey maps a guess onto its 13 scales; here the guess is
   major or minor only, since nothing else needs a scale. Enough?

---

## 10. Repository layout

```
TonnetzML.jucer        Source/core/{lattice,engine,theory,midiio}   Source/plugin   Source/ui
Tests/   run_tests.sh  run_snapshot.sh  run_validation.sh  run_bench.sh  test_*.cpp
         render_scenario.cpp  fixtures/  Snapshot/  FakeHost.h  TestUtil.h
Tools/   install.sh  gen_lattice_fixture.py
LiveBridge/tests/      one script per Live check (pytest, the bridge's port 9873)
docs/    PLAN.md (this file)  README.md  screenshots
```

---

## 11. Naming

"TonnetzML" is the folder's name (ML = Musical Lattices). It is used throughout as a working name:
the namespace `tonnetz`, the processor `TonnetzProcessor`, the user folder `~/Music/TonnetzML`.

---

## 12. Status

| Phase | Status | Notes |
|-------|--------|-------|
| 0. Skeleton | **Done (2026-11-10)** | **Built:** `TonnetzML.jucer` (VST3 + Standalone, `ARut`/`Tnzl`, `com.alistairrutherford.tonnetzml`, synth with MIDI in and out, JUCE 8.0.14) and its Xcode project; `Source/plugin/TonnetzProcessor` (namespace `tonnetz`): MIDI in to MIDI out untouched, silent stereo output, parameters `enable` and `testRig` only, state = the APVTS tree; `Source/ui/TonnetzEditor`: a placeholder page (760 × 520, resizable), nothing from the siblings' UI. Core (JUCE-free, `Source/core/engine`): `MidiEvents.h`, `Transport.h`, `SnapshotExchange.h` copied from ChordMonkey, and **`TestRig.h`** (§7.3): `parseRigText` (locale-free; `#` comments, commas or spaces, bad lines counted, 512 keys) and `RigPlayer` (audio thread, fixed arrays): keys placed from the host's ppq, continuous across blocks (takes up where the last block ended if the host's ppq is within 2 samples), a loop wrap inside a block handled as two segments, held keys ended on stop, locate, loop wrap, rig off, Enable off, bypass and reset; two keys on one pitch restrike. The processor polls `Keys.txt` 20 times a second on the message thread (modification time and size), hands the keys to the audio thread through a `SnapshotExchange`, and writes `Status.txt` (at most 4 times a second). **As built, beyond the plan:** Enable off silences the rig but MIDI still passes; `testRig` is saved with the set like any parameter (a set saved with it on plays `Keys.txt` when reopened); `setRigFolder` lets tests use a temporary folder. `Tools/install.sh` (`--validate`, `--no-build`, `--uninstall`, `--juce`, `--no-standalone`; builds in `dist/build`), `Tests/run_tests.sh`, `run_validation.sh`, `run_snapshot.sh` with `Tests/Snapshot` (console app, real processor and editor), `FakeHost.h` (ChordMonkey's, made generic: `Host::process` is any block processor), `README.md`. `run_bench.sh`, `render_scenario` and `LiveBridge/tests` are not started (the Live check below was run by hand through the MCP tools). **Checks:** `Tests/run_tests.sh`: `test_rig` 19/19 (parser; placement, same-pitch, stop, locate, rig off and three loop styles, each over 26 rate × block × tempo configurations, worst 1 sample). `Tests/run_snapshot.sh`: 28/28 (editor images at 760 × 520 and 1200 × 800 against `Tests/fixtures/snapshot`; MIDI through byte for byte playing, stopped, bypassed, Enable off; the rig from a real `Keys.txt` on the sample with host MIDI merged; `Status.txt`; **0 allocations in `processBlock`** under the malloc hook; state round trip, garbage ignored). `Tools/install.sh --validate`: universal build, ad-hoc signed, installed, **pluginval strictness 10 PASS**; `--uninstall` and `--no-build` exercised. **Live 12.4.16 (project `TonnetzML Phase 0`, 120 BPM):** listed under VST3 › Alistair Rutherford and loads (3 parameters with Device On) **after a manual Rescan** (below). TZ Src (TonnetzML, No Input) → TZ Rec (Drift, *MIDI From: TZ Src → TonnetzML*, armed): `Keys.txt` of C, G, Am, F one bar each from beat 4 plus three off-grid keys (5.3333, 10.75 of 1/16, 14.001; velocities 1 to 127), 15 keys. Arrangement take: **15/15 notes, pitch and velocity exact, worst start error 0.000012 beat, worst length error 0.000023**; `Status.txt` 15 on / 15 off / 0 held; `get_meters` 0 on every track after stop. A first take stopped early (beat 18.16) had the last chord cut exactly at the stop, nothing stuck. **Fed from a MIDI track:** the same take recorded on a third track (TZ Clip), then rig off, TZ Src *MIDI From: TZ Clip*, Monitor In: TZ Rec recorded the same 15 notes (worst 0.000046 beat), meters 0. **Bridge findings:** `list_commands` has 49 commands; for clips only `clip_slots`, `get_clip_notes`, `clear_session_clips`, `clear_arrangement_clips`, `load_clip_file`, `start_recording`, `stop_recording`. **It still cannot create a clip with notes or launch one:** there is no create/set-notes/fire command, and `start_recording` (session, with a track) refuses a slot that has a clip. `load_clip_file` on a .mid put in *User Library/Clips* returned "loaded" but no clip or track appeared (Arrangement view in front; not pursued, the file was removed). What works instead: an arrangement clip recorded from the rig plays with the transport and feeds the plug-in (§7.3, amended). New quirks, added to §7.4: a new plug-in needs Live's Rescan, which the bridge cannot do (the user clicked it; Live's `PluginScanner.txt` shows when it ran); *No Input* tracks cannot be armed or disarmed; Monitor In arms the track; `start_recording`/`stop_recording` results are stale; count-in plus start-up costs about 4 s. **Not done:** the set is unsaved in Live (the bridge cannot save: Cmd-S); two tempos and buffer sizes in Live (Phase 2's check). |
| 1. Lattice core | **Done (2026-11-10)** | Picked up here after the first Phase 1 session failed partway; its `Lattice.h`, `Embedding.h`, `theory/ChordNamer.h` and `theory/Scales.h` (already in the "Add files." commit) were reviewed and kept as they were. **Built:** `core/lattice/Ops.h`: `Chord` (pitch-class mask, anchor, parity), `PcMap` (each op is `x → ±x + t`, so every output tone has one source tone), `apply` for P, L, R, N, S, H (compounds atomic: all three steps or nothing), `Translate` (any lattice vector; the written interval `7a + 4b` kept for *Register: Follow*), `Flip`, `Pivot`, `Grow` (adds the third tone of the other triangle on an edge; unavailable unless exactly one of the two triangles is in the chord), `inverseOf`. `Word.h`: up to 32 ops; text `P L R N S H +5 -5 +M3 -M3 +m3 -m3 T(a,b) Flip(i,j) Pivot(i) Grow(i,j) Shrink` (spaces, commas, `·` or nothing between letters), `printWord`, `reduce` (cancels P P, L L, R R, H H, a translation and its opposite, Grow Shrink; keeps Flip/Pivot pairs, whose availability can differ, and N N, which is not an involution off the triads), `WordRunner` (op by op with the Grow history Shrink needs: the engine's walk) and `applyWord` (an unavailable op is skipped). `Cycles.h`: the factory cycles PL, PR, LR, PLR and `cycleOf`. **As built, beyond the plan:** the edge rule of §2 (anchor and parity) replaces "containing the root, then the lowest"; N, S, H are applied left to right (N = R then L then P), which matches the table's examples. `Tools/gen_lattice_fixture.py` (plain Python, pitch-class arithmetic and a breadth-first search of the lattice graph, no shared code) writes `Tests/fixtures/lattice_reference.json` (312 KB, committed; `--check` compares): 23 ops × 30 chord types × 12 roots × 2 parities, 12 words (with Grow/Shrink) × 30 × 12, brute-force embedding costs, the 9 × 7 window and 24 key spellings (scale letters assigned diatonically, an independent method), the PLR group order, 432 cycles. **Checks:** `Tests/run_tests.sh` passes: `test_rig` 19/19, `test_lattice` 21/21 (torus identities; `latticeNorm` = breadth-first distance; `nearestNode`, `torusDistance` by brute force; the window and the 24 keys spell as the reference and as ChordMonkey's `makeSpelling`; embedding costs equal the brute force for all 30 × 12; triads are triangles (major up), maj7/m7 rhombi; one shape per type in every key; the previous chord picks the copy; all 4095 sets embed in 15 ms, worst 0.3 ms; `chordRoot` right for 21 plain types and transposition-invariant), `test_ops` 51/51 (16 560/16 560 op results and 4 320/4 320 word results equal the reference; the textbook P, L, R, N, S, H on all 24 triads, each of P, L, R changing one tone by 1 or 2 semitones; C7 P = C E♭ G A; Grow → Cmaj7 / Am7; **P, L, R, H involutions on all 49 152 chord states**; N = RLP, S = LPR, H = LPL on all of them; the edge choice commutes with transposition and inversion; unavailable ops; parse, print, errors, 32-op limit; **reduction never changed the result** in 1 080 000 word applications; PL 6, PR 8, LR 24 chords from every triad; the PLR group has 24 elements). No plug-in change; the headers are listed in `TonnetzML.jucer`. `run_snapshot.sh` and `install.sh --validate` (pluginval strictness 10) still pass. **Notes for Phase 2:** voicing (G5's "exactly one voice moves") is the voicer's job: the op's map swaps the two tones of its edge, so the voicer must keep common tones by pitch class, not follow the map blindly; seed the anchor with `chordRoot`; ChordMonkey's namer reads A C E over C as C6 (ties go to the bass). No Live check (none planned). |
| 2. Mirror | Not started | |
| 3. Walk, Orbit and key | Not started | |
| 4. Lattice editor | Not started | |
| 5. Capture and export | Not started | |
| 6. Finish | Not started | |
