#!/usr/bin/env python3
"""Phase 3's Live check (PLAN.md §6 row 3, §7.4), through ClaudeLiveBridge on port 9873.

The set (`TonnetzML Phase 3`): TZ Src (TonnetzML, input No Input, Test rig on) and TZ Rec (Drift,
MIDI From TZ Src -> TonnetzML, armed). Three parts, each compared with Tests/build/render_scenario
at Live's sample rate and buffer size (tolerance 0.001 beat):

  walk   Mode Walk: the rig holds C E G and presses operator keys (P, L, +5, Undo, R, Home, P),
         then plays F A C (Relative keeps W). Arrangement take on TZ Rec.
  orbit  Mode Orbit, cycles PL and LR at 1 bar and 1/4 bar, over an arrangement loop (beats
         4-12) with the rig re-pressing C E G every pass; recorded into a session clip on TZ Rec
         (arrangement takes over a loop make take lanes). The clip starts on a bar Live picks,
         so the render is matched at the bar offset that fits; every note on the grid.
  key    Mode Mirror, slot E (P L): the rig plays a tune in D major, then (Key Off, then Listen:
         a fresh analysis) one in F minor. Arrangement takes; Status.txt must show each key,
         confident, and the take must equal the render (the generated chords sounding throughout).

After each take every meter must read 0.

    phase3_walk_orbit_key.py --rate 44100 --block 1024 [--parts walk orbit key] [--out DIR]

Plain Python 3, no packages. Prints one PASS/FAIL line per take; exits 1 on any failure.
"""
import argparse, json, subprocess, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from phase2_mirror import Bridge, compare, meters_zero, REPO, RENDER, KEYS, SRC, REC, TOL  # noqa: E402

STATUS = KEYS.parent / "Status.txt"
DEVICE = "TonnetzML"

# Choice and int parameters are 0..1 in Live (PLAN.md §7.4).
CHOICES = {"Mode": 3, "Walk input": 1, "Orbit cycle": 4, "Orbit rate": 4, "Key": 2, "Key tonic": 11, "Slot": 7}


def setp(b, name, value):
    if name in CHOICES:
        value = value / CHOICES[name]
    elif name == "Gather (ms)":
        value = value / 80.0
    elif name == "Operator keys from":
        value = value / 116.0
    b("set_parameter", track=SRC, device=DEVICE, parameter=name, value=float(value))


def render(keys_path, rate, block, until, *extra):
    cmd = [str(RENDER), "--bpm", "120", "--rate", str(rate), "--block", str(block), "--gather", "0",
           "--until", str(until), *[str(x) for x in extra], str(keys_path)]
    return json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout)


def status():
    out = {}
    if STATUS.exists():
        for line in STATUS.read_text().splitlines():
            if ": " in line:
                k, v = line.split(": ", 1)
                out[k] = v
    return out


def settle_meters(b, limit=8.0):
    t0 = time.monotonic()
    bad = meters_zero(b)
    while bad and time.monotonic() - t0 < limit:
        time.sleep(0.25)
        bad = meters_zero(b)
    return bad, time.monotonic() - t0


def arrangement_take(b, end_beat, bpm=120.0):
    # A session clip played on TZ Rec (the orbit part) keeps the track off the arrangement, and an
    # arrangement take would record that clip: remove it and go back to the arrangement first.
    b("clear_session_clips", track=REC)
    b("set_song_properties", back_to_arranger=True)
    b("clear_arrangement_clips", track=REC)
    time.sleep(0.5)
    b("start_recording", mode="arrangement", from_beat=0.0)
    time.sleep((end_beat + 4.0) * 60.0 / bpm + 2.5)      # count-in and start-up (§7.4)
    b("stop_recording", mode="arrangement")
    time.sleep(1.5)
    notes = b("get_clip_notes", track=REC, source="arrangement")
    return notes["notes"] if isinstance(notes, dict) else notes


def report(tag, ok, want, got, ws, wl, bad, settle, problems, extra=""):
    print(f"{'PASS' if ok else 'FAIL'}  {tag}: {len(want)} expected, {len(got)} recorded, worst start {ws:.6f}, "
          f"worst length {wl:.6f} beat; meters {'0 after %.1f s' % settle if not bad else bad}{extra}")
    for p in problems[:8]:
        print("      ", p)
    sys.stdout.flush()


# ---------------------------------------------------------------------------------------------

WALK_KEYS = """# Phase 3 walk: Home (W left empty by any earlier run), C E G held, operator keys
# (C0 = 24: P L R N S H +5 -5 +M3 -M3 Undo Home), then F A C
2 35 100 0.5
4 60 100 16
4 64 100 16
4 67 100 16
6 24 100 0.5
8 25 100 0.5
10 30 100 0.5
12 34 100 0.5
14 26 100 0.5
16 35 100 0.5
18 24 100 0.5
22 65 90 4
22 69 90 4
22 72 90 4
"""


def part_walk(b, a, out):
    KEYS.write_text(WALK_KEYS)
    time.sleep(0.5)
    setp(b, "Mode", 1)
    setp(b, "Walk input", 0)
    setp(b, "Latch", 0)
    got = arrangement_take(b, 27.0)
    want = render(KEYS, a.rate, a.block, 27.0, "--mode", "walk")
    problems, ws, wl = compare(got, want["notes"])
    bad, settle = settle_meters(b)
    st = status()
    if st.get("walk") != "P":
        problems.append(f"Status.txt walk {st.get('walk')!r}, not 'P'")
    (out / f"phase3_walk_{a.block}.json").write_text(json.dumps({"take": got, "render": want, "status": st}, indent=1))
    ok = not problems and not bad
    report(f"walk ({a.block} samples): C E G under P, P L, P L +5, P L, P L R, -, P; then F A C under P", ok, want["notes"], got,
           ws, wl, bad, settle, problems, f"; Status.txt walk={st.get('walk')!r}, block {st.get('block')}/{st.get('maxBlock')}")
    return ok


ORBIT_KEYS = """# Phase 3 orbit: Home (restart the cycle), then C E G held through the loop (4-12), re-pressed each pass
2 35 100 0.5
4 60 100 8
4 64 100 8
4 67 100 8
"""


def session_take(b, beats, bpm=120.0):
    """Plays from 0 with the loop on and records `beats` into TZ Rec's first empty slot."""
    b("clear_session_clips", track=REC)
    time.sleep(0.5)
    b("set_song_properties", current_song_time=0.0)
    b("start_transport")
    b("start_recording", mode="session", track=REC, length_beats=beats)
    time.sleep((beats + 10.0) * 60.0 / bpm + 2.0)
    b("stop_transport")
    time.sleep(1.5)
    return b("get_clip_notes", track=REC, source="session")


def match_offset(clip, want, beats):
    """The render's notes inside the recorded window, for the offset (song beat at the clip's
    time 0) that fits best. Session recording starts when Live gets to it, not on a bar, so the
    offsets tried are those that put a recorded note on a rendered one of the same pitch. Notes
    that start before the window are not expected; notes cut by its end are compared by start."""
    got = [dict(n, song_time=n["start_time"]) for n in clip["notes"]]
    length = clip.get("length") or beats
    offsets = sorted({round(w["start"] - g["start_time"], 6) for g in got for w in want if w["pitch"] == g["pitch"]})
    best = None
    for B in offsets:
        inside = []
        for w in want:
            s = w["start"] - B
            if s < -TOL or s >= length - TOL:
                continue
            w2 = dict(w, start=s)
            if w["start"] + w["length"] > B + length - TOL:
                w2["length"] = None              # cut by the end of the recording
            inside.append(w2)
        problems = []
        used = [False] * len(got)
        for w in inside:
            hit = None
            for i, g in enumerate(got):
                if not used[i] and g["pitch"] == w["pitch"] and abs(g["song_time"] - w["start"]) <= TOL:
                    hit = i
                    break
            if hit is None:
                problems.append(f"missing {w['pitch']} @{w['start']:.6f}")
                continue
            used[hit] = True
            if w["length"] is not None and abs(got[hit]["duration"] - w["length"]) > TOL:
                problems.append(f"length {got[hit]['duration']:.6f} != {w['length']} ({w['pitch']} @{w['start']:.6f})")
        problems += [f"extra {g['pitch']} @{g['song_time']:.6f}" for i, g in enumerate(got) if not used[i]]
        if best is None or len(problems) < len(best[1]):
            best = (B, problems, len(inside))
    return best if best else (0.0, ["nothing recorded"], 0)


def part_orbit(b, a, out):
    KEYS.write_text(ORBIT_KEYS)
    time.sleep(0.5)
    setp(b, "Mode", 2)
    setp(b, "Walk input", 0)
    b("set_song_properties", loop=True, loop_start=4.0, loop_length=8.0)
    beats = 32.0
    failures = 0
    for cycle, ci in (("PL", 0), ("LR", 2)):
        for rate, ri in (("1", 2), ("1/4", 0)):
            setp(b, "Orbit cycle", ci)
            setp(b, "Orbit rate", ri)
            clip = session_take(b, beats)
            want = render(KEYS, a.rate, a.block, 60.0, "--mode", "orbit", "--cycle", cycle, "--orbit-rate", rate, "--loop", "4", "12")
            B, problems, n = match_offset(clip, want["notes"], beats)
            # On the grid: every recorded start, in song time, a multiple of the rate.
            step = 4.0 if rate == "1" else 1.0
            off = [nt for nt in clip["notes"] if abs(((nt["start_time"] + B) / step) - round((nt["start_time"] + B) / step)) * step > TOL]
            if off:
                problems.append(f"{len(off)} notes off the grid, e.g. {off[0]['pitch']} @{off[0]['start_time']}")
            bad, settle = settle_meters(b)
            (out / f"phase3_orbit_{cycle}_{rate.replace('/', 'of')}_{a.block}.json").write_text(
                json.dumps({"clip": clip, "render": want, "offset": B}, indent=1))
            ok = not problems and not bad
            failures += 0 if ok else 1
            print(f"{'PASS' if ok else 'FAIL'}  orbit {cycle} at {rate} bar over the loop 4-12 ({a.block} samples): {n} notes expected in the "
                  f"clip (its time 0 = song beat {B:.6f}), {len(clip['notes'])} recorded, {len(clip['notes']) - len(off)} on the grid; meters "
                  f"{'0 after %.1f s' % settle if not bad else bad}")
            for p in problems[:8]:
                print("      ", p)
            sys.stdout.flush()
    b("set_song_properties", loop=False)
    return failures == 0


# The tunes of ChordMonkey's key corpus (Tests/test_keys.cpp): Twinkle (C major) and Greensleeves
# (A minor), as (pitch, beats); played from beat 4, each note 90% of its beats.
TWINKLE = [(60, 1), (60, 1), (67, 1), (67, 1), (69, 1), (69, 1), (67, 2), (65, 1), (65, 1), (64, 1), (64, 1), (62, 1), (62, 1), (60, 2),
           (67, 1), (67, 1), (65, 1), (65, 1), (64, 1), (64, 1), (62, 2), (67, 1), (67, 1), (65, 1), (65, 1), (64, 1), (64, 1), (62, 2),
           (60, 1), (60, 1), (67, 1), (67, 1), (69, 1), (69, 1), (67, 2), (65, 1), (65, 1), (64, 1), (64, 1), (62, 1), (62, 1), (60, 2)]
GREENSLEEVES = [(57, 1), (60, 2), (62, 1), (64, 1.5), (65, 0.5), (64, 1), (62, 2), (59, 1), (55, 1.5), (57, 0.5), (59, 1),
                (60, 2), (57, 1), (57, 1.5), (56, 0.5), (57, 1), (59, 2), (56, 1), (52, 2), (57, 1),
                (60, 2), (62, 1), (64, 1.5), (65, 0.5), (64, 1), (62, 2), (59, 1), (55, 1.5), (57, 0.5), (59, 1),
                (60, 1.5), (59, 0.5), (57, 1), (56, 1.5), (54, 0.5), (56, 1), (57, 3)]


# A block chord a bar under each (written in C / A minor), so P L has triads to turn into chords.
C, F, G, AM, E = [48, 52, 55], [53, 57, 48], [43, 47, 50], [45, 48, 52], [40, 44, 47]
TWINKLE_CHORDS = [C, F, C, G, C, G, C, G, C, F, G, C]
GREENSLEEVES_CHORDS = [AM, [43, 47, 50], AM, E, AM, [43, 47, 50], AM, E, AM, E, AM, AM]


def tune_keys(tune, shift, chords):
    lines, beat = [], 4.0
    for pitch, beats in tune:
        lines.append(f"{beat:.4f} {pitch + shift} 100 {0.9 * beats:.4f}")
        beat += beats
    end = beat
    for bar, chord in enumerate(chords):
        if 4.0 + 4 * bar < beat:
            for n in chord:
                lines.append(f"{4.0 + 4 * bar:.4f} {n + shift} 80 3.8")
            end = max(end, 4.0 + 4 * bar + 3.8)
    return "\n".join(lines) + "\n", end


def part_key(b, a, out):
    setp(b, "Mode", 0)
    setp(b, "Slot", 4)                 # E: P L
    ok_all = True
    for name, tune, chords, shift, tonic in (("D major (Twinkle, a chord a bar)", TWINKLE, TWINKLE_CHORDS, 2, "D major"),
                                            ("F minor (Greensleeves, a chord a bar)", GREENSLEEVES, GREENSLEEVES_CHORDS, -4, "F minor")):
        text, end = tune_keys(tune, shift, chords)
        KEYS.write_text(text)
        setp(b, "Key", 0)              # Off, then Listen: a fresh analysis
        time.sleep(0.3)
        setp(b, "Key", 1)
        time.sleep(0.5)
        got = arrangement_take(b, end + 1.0)
        time.sleep(0.5)
        st = status()
        want = render(KEYS, a.rate, a.block, end + 1.0, "--slot", "E")
        problems, ws, wl = compare(got, want["notes"])
        if st.get("key") != tonic or st.get("keyState") != "confident":
            problems.append(f"Status.txt key {st.get('key')!r} {st.get('keyState')!r} {st.get('keyFit')}%, not {tonic} confident")
        rk = want["key"]
        bad, settle = settle_meters(b)
        (out / f"phase3_key_{tonic.replace(' ', '_')}_{a.block}.json").write_text(json.dumps({"take": got, "render": want, "status": st}, indent=1))
        ok = not problems and not bad
        ok_all = ok_all and ok
        report(f"key: {name} under P L ({a.block} samples)", ok, want["notes"], got, ws, wl, bad, settle, problems,
               f"; Status.txt: key {st.get('key')}, {st.get('keyState')}, fit {st.get('keyFit')}%, {st.get('keyNotes')} notes heard "
               f"(render: tonic {rk['tonic']}, {rk['state']}, {rk['fit']}%)")
    return ok_all


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rate", type=float, required=True)
    ap.add_argument("--block", type=int, required=True)
    ap.add_argument("--parts", nargs="+", default=["walk", "orbit", "key"])
    ap.add_argument("--out", default=str(REPO / "Tests" / "build" / "live"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    b = Bridge()
    b("set_tempo", bpm=120.0)
    b("set_song_properties", loop=False)
    setp(b, "Gather (ms)", 0)
    setp(b, "Operator keys", 1)
    setp(b, "Operator keys from", 24)
    setp(b, "Mix", 0)
    setp(b, "Test rig", 1)
    ok = True
    for part in a.parts:
        ok = {"walk": part_walk, "orbit": part_orbit, "key": part_key}[part](b, a, out) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
