#!/usr/bin/env python3
"""Phase 2's Live check (PLAN.md §6 row 2, §7.4), through ClaudeLiveBridge on port 9873.

The set: TZ Src (TonnetzML, input No Input, Test rig on) and TZ Rec (Drift, MIDI From TZ Src ->
TonnetzML, armed). The rig plays ~/Music/TonnetzML/TestRig/Keys.txt (I-V-vi-IV from beat 4).
For each tempo and slot: set the slot (and gather 0), record an arrangement take on TZ Rec, read
its notes back and compare them with Tests/build/render_scenario at Live's sample rate and
buffer size (tolerance 0.001 beat); after each take every meter must read 0.

    phase2_mirror.py --rate 44100 --block 1024 [--tempos 120 97] [--slots A B C D E]
                     [--gather 0] [--out DIR]

Live's buffer size can't be changed through the bridge: pass the one Live is using (the
`get_audio_settings` tool reads it from Log.txt). Prints one PASS/FAIL line per take and exits 1
on any failure. Plain Python 3, no packages.
"""
import argparse, json, os, socket, subprocess, sys, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RENDER = REPO / "Tests" / "build" / "render_scenario"
KEYS = Path.home() / "Music" / "TonnetzML" / "TestRig" / "Keys.txt"
SRC, REC = "TZ Src", "TZ Rec"
TOL = 0.001


class Bridge:
    def __init__(self, port=9873):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=30)
        self.buf = b""
        self.n = 0

    def __call__(self, cmd, **args):
        self.n += 1
        args = {k: v for k, v in args.items() if v is not None}
        self.sock.sendall(json.dumps({"id": self.n, "cmd": cmd, "args": args}).encode() + b"\n")
        while True:
            while b"\n" not in self.buf:
                data = self.sock.recv(65536)
                if not data:
                    raise ConnectionError("bridge closed")
                self.buf += data
            line, self.buf = self.buf.split(b"\n", 1)
            resp = json.loads(line)
            if resp.get("id") == self.n:
                if not resp.get("ok"):
                    raise RuntimeError(f"{cmd}: {resp.get('error')}")
                return resp.get("result")


def render(slot, bpm, rate, block, gather):
    out = subprocess.run([str(RENDER), "--slot", slot, "--bpm", str(bpm), "--rate", str(rate), "--block", str(block),
                          "--gather", str(gather), str(KEYS)], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def compare(got, want):
    used = [False] * len(got)
    worst_s = worst_l = 0.0
    problems = []
    for w in want:
        best = None
        for i, g in enumerate(got):
            if used[i] or g["pitch"] != w["pitch"]:
                continue
            d = abs(g["song_time"] - w["start"])
            if d <= TOL and (best is None or d < best[1]):
                best = (i, d)
        if best is None:
            problems.append(f"missing {w['pitch']} @{w['start']}")
            continue
        i, d = best
        used[i] = True
        dl = abs(got[i]["duration"] - w["length"])
        worst_s, worst_l = max(worst_s, d), max(worst_l, dl)
        if dl > TOL:
            problems.append(f"length {got[i]['duration']} != {w['length']} ({w['pitch']} @{w['start']})")
        if got[i]["velocity"] != w["velocity"]:
            problems.append(f"velocity {got[i]['velocity']} != {w['velocity']} ({w['pitch']} @{w['start']})")
    problems += [f"extra {g['pitch']} @{g['song_time']}" for i, g in enumerate(got) if not used[i]]
    return problems, worst_s, worst_l


def meters_zero(b):
    levels = b("get_meters")
    bad = []
    for m in levels if isinstance(levels, list) else levels.get("tracks", []):
        vals = [v for k, v in m.items() if k not in ("track", "name", "index") and isinstance(v, (int, float))]
        if any(v > 0.0 for v in vals):
            bad.append(m)
    return bad


def param_index(b, name):
    params = b("list_parameters", track=SRC, device="TonnetzML")
    for i, p in enumerate(params["parameters"] if isinstance(params, dict) else params):
        if p["name"] == name:
            return i, p
    raise KeyError(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rate", type=float, required=True)
    ap.add_argument("--block", type=int, required=True)
    ap.add_argument("--tempos", type=float, nargs="+", default=[120.0, 97.0])
    ap.add_argument("--slots", nargs="+", default=list("ABCDE"))
    ap.add_argument("--gather", type=int, default=0)
    ap.add_argument("--out", default=str(REPO / "Tests" / "build" / "live"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    b = Bridge()
    _, slot_p = param_index(b, "Slot")
    _, gather_p = param_index(b, "Gather (ms)")
    # Choice and int parameters are 0..1 in Live (PLAN.md §7.4).
    b("set_parameter", track=SRC, device="TonnetzML", parameter="Gather (ms)", value=a.gather / 80.0)
    b("set_parameter", track=SRC, device="TonnetzML", parameter="Test rig", value=1.0)
    failures = 0
    end_beat = 21.0
    for bpm in a.tempos:
        b("set_tempo", bpm=bpm)
        for slot in a.slots:
            b("set_parameter", track=SRC, device="TonnetzML", parameter="Slot", value=(ord(slot) - ord("A")) / 7.0)
            b("clear_arrangement_clips", track=REC)
            time.sleep(0.5)
            b("start_recording", mode="arrangement", from_beat=0.0)
            # The take, plus Live's one-bar count-in at this tempo, plus ~1 s before it moves (§7.4).
            time.sleep((end_beat + 4.0) * 60.0 / bpm + 2.5)
            b("stop_recording", mode="arrangement")
            time.sleep(1.5)
            notes = b("get_clip_notes", track=REC, source="arrangement")
            got = notes["notes"] if isinstance(notes, dict) else notes
            want = render(slot, bpm, a.rate, a.block, a.gather)
            problems, ws, wl = compare(got, want["notes"])
            # Meters: 0 once Drift's release has died away (polled for up to 8 s after the stop).
            t0 = time.monotonic()
            bad = meters_zero(b)
            while bad and time.monotonic() - t0 < 8.0:
                time.sleep(0.25)
                bad = meters_zero(b)
            settle = time.monotonic() - t0
            tag = f"{bpm:g} BPM, {a.block} samples, slot {slot} ({want['word']})"
            (out / f"take_{a.block}_{bpm:g}_{slot}.json").write_text(json.dumps({"take": got, "render": want}, indent=1))
            ok = not problems and not bad
            failures += 0 if ok else 1
            print(f"{'PASS' if ok else 'FAIL'}  {tag}: {len(want['notes'])} expected, {len(got)} recorded, "
                  f"worst start {ws:.6f}, worst length {wl:.6f} beat; meters {'0 after %.1f s' % settle if not bad else bad}")
            for p in problems[:8]:
                print("      ", p)
            sys.stdout.flush()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
