# DrumChimp

A drum instrument plug-in (VST3) for Ableton Live 12 on macOS. It builds 8-pad kits from the samples
in your Ableton library, randomises new kits, and generates drum patterns in different styles that
become MIDI clips in Live. The plan and status are in [PLAN.md](PLAN.md).

**Status:** Phase 5 is done (PLAN.md §10).
- **Into Live:** the pattern records on another track through *MIDI From*, every note within a
  sample of its beat, checked by the Live tests. *Drag MIDI* drops it on a clip slot. If a
  dragged clip and the generator both play, the status line says to set Source to *MIDI In*.
- **Patterns:** ten styles (Rock, Pop, Funk, Hip-Hop, Trap, House, Techno, Drum & Bass,
  Breakbeat, Reggae). Set Bars, Density, Complexity, Swing, Humanise, Ghost notes and Fill.
  Generate makes a new pattern; changing a setting remakes this one.
- **Step grid:** click a step to edit it, and lock a lane to keep it.
- **Playback and MIDI:** with Source on *Generator*, the pattern plays the pads in time with
  Live and goes to MIDI out, for another track's *MIDI From*. *Drag MIDI* drops the pattern on a
  clip slot.
- **Kits:** Randomise builds 8-pad kits (leaning toward the style's families: 808 for Trap, 909
  for House…) with locks, dice and undo, and the browser previews and loads samples.
- **Ableton Packs:** samples in installed Packs are protected (only Live can play them), so
  DrumChimp uses the Core Library, your User Library and your own folders.

## Install

Needs Xcode and JUCE 8.0.14 at `~/Development/JUCE` (as for ChordChimp):

```sh
Tools/install.sh --validate   # build (Release, universal), sign ad hoc, install, run pluginval
```

This installs `~/Library/Audio/Plug-Ins/VST3/DrumChimp.vst3` and `~/Applications/DrumChimp.app`.
Restart Live afterwards: Live keeps a plug-in's old binary loaded until it quits.

## Test

```sh
Tests/run_tests.sh            # core tests (no JUCE)
Tests/run_host_tests.sh       # processor tests (JUCE console app): loading, timing, state, no allocation
python3 Tests/live/run_live_tests.py   # in Live (ClaudeLiveBridge running): MIDI From timing, swing, state
Tests/run_validation.sh       # pluginval strictness 10 on the installed VST3
Tools/classify.sh             # scan and classify the Ableton library: timings, counts (--csv FILE: every sample, --kits N: example kits)
```

## Phase 0 spikes

The Phase 0 build logged what it saw to `~/Library/Logs/DrumChimp/spike.log` (later builds don't). Results are in PLAN.md §10.

| Spike | How it's checked |
|---|---|
| (a) MIDI out → *MIDI From* | Automated through ClaudeLiveBridge: DrumChimp on track 1 (Source: Generator), a MIDI track with *MIDI From: 1-DrumChimp → DrumChimp*, armed; record and read the clip's notes. |
| (b) Drag-out onto the same track | **By hand**, below. |
| (c) Drop from Live's browser | **By hand**, below. |
| (d) Library scan time | `Tools/scan_library.sh` (since replaced by `Tools/classify.sh`). |
| (e) A Remote Script can tell DrumChimps apart | Read the *Instance ID* parameter through the LOM (ClaudeLiveBridge `list_parameters`). |

### By hand in Live (b and c)

1. Put DrumChimp on a MIDI track and open its window.
2. **(b)** Drag the **Drag MIDI** handle (in Phase 0: *Drag 4-bar MIDI*) onto an empty clip slot on **DrumChimp's own track**.
   Then try the Arrangement on the same track. Expected: a 4-bar clip with the test pattern (kick,
   snare, hats, an open hat, a tom pickup, a crash on each bar's one).
3. Set **Source** to *MIDI In* and launch the clip. Expected: the pads play from the clip, and
   **Notes in** counts up. (With Source on *Generator* both play, which is why Source exists.)
4. **(c)** In Live's browser open *Samples* (or *Packs → Drum Essentials → Samples → Drums → Kick*)
   and drag a sample onto a pad. Expected: the pad shows the sample's name and the log under the
   pads shows its full path. Also try dragging a file from Finder.
