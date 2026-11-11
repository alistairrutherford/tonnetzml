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

*As built (Phase 3):* Undo removes W's last op (the same thing); a full W (32 ops) refuses
further ops rather than dropping its oldest. Explore takes the operator keys as Walk does, and
(Phase 4) the editor's ghost clicks and word bar, as Walk also does; in Orbit, Home restarts the cycle and Undo steps it back; in Mirror they do
nothing. Several keys on one sample give one retune. Orbit's W is the cycle's first k ops, k
counted modulo the cycle's length on the chord (`cycleOf`), so it runs past 32 ops for ever; a
line steps it if a chord was sounding just before the line, the retune after the line's other
input (a legato chord change on the line comes in at the step reached; a chord let go on a line
still counts that line); stopped, Orbit runs on the internal clock from each new chord. A new
chord is a source key going down with none down: Reset clears W / restarts the cycle then. Latch:
the next chord replaces the latched one when its window closes; stop, Enable off and bypass end
it but keep it (silent) until the transport next starts. *In key* in Orbit goes on through the
cycle past chords outside the key, for one lap at most.

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
  goes toward the chord's centre). Common tones do not move: voices are kept by pitch class, not
  moved along the op's map (which swaps the two tones of the edge it reflects through). N, S and
  H are voiced as their three reflections in turn, so S on C E G moves C and G up a semitone
  each (Phase 2).
- A translation moves every voice by the same interval, up or down by whichever keeps the chord's
  centre nearest the source's (**Register: Stay**), or literally (**Register: Follow**, the chord
  climbs as the walk goes on).
- **Bass:** Off / Root below (adds the new root one or two octaves under the chord; the root in
  any inversion, so A C E over C has the bass A, not the namer's C6).
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

*As built (Phase 5):* `core/midiio/` has `CaptureQueue.h` (audio thread: `CaptureClock`, the
tape; `CaptureQueue`, an SPSC FIFO of 16 384 events with a drop count; `CaptureWriter`),
`CaptureModel.h` (message thread: the last 64 bars, the take, `chordsOf`, `renderCapture`) and
`MidiFile.h` (ChordMonkey's, extended with several named note tracks and a key signature).
ChordMonkey has no capture queue or model to copy: its `ExportRecorder.h` (a ring the message
thread copies) was the model for the queue, not copied. **Beats** are a *tape* that never runs
backwards: playing, the host's ppq plus an offset fixed while the host plays continuously; at a
start, loop wrap or locate the offset moves on (less than a bar) to the host's place in the bar,
so the tape's bar lines are the host's and a whole-bar loop unrolls with no gap; stopped, it
runs on at the host's tempo. **What is captured:** generated = the output notes on the generated
channel (exactly what a recording track gets, G6), while Mix sends them; source (with
`captureSource`) = the source keys played in (not operator keys), whatever Mix sends, written
on the source channel; when Mix sends source on the generated channel itself they are in the
generated stream once. A captured note always gets its note-off (bitsets), whatever changes.
`capture` going on or off is marked in the queue on the block's first sample: the take starts
there (notes sounding join it) and ends there (notes sounding are cut); it is kept until the
next arming, and the strip and drag-out use it if there is one, else the last 64 bars. A full
queue drops events and the model ends every open note. **Files:** the file starts at the bar
line at or before its first note (tape bar lines); conductor track with name, tempo, meter and
the key in force's signature; the generated notes' track named like the file
("TonnetzML C major 4 bars", Live names the clip so), and a "TonnetzML source" track only if
source notes were captured. **Chords:** a piece of the pitch set shorter than 1/16 beat, or
shorter than a sixteenth and contained in the piece it touches (a spread or ragged chord),
joins its neighbour; snapped to the sixteenth; an overlap is cut at the next chord; one that
snaps to nothing gives way to the next. The capture isn't saved with the set (§9 question 3).

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

*As built (Phase 6):* `previewTone` is appended after `captureSource` (24 parameters; `testRig`
stays second, where Phase 0 put it). `core/engine/PreviewVoice.h`: 16 voices of a sine with a
quieter octave and twelfth, 4 ms attack, decay to 60 % over 1.2 s, ~220 ms release, velocity
sets the level, a 17th note steals; it plays what TonnetzML sends (every note of the block's
output, on its sample) and the editor's auditions (`EditorCommand::audition`: heard, never sent
or gathered). Bypass and the parameter going off silence it at once. The state also saves the
editor's view (a `VIEW` child: theme, Follow, Sticky, the settings column, zoom, camera, the
computer keyboard's octave; damaged values fall back to their defaults; a state without it
leaves the view alone; restored into an open editor, the editor takes it). The capture is not
saved (§9 question 3: answered no, for now).

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
`--validate` runs pluginval), as ChordMonkey's. `Tools/release.sh` (v1.0): the release zip in `dist/`, Developer ID +
notarized, or `--adhoc`.

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

*As built (Phase 5):* `ui/CaptureStrip` sits between the lattice and the keyboard (46 px).
Left: `⦿ capture` and `+ source` (the two parameters, HUD toggles) over a summary ("take · 4
bars", "last take · …", "last 12 bars"). Glyphs, 40 px each, newest at the right, the wheel
scrolls back; each is the chord's compact embedding (triangles filled, edges, nodes) in its
ink, named under it in root position, spelled in the key; a source chord comes before the
generated one it gives. Clicking a glyph holds its exact pitches as the lattice's set
(`LatticeView::holdSet`: only the voices that differ move), lit while held; a second click,
Release or a lattice click lets go. Right: `⇩ drag MIDI` (`performExternalDragDropOfFiles` of a
file written to `$TMPDIR/TonnetzML Export/`; Alt: Chords) and `save…` (a file chooser; Alt-click:
Chords).

*As built (Phase 6):* **free ops on the generated chord**: a click on one of its nodes Pivots
on it, on the middle of one of its edges Flips through it, on a node marked with a dashed ring
and + across one of its edges Grows into it (only where Grow is available); the palette has
Shrink (−g). They are appended wherever the word bar edits (Walk's `W`, the slot's word in
Mirror, the cycle in Orbit when it is the slot's) and are not offered where it can't. Hovering
a ghost or a free op names its result and, with Preview tone on, auditions it voiced near the
generated chord (common tones kept). Shift-click on nodes still builds sets.

**Keyboard strip.** Full width, 88 keys, scrollable. Shows source and generated notes in the two
inks and tints the operator zone, with each operator key labelled. Click to play; **Sticky**
(default on for the mouse) holds clicked keys so a chord can be built one key at a time, with a
*Release* button; the computer keyboard plays a two-octave window.

**Mechanics.** Plain `juce::Graphics` vector drawing, repainting only the lattice layer that
changed, on a 60 Hz timer fed by the snapshot. Resizable from 760 × 520 with no fixed layout
grid: the lattice takes whatever the three bottom strips leave. Every host parameter still has an
on-screen control (checked by the snapshot test), but as HUD toggles, chips and click-to-cycle
labels rather than generated rows.

*As built (Phase 4):* the editor writes to the engine only through a lock-free command queue
(`core/engine/EditorCommands.h`: notes, operator keys, a new W) and reads `EngineStatus` (now with
every source and generated note's velocity, the chord and W(chord) with W's anchor, and a release
count). Left-drag glides, so the plane pans with a right-, ⌘- or middle-drag or a trackpad's two
fingers; the wheel and a pinch zoom. Sticky (on by default) applies to the lattice as to the
keyboard: a clicked triangle or node set stays down until clicked again, replaced, or Release.
Ghost chips are clickable in Walk and Explore and are labels elsewhere; unavailable ops are drawn
crossed out where their direction points. The parameters without a place in the header or word bar
are a right-aligned column of HUD labels over the lattice's top-right corner (`settings ▴` folds
it). Node names are spelled literally from the tonic nearest the camera (or, with no key, from the
node nearest it as a major tonic), so the far plane shows double accidentals; chord names use the
key's spelling (C major's with no key); the generated chord is named in root position. Every shape
is also drawn faintly at its other copies on the torus. Numerals: `core/theory/RomanNumeral.h`
(ChordMonkey's convention; its file works on its own chord slots, so it was written anew). Not yet:
hover audition (Phase 6, done), the camera and theme saved
with the set (Phase 6, done).

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
Operator keys are just notes in the file. Keys are placed from the host's ppq, on their nearest
sample (Phase 3: before, a key in a block's last half-sample was put on that block's last sample,
one early, which let an Orbit line see a chord written on it as already sounding), so start them at
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
`refresh_index` only re-reads the browser. An updated build of a known plug-in needs no rescan, **but
Live keeps a VST3's code in memory for the whole Live session** (found in Phase 2): deleting and
reloading the device, or switching sets and back, still runs the build Live first loaded; only
a Live restart loads a new one (the Phase 2 build did load on opening the new set, the first
time this Live session met it). *Device On* off stops Live calling the device at all, so no
note-off can go out until it is on again (the plug-in then ends everything on its first block).
`load_device` sometimes times out and loads onto the *selected* track instead (Phase 2: it
replaced Drift on TZ Rec twice): check `list_tracks` after it, load the instrument back, and
re-route and re-arm TZ Rec, which also loses its input whenever TZ Src's device is deleted. Notes come back within about 0.00005 beat (one sample at 120 BPM); the
tolerance stays 0.001.
**Found in Phase 3:** a session clip recorded on TZ Rec keeps that track off the arrangement,
and a later arrangement take records the session clip's playback: clear the slots and set
`back_to_arranger` first. A session slot fired with `record_length` starts recording when Live
gets to it, not on a bar (0.19 beat into the song here): match the clip to the render at the
offset a note implies. "Restart Live" must be checked: `ps -o lstart=` on Live's process (or
`Started: Live` in Log.txt) shows whether it really restarted; a device on a stale Live shows the
old build's parameter count.

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
   buffer. Should the last capture survive a set reload? *(Phase 6: not for now; `captureSong`
   would give what to store.)*
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
Tools/   install.sh  release.sh  gen_lattice_fixture.py
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
| 2. Mirror | **Done (2026-11-11)** | **Built:** `core/engine/ZoneRouter.h` (source note / operator key / other; the zone is off until Phase 3 adds `opZoneStart`, `opZoneOn`; a note-off follows its key's note-on, so moving the zone never strands a key), `ChordGatherer.h` (held keys and the gather window in absolute samples: at 0 ms it closes after every event on its sample, longer windows are [open, open + length); a note after the window opens a window of its own and retunes the held chord when it closes), `Voicer.h` (§3.4: voices kept by pitch class, only replaced tones move, nearest pitch, ties toward the centre; Register Stay/Follow for translations; Grow/Shrink; Octave; Bass Root below; clamped by whole-chord octave shifts), `LatticeEngine.h` (the block processor: each output voice bound to its key, per-voice release, retunes only voices whose pitch changed, velocities carried; Mix Generated / Source + Generated / Source only on `sourceChannel` / `genChannel`, source at once and generated at the window's close; non-note MIDI through; stop and All Notes Off end everything; W or voicing changes while held retune; Mix or channel changes end and re-sound). `VoiceTracker.h`, `EventQueue.h`, `Clock.h` copied from ChordMonkey (namespace `tonnetz`; VoiceTracker cut to 320 sources of 4 notes; EventQueue unused until Orbit; the engine uses Clock's sample count). The processor feeds the host's notes plus the rig's keys into the engine (SysEx straight through), and gains `slot` (A–H), `gatherMs` (0–80, 20), `mix`, `sourceChannel` (1), `genChannel` (2), `register`, `bass`, `octave` (−2…+2), appended after `enable` and `testRig` (10 parameters; Live shows 11 with Device On). Slot words are state, not parameters: factory A P, B L, C R, D N, E PL, F PR, G LR, H S (`factorySlotTexts`), saved as a `SLOTS` child of the state (text, `printWord`), handed to the audio thread by a `SnapshotExchange`; a state without them (Phase 0's) or with one that doesn't parse gets the factory word. The editor is still a stub with one field: the current slot's word (`parseWord`/`printWord`; refused text shown in red; follows `slot`). `Status.txt` adds slot, word, source and generated chord names, notes sounding, and (in the build committed, not the one Live ran: below) sample rate, block and longest block. `Tests/render_scenario.cpp` (Keys.txt → RigPlayer → LatticeEngine, the plug-in's loop, as JSON in beats). `LiveBridge/tests/phase2_mirror.py` and `phase2_release.py` (plain Python on the bridge's port 9873) run the Live check. **As built, beyond the plan:** N, S and H are voiced through their three reflections (S on C E G: C♯ E G♯ with every voice moving 1, where the one-map voicing gave G♯ E C♯ moving −4 and −6); the bass is the root in any inversion (`voicer::rootOf`: the namer reads A C E over C as C6); *Enable off* = MIDI through, minus duplicate note-ons and note-offs with no note-on, so flipping it either way leaves nothing stuck; output keeps the engine's own order on a sample (an offs-first sort swapped a voice's start and release on one sample: found by the fuzz); non-note events go out only while half the output buffer is free (as VoiceTracker keeps room for note-offs). **Checks:** `Tests/run_tests.sh` passes: test_rig 19/19, test_lattice 21/21, test_ops 51/51, **test_voicing 30/30** (G5: P, L, R on 3 960 triad voicings, 24 triads × 3 inversions and 3 open shapes × every register: exactly one voice moves, by 1 or 2; N, S, H on 1 296 voicings move no voice more than 2; 20 000 random words over random chords: no reflection moves a voice more than 6, common tones never move, the voicing's pitch classes equal the word's chord after every op; translations, Grow/Shrink, bass, octave, clamping at both ends, nothing dropped), **test_engine 26/26** (same-sample chords, 5–80 ms spreads, a key down and up inside the window, late notes retuning one voice, release on the window's closing sample, per-voice release and velocities, the three mixes, one shared channel, channels 16 and 9 with a controller through, the operator zone, word and Mix changes while held, stop, Enable off/on, and P, L, R, N, PL, +5, `S H` on I–V–vi–IV against the voicer, each over 26 rate × block × tempo configurations, worst 0 samples), **test_edge 6/6** (1 500 fuzz runs, 133 789 blocks, 562 732 notes out, random 1–4096-sample blocks at 8–192 kHz with loops, locates, stops, releaseAll, every parameter changing, garbage MIDI: clean and nothing left sounding; a 2 000-event block and a full buffer; **0 allocations in the engine** under libmalloc's logger hook), and a render_scenario smoke test. `Tests/run_snapshot.sh` 54/54 (new reference images of the stub; the word field; through when bypassed and Enable off, non-notes through when enabled; the rig as Source only; every factory slot on C E G; the gather window, a late note, channels 3 and 9, Bass with Octave; the rig under PL with Status.txt; the ten parameters and defaults; state round trip of every parameter and the slot words, Phase 0 states and bad words; **0 allocations in processBlock**). `Tools/install.sh --validate`: **pluginval strictness 10 PASS** (on both builds). **Live 12.4.16, 44.1 kHz** (project `TonnetzML Phase 2`: TZ Src = TonnetzML, No Input, Test rig on, gather 0; TZ Rec = Drift, *MIDI From: TZ Src → TonnetzML*, armed). Keys.txt: C, G, Am, F, one bar each from beat 4, 3.5 beats long, velocities 100/90/80. Arrangement takes with slot A–E (P, L, R, N, PL) at **120 and 97 BPM and buffers of 1024 and 256 samples: 20/20 takes equal `render_scenario` note for note** (12 notes each, pitch and velocity exact, worst start and length error under 0.000001 beat; e.g. P gives C E♭ G, G B♭ D, A C♯ E, F A♭ C; PL gives C E♭ A♭, G B♭ E♭, G♯ C♯ E, F A♭ C♯), every meter 0 within 0.4 s after each stop. One more take at the default 20 ms window: the chords at beat 4.04, as rendered. The 256-sample buffer was set by the user (Live's audio settings are out of the bridge's reach) and **is on the user's word**: Live's Log.txt only logs the buffer when the audio device restarts, and the build that reports its block size in Status.txt never ran in Live (below). Also, the render is block-size independent at gather 0, so the second buffer size tests the plug-in in Live rather than the renderer. **Release** (one long chord): transport stop → meters 0 after 1.9 s; *Device On* off while it sounds → Drift still sounds 2 s later (Live stops calling the device), back on → the recorded chord ends at beat 10.73, as Device On came back (≈10.5 plus the bridge's latency), meters 0 after Drift's tail; deleting the device while it sounds → meters 0 after 8.5 s (Live ends the notes). A first 97 BPM run cut the last chord at the stop (the script's wait didn't scale the count-in with tempo; fixed, rerun). **Not done / caveats:** Live ran the build without the block-size lines in Status.txt: Live keeps a VST3's code for its whole session (§7.4, corrected), and a restart wasn't asked for, since the engine is the same in both builds; the operator zone, latch and Walk are Phase 3. **Notes for Phase 3:** add `mode`, `latch`, `walkInput`, `orbitCycle`, `orbitRate`, `opZoneStart`, `opZoneOn`, `key…` after `octave`; the zone is `EngineSettings::zone` (`ZoneRouter.h`, `operatorIndex` gives 0–11; operator keys are swallowed in `LatticeEngine::handle`, ready for their ops); Walk can drive `voiceWord`'s step function op by op from the last voicing (the engine re-voices from the input today: Mirror); EventQueue and Clock are in place for Orbit; a re-evaluation already retunes only changed voices, which is what a Walk step needs; to test a rebuilt plug-in in Live, restart Live (or the check runs the old build). |
| 3. Walk, Orbit and key | **Done (2026-11-11)** | **Built:** `core/engine/WordEditor.h` (the twelve operator keys P L R N S H +5 −5 +M3 −M3 Undo Home, `wordedit::apply`, `opsInKey`: which of the ten op keys land in a key), `OrbitClock.h` (grid lines every Rate, 1/4, 1/2, 1, 2 or 4 bars, from the host's bar start, multi-bar rates counted from ppq 0; each line on its nearest sample, taken once in continuous time, one up to a sample before a block, wrap or locate taken there; worked out from Clock's segments every block), `theory/KeyBand.h` (a key's seven notes, `KeyRef`), and ChordMonkey's `KeyListener.h` and `KeyProfiles.h` copied unchanged (namespace `tonnetz`; `RomanNumeral.h` not needed until Phase 4's numerals). `LatticeEngine.h` rewritten around one time-ordered loop per block (on a sample: Orbit's line, a window of > 0 ms closing, the input, a 0 ms window closing, then one retune for whatever changed W): modes Mirror / Walk / Orbit / Explore, Walk input Relative / Reset, latch (`ChordGatherer` keeps latched keys), Orbit (W = the cycle's first k ops mod its length; `voiceOps` in `Voicer.h` voices an op sequence of any length), *In key* in Orbit, the key listener on source note-ons and note-offs only (Listen; Hold freezes; Off forgets; leaving Hold or Off for Listen starts afresh), `WalkState` (Walk's W, Orbit's k, the latched chord) out every block and back by `restore`. Parameters appended after `octave` (21 now; Live shows 22 with Device On): `mode`, `latch`, `walkInput`, `orbitCycle` (PL PR LR PLR Slot), `orbitRate` (default 1 bar), `opZoneStart` (default 24, named as Live names keys: C0), `opZoneOn` (default on), `key` (Off/Listen/Hold, default Listen), `keyTonic`, `keyMinor`, `inKey`. The processor hands `EngineStatus` (W, its length, Orbit's step, the latched chord, the guess, the key in force) to the message thread through a `SnapshotExchange`, saves a `WALK` child (`word`, `orbitStep`, `latched="60:90 64:90"`) and restores it through another (taken on the next block; `getWalkState` answers from the request until then), and on its timer copies the guess into `keyTonic`/`keyMinor` when Key goes Listen → Hold (not when a set saved in Hold loads). `Status.txt` adds `mode`, `wordOps`, `walk`, `orbitStep`, `latch`, `latched`, `keyMode`, `key`, `keyFit`, `keyState`, `keyNotes`, `keyInForce`; `word` is now W as it stands. The editor stub adds a line: mode · W · key (reference images updated after looking at them). `render_scenario` takes `--mode --latch --walk-input --cycle --orbit-rate --zone --key --key-tonic --key-minor --in-key --loop` and prints each note's `ppq`, the final key guess, `orbitStep` and Walk's word. `Tools/gen_key_fixture.cpp` (built against `../chordmonkey/Source`, read only) wrote `Tests/fixtures/chordmonkey_own_chords.txt` (209 KB): the notes ChordMonkey's engine plays for its key test's 192 "own chords" cases. `LiveBridge/tests/phase3_walk_orbit_key.py`. **As built, beyond the plan:** §3.1's *as built* note (Undo = remove the last op, a full W refuses ops, Orbit's lines and steps, latch's sleep and wake, Explore = Walk for now, operator keys in Orbit and Mirror); the operator zone applies in every mode (as §3.2 says), so C0–B0 never sound by default even in Mirror (`opZoneOn` off gives them back); the test rig places keys on their nearest sample (§7.3: it put a key in a block's last half-sample one sample early, which made Orbit step a chord written on a line at once); Orbit doesn't use `EventQueue.h` (its lines are recomputed from the grid each block, which keeps them on it through loops and locates with nothing to move); the key in force is `keyTonic`/`keyMinor` under Hold, the guess under Listen, none under Off; *In key* uses natural minor. **Checks:** `Tests/run_tests.sh` passes: test_rig 19/19, test_lattice 21/21, test_ops 51/51, test_voicing 30/30, **test_engine 36/36** (Phase 2's 26 plus Walk: P, L, +5, Undo, Home on a held chord retune only the moving voices on their sample; two keys on one sample, one retune; Relative / Reset with a late note; Explore as Walk, Mirror ignoring the keys; a full W; latch replacing at the next window and ending at latch off; stop, then the transport starting, re-sounds the latched chord; a mode change resounds; a restored W and latched chord, silent until play; each over 26 rate × block × tempo configurations, worst 0 samples), **test_orbit 28/28** (PL, LR, PR, PLR at 1 bar and PL at every rate, off-grid entry, 3/4 time, 2-bar and 3.5-beat loops in all three host loop styles, a locate, tempo changes to 90 and 150 BPM, Relative and Reset on a bar-line chord change, latch, stopped on the internal clock, Home/Undo in Orbit, the slot's word as the cycle, *In key* in C major and A minor, 60 bars of LR: each against grid lines worked out from the host's ppq and the voicer, worst 0 samples), **test_keys 53/53** (ChordMonkey's test with its corpus unchanged: profiles, 13/13 corpus cases right in all 12 keys, **180/192 own chords** plus the 12 known Canon-sevenths cases, Temperley beats Krumhansl-Kessler 193 to 169, modulation, silence and ambiguity, weighting, hysteresis, fuzz; ChordMonkey's 13-scale mapping checks replaced by the two scales here, its Analyse-mode engine checks by: the engine's guess equals the listener's alone in 48/48 runs per tune, D major and F minor, over 4 modes × 2 mixes × 6 words, generated notes sounding; 200 operator-key presses unheard; Hold freezes, Listen after Hold or Off starts empty, Off hears nothing; `opsInKey` exact on 80 640 op × chord × key cases), **test_edge 6/6** (1 500 fuzz runs, 131 672 blocks, 566 871 notes, now also every mode, latch, walk input, cycle, rate, key setting, *In key*, the zone, operator-key bursts and state restores: clean, nothing left sounding, **0 allocations**; also clean on six more seeds, 9 000 runs, not committed), render_scenario's Mirror and Orbit smoke checks. `Tests/run_snapshot.sh` **74/74** (Phase 2's checks plus: operator keys from the host walk a held chord (P; L and +5 on one sample; Home) and never sound, no zero-length notes, the listener hears the 3 source notes and no operator keys; the rig walks C E G with P, L, −M3 (Status.txt `mode: Walk`, `word: P L -M3`); Orbit PL at 1/4 bar from the rig steps on beats 5, 6, 7 (`orbitStep: 3`); 0 allocations in processBlock while walking and while orbiting; a D major tune from the rig under P L gives `key: D major`, `keyState: confident`, 42 notes heard; Listen → Hold copies D into Key tonic; the 21 parameters, defaults and value names; every Phase 3 parameter round trip; a set saved in Hold keeps its key; a latched, walked chord saved (`WALK word=L latched=60:90 64:90 67:90`) and restored: silent while stopped, sounding at play, ended by stop; a Phase 2 state has neither). `Tools/install.sh --validate`: **pluginval strictness 10 PASS**. **Live 12.4.16, 44.1 kHz, 1024 samples** (Status.txt `block: 1024`, `maxBlock: 1024`; project `TonnetzML Phase 3`: TZ Src = TonnetzML, No Input, Test rig on, gather 0; TZ Rec = Drift, *MIDI From: TZ Src → TonnetzML*, armed; Live restarted at 09:00, after a first "restart" that wasn't one, §7.4), `phase3_walk_orbit_key.py`: **walk** — C E G held from beat 4 with P, L, +5, Undo, R, Home, P on the operator keys, then F A C (Relative: under P): **18/18 notes equal the render, worst 0.000000 beat**, Status.txt `walk: P`; **orbit** — an arrangement loop on beats 4–12 with the rig re-pressing C E G each pass, recorded into a 32-beat session clip on TZ Rec, PL and LR at 1 bar and 1/4 bar: **4/4 takes equal the render note for note (16, 37, 16, 37 notes, lengths included), every note on the grid** at each clip's offset (0.185760 or 0.232200 beat: where Live began recording); **key** (Mirror, slot E = P L, each tune with a block chord a bar) — D major (Twinkle): **87/87 notes, Status.txt `key: D major`, confident, 96%**; F minor (Greensleeves; Key Off then Listen between): **96/96 notes, `key: F minor`, confident, 91%**, both as rendered; every meter 0 within 0.7 s of each stop; the last full run, all three parts in one go, **7/7 PASS**. One buffer size only (Live's audio settings are out of the bridge's reach; the engine is swept at 32–2048 offline). Two script fixes on the way (both in the committed script): the clip offset (session recording starts off the bar), and going back to the arrangement before an arrangement take (§7.4). **Not done / caveats:** the set is unsaved (Cmd-S in Live) and still has the template's Vocal track; no hardware-keyboard check of the operator keys (manual checklist, §7.4); one tempo in Live (the offline sweeps cover 97 and 174 BPM). **Notes for Phase 4:** the editor reads `TonnetzProcessor::getEngineStatus()` (W, its length, Orbit's step, the latched chord, the packed `KeyGuess`, the key in force) and `getWalkState()`; nothing can *write* to the engine yet: add a lock-free command FIFO (append an op, set W, play/stop a chord as a source) — `wordedit::apply` and `LatticeEngine::operatorKey` are the model for Explore's ghost clicks, and Explore is exactly Walk until then; ghost / in-key marking is `opsInKey (chord, KeyRef)` (bit i = operator key i); the key band is `keyMask (tonic, minor)`; spelling in the key: `makeSpelling` from the key in force; numerals need ChordMonkey's `RomanNumeral.h` (not copied yet); the snapshot references will change wholesale with the lattice; to test a rebuild in Live, check that Live really restarted (§7.4). |
| 4. Lattice editor | **Done (2026-11-11)** | **Built:** the editor writes to the engine only through **`core/engine/EditorCommands.h`** (a single-producer, single-consumer FIFO of 512 commands: NoteOn, NoteOff, Operator, SetWalk; `TonnetzProcessor::sendEditorCommand`, message thread): each block the processor puts the editor's notes into the engine's input as if the host sent them, one sample apart from the block's start (a press and its release in one block keep their order; the rest wait for the next block), and hands ghost clicks and W edits to `LatticeEngine::editorOperator` / `editorSetWalk`, taken on the next block's first sample as an operator key would be (whether or not the zone is on; dropped while Enable is off; SetWalk in Walk and Explore only). It reads `EngineStatus`, which now also carries every source and generated note's velocity, the chord and W(chord) with W's anchor and parity (`sourceChord` is public; W(chord) is worked out under every Mix, Source only too, so the lattice stays live) and the engine's release count (the editor forgets the keys it holds when the engine lets go of everything). `core/theory/RomanNumeral.h` (new: ChordMonkey's convention, upper/lower case, ° and +, ♭/♯ from the root's letter in the key's spelling; theirs works on its own chord slots, so it wasn't copied). **`Source/ui`, all new:** `Ink.h` (paper and night inks: teal source, vermilion generated, an ochre key band; Avenir Next and Georgia with Apple Symbols as the fallback for ♭ ♯ and the symbols), `LatticeGeometry.h` (camera, node and triangle positions, hit-testing; no JUCE), `LatticeModel` (from the snapshot: shapes embedded near the shape before, W's path op by op, the ten ghost neighbours with name, voice movement and In key, the trail of the last 12 generated chords, Orbit's cycle and the step reached, names and numerals in the key, the header's sentence), `LatticeView` (cached grid of lines, spelled nodes and key band; chords as filled shapes with faint copies on the torus, common tones ringed in both inks, pulsing with velocity; ghost chips; trail; path arrows; Orbit's dotted loop; hover tips; Follow; direct play: click a triangle, drag to glide, shift-click nodes, Sticky, Release; right/⌘/middle-drag or two fingers pan, wheel or pinch zoom, double-click recentres), `KeyboardStrip` (88 keys, both inks, the operator zone tinted and labelled, Sticky and Release, the computer keyboard's two octaves with ‹ ›), `WordBar` (W as chips: the slot's word in Mirror, Walk's W in Walk and Explore, the cycle in Orbit, read only unless it is the slot's; `+` opens `OpPalette`, laid out as the lattice's directions; drag to reorder, drop off to delete; ⌫ ⌂; double-click to type a word; slots A–H), `Hud` (`HudToggle`, `HudCycle`, `HudChoices`, `KeyBadge`, `HudButton`; the header: name, Enable, mode, key badge, the sentence, theme; the settings column), `EditorNotes.h` (who holds which note: lattice, mouse keys, computer keys), `TonnetzEditor` (60 Hz timer; 760 × 520 up to 2400 × 1600). **As built, beyond the plan:** §5's *as built* paragraph (left-drag glides, so panning moved to the right/⌘/middle button; Sticky also on the lattice; the settings column; literal spelling with double accidentals on the far plane; chords drawn at every torus copy; the generated chord named in root position by the cheapest reading, ties going to the voicing's bass: the Live check showed A C E named C6 when the lowest pitch class stood in for the bass, now fixed and tested); Explore = Walk plus the editor (§3.1); view preferences (theme, Follow, Sticky, zoom, camera, the computer keyboard's octave) live in the processor so they outlive the window, but are not yet in the saved set (Phase 6). **Not built (other phases):** the capture strip (Phase 5), hover audition (needs Preview tone, Phase 6), Grow/Shrink and Flip/Pivot from the lattice (Phase 6). **Checks:** `Tests/run_tests.sh` passes, with the new **test_editor 25/25** (the FIFO: order, the limit, 200 000 commands between two threads; ghost clicks in Walk, Explore, Orbit (Undo, Home), ignored in Mirror and with Enable off; SetWalk; `sourceChord`/`generatedChord` with W's anchor, and all ten ghosts equal to what the operator key gives, 72/72 (24 triads × 3 mixes); generated notes and velocities; the release count; numerals: the seven diatonic triads of all 24 keys, ♭VI, ♯iv°, I+, C♭ major as IV of G♭). `Tests/run_snapshot.sh` **all passed (139 checks)**: 12 reference images (`Tests/fixtures/snapshot/lattice_*.png`, looked at before `--update`; the Phase 0–3 `editor_*.png` removed): Mirror slot E (P L) on C in both themes at 760 × 520, 1200 × 800 and 1800 × 1100; Walk Cmaj7 with W = P L, C major held, In key, both themes at two sizes; Orbit LR; empty. No stock combo boxes, sliders or text buttons; the sentences `C ──P·L──► A♭` and the plan's `Cmaj7 (I) ──P·L──► A♭maj7 (♭VI)`. **Hit-testing: a click at the centre of each of the 24 triangles (a copy in the open, found through `getComponentAt`) plays that triad, 24/24**; a drag from C major into E minor moves one voice (C to B); shift-clicks build C7 and take E out again; Sticky; a ghost click appends L in Walk (zone off) and is only a label in Mirror; the keyboard strip plays and its C0 key walks W without sounding; a stop makes the editor forget. **Every parameter has a control, 21/21, visible, inside the window at full width, none overlapping, and a click changes it, at 760 × 520 and 1400 × 900.** The word bar (a typed word, refused text, the palette, reorder and delete by dragging, ⌫ ⌂, following the slot, W in Walk with −M3 cancelling +M3, read only in Orbit); E♭ minor spells C♭ (VI), at most double accidentals on screen, Am not C6, In key marks exactly `opsInKey`'s ghosts (4 for C major); **0 allocations in processBlock** while the editor sends chords, ghost clicks and words; a press and a release in one block a sample apart; plus every Phase 0–3 check. `Tools/install.sh --validate`: **pluginval strictness 10 PASS**. **Live 12.4.16 (44.1 kHz; Live restarted at 10:04:28 after the install, checked with `ps -o lstart=`; project `TonnetzML Phase 4`: TZ Src = TonnetzML, No Input, Walk, Mix Source + Generated, gather 20 ms; TZ Rec = Drift, *MIDI From: TZ Src → TonnetzML*, armed), by computer use on the plug-in window, allowed by the user this time, with full-screen control (Live hides a plug-in's window when Live isn't the front app, and when its track isn't selected):** one arrangement take: a click on the C major triangle → C E G (beat 0.42; the generated chord 20 ms later); a click on the L ghost → C moved to B, E minor (23.17), the header `C ──L──► Em`; Release → all three ended (37.76); F3, A3 and C4 clicked on the sticky keyboard → F, then F A, then A minor (F → E at 57.58: W = L kept, Relative); each clicked again → each image ended in turn (102.77–104.77). **14 notes recorded, every one what was clicked**, velocities 100 (lattice) and 105 (keyboard, from the click's height); every meter 0 after the stop. Screenshot `docs/phase4-live-editor.jpg`. Source and generated notes of one pitch share TZ Rec's single track, so a source note shows as 0.04 beat (the gather window) before its generated twin takes over. **Not done / caveats:** the set is unsaved (Cmd-S in Live); an accidental click on Live's *Save Preset* was cancelled with Escape (no preset written); the header in Live showed "C6" for A minor during the take (fixed after the check, in the build committed; Live ran the earlier build, whose sound is the same); trackpad pan and pinch, the computer keyboard and Alt-drag are on the manual checklist (§7.4); one window size in Live. **Notes for Phase 5:** put the capture strip between the lattice and the keyboard strip (`TonnetzEditor::resized`); `LatticeModel::chordName`, `rootInPosition`, the numerals and the shapes (`embed`, `trianglesOf`) are what its glyphs need; `capture` and `captureSource` need `HudToggle`s (the "every parameter has a control" test will ask for them); drag-out can only be called, not dragged, in the snapshot app, so test the file there and the drop in Live; for a Live check of the editor, select TZ Src and bring Live to the front (full-screen control), or the plug-in window stays hidden. |
| 5. Capture and export | **Done (2026-11-11)** | **Built:** `core/midiio/` (§3.5 *as built*): `CaptureQueue.h` (`CaptureClock`, the tape: the host's ppq plus an offset fixed while it plays continuously, moved on to the host's place in the bar at a start, loop wrap or locate, free-running at the host's tempo while stopped; `CaptureQueue`, an SPSC FIFO of 16 384 events with a drop count and the clock's state through a `SnapshotExchange`; `CaptureWriter`, once per block after the engine: generated = output notes on the generated channel, source = routed source keys with `captureSource`, take marks when `capture` changes, every captured note-off guaranteed by bitsets), `CaptureModel.h` (the last 64 bars and the take, `current`, `chordsOf`, `renderCapture` As played / Chords, `keySignatureOf`), `MidiFile.h` (ChordMonkey's, extended: several named note tracks, key signature written and read). ChordMonkey has no `CaptureQueue.h` or `CaptureModel.h`: its `ExportRecorder.h` was the model, not copied. Parameters `capture` and `captureSource` appended after `inKey` (23; Live shows 24 with Device On). The processor feeds the writer every block, drains the model on its 20 Hz timer (and the editor's 60 Hz one), and has `captureSong`, `captureName` ("TonnetzML C major 4 bars", "… chords …"), `writeCapture`, `writeCaptureForDrag` (`$TMPDIR/TonnetzML Export/`); Status.txt adds `capture`, `captureShows`, `captureNotes`, `captureBars`, `captureLost`. `ui/CaptureStrip` between the lattice and the keyboard (§5 *as built*): the two toggles, a summary, glyphs (the chord's embedding in its ink, named in root position in the key, newest right, wheel scrolls back), click to recall as the source (`LatticeView::holdSet`), click again to let go, `⇩ drag MIDI` (`performExternalDragDropOfFiles`, Alt: Chords) and `save…` (Alt-click: Chords). `LiveBridge/tests/phase5_capture.py` (`record`, `compare`). **As built, beyond the plan:** the tape (beats never run backwards; a whole-bar loop unrolls with no gap); the take starts and ends on the block where `capture` changes (automatable), notes sounding at the arming join it, notes sounding at the disarming are cut, and it is kept until the next arming; the strip and drag-out show the take if there is one, else the last 64 bars; the file starts at the bar line before its first note; a source track only when source notes were captured; the Chords rendering's rules for spread and ragged chords (§3.5); a queue overflow ends every open note (`captureLost`). **Checks:** `Tests/run_tests.sh` passes, with the new **test_capture 45/45** (overflow: drops counted, nothing left open, capture recovers; stamps playing equal the host's ppq over 15 rate × block configurations with a tempo change, worst 2e-15 beat; first start mid-bar; stopped free-running; a restart and a locate move on to the host's place in the bar, never back; a host-wrapped 2-bar loop unrolls with no gap; streams: generated channel only, source keys not operator keys, no double capture on a shared channel, Enable off; the last 64 bars, the take's arming, disarming and re-arming; **the real engine** (Mirror P L, I–V–vi–IV at 64/256/1024/2048-sample blocks; Walk with P, L, +5, Undo): the As played file equals what the engine sent, to the tick; the walk is five chords and its Chords file is on the grid; Chords rendering of a spread and ragged chord, snapping, overlaps; .mid round trip: type 1, 960 ppq, 97.5 BPM, 3/4, E♭ minor's 6 flats, track names, every note, within 0.001 beat; all 24 key signatures; **0 allocations in `CaptureWriter::process`**). `Tests/run_snapshot.sh` **all passed (162 checks)**: 15 reference images (looked at before `--update`; the 12 Phase 4 lattice images redrawn with the strip, plus `capture_strip_paper_1200x800`, `_dark_1200x800`, `capture_strip_source_paper_760x520`); four chords → four glyphs A♭ E♭ D♭m D♭ in vermilion; the strip between lattice and keyboard; **drag-out as a call**: `TonnetzML C major 4 bars.mid` equals what the host received on channel 2, every note to the tick, with 960 ppq, 120 BPM, 4/4, C major's key signature and the track name; Alt: the Chords file on the grid; Save's writer; a glyph click recalls its chord as the source and W(chord) sounds, a second click lets go; the take kept after disarming, re-arming starts empty; Capture source gives a teal glyph and a second track on channel 1; **every parameter has a control, 23/23**; Capture and Capture source in the state round trip; **0 allocations in processBlock while capturing**; plus every Phase 0–4 check. `Tools/install.sh --validate`: **pluginval strictness 10 PASS**. **Live 12.4.16 (44.1 kHz, 1024 samples; Live started at 16:09:54 after the 16:09:19 install, checked with `ps -o lstart=`; 24 parameters shown; project `TonnetzML Phase 5`: TZ Src = TonnetzML, No Input, Test rig on; TZ Rec = Drift, *MIDI From: TZ Src → TonnetzML*, armed; TZ Drop = Drift)**, `phase5_capture.py`: **record** — Walk, gather 0, Capture armed, Phase 3's walk (C E G with P, L, +5, Undo, R, Home, P, then F A C) as an arrangement take: 18 notes on TZ Rec; Capture disarmed; Status.txt `captureShows: take`, `captureNotes: 18`, 6 bars, `captureLost: 0`; meters 0. **The drag was done by the user by hand** (computer-use access to Live was declined this time): the strip's handle dropped on TZ Drop's arrangement at bar 1. **compare (G6): 18/18 notes equal, pitch and velocity exact, worst start and length error 0.000000 beat**, the drop 4 beats (one bar) from the take, as the file starts on the bar line before its first note. **Not done / caveats:** no screenshot of Live (no screen access): `docs/phase5-capture-strip.png` is the snapshot app's image of the same strip; the Alt-drag (Chords) and Save… were checked in the snapshot app only, not in Live; the set is unsaved (Cmd-S in Live); the capture is not saved with the set (§9 question 3, still open). **Notes for Phase 6:** state save and restore should decide §9 question 3 (the capture is message-thread state in `CaptureModel`; `captureSong` gives a song to store); hover audition needs Preview tone; `LatticeView::holdSet` is the way to play an exact voicing from the editor; to test a rebuild in Live, restart it and check `ps -o lstart=`. |
| 6. Finish | **Done (2026-11-11)** | **Built:** **Preview tone** (§3.7 *as built*): `core/engine/PreviewVoice.h` (16 voices, sine + octave + twelfth, 4 ms attack, decay to 60 %, ~220 ms release, velocity level, stealing; JUCE-free, no allocation), the `previewTone` parameter appended (24; off in a host, on in the Standalone via `PluginHostType::getPluginLoadedAs`), rendering the block's output notes and the editor's auditions (`EditorCommandType::AuditionOn/Off`: heard, never sent or gathered), silenced by bypass, reset and the parameter going off; a HUD toggle in the settings column (*sound ● preview*). **Hover audition:** hovering a ghost or a free op auditions its result voiced near the generated chord (common tones kept). **Free ops from the lattice** (§5 *as built*): Pivot (click a node of the generated chord), Flip (the middle of one of its edges), Grow (a node marked + across an edge), appended where the word bar edits (`LatticeView::latticeOps`, `canEditWord`, `appendOp` → `WordBar::appendOp`); Shrink (−g) in the op palette. **State:** a `VIEW` child (theme, Follow, Sticky, settings column, zoom, camera, the computer keyboard's octave); saved from any thread (a copy kept by the timer), restored on the message thread at once or by the timer, the open editor re-reading it (`editorViewRestores`). Status.txt adds `previewTone`, `view` and `slots`. README rewritten (what it does, the editor, Live, tests). `LiveBridge/tests/phase6_state.py` (`before`, `als`, `after`). **As built, beyond the plan:** §9 question 3 answered *no* for now (the capture isn't saved); a Phase 4 bug fixed: the word bar showed the word it had sent for up to 400 ms after the engine had shown it, so an Undo straight after an edit came back as the old word (found by the Phase 6 test; `WordBar::refresh` now drops the pending word once the engine shows it, and Undo and Home drop it). **Checks:** `Tests/run_tests.sh` passes, with the new **test_preview 12/12** (silence, a note on its sample at 440 Hz for A4, both channels, the level, release within 300 ms, velocity, restrike, 16-voice stealing, All Notes Off, silence(), **0 allocations in render**). `Tests/run_snapshot.sh` **all passed (183 checks)**: 16 reference images (looked at before `--update`: the settings column's new line; `finish_walk_grow_paper_1200x800` new); Preview tone off silent, on heard (peak 0.14), bypass silent, 0 allocations with it on; hovering the L ghost auditions B E G and sends nothing, moving off stops it; C major offers 3 pivots, 3 flips, 3 grows; clicks give Flip(0,7) → C minor, Grow(4,7) → Cmaj7, Pivot(4) → C♯ minor; Shrink from the palette returns to C; none offered in Orbit with a factory cycle; **every parameter has a control, 24/24**; the view round trip into a new instance and into an open editor; damaged view values fall back, a Phase 5 state leaves the view alone; plus every Phase 0–5 check. `Tools/install.sh --validate`: **pluginval strictness 10 PASS**. **Live 12.4.16 (44.1 kHz, 1024 samples; project `TonnetzML Phase 6`, saved by the user as `~/Development/projects/music/tests/TonnetzML Phase 6 Project/TonnetzML Phase 6.als`; TZ Src = TonnetzML, No Input, Test rig on; TZ Rec = Drift, *MIDI From: TZ Src → TonnetzML*, armed), `phase6_state.py`:** *before* — Walk, latch off: Phase 3's walk as take A, 18 notes; latch on and a rig pass (Home, C E G, P, L) leave W = P L with C E G latched; Slot C, Orbit rate 1/2 bar, In key and Capture source set away from their defaults; the user panned and zoomed the lattice (Status.txt `view: zoom 0.4500 centre -4.9646 -11.6615`); *als* — the saved `.als`'s ProcessorState decodes to `SLOTS` (factory), `WALK word="P L" latched="60:100 64:100 67:100"` and `VIEW zoom="0.45" centreA="-4.96…" centreB="-11.66…"`; *after* — **Live quit and restarted (start 16:55:25, after the 16:53:05 of the session that saved; `ps -o lstart=` and Log.txt `VST3: Restored: TonnetzML`), the set reopened: all 25 parameters (24 + Device On) equal, Status.txt `walk: P L`, `latched: C`, the slot words and the view equal; Latch off and the same walk recorded again: take B equals take A, 18/18 notes, worst 0.000000 beat.** Two earlier rounds didn't count: the first reopened a copy of the set in the same Live session (everything came back, but no restart), the second reopened a file saved after the first round had changed it (it restored exactly that file: W = P, nothing latched). **Not done / caveats:** the slot words were checked at their factory values in Live (editing one needs the editor; the snapshot app checks edited words); no screenshot of Live (no screen access); hover audition, the free ops and Preview tone were checked in the snapshot app only, not by hand in Live; the set is left with Latch off after the last take (don't save it over the checked state). **Notes after Phase 6:** P1 and P2 are done; open questions §9 1, 2, 4, 5 remain; manual checklist (§7.4) still to do by hand: a hardware keyboard on the operator zone, drag-out landing at the drop point, trackpad pan and zoom, resizing, and now Preview tone by ear. |
| v1.0.0 release | **Done (2026-11-11)** | Version 1.0.0 in `TonnetzML.jucer` (and the snapshot app's), the generated Xcode projects and Info.plists re-saved with Projucer (version code 0x10000). `CHANGELOG.md` (Phases 0–6 and the known limits, §9). **`Tools/release.sh`**: from `dist/build` (or `--build`), stages the VST3, the app, README and CHANGELOG; signs them with a Developer ID and the hardened runtime, notarizes (`notarytool --keychain-profile`, default `TonnetzML`), staples and zips to `dist/TonnetzML-1.0.0-macOS.zip` + `.sha256`; checks the identity and the profile before anything else and lists what is missing; `--adhoc` signs ad hoc and writes `…-macOS-adhoc.zip`. This Mac has an Apple Development identity only and no notary profile, so 1.0.0 was packaged with `--adhoc` (`dist/TonnetzML-1.0.0-macOS-adhoc.zip`, 7.8 MB, both bundles verify after unzipping); without flags it stops and names both. Checked at 1.0.0: `Tests/run_tests.sh` all passed, `Tests/run_snapshot.sh` all passed, `Tools/install.sh --validate` universal Release, **pluginval strictness 10 PASS**, the installed VST3 reports 1.0.0. Not re-run in Live for the release (nothing but the version changed since Phase 6's check). Local tag `v1.0.0`. |
