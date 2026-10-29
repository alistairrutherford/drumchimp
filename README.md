# DrumChimp

A drum instrument plug-in (VST3) for Ableton Live 12 on macOS. It builds 12-pad kits from the
samples in your Ableton library, randomises new kits, and generates drum patterns in 24 styles
that you can record or drag into Live as MIDI clips.

![DrumChimp's window: twelve pads (Kick, Snare, Clap, Closed hat, Open hat, Low tom, High tom, Crash, Rim, Mid tom, Ride, Perc) with the Core Library's 909 kit; the kit bar with Randomise kit, Undo, Redo, From library and the Matched slider; the library browser and the selected pad's settings on the right; the pattern settings (House, 4 bars, Fill every 4, Generate, Density, Complexity, Swing, Humanise, Ghost notes); and a House pattern in the step grid along the bottom](docs/screenshot.png)

The design, the decisions and the results of each phase are in [PLAN.md](PLAN.md).

## Install

You need macOS, Xcode (open it once after installing), and JUCE 8.0.14:

```bash
git clone --branch 8.0.14 https://github.com/juce-framework/JUCE.git ~/Development/JUCE
```

Then build and install:

```bash
Tools/install.sh --validate
```

This builds a universal Release, signs it ad hoc, and installs it for your user only:

- `~/Library/Audio/Plug-Ins/VST3/DrumChimp.vst3`
- `~/Applications/DrumChimp.app`, a stand-alone version for trying it without Live

`--validate` also runs pluginval. Other options:

- `--juce DIR` builds against JUCE somewhere else.
- `--no-standalone` installs only the plug-in.
- `--uninstall` removes both.

In Live, turn on **Settings → Plug-Ins → Use VST3 Plug-In System Folders** once, then **Rescan**.
DrumChimp appears under **Plug-Ins → VST3 → Alistair Rutherford**. After installing a new build,
restart Live, because Live keeps a plug-in's old version loaded until it quits.

## Using DrumChimp

### Getting started

1. Drop DrumChimp on a MIDI track and open its window.
2. A new DrumChimp starts with the Core Library's 909 kit and a Rock pattern.
3. Press play in Live. With **Source** set to *Generator*, the pattern plays in time with Live,
   looping from bar 1.
4. Click a pad to hear it.

In the background, DrumChimp finds your Ableton library and sorts every one-shot into a drum
role. The bottom-right corner shows how many it found. Click **Settings** to see where it looked.

### Kits

- **Randomise kit** gives every unlocked pad a new sample of its role.
- **From library / From Ableton kits** chooses where Randomise looks:
  - *From library* builds a kit from your samples. The **Matched … Wild** slider sets how alike
    they are: *Matched* keeps the kit to one family, such as 808, 909, 707 or *Acoustified*,
    leaning toward the style's sound (808 for Trap, 909 for House). *Wild* takes any sample of
    each role.
  - *From Ableton kits* picks one of Ableton's own Drum Rack kits from the Core Library.
- **Undo** and **Redo** step through the last 20 kit changes.
- **On each pad:**
  - the **padlock** keeps the pad when you Randomise;
  - the **die** gives that pad alone a new sample.
- **Library browser** (top right):
  - It shows samples of the selected pad's role. Change that with the menu, or type in
    **Search**.
  - **Click** a sample to hear it.
  - **Double-click** a sample to load it on the selected pad, or **drag** it onto any pad.
  - You can also drag samples onto a pad from Live's browser or from Finder.
- **Pad settings** (under the browser): click a pad to select it.
  - **Role:** what Randomise, the dice and the browser give this pad, and which part of the
    pattern it plays.
  - **Load… / Clear:** choose a sample from a file, or empty the pad.
  - **Level, Pan, Tune, Decay, Velocity:** Decay at *Full* plays the whole sample. Velocity sets
    how much the hit's velocity changes its level.
  - **Choke:** pads in the same group cut each other off. The hats share group A, so a closed
    hat stops an open one.
  - **Mute** and **Solo**.
- **Kits menu:**
  - **Save kit…** saves the samples, roles and pad settings.
  - **Ableton kits** loads one of Ableton's Drum Rack kits by name.
  - Your saved kits are listed below; **Show kits folder** opens their folder.
- **Settings:** shows the places DrumChimp looks for samples: your Core Library, User Library
  and Live's *Places*. **Add folder…** adds a folder of your own, and **Rescan** looks again.

### Patterns

- **Style:** 24 styles: Rock, Pop, Funk, Hip-Hop, Trap, House, Techno, Drum & Bass, Breakbeat,
  Reggae, Disco, Shuffle, Jazz, Bossa Nova, Afrobeat, 2-Step, Dembow, Dubstep, Motown, Punk,
  Waltz and Jazz Waltz (3/4), and 6/8 Ballad and Afro 6/8 (6/8). Choosing a style sets its own
  swing.
- **Bars:** the pattern's length, 1, 2, 4 or 8 bars.
- **Fill:** a fill (toms or snare) at the end of every 2, 4 or 8 bars, then a crash. Or no
  fills.
- **Density:** how many of the style's hits play.
- **Complexity:** how far the pattern strays from the style's basic groove: variations, ghost
  notes and rolls. At **Complexity 0** (with Density at 50% or more), every style plays its
  groove exactly as written.
- **Swing** (50% straight to 75%): delays the off-beat sixteenths.
- **Humanise:** small changes to timing and velocity.
- **Ghost notes:** turns quiet in-between notes on or off (mostly snare; some styles have them
  on the kick or percussion).
- **Generate:** makes a new pattern. Changing any setting above remakes the current pattern
  instead: turning Density up only adds hits.

**Step grid** (along the bottom): one lane per pad. A brighter step is a harder hit, and dots
are a roll.
- **Click** an empty step to add a hit, or click a hit to remove it.
- **Drag** a hit up or down to set its velocity.
- The **padlock** at the start of a lane keeps that lane when you Generate or change a setting.

Hand edits last until you change a setting or Generate. The pattern, your edits and the lane
locks are saved with the Live set.

**3/4 and 6/8 styles:** set Live's time signature to match. The status line tells you when they
differ.

### Getting the pattern into Live

**Record it on another track.** This also catches fills and anything you change while it plays.

1. Make a second MIDI track.
2. Set its **MIDI From** to DrumChimp's track, and the channel below it to **DrumChimp**.
3. Arm it and record. Every note lands within a sample of where DrumChimp played it.
4. To hear the recording, put an instrument on that track (a Drum Rack works, since DrumChimp's
   notes are General MIDI), or drag the clip onto DrumChimp's track as below.

**Drag it as a clip.**

1. Drag **Drag MIDI** onto a clip slot or into the Arrangement. You get the whole pattern, Bars
   long, with its swing and humanise.
2. To have that clip play DrumChimp, put it on DrumChimp's own track and set **Source** to
   **MIDI In**, so the generator doesn't play as well. If both play, the status line warns you.

**Play it from MIDI.** With **Source** on **MIDI In**, DrumChimp is a 12-pad drum sampler for
clips, a keyboard or a controller. **Note map** chooses which notes play which pads:

| Pad | Kick | Snare | Clap | Closed hat | Open hat | Low tom | High tom | Crash | Rim | Mid tom | Ride | Perc |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **GM** (default) | C1 36 | D1 38 | D#1 39 | F#1 42 | A#1 46 | A1 45 | D2 50 | C#2 49 | C#1 37 | B1 47 | D#2 51 | F#2 54 |
| **Chromatic** | C1 36 | C#1 37 | D1 38 | D#1 39 | E1 40 | F1 41 | F#1 42 | G1 43 | G#1 44 | A1 45 | A#1 46 | B1 47 |

### Good to know

- **Ableton Packs are protected.** Samples in Ableton's installed Packs can only be played by
  Live, so DrumChimp leaves them out of its library. Settings says how many it found. It uses
  the Core Library, your User Library and folders you add, which comes to about 2,400 drums and
  119 Ableton kits on a Live 12 Standard Mac.
- **Missing samples.** If a sample used in a set has moved, DrumChimp looks for it in the library
  by name and size. If it can't find it, the pad and the status line say so.
- **Window size.** Drag the bottom-right corner to resize the window. The size is saved with the
  set.

## Development

```bash
Tests/run_tests.sh
```

The core tests (no JUCE): sampler, classifier, library scan, kits, styles and patterns. The
classifier is checked against 300 hand-labelled library paths.

```bash
Tests/run_host_tests.sh
```

The processor and editor in a JUCE console app. It covers loading, timing, saved state, no
allocation while playing, CPU, damaged states, Ableton kits and the editor snapshot. It then
runs again as a Mac with no library. `--update-snapshot` writes a new reference image after a
deliberate UI change.

```bash
python3 Tests/live/run_live_tests.py
```

Tests in Live through ClaudeLiveBridge: *MIDI From* timing, swing, saved state, the 12 pads, and
3/4. They add their own tracks and leave the set as it was.

```bash
Tests/run_validation.sh
```

pluginval at strictness 10 on the installed VST3.

```bash
Tools/classify.sh --kits 3
```

Scans and sorts your Ableton library and prints the counts. `--csv FILE` writes every sample,
and `--kits N` prints example kits.

```bash
Tests/Host/Builds/MacOSX/build/Release/DrumChimpHost --screenshot docs/screenshot.png
```

Remakes the screenshot above. Run `Tests/run_host_tests.sh` first, to build the test app.
