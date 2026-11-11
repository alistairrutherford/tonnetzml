#!/usr/bin/env python3
"""Phase 6's Live check (PLAN.md §6 row 6), through ClaudeLiveBridge on port 9873.

Save the set, restart Live, reopen: the slots, the word, the latched chord and the camera are
back, and the same take records again. The set (`TonnetzML Phase 6`): TZ Src (TonnetzML, No
Input, Test rig on), TZ Rec (Drift, MIDI From TZ Src -> TonnetzML, armed).

  before   Walk, latch off: Phase 3's walk recorded on TZ Rec (take A). Then latch on and a short
           rig pass (Home, C E G, P, L) leaves W = P L and C E G latched; a few parameters are set
           away from their defaults. Writes Tests/build/live/phase6_before.json.
  als      after the set was saved (Cmd-S): reads the saved .als and decodes TonnetzML's state
           (SLOTS, WALK, VIEW children), printing them.
  after    after Live was restarted and the set reopened: Live's start time is after `before`;
           every parameter equals; Status.txt's word, latched chord, slots and view equal; then
           latch off and the same walk recorded again (take B) must equal take A (0.001 beat).

    phase6_state.py before | als PATH.als | after

Plain Python 3, no packages. Exits 1 on any failure.
"""
import argparse, binascii, gzip, json, re, subprocess, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from phase2_mirror import Bridge, KEYS, REC, REPO, SRC, TOL  # noqa: E402
from phase3_walk_orbit_key import WALK_KEYS, arrangement_take, setp, settle_meters, status  # noqa: E402

OUT = REPO / "Tests" / "build" / "live"
DEVICE = "TonnetzML"

LATCH_KEYS = """# Phase 6 latch: Home, C E G for a beat, then P and L on the operator keys (C0, C#0)
2 35 100 0.5
4 60 100 1
4 64 100 1
4 67 100 1
6 24 100 0.5
7 25 100 0.5
"""

STATE_KEYS = ["walk", "latched", "latch", "mode", "slot", "slots", "view", "previewTone", "keyMode"]


def params(b):
    ps = b("list_parameters", track=SRC, device=DEVICE)
    return {p["name"]: round(p["value"], 6) for p in (ps["parameters"] if isinstance(ps, dict) else ps)}


def live_started():
    pid = subprocess.run(["pgrep", "-f", "Contents/MacOS/Live$"], capture_output=True, text=True).stdout.split()
    if not pid:
        return None
    return subprocess.run(["ps", "-o", "lstart=", "-p", pid[0]], capture_output=True, text=True).stdout.strip()


def compare_takes(a, b):
    key = lambda n: (round(n["song_time"], 3), n["pitch"])
    a, b = sorted(a, key=key), sorted(b, key=key)
    problems, worst = [], 0.0
    if len(a) != len(b):
        problems.append(f"{len(a)} notes before, {len(b)} after")
    for x, y in zip(a, b):
        d = max(abs(x["song_time"] - y["song_time"]), abs(x["duration"] - y["duration"]))
        worst = max(worst, d)
        if x["pitch"] != y["pitch"] or x["velocity"] != y["velocity"] or d > TOL:
            problems.append(f"{x['pitch']} v{x['velocity']} @{x['song_time']:.6f}/{x['duration']:.6f} vs "
                            f"{y['pitch']} v{y['velocity']} @{y['song_time']:.6f}/{y['duration']:.6f}")
    return problems, worst


def before(b):
    KEYS.write_text(WALK_KEYS)
    time.sleep(0.5)
    setp(b, "Test rig", 1)
    setp(b, "Mode", 1)
    setp(b, "Walk input", 0)
    setp(b, "Latch", 0)
    setp(b, "Gather (ms)", 0)
    setp(b, "Mix", 0)
    take = arrangement_take(b, 27.0)
    bad, _ = settle_meters(b)
    print(f"{'PASS' if take and not bad else 'FAIL'}  take A: {len(take)} notes on TZ Rec, meters {'0' if not bad else bad}")

    # Latch a walked chord: W = P L on C E G.
    KEYS.write_text(LATCH_KEYS)
    time.sleep(0.5)
    setp(b, "Latch", 1)
    b("set_song_properties", back_to_arranger=True)
    b("start_transport", from_beat=0.0)
    time.sleep(10 * 60.0 / 120.0 + 1.5)
    b("stop_transport")
    time.sleep(1.0)
    # Some parameters away from their defaults.
    setp(b, "Slot", 2)
    setp(b, "Orbit rate", 1)
    setp(b, "In key", 1)
    setp(b, "Capture source", 1)
    time.sleep(1.0)
    st = status()
    ok = st.get("walk") == "P L" and st.get("latched") not in (None, "-")
    print(f"{'PASS' if ok else 'FAIL'}  latched and walked: Status.txt walk={st.get('walk')!r}, latched={st.get('latched')!r}, "
          f"slots={st.get('slots')!r}, view={st.get('view')!r}")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "phase6_before.json").write_text(json.dumps({"take": take, "params": params(b), "status": {k: st.get(k) for k in STATE_KEYS},
                                                        "live_started": live_started(), "at": time.time()}, indent=1))
    return bool(take) and not bad and ok


def als(path):
    data = gzip.open(path).read().decode("utf-8", "replace")
    found = None
    for blob in re.findall(r"<(?:Buffer|ProcessorState)>\s*([0-9A-Fa-f\s]+?)\s*</(?:Buffer|ProcessorState)>", data, re.S):
        raw = binascii.unhexlify(re.sub(r"\s", "", blob))
        i = raw.find(b"<TonnetzML")
        if i >= 0:
            j = raw.find(b"</TonnetzML>", i)
            found = raw[i:j + len("</TonnetzML>")].decode("utf-8", "replace")
            break
    if found is None:
        print("FAIL  als: no TonnetzML state found in", path)
        return False
    parts = {name: re.search(rf"<{name}[^>]*/?>", found) for name in ("SLOTS", "WALK", "VIEW")}
    for name, m in parts.items():
        print(f"  {name}: {m.group(0) if m else '(missing)'}")
    ok = all(parts.values())
    print(f"{'PASS' if ok else 'FAIL'}  als: the saved set holds TonnetzML's slot words, W and latched chord, and view")
    return ok


def after(b):
    saved = json.loads((OUT / "phase6_before.json").read_text())
    started = live_started()
    restarted = started is not None and started != saved["live_started"]
    print(f"{'PASS' if restarted else 'FAIL'}  Live restarted: was {saved['live_started']!r}, now {started!r}")
    time.sleep(1.0)
    now = params(b)
    diff = {k: (v, now.get(k)) for k, v in saved["params"].items() if now.get(k) != v}
    print(f"{'PASS' if not diff else 'FAIL'}  every parameter is back ({len(now)}){'' if not diff else ': ' + str(diff)}")
    st = status()
    sdiff = {k: (saved["status"].get(k), st.get(k)) for k in STATE_KEYS if st.get(k) != saved["status"].get(k)}
    print(f"{'PASS' if not sdiff else 'FAIL'}  W, the latched chord, the slot words and the view are back: "
          f"walk={st.get('walk')!r} latched={st.get('latched')!r} view={st.get('view')!r}{'' if not sdiff else ' DIFF ' + str(sdiff)}")

    # The same take again.
    setp(b, "Latch", 0)
    KEYS.write_text(WALK_KEYS)
    time.sleep(0.6)
    take = arrangement_take(b, 27.0)
    bad, _ = settle_meters(b)
    problems, worst = compare_takes(saved["take"], take)
    print(f"{'PASS' if not problems and not bad else 'FAIL'}  take B equals take A: {len(take)} notes, worst {worst:.6f} beat")
    for p in problems[:8]:
        print("      ", p)
    (OUT / "phase6_after.json").write_text(json.dumps({"take": take, "params": now, "status": st}, indent=1))
    return restarted and not diff and not sdiff and not problems and not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("step", choices=["before", "als", "after"])
    ap.add_argument("path", nargs="?")
    a = ap.parse_args()
    if a.step == "als":
        sys.exit(0 if als(a.path) else 1)
    b = Bridge()
    sys.exit(0 if (before(b) if a.step == "before" else after(b)) else 1)


if __name__ == "__main__":
    main()
