#!/usr/bin/env python3
"""Phase 5's Live check (PLAN.md §6 row 5, G6, §7.4), through ClaudeLiveBridge on port 9873.

The set (`TonnetzML Phase 5`): TZ Src (TonnetzML, input No Input, Test rig on), TZ Rec (Drift,
MIDI From TZ Src -> TonnetzML, armed) and TZ Drop (Drift, for the dragged file). Two steps, with
the drag between them (computer use, or by hand):

  record   Mode Walk, Capture armed: the rig holds C E G and presses operator keys (P, L, +5,
           Undo, R, Home, P), then plays F A C (Phase 3's walk). Arrangement take on TZ Rec;
           Capture is then disarmed (the take is kept). Status.txt must show the take's notes.
  compare  after the capture strip's handle was dragged onto TZ Drop's arrangement lane: TZ
           Drop's clip must equal TZ Rec's take (pitch, velocity, start and length within 0.001
           beat), at the whole-bar offset where it was dropped.

    phase5_capture.py record  [--out DIR]
    phase5_capture.py compare [--out DIR]

Plain Python 3, no packages. Prints PASS/FAIL lines; exits 1 on any failure.
"""
import argparse, json, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from phase2_mirror import Bridge, KEYS, REC, REPO, TOL  # noqa: E402
from phase3_walk_orbit_key import WALK_KEYS, arrangement_take, setp, settle_meters, status  # noqa: E402

DROP = "TZ Drop"


def notes_of(result):
    return result["notes"] if isinstance(result, dict) else result


def record(b, out):
    KEYS.write_text(WALK_KEYS)
    time.sleep(0.5)
    setp(b, "Mode", 1)
    setp(b, "Walk input", 0)
    setp(b, "Latch", 0)
    setp(b, "Gather (ms)", 0)
    setp(b, "Mix", 0)
    setp(b, "Capture source", 0)
    setp(b, "Capture", 1)               # arms a new take
    time.sleep(0.5)
    take = arrangement_take(b, 27.0)
    setp(b, "Capture", 0)               # the take is kept
    time.sleep(0.6)
    bad, settle = settle_meters(b)
    st = status()
    (out / "phase5_take.json").write_text(json.dumps({"take": take, "status": st}, indent=1))
    ok = len(take) > 0 and st.get("captureShows") == "take" and st.get("captureNotes") == str(len(take)) and not bad
    print(f"{'PASS' if ok else 'FAIL'}  record: {len(take)} notes on TZ Rec; Status.txt capture={st.get('capture')!r}, "
          f"shows {st.get('captureShows')!r}, {st.get('captureNotes')} notes, {st.get('captureBars')} bars, lost {st.get('captureLost')}; "
          f"meters {'0 after %.1f s' % settle if not bad else bad}")
    return ok


def compare(b, out):
    take = notes_of(b("get_clip_notes", track=REC, source="arrangement"))
    dropped = notes_of(b("get_clip_notes", track=DROP, source="arrangement"))
    (out / "phase5_compare.json").write_text(json.dumps({"take": take, "dropped": dropped}, indent=1))
    if not take or not dropped:
        print(f"FAIL  compare: {len(take)} notes on TZ Rec, {len(dropped)} on TZ Drop")
        return False
    # The drop lands where it was dropped: the offset between the first notes, which must be a
    # whole number of bars (the file starts on a bar line).
    first_take = min(n["song_time"] for n in take)
    first_drop = min(n["song_time"] for n in dropped)
    offset = first_drop - first_take
    problems = []
    if abs(offset / 4.0 - round(offset / 4.0)) * 4.0 > TOL:
        problems.append(f"the drop is {offset:.6f} beats from the take: not a whole number of bars")
    key = lambda n: (round(n["song_time"], 3), n["pitch"])
    a = sorted(take, key=key)
    d = sorted(({**n, "song_time": n["song_time"] - offset} for n in dropped), key=key)
    worst_s = worst_l = 0.0
    if len(a) != len(d):
        problems.append(f"{len(a)} notes taken, {len(d)} dropped")
    for x, y in zip(a, d):
        ds, dl = abs(x["song_time"] - y["song_time"]), abs(x["duration"] - y["duration"])
        worst_s, worst_l = max(worst_s, ds), max(worst_l, dl)
        if x["pitch"] != y["pitch"] or x["velocity"] != y["velocity"] or ds > TOL or dl > TOL:
            problems.append(f"take {x['pitch']} v{x['velocity']} @{x['song_time']:.6f} len {x['duration']:.6f} vs "
                            f"drop {y['pitch']} v{y['velocity']} @{y['song_time']:.6f} len {y['duration']:.6f}")
    ok = not problems
    print(f"{'PASS' if ok else 'FAIL'}  compare (G6): {len(a)} notes on TZ Rec, {len(d)} on TZ Drop (dropped {offset:+.6f} beats away); "
          f"worst start {worst_s:.6f}, worst length {worst_l:.6f} beat")
    for p in problems[:10]:
        print("      ", p)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("step", choices=["record", "compare"])
    ap.add_argument("--out", default=str(REPO / "Tests" / "build" / "live"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    b = Bridge()
    ok = record(b, out) if a.step == "record" else compare(b, out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
