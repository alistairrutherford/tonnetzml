#!/usr/bin/env python3
"""Phase 2's release check in Live (PLAN.md §6 row 2): with the rig playing Keys.txt through
TonnetzML (TZ Src) into Drift (TZ Rec), stop the transport, turn the device off (bypass), and
delete the device, each while a generated chord sounds. After each, every meter must fall to 0
(Drift's release allowed: polled for a while).

Bypass: Live stops calling a VST3 whose Device On is off, so nothing (no note-off either) can
go out while it is off (found in Chordism, §12 Phase 8 there). What the plug-in can do is end
everything on the first block after Device On comes back (reset() → releaseAll), so that is
what is checked: still sounding 2 s after Device On off (recorded as Live's behaviour), then
every meter 0 once it is back on. Deleting is last; reload the device by hand afterwards (and
re-route TZ Rec, which loses its input with it).

    phase2_release.py
"""
import sys, time
from phase2_mirror import Bridge, SRC, REC, KEYS, meters_zero

# One long chord (C E G from beat 4 for 60 beats), so each release is measured on its own: the
# check swaps it into Keys.txt and puts the original back at the end.
LONG_CHORD = "# phase2_release.py: one long chord\n4 60 100 60\n4 64 90 60\n4 67 80 60\n"

def sounding(b):
    for m in b("get_meters"):
        if m.get("name") == REC and m.get("level", 0) > 0.01:
            return True
    return False

def settle(b, limit=8.0):
    t0 = time.monotonic()
    bad = meters_zero(b)
    while bad and time.monotonic() - t0 < limit:
        time.sleep(0.25)
        bad = meters_zero(b)
    return not bad, time.monotonic() - t0

def play_into_chord(b):
    b("start_transport", from_beat=0.0)
    # The first chord starts at beat 4 and lasts 3.5 beats: at 120 BPM, beat 6 is 3 s in.
    time.sleep(3.0)
    return sounding(b)

def main():
    original = KEYS.read_text()
    KEYS.write_text(LONG_CHORD)
    time.sleep(0.5)                       # the plug-in polls Keys.txt 20 times a second
    try:
        return run()
    finally:
        KEYS.write_text(original)


def run():
    b = Bridge()
    b("set_tempo", bpm=120.0)
    b("set_parameter", track=SRC, device="TonnetzML", parameter="Test rig", value=1.0)
    b("set_parameter", track=SRC, device="TonnetzML", parameter="Slot", value=0.0)
    failures = 0

    def report(what, was_sounding, ok, secs):
        nonlocal failures
        good = was_sounding and ok
        failures += 0 if good else 1
        print(f"{'PASS' if good else 'FAIL'}  {what}: Drift sounding before ({was_sounding}), every meter 0 after {secs:.1f} s ({ok})")

    s = play_into_chord(b)
    b("stop_transport")
    report("stop", s, *settle(b))

    s = play_into_chord(b)
    b("set_parameter", track=SRC, device="TonnetzML", parameter="Device On", value=0.0)
    time.sleep(2.0)
    rings = sounding(b)
    print(f"INFO  Device On off: Drift still sounding 2 s later: {rings} (Live no longer calls the device)")
    b("set_parameter", track=SRC, device="TonnetzML", parameter="Device On", value=1.0)
    ok, secs = settle(b)
    b("stop_transport")
    report("bypass: Device On back on (transport still playing) ends it", s, ok, secs)
    time.sleep(1.0)

    s = play_into_chord(b)
    b("delete_device", track=SRC, device="TonnetzML")
    ok, secs = settle(b, 15.0)
    b("stop_transport")
    report("delete the device while playing", s, ok, secs)
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
