"""DrumChimp's tests in Live (PLAN.md §7, Phase 5; G8), through ClaudeLiveBridge.

Needs Live 12 running with DrumChimp installed and the ClaudeLiveBridge Remote Script selected
(the ableton-mcp project: ~/Development/projects/python/ableton-mcp, or DRUMCHIMP_LIVEBRIDGE for
its src folder). Only Python's standard library.

It works in whatever set is open, and leaves it as it was: it adds its own tracks (named
"DC test …") and deletes them, disarms your armed tracks while it records and re-arms them, and
puts the tempo back. It records into the Arrangement from beat 0 on its own tracks only.

  1. Routing    DrumChimp (House, 2 bars, Humanise 0, no fills) recorded through MIDI From for two
                loops at 120 BPM: every note on its sixteenth to within a sample, the kick on every
                beat, the second loop the same as the first
  2. Swing      Funk (sixteenth hats) at Swing 66%: the off-beat sixteenths a third of a step late,
                the rest on the grid
  3. State      a duplicate of the DrumChimp track plays exactly the same pattern: the pattern is
                saved with the device, not made again (a new instance would have a new seed)
  4. 12 pads    Reggae's one drop: the rim on the Rim pad (note 37, the third row), with the
                kick, on beat 3 of every bar; the snare (38) silent
  5. 3/4        with Live's set in 3/4, Waltz: every note on its sixteenth, the kick on the 1 of
                every 3-beat bar and nowhere else, the snare on 2 and 3 (the set goes back to its
                own time signature after)

  python3 Tests/live/run_live_tests.py
"""

import os
import sys
import time

BRIDGE_SRC = os.environ.get("DRUMCHIMP_LIVEBRIDGE", os.path.expanduser("~/Development/projects/python/ableton-mcp/src"))
sys.path.insert(0, BRIDGE_SRC)
from livebridge.client import BridgeError, LiveClient  # noqa: E402

PLUGIN = "DrumChimp"
PREFIX = "DC test "
# DrumChimp's choice lists, in order (Source/plugin/Parameters.cpp). Live shows no value items for a
# VST3 plug-in's choices, so a choice is set by its normalised position and checked by its text.
CHOICES = {
    "Style": ["Rock", "Pop", "Funk", "Hip-Hop", "Trap", "House", "Techno", "Drum & Bass", "Breakbeat", "Reggae",
              "Disco", "Shuffle", "Jazz", "Bossa Nova", "Afrobeat", "2-Step", "Dembow", "Dubstep", "Motown", "Punk",
              "Waltz", "Jazz Waltz", "6/8 Ballad", "Afro 6/8"],
    "Bars": ["1", "2", "4", "8"],
    "Fill": ["Off", "Every 2 bars", "Every 4 bars", "Every 8 bars"],
    "Source": ["Generator", "MIDI In"],
}
RANGES = {"Swing": (0.5, 0.75), "Humanise": (0.0, 1.0), "Density": (0.0, 1.0), "Complexity": (0.0, 1.0)}
# One sample at 44.1 kHz and 120 BPM is 4.5e-5 beats: allow a sample and a half.
TOLERANCE = 7e-5

failures = 0


def check(ok, what):
    global failures
    print(("  PASS  " if ok else "  FAIL  ") + what, flush=True)
    failures += 0 if ok else 1


def tracks(live):
    return live.call("get_state", include_devices=False)["tracks"]


def index_of(live, name):
    return next(t["index"] for t in tracks(live) if t["name"] == name)


def make_track(live, name):
    live.call("create_midi_track", index=-1, name=name)
    return index_of(live, name)


def set_param(live, track, name, value):
    """A choice by its text, or a number in the parameter's own units. Returns the display text."""
    device = 0
    if name in CHOICES:
        options = CHOICES[name]
        normalised = options.index(value) / (len(options) - 1)
    else:
        lo, hi = RANGES[name]
        normalised = (value - lo) / (hi - lo)
    live.call("set_parameter", track=track, device=device, parameter=name, value=normalised)
    time.sleep(0.3)                          # DrumChimp remakes its pattern on its 20 Hz timer
    return live.call("get_parameter", track=track, device=device, parameter=name)["display"]


def route_from(live, rec, source):
    live.call("set_routing", track=rec, direction="input", source_track=source, channel=PLUGIN)


def record(live, beats, rec_tracks):
    """Record `beats` from beat 0 on the armed tracks; the notes of each of `rec_tracks`."""
    seconds = beats * 60.0 / live.call("get_state", include_devices=False)["tempo"]
    live.call("start_recording", mode="arrangement", from_beat=0.0)
    try:
        live.wait_song_time(beats + 0.25, seconds * 2 + 10)
    finally:
        live.call("stop_recording", mode="arrangement", stop_transport=True)
    return [live.call("get_clip_notes", track=t, source="arrangement")["notes"] for t in rec_tracks]


def clear(live, rec_tracks):
    for t in rec_tracks:
        try:
            live.call("clear_arrangement_clips", track=t)
        except BridgeError:
            pass


def starts(notes, below=None):
    return sorted((round(n["start_time"], 6), n["pitch"], n["velocity"]) for n in notes if below is None or n["start_time"] < below)


def test_routing(live, source, rec):
    print("1. Routing: DrumChimp → MIDI From → a recorded clip")
    shown = [set_param(live, source, "Style", "House"), set_param(live, source, "Bars", "2"), set_param(live, source, "Fill", "Off"),
             set_param(live, source, "Humanise", 0.0), set_param(live, source, "Swing", 0.5), set_param(live, source, "Source", "Generator")]
    check(shown[:3] == ["House", "2", "Off"] and shown[3] == "0%" and shown[4] == "50%", "set: " + ", ".join(shown))
    notes = record(live, 16, [rec])[0]
    notes = [n for n in notes if n["start_time"] < 16.0 - 1e-6]
    off_grid = [n for n in notes if abs(n["start_time"] * 4 - round(n["start_time"] * 4)) / 4 > TOLERANCE]
    check(len(notes) > 40 and not off_grid, "%d notes, every one on its sixteenth to within a sample%s"
          % (len(notes), "" if not off_grid else " (off: %s)" % [(n["pitch"], n["start_time"]) for n in off_grid[:4]]))
    kicks = {round(n["start_time"]) for n in notes if n["pitch"] == 36 and abs(n["start_time"] - round(n["start_time"])) < TOLERANCE}
    check(kicks >= set(range(16)), "the kick on all 16 beats")
    first = [(round(t, 4), p, v) for t, p, v in starts(notes) if t < 8.0 - 1e-6]
    second = [(round(t - 8.0, 4), p, v) for t, p, v in starts(notes) if t >= 8.0 - 1e-6]
    check(first == second and len(first) > 20, "the second loop is the first again (%d notes each)" % len(first))
    return notes


def test_swing(live, source, rec):
    print("2. Swing")
    set_param(live, source, "Style", "Funk")    # sixteenth hats: plenty of off-beats (its own swing comes too)
    shown = set_param(live, source, "Swing", 0.66)
    clear(live, [rec])
    notes = [n for n in record(live, 8, [rec])[0] if n["start_time"] < 8.0 - 1e-6]
    late, straight, wrong = 0, 0, []
    for n in notes:
        sixteenth = n["start_time"] * 4
        step = int(sixteenth + 0.5) if abs(sixteenth - round(sixteenth)) <= TOLERANCE * 4 else int(sixteenth)
        if step % 2 == 0:
            ok = abs(n["start_time"] - step / 4) <= TOLERANCE
            straight += 1 if ok else 0
        else:
            ok = abs(n["start_time"] - (step / 4 + 0.32 * 0.25)) <= TOLERANCE
            late += 1 if ok else 0
        if not ok:
            wrong.append((n["pitch"], round(n["start_time"], 5)))
    check(shown == "66%" and late >= 8 and not wrong, "Funk, Swing %s: %d off-beat sixteenths a third of a step late, %d on the grid%s"
          % (shown, late, straight, "" if not wrong else ", wrong: %s" % wrong[:4]))
    set_param(live, source, "Style", "House")
    set_param(live, source, "Swing", 0.5)


def test_state(live, source, rec):
    print("3. State: a duplicated DrumChimp plays the same pattern")
    live.call("duplicate_track", track=source)
    copy = source + 1
    live.call("set_track", track=copy, name=PREFIX + "DrumChimp copy")
    rec2 = make_track(live, PREFIX + "rec copy")
    rec = index_of(live, PREFIX + "rec")
    route_from(live, rec2, index_of(live, PREFIX + "DrumChimp copy"))
    live.call("set_track", track=rec2, arm=True)
    live.call("set_track", track=index_of(live, PREFIX + "DrumChimp copy"), arm=False)
    check(live.call("get_parameter", track=index_of(live, PREFIX + "DrumChimp copy"), device=0, parameter="Style")["display"] == "House",
          "the copy's Style is House")
    clear(live, [rec, rec2])
    a, b = record(live, 8, [rec, rec2])
    a, b = starts(a, 8.0 - 1e-6), starts(b, 8.0 - 1e-6)
    check(a == b and len(a) > 20, "the copy's notes are the original's (%d and %d)" % (len(a), len(b)))


def test_twelve_pads(live, source, rec):
    print("4. Twelve pads: Reggae's rim on the Rim pad")
    shown = set_param(live, source, "Style", "Reggae")
    set_param(live, source, "Humanise", 0.0)
    set_param(live, source, "Fill", "Off")
    clear(live, [rec])
    notes = [n for n in record(live, 8, [rec])[0] if n["start_time"] < 8.0 - 1e-6]
    rims = sorted({round(n["start_time"], 3) for n in notes if n["pitch"] == 37})
    kicks = sorted({round(n["start_time"], 3) for n in notes if n["pitch"] == 36})
    check(shown == "Reggae" and rims == [2.0, 6.0] and kicks == [2.0, 6.0], "the rim (37) and the kick together on beat 3 of both bars: %s, %s" % (rims, kicks))
    check(not any(n["pitch"] == 38 for n in notes), "and no snare: it has its own pad now")


def test_three_four(live, source, rec):
    print("5. 3/4: Waltz in a 3/4 set")
    live.call("set_song_properties", signature_numerator=3, signature_denominator=4)
    shown = set_param(live, source, "Style", "Waltz")
    set_param(live, source, "Humanise", 0.0)
    set_param(live, source, "Fill", "Off")
    clear(live, [rec])
    notes = [n for n in record(live, 12, [rec])[0] if n["start_time"] < 12.0 - 1e-6]
    off_grid = [n for n in notes if abs(n["start_time"] * 4 - round(n["start_time"] * 4)) / 4 > TOLERANCE]
    kicks = sorted({round(n["start_time"], 3) for n in notes if n["pitch"] == 36})
    snares = {round(n["start_time"], 3) for n in notes if n["pitch"] == 38}
    check(shown == "Waltz" and notes and not off_grid, "%d notes, every one on its sixteenth" % len(notes))
    check(kicks == [0.0, 3.0, 6.0, 9.0], "the kick on every bar's 1 and nowhere else: %s" % kicks)
    check(snares >= {1.0, 2.0, 4.0, 5.0, 7.0, 8.0, 10.0, 11.0}, "the snare on 2 and 3 of every bar")


def main():
    live = LiveClient().connect()
    state = live.call("get_state", include_devices=False)
    tempo = state["tempo"]
    signature = state["signature"]
    armed = [t["index"] for t in state["tracks"] if t.get("arm")]
    for t in armed:
        live.call("set_track", track=t, arm=False)
    try:
        live.call("set_tempo", bpm=120.0)
        source = make_track(live, PREFIX + "DrumChimp")
        live.load_device_and_wait(PLUGIN, source, "plugins", "vst3")
        rec = make_track(live, PREFIX + "rec")
        source = index_of(live, PREFIX + "DrumChimp")
        route_from(live, rec, source)
        live.call("set_track", track=rec, arm=True)
        live.call("set_track", track=source, arm=False)
        time.sleep(1.0)                      # the default kit loads

        test_routing(live, source, rec)
        test_swing(live, source, rec)
        test_state(live, index_of(live, PREFIX + "DrumChimp"), rec)
        test_twelve_pads(live, index_of(live, PREFIX + "DrumChimp"), index_of(live, PREFIX + "rec"))
        test_three_four(live, index_of(live, PREFIX + "DrumChimp"), index_of(live, PREFIX + "rec"))
    finally:
        live.call("stop_transport")
        for t in sorted((t["index"] for t in tracks(live) if t["name"].startswith(PREFIX)), reverse=True):
            live.call("delete_track", track=t)
        names = {t["index"]: t["name"] for t in tracks(live)}
        for t in armed:
            if t in names:
                live.call("set_track", track=t, arm=True)
        live.call("set_tempo", bpm=tempo)
        live.call("set_song_properties", signature_numerator=signature[0], signature_denominator=signature[1])
    print("\n" + ("Live tests: all passed" if failures == 0 else "*** Live tests: %d failure(s)" % failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
