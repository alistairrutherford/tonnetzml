# TonnetzML

*Tonnetz / Musical Lattices.* A VST3 (and Standalone) for macOS that makes new chords from the
ones you play by moving them on the Tonnetz, the lattice of fifths and thirds, and sends them out
as MIDI. The plan, with the theory, the design and the status of each phase, is [PLAN.md](PLAN.md).

**Status: Phase 0 (skeleton).** The plug-in loads, passes MIDI in to MIDI out unchanged, is
silent, and shows a placeholder editor. The lattice engine and the editor come in phases 1 to 6.

## Build and install

Needs macOS 12 or later, Xcode, and [JUCE 8.0.14](https://github.com/juce-framework/JUCE) at
`~/Development/JUCE` (or pass `--juce DIR`).

```bash
Tools/install.sh
```

builds the VST3 and the Standalone (Release, arm64 + x86_64) in `dist/build`, signs them ad hoc
and installs them for the current user:

- `~/Library/Audio/Plug-Ins/VST3/TonnetzML.vst3`
- `~/Applications/TonnetzML.app`
- `~/Music/TonnetzML/TestRig/` (the test rig's folder, see below)

`--validate` also runs pluginval at strictness 10, `--no-build` installs the last build again,
`--uninstall` removes the plug-in and the app. Nothing needs a password.

To work on it in Xcode, open `TonnetzML.jucer` in Projucer and save, or use the generated
`Builds/MacOSX/TonnetzML.xcodeproj`.

## In Live

Rescan plug-ins (*Settings → Plug-Ins → Rescan*, with *Use VST3 Plug-In System Folders* on).
TonnetzML is under *Plug-Ins → VST3 → Alistair Rutherford*. Put it on a MIDI track; on an
instrument track choose *MIDI From: (that track) → TonnetzML* and arm it or set Monitor to *In*.

## Tests

| Command | What it runs |
|---------|--------------|
| `Tests/run_tests.sh` | The core tests. `Source/core` is header-only C++17 with no JUCE, so this needs only clang. |
| `Tests/run_snapshot.sh` | A console app around the real processor and editor: editor images against `Tests/fixtures/snapshot` (`--update` rewrites them), MIDI through, the test rig, no allocation on the audio thread, state. |
| `Tests/run_validation.sh` | pluginval, strictness 10, on the installed VST3. |

### The test rig

For checks in a host. With the **Test rig** parameter on, the plug-in reads
`~/Music/TonnetzML/TestRig/Keys.txt` and plays its keys as if they came from the host, on the
sample, while the transport runs:

```
# beat note velocity length     (beats are quarter notes from the start of the song)
0 60 100 2
0 64 100 2
0 67 100 2
```

Up to 512 keys; the file is read again when it changes. `Status.txt` beside it says what was
loaded and played. Live checks are run through the Ableton MCP bridge (PLAN.md §7.4).

## Layout

```
TonnetzML.jucer   Source/core (JUCE-free)   Source/plugin   Source/ui
Tests/            run_*.sh, test_*.cpp, FakeHost.h, Snapshot/ (the snapshot app)
Tools/install.sh
```
# tonnetzml
