# DrumChimp

A drum instrument plug-in (VST3) for Ableton Live 12 on macOS. It builds 8-pad kits from the samples
in your Ableton library, randomises new kits, and generates drum patterns in different styles that
become MIDI clips in Live. The plan and status are in [PLAN.md](PLAN.md).

**Status:** Phase 1 is done (PLAN.md §10): eight sampler pads with Level, Pan, Tune, Decay,
Velocity, Choke, Mute and Solo; drop a sample from Live's browser or Finder on a pad, or use
Load…. A new instance starts with the Core Library's 909 kit. The pattern generator is still the
Phase 0 test pattern (Source: Generator), and there's no library browser or Randomise yet.

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
Tests/run_validation.sh       # pluginval strictness 10 on the installed VST3
Tools/scan_library.sh         # spike (d): time a scan of the Ableton library (--csv FILE for the list)
```

## Phase 0 spikes

The Phase 0 build logged what it saw to `~/Library/Logs/DrumChimp/spike.log` (later builds don't). Results are in PLAN.md §10.

| Spike | How it's checked |
|---|---|
| (a) MIDI out → *MIDI From* | Automated through ClaudeLiveBridge: DrumChimp on track 1 (Source: Generator), a MIDI track with *MIDI From: 1-DrumChimp → DrumChimp*, armed; record and read the clip's notes. |
| (b) Drag-out onto the same track | **By hand**, below. |
| (c) Drop from Live's browser | **By hand**, below. |
| (d) Library scan time | `Tools/scan_library.sh`. |
| (e) A Remote Script can tell DrumChimps apart | Read the *Instance ID* parameter through the LOM (ClaudeLiveBridge `list_parameters`). |

### By hand in Live (b and c)

1. Put DrumChimp on a MIDI track and open its window.
2. **(b)** Drag the **Drag 4-bar MIDI** handle onto an empty clip slot on **DrumChimp's own track**.
   Then try the Arrangement on the same track. Expected: a 4-bar clip with the test pattern (kick,
   snare, hats, an open hat, a tom pickup, a crash on each bar's one).
3. Set **Source** to *MIDI In* and launch the clip. Expected: the pads play from the clip, and
   **Notes in** counts up. (With Source on *Generator* both play, which is why Source exists.)
4. **(c)** In Live's browser open *Samples* (or *Packs → Drum Essentials → Samples → Drums → Kick*)
   and drag a sample onto a pad. Expected: the pad shows the sample's name and the log under the
   pads shows its full path. Also try dragging a file from Finder.
