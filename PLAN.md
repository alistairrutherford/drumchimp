# DrumChimp: Project Plan

A macOS VST3 drum instrument for Ableton Live 12. It builds 8-pad drum kits from the samples
already in your Ableton library, randomises new kits on demand, and generates drum patterns in a
choice of styles that you can turn into MIDI clips in Live.

**The brief (2026-10-28):**

- A VST that generates drum rhythms.
- It picks samples from Ableton's sample library.
- It has **eight assignable pads**.
- It sorts library samples into drum categories **by name**, and sets up a kit of the drums
  a real kit usually has.
- Pads can also be assigned other samples **by hand**.
- A **Randomise** button creates new kits.
- It generates MIDI for drum patterns in **different styles**. That MIDI is recorded into the
  same track the plug-in is on if possible; otherwise it goes to another track to be recorded.

**The short answer on recording:** Live records the MIDI that *arrives at* a track's input, not
the MIDI a plug-in *produces* on that track (NoteCap plan §2, point 3). So a plug-in cannot
record its own output onto its own track by itself. DrumChimp still gets its patterns onto its
own track in two ways: **drag the pattern out as a MIDI clip** (v1), or have a small Remote
Script **write the clip directly** (Phase 7). Recording live onto **another track** through
VST3 *MIDI From* is also in v1; Chordism and NoteCap measured that route as sample-accurate.
§3.6 has the details.

---

## 1. Goals

| # | Requirement | Acceptance criterion |
|---|-------------|----------------------|
| G1 | Loads in Live 12 as a VST3 instrument and plays 8 pads from incoming MIDI | `pluginval` strictness 10 passes. A MIDI clip on DrumChimp's track plays the pads, sample-accurately. |
| G2 | **Finds Ableton's samples** without set-up | On first run it auto-detects the Core Library, Factory Packs and User Library, and indexes them in the background (this Mac: about 4,000 drum one-shots in under 10 s). |
| G3 | **Sorts samples by name** into drum roles | ≥ 95% correct on a hand-labelled set of 300 library paths (classifier report, §8). Loops, `.asd` files and non-drum one-shots are left out. |
| G4 | **Builds a real-kit-style kit** in one click | Each of the 8 pads gets a sample of its role. Samples come from one "family" (same machine, pack or character) where possible (§3.3). |
| G5 | **Randomise** creates new kits; **locked pads stay** | Randomise changes every unlocked pad and none of the locked ones. Undo brings back the previous kit. |
| G6 | **Manual assignment** | Drag a sample from Live's browser or Finder onto a pad, or pick one in DrumChimp's own browser. |
| G7 | **Generates patterns** in at least 10 styles | Each style passes its invariants (for example, House has a kick on every beat; Rock has the snare on 2 and 4). The same seed gives the same pattern. |
| G8 | **Patterns become MIDI in Live** | Drag-out puts an N-bar clip on DrumChimp's own track. *MIDI From: DrumChimp* records the generator, sample-accurately, on another track. |
| G9 | Saves and recalls everything with the set | Kit, pad settings, pattern and parameters survive saving and reopening, and duplicating the track. Missing samples are found again by name, or reported. |
| G10 | Real-time safe | No allocation, locks or file I/O on the audio thread (malloc-hook test). Sample loading happens on a background thread. |

**Out of scope for v1:** Windows, AU, AAX, multi-out, time signatures other than 4/4, sample
editing (start point and loop), audio analysis in the classifier, and loading Ableton
Drum Rack presets as kits. The last two are candidates for Phase 7.

---

## 2. Ableton constraints that shape the design

Mostly learned from Chordism, ChordChimp and NoteCap:

1. **VST3 only.** Live routes a plug-in's MIDI output (*MIDI From → track → plug-in*) only for
   VST plug-ins, not for AU. Same choice as NoteCap and ChordChimp.
2. **No recording onto its own track.** Live records what arrives at a track's input. The
   plug-in's own MIDI output can only reach *another* track's input (point 1).
3. **The CoreMIDI virtual-port trick is not worth it.** NoteCap tried it and dropped it on
   2026-10-21: it arrived one buffer + ~1.6 ms late, and it leaked into every track whose input
   was *All Ins*. Using it here would also feed DrumChimp's own output back into its input.
4. **A plug-in cannot see Live's browser.** DrumChimp reads the library folders on disk itself.
   The Live Object Model can reach the browser, but only from a Remote Script, not from a plug-in.
5. **Drag-and-drop works both ways.** Live accepts a `.mid` file dragged from a plug-in window
   onto a clip slot or the Arrangement (ChordChimp's export drag-out does this). Plug-in windows
   can accept files dragged from Live's browser. Both get checked again in Phase 0.
6. **Samples in installed Packs are protected (found in Phase 4).** Every sample in
   `~/Music/Ableton/Factory Packs` is an AIFC file with an `able` chunk. Its header reads, but its
   audio can only be decoded by Live; macOS's own `afinfo` can't open it either. The Core
   Library's samples, the User Library and the user's own folders are ordinary files.
   - DrumChimp leaves Pack samples out of its library and counts them.
   - If one is dropped on a pad, DrumChimp says why it can't load.

   So the drums DrumChimp can use from Ableton's own library are the Core Library's (2,439 on
   this Mac). Pack sounds would only be usable by exporting them from Live as new audio.

### Where the samples are (on this Mac)

| Location | Path | Drum one-shots |
|---|---|---|
| Core Library | `/Applications/Ableton Live 12 Standard.app/Contents/App-Resources/Core Library/Samples/` | ~2,550 (`One Shots/Drums/<Role>/`, `Multisamples/Drum Machines/808, 909, …`) |
| Factory Packs | `~/Music/Ableton/Factory Packs/<Pack>/Samples/` | ~1,490, **protected: only Live can play them** (§2, point 6) |
| User Library | `~/Music/Ableton/User Library/Samples/` | 0 for now |
| Drum Rack presets | `*.adg` in packs and the Core Library | ~300 (Phase 7: curated kits) |

Ableton already sorts one-shots into role folders: `Kick`, `Snare`, `Clap`, `Hihat`, `Cymbal`,
`Ride`, `Rim`, `Tom`, `Shaker`, `Tambourine`, `Conga`, `Bongo`, `Timbales`, `Wood`, `Bell`,
`Electronic Percussion`, `Misc Percussion`, `FX Hit`, `Utility`. File names mostly follow the
pattern `<Role> <Family/Character> <n>`: `Kick 808 Tone 1 Long`, `Hihat-909-Open`,
`ClosedHH Acoustified 14`, `Snare PL1465 Rimshot`, `Crash Cymbal 505 Classic`. The classifier
uses both the folder and the file name (§3.2).

The app name (Standard / Suite / Intro, or a beta), the Packs folder (set in Live's
*Preferences → Library*), and any extra user folders can all differ, so every root is
auto-detected and can also be edited in Settings. Live's own
`~/Library/Preferences/Ableton/Live 12.x/Library.cfg` lists the User Library, every installed
pack wherever it is, and the user's *Places* folders, so detection reads the newest one
(`Source/core/library/LibraryRoots.h`, Phase 0). Only the Core Library is found by looking for
the app.

---

## 3. Functional design

### 3.1 Pads

Eight pads, each with a **role**, a **sample** and its own sound settings. The default roles
follow a standard kit, with General MIDI note numbers, so the generated MIDI also plays an
Ableton Drum Rack correctly:

| Pad | Default role | Note | Choke group |
|---|---|---|---|
| 1 | Kick | 36 (C1) | – |
| 2 | Snare | 38 (D1) | – |
| 3 | Clap (or Rim) | 39 (D#1) | – |
| 4 | Closed hi-hat | 42 (F#1) | Hats |
| 5 | Open hi-hat | 46 (A#1) | Hats (a closed hat cuts it off) |
| 6 | Low tom | 45 (A1) | – |
| 7 | High tom | 50 (D2) | – |
| 8 | Crash or ride | 49 (C#2) | – |

- **Role per pad** can be changed (for example, pad 7 → Shaker or Perc). Changing a role changes
  what Randomise picks for that pad and which pattern lane it plays.
- **Note map:** *GM* (above, the default) or *Chromatic* (36–43, like a Drum Rack's bottom row).
- **Per-pad settings:** Level, Pan, Tune (±24 semitones), Decay, Velocity sensitivity, Choke
  group, Mute, Solo, and **Lock** (Randomise leaves the pad alone).
- **Audition:** clicking a pad plays it. MIDI notes play the pads in both source modes (§3.5).

### 3.2 Sample classifier

*As built in Phase 2* (`Source/core/library/Classifier.h`, `LibraryIndex.h`; `Source/plugin/Library.h`).

The scan runs in the background when the first DrumChimp opens, and every DrumChimp in the process
shares it. It keeps a header cache (`~/Library/Application Support/DrumChimp/library-cache.tsv`:
path, size, modification time, length). A file whose size and modification time haven't changed
isn't read again. Classifications aren't cached: they take microseconds, so a better classifier
needs no rescan.

1. **Collect** `.wav`/`.aif`/`.aiff`/`.flac`. Skip `.asd` analysis files, anything in a `Loops`
   folder, names containing `loop` or `NNN bpm`, and anything over 30 s (too long for a pad).
2. **Tokenise** the path below its root: split on anything not a letter or digit, on camelCase
   and on letter–digit boundaries, then lower-case (`ClosedHH` → `closed hh`). Join the pairs that
   mean one thing: *hi hat*, *bass drum*, *side stick*, *rim shot*, *e perc*, *floor tom*.
3. **Not a drum** → *Unsorted*:
   - a folder of something else (Synth, Stab, Vocal/Vox, Bass, Chords, Piano & Keys, Guitar,
     Strings, Instrument, Metronomes/Defaults, a top-level FX, or any Multisamples folder except
     Drum Machines);
   - a drum folder of effects or utilities (`FX Hit`, `Utility`).
4. **A role folder decides**, the deepest one. Ableton already sorts its one-shots by role, so
   this is the main rule. The name only refines it:
   - in a Snare folder, a name with *rim / rimshot / sidestick* is a Rim, and one starting with
     *clap* is a Clap;
   - in a Cymbal folder, *ride* is a Ride;
   - in a hat folder, *open/half* or *closed/pedal/foot/tight* in the name (or the folder) decide;
     with neither, under 0.5 s is closed.

   A percussion folder (Shaker, Tambourine, Conga, Bongo, Timbales, Wood, Bell, Misc and
   Electronic Percussion) is always Perc, whatever drum words its names contain (*Timbale Rim*,
   *E-Perc Screech Kick*).
5. **No role folder:** the name's first role word decides (`Drum Machines/808/Kick 808 Tone1`,
   or a user's `BD_909_01.wav`):

   | Role | Words |
   |---|---|
   | Kick | kick, kik, kck, bd, bass drum, kick drum |
   | Snare | snare, snr, sd |
   | Clap | clap, clp, handclap, snap |
   | Rim | rim, rimshot, side stick, rimclick |
   | Closed / Open hat | hihat, hat, hh (then open/closed as in 4); ch, chh / oh, ohh on their own |
   | Tom | tom, floor tom (range: lo/low/floor, mid/med, hi/high/rack) |
   | Crash | crash, cymbal, cym, splash, china |
   | Ride | ride |
   | Perc | perc, e-perc, shaker, tamb, conga, bongo, timbale, wood, block, cowbell, bell, triangle, clave, guiro, cabasa, agogo, maracas, cuica, tabla, timpani |

   A `Chops` folder of break slices counts only a role word that comes first (*Snare Ghosts*,
   not *Heavy Kicks*), and never a slice of whole drums or a fill.
6. **Too long for a one-shot** → *Unsorted*. Kicks, snares, toms, cymbals and open hats may ring
   for up to 10 s (808s, floor toms); everything else, 4 s.
7. **Confidence and tags.** Confidence is 0.95 from a role folder, 0.8 from a name's first word
   and 0.6 from a later one, scaled by 0.8 when only the length decided a hat. Tags are the
   name's other words (`808`, `acoustified`, `vinyl`, …), for the kit builder's families (§3.3).
   *Unsorted* samples can still be browsed and assigned by hand, but Randomise never picks them.

A dev tool, `Tools/classify.sh`, scans and classifies the whole library, prints the timings and
counts, and with `--csv` writes one row per sample (root, path, length, role, confidence, tom
range, tags, reason). That makes the misses easy to read and is the basis for the hand-labelled
test set.

### 3.3 Kit builder and Randomise

*As built in Phase 3* (`Source/core/kit/KitBuilder.h`).

- **Pad roles:** each pad has a role: Kick, Snare, Clap, Rim, Closed hat, Open hat, Low, Mid or
  High tom, Crash, Ride or Perc. Set it in the pad panel. The role decides what Randomise and the
  pad's dice pick, and what the browser shows. Toms prefer their range, then an unknown one.
- **Families:** a family is a tag in the samples' names that runs across many roles (808, 909,
  707, 505, DMX, *Acoustified*, *Kindified*, *Electrified*, *Session*, *Canvas*, …). Generic words
  (dry, hit, room, stick …) aren't families. A pack is used as the family only when no tag
  qualifies, because a pack mixes kits.
- **Character**, from *Matched* to *Wild* (a parameter, default 25%):
  - Randomise first chooses a family that covers as many of the unlocked pads' roles as it can.
    The choice is random, weighted by that coverage (cubed), and four times as likely per locked
    pad already in the family.
  - Each unlocked pad then takes a sample from the family with probability 1 − Character. If the
    family has nothing for the role, it takes any sample of the role.
  - On this Mac, Matched gives kits such as an all-707 kit, an *Electrified* kit or a *Canvas*
    kit (`Tools/classify.sh --kits N` prints some).
- **Randomise** re-rolls every unlocked pad. Each pad's **dice** re-rolls only it, always to a
  different sample. No sample is used twice in a kit. A role with nothing in the library keeps
  its sample. Every kit change can be undone: Randomise, a dice, a drop, Load…, Clear, or loading
  a kit preset. **Undo/Redo** keep the last 20 kits for the session; they aren't saved with the set.
- **Kit presets** (*Kits* menu): the samples, roles and sound settings (level, pan, tune, decay,
  velocity, choke) as JSON in `~/Library/Application Support/DrumChimp/Kits/`. A preset can be
  loaded in any project.
- **Style-aware kits** (Trap and Hip-Hop toward 808, Techno and House toward 909, Rock and Jazz
  toward acoustic) wait for the styles, in Phase 4.

### 3.4 Pattern generator

*As built in Phase 4* (`Source/core/pattern/`).

**Styles are text**, not code. They live in `FactoryStyles.h`, in the format that `Style.h`
documents: a name, tempo, grid, default swing, the kit families the style likes, the fill
(none, toms or snare), crash on the one or only after fills, and lanes like
`kick = X... X... X... X...`. Lanes can span one or more bars. The step symbols are:

- `X` anchor: always plays, accented
- `x` core: thins out only at low Density
- `o` variation: more likely with Complexity
- `g` ghost note: only with Ghost notes on
- `r` core, and may roll 2 or 3 times with Complexity
- `.` rest. An `extras` lane may still gain hits on rests at high Complexity.

**The generator** is a pure function of the style, settings, pad roles and seed, run on the
message thread:

- **Probabilities:** each step's chance of playing grows with Density and Complexity.
- **Fixed draws:** every step draws its random numbers in a fixed order, so the same seed gives
  the same pattern. More Density only ever adds hits, and so does more Complexity (with fills
  off).
- **Pads:** a lane plays on every pad with its role. With no such pad, ride falls back to the
  closed hats, rim to the snare, and any tom to any tom.
- **Humanise:** up to ±14 velocity and ±12 ms timing, rounded to 0.1 ms so it saves exactly.
- **Fills:** in every 2nd, 4th or 8th bar (Fill), the last beat becomes a fill (the last two at
  Complexity > 0.6). Toms run high → low, or the snare builds and may roll. A crash follows on
  the next bar's one.
- **Locks:** a locked lane keeps its hits.

**Controls** (parameters): Style, Bars (1 / 2 / 4 / 8), Density, Complexity, Swing (50–75%,
applied as it plays; a new style brings its own), Humanise, Ghost notes, Fill (Off / every 2, 4
or 8 bars).
- Changing any of them, or a pad's role, makes the pattern again with the same seed.
- **Generate** picks a new seed.
- Clicking a step in the grid toggles it. Hand edits last until the next change of setting.
  Lane locks keep a lane.

**Styles (20):** Rock, Pop, Funk, Hip-Hop, Trap, House, Techno, Drum & Bass, Breakbeat,
Reggae, and (added after Phase 6, on request) Disco, Shuffle, Jazz, Bossa Nova, Afrobeat, 2-Step,
Dembow, Dubstep, Motown, Punk.
- **Triplet grid:** Shuffle and Jazz use the 12-step triplet grid.
- **Two-bar lane:** Bossa Nova's rim plays a two-bar clave.
- **Each style as a preset:** at Complexity 0 and Density 50% or more, a style plays its groove
  exactly as written (plus the opening crash for `crash = one` styles). Raising Complexity adds
  the variations.

**Playback:** the pattern reaches the audio thread whole, through `Handoff`. It loops from the
song's bar 1. Swing delays the second sixteenth of each eighth. Every event goes on the first
sample at or after its time. A new pattern, a stop or a jump ends what's sounding. Style-aware
kits (§3.3): the style's families are three times as likely when Randomise chooses one.

### 3.5 Playback: where the notes come from

A **Source** switch decides what plays the pads:

| Source | What happens | When to use it |
|---|---|---|
| **Generator** | The internal sequencer plays the pattern, locked to Live's transport (`AudioPlayHead` PPQ). It plays the pads **and** sends the notes to the plug-in's MIDI output. Notes coming in also play. | Auditioning styles and kits; recording live onto another track. |
| **MIDI In** | The sequencer is silent. Only incoming notes (clips on the track, a keyboard) play the pads. | After the pattern has been dragged out as a clip, so nothing plays twice. |

The step grid (8 lanes × steps) shows the pattern, and clicking a step turns it on or off. A
"Changes" indicator shows when the grid differs from what was last dragged out.

### 3.6 Getting patterns into Live

| Route | How | Phase |
|---|---|---|
| **A. Drag to clip (same track)**, recommended | Drag the handle in the header. DrumChimp writes an N-bar `.mid` to a temp folder and starts a drag (`performExternalDragDropOfFiles`). Drop it on a clip slot on DrumChimp's own track (or in the Arrangement). Switch Source to *MIDI In*. The clip then plays DrumChimp. | 5 |
| **B. Record on another track** | A second MIDI track with *MIDI From: <DrumChimp's track> → DrumChimp*, armed. Live records exactly what the generator plays, including changes made while it plays (re-rolls, fills). Its notes can be dragged back onto DrumChimp's track afterwards. | 5 |
| **C. Send to Clip (same track, one click)** | A **DrumChimpBridge** Remote Script (based on LiveBridge) listens on localhost. DrumChimp sends the pattern and its instance ID (the *Instance ID* parameter, which the LOM can read; it has to be automatable, because Live leaves non-automatable VST3 parameters out of the LOM: Phase 0). The script finds that device's track, creates a clip in the selected or first empty slot, and calls `add_new_notes`. Needs the Remote Script to be installed once. | 7 (optional) |

Route A covers "record into the same track" for v1, with no set-up. Route C makes it one click.

---

## 4. Architecture

| Area | Choice | Why |
|---|---|---|
| Framework | **JUCE 8.0.14**, C++17, **Projucer + xcodebuild** | Same as ChordChimp and NoteCap. |
| Formats | **VST3 instrument** with an event output bus, plus **Standalone** (dev) | MIDI From needs VST (§2). Standalone is quicker for debugging the sampler and UI. |
| Audio files | JUCE `AudioFormatManager` (WAV, AIFF, FLAC) | Covers everything in Ableton's library. |
| Tests | Plain clang test scripts for the core (`Tests/run_tests.sh`), a JUCE console app for the processor (`Tests/run_host_tests.sh`, with ChordChimp's malloc hook), `pluginval` strictness 10, and the ClaudeLiveBridge for tests in Live | Same toolchain as the sibling projects. |

```
                    message thread                                  audio thread
 ┌───────────────────────────────────────────────┐        ┌───────────────────────────────┐
 │ LibraryScanner ─▶ Classifier ─▶ LibraryIndex  │        │ Sequencer (host PPQ, swing)   │
 │      (background thread, json cache)          │        │     │ notes      ▲ MIDI in    │
 │ KitBuilder ◀── LibraryIndex, Character, locks │        │     ▼            │            │
 │ SampleLoader (bg thread) ─▶ SampleBank ───────┼─swap──▶│ PadVoices ×8 (choke, tune)    │──▶ stereo out
 │ PatternGenerator ◀── StyleLibrary (json)      │        │     │                         │
 │       └──────────── Pattern ──────────────────┼─swap──▶│     └──▶ MIDI out (MIDI From) │
 │ MidiExporter ─▶ temp .mid ─▶ drag-out         │        └───────────────────────────────┘
 │ Editor (pads, browser, step grid, settings)   │
 └───────────────────────────────────────────────┘
```

### Modules

| Module | Job |
|---|---|
| `LibraryRoots` | Find Ableton's library folders and any user folders; keep them in Settings. |
| `LibraryScanner` (`scanLibrary`, `Library`) | Walk the roots on a background thread; read file headers (length, rate); keep the cache up to date. |
| `Classifier` | Path → (role, confidence, family tags). Pure and table-driven, so it is easy to unit-test. |
| `LibraryIndex` | Look up samples by role and family; search text for the browser. |
| `KitBuilder` | Choose samples for the pads (coherence, locks, seed); keep the kit history. |
| `SampleLoader` / `KitBank` | Decode off the audio thread, at the file's own rate; hand a finished bank to the audio thread through an atomic pointer swap. Old banks are freed on the message thread. (Phase 1: the voices resample, which Tune needs anyway, so a host rate change needs no reload.) |
| `DrumEngine` (voices) | One-shot playback with pitch (Hermite-interpolated), decay envelope, pan and choke; 32 voices. Retriggering uses a 2 ms fade to avoid clicks. |
| `StyleLibrary` / `PatternGenerator` | Load the style JSON; `(style, params, seed) → Pattern`. |
| `Sequencer` | Turns a Pattern plus host PPQ into sample-accurate note events; handles loop wrap, transport jumps and swing. |
| `MidiExporter` | Pattern → Standard MIDI File (with the tempo and the note map). |
| `PluginState` | ValueTree: parameters, pads (sample path, root ID, file size, settings), pattern, seed, kit history. Relocates missing samples by file name and size through the index. |
| `Editor` | Header, pads, browser, step grid, pad editor, settings. |

### Real-time rules

The same rules as Chordism and NoteCap: no allocation, locks, file I/O or logging on the audio
thread. Data crosses threads only through atomics and pre-allocated swaps. A malloc-hook test
runs the processor through transport jumps, tempo changes, kit swaps and pattern swaps.

### Identity

Name **DrumChimp**, manufacturer code as the sibling projects, its own plug-in code and bundle
ID. VST3 category *Instrument|Drum*. The user presets folder is
`~/Library/Application Support/DrumChimp/Kits/`.

---

## 5. UI (one window, about 960 × 640)

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ DrumChimp   Style [House ▾]  Bars [4 ▾]  [Generate]  [🎲 Randomise Kit]  Character ◐ │
│             Source (Generator|MIDI In)   Swing ◐  Density ◐  Complexity ◐  [⇲ Drag]│
├───────────────────────────────┬────────────────────────────────────────────────────┤
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐   │  Browser   [Role: Snare ▾] [search…]        │
│  │ Kick │ │Snare │ │ Clap │ │ CHH  │   │  ▸ Snare 808 Powder Short    Drum Ess.  ▶  │
│  │808 T1│ │808 Pw│ │Dub Lg│ │909 Cl│   │  ▸ Snare Acoustified 05      Drum Ess.  ▶  │
│  │🔒 🎲 │ │🔒 🎲 │ │🔒 🎲 │ │🔒 🎲 │   │  ▸ Snare 909 Tune10 k        Core       ▶  │
│  └──────┘ └──────┘ └──────┘ └──────┘   │   (double-click or drag onto a pad)          │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐   ├────────────────────────────────────────────┤
│  │ OHH  │ │LoTom │ │HiTom │ │Crash │   │  Pad 2 · Snare   Level ◐ Pan ◐ Tune ◐       │
│  └──────┘ └──────┘ └──────┘ └──────┘   │  Decay ◐  Vel ◐  Choke [–▾]  Role [Snare ▾] │
├───────────────────────────────┴────────────────────────────────────────────────────┤
│ Kick  ■□□□■□□□■□□□■□□□ │ … (bars)                                           🔒      │
│ Snare □□□□■□□□□□□□■□□□ │                                                    🔒      │
│ …  8 lanes × steps, with velocity shown as shading, and the playhead               │
└────────────────────────────────────────────────────────────────────────────────────┘
```

- Pads accept files dropped from Live's browser and Finder. When a sample is dropped, the
  classifier suggests a role, but the pad keeps its current role unless it is empty.
- The browser has its own preview (through the plug-in's output), filtered by the selected
  pad's role by default.
- The settings page has library roots, Rescan, note map and classifier threshold.

---

## 6. Milestones

| Phase | Content | Done when |
|---|---|---|
| **0. Feasibility** (1–2 days) | Spikes in Live 12: (a) VST3 *instrument* with MIDI out → *MIDI From* on another track records it; (b) drag-out `.mid` onto a clip slot on the same track; (c) drag from Live's browser into the plug-in window delivers a file path; (d) scan time for the whole library; (e) *optional*: a Remote Script writes a clip on a device's own track. | Each spike passes or has a written fallback. |
| **1. Skeleton and sampler** | Projucer project, VST3 + Standalone. 8 `PadVoice`s, `SampleBank` swap, per-pad settings, choke groups, GM note map, state save. Pads loaded from hard-coded paths. | `pluginval` 10 passes; MIDI plays pads sample-accurately (offline render test); malloc-hook test. |
| **2. Library and classifier** | `LibraryRoots`, `LibraryScanner`, `Classifier`, cache, `Tools/classify` CSV report. | G2 and G3 pass on this Mac's library. |
| **3. Kits** | `KitBuilder`, Character, locks, per-pad dice, kit history, manual assignment (browser and drag-drop), kit presets, relocating missing samples. | G4, G5, G6 and G9 for kits. |
| **4. Patterns** | `StyleLibrary` + 10 styles, `PatternGenerator`, `Sequencer` synced to the host, Source switch, MIDI out, step grid (view and toggle). | G7; the sequencer stays on the grid through loop, tempo change and transport jumps (integration test with a fake transport). |
| **5. Into Live** | `MidiExporter` + drag-out (route A); *MIDI From* recording (route B); a DrumChimpBridge (LiveBridge copy) with tests in Live. | G8 checked in Live: the recorded notes match the pattern at 0 ms offset. |
| **6. UI and hardening** | Full editor (§5), audition, style-aware kits, Humanise, fills. Tune performance. Missing-file reporting. Signing and notarisation, if certificates are available. | G9 and G10; the UI snapshot test; a fresh-Mac install check. |
| **7. Extras** (optional) | Send to Clip (route C); curated kits from Ableton Drum Rack `.adg` presets (these are gzipped XML, which give the pad samples); an audio-feature fallback for the classifier (length, spectral centroid); more styles; multi-out; 3/4 and 6/8. | Chosen when Phase 6 is done. |

**The risky parts come first:** Phase 0 settles the Live integration before anything depends on
it. The classifier (Phase 2) is checked against your real library before the kit builder and
Randomise are built on top of it.

---

## 7. Testing

| Level | What it checks |
|---|---|
| **Classifier** | A fixture of about 300 hand-labelled paths from this Mac's library: **paths only, no audio**. Ableton's samples are licensed to you, not for redistribution, so none go in the repo. Target ≥ 95%. There are also unit tests for tokenising and for conflicts (rimshot, hat without open/closed). |
| **Kit builder** | With a fixed seed and a fake index: roles are filled, locks are kept, Matched kits share a family, history undo and redo work. |
| **Pattern generator** | With fixed seeds: each style's invariants hold over 1,000 seeds; Density changes note count in one direction only; locked lanes don't change; fills land only where asked. |
| **Sequencer / sampler** | Offline render against a fake transport: notes land on the exact sample, swing offsets are right, choke works, nothing gets stuck on a transport jump, and output doesn't depend on block size. |
| **RT safety** | Malloc-hook test (from Chordism). |
| **Host** | `pluginval` strictness 10, in CI. |
| **Live** (`Tests/live/run_live_tests.py`, through ClaudeLiveBridge; as built in Phase 5) | Routing: the generator through *MIDI From* is recorded with every note on its sixteenth to within a sample, the kick on all 16 beats, and loop 2 the same as loop 1. Swing: Funk at 66% puts the off-beat sixteenths exactly a third of a step late. State: a duplicated DrumChimp plays exactly the original's pattern. Dragging stays a manual check. Saving and reopening uses the same state calls as duplicating, but isn't automated, because the LOM has no save. A clip playing DrumChimp in *MIDI In* mode isn't automated either: the bridge can't write a clip, and the processor tests cover incoming notes. |

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Odd file names classify badly | Folder tokens do most of the work for Ableton content. *Unsorted* is never randomised. The CSV report shows the misses. The audio-feature fallback is in Phase 7. |
| Library paths differ (edition, custom Packs folder, Live updates) | Auto-detect plus editable roots. Store samples by root ID + relative path, with relocation by name and size. |
| A pattern dragged out and the generator both play (double hits) | A clear Source switch. The header warns when Source is *Generator* and notes are arriving at the same time. |
| Loading samples causes clicks or glitches | Decode in the background, swap atomically, fade on retrigger. Banks are small (8 one-shots). |
| The Remote Script needs installing, and could break with Live updates | Route C is optional and extra; routes A and B need nothing installed. |

---

## 9. Open questions

1. **Default 8 roles:** is Kick, Snare, Clap, Closed hat, Open hat, Low tom, High tom, Crash
   right, or would you rather have Ride or a Perc slot instead of one tom?
2. **Styles:** are the 10 v1 styles in §3.4 the right first set?
3. **Should the generator play** inside the plug-in at all (Source = *Generator*), or should
   DrumChimp only make clips that you then play?
4. **Send to Clip (route C):** worth the Remote Script, or is drag-out enough?
5. **Time signatures:** is 4/4 only all right for v1?
6. **Drum Rack kits:** should Ableton's ~300 curated Drum Rack presets be a Randomise source in
   v1 ("Curated" mode), rather than Phase 7?

---

## 10. Status

**2026-10-28:** Plan written.

### Phase 0 (2026-10-28, Live 12.4.7): done, all 5 spikes pass

The skeleton builds with Projucer + xcodebuild like ChordChimp (`Tools/install.sh`): a VST3
instrument with MIDI out, plus a Standalone build. It plays a fixed one-bar test pattern
(`core/engine/TestPattern.h`) in time with the host, on synthesised placeholder sounds
(`core/engine/BlipKit.h`), and sends the pattern to its MIDI output. `pluginval` strictness 10
passes, and the 32 core tests pass (`Tests/run_tests.sh`).

| Spike | Result |
|---|---|
| (a) VST3 instrument MIDI out → *MIDI From* | **Pass.** Live offers *1-DrumChimp → DrumChimp* as a MIDI input. Recorded at 120 BPM: every note, velocity (115, 92, 68 …) and length (1/32) of the pattern, over 7 bars. The first run found notes landing one sample (22.7 µs) early, because of truncation in DrumChimp. Fixed: each event now goes on the first sample at or after its beat, with a test across block sizes (`forEachEventInBlock`). **Re-checked after a Live restart:** 4½ bars, every note exactly on its beat. The only offset is the high tom on each x.75, at +0.0000226 beats: that beat falls between two samples (120 BPM at 44.1 kHz), so the note goes on the next one, as designed. So the fallback (a companion MIDI-effect plug-in) is not needed. |
| (b) Drag-out `.mid` onto the plug-in's own track | **Pass** (checked by hand). Dragging *Drag 4-bar MIDI* onto DrumChimp's own track makes a clip with the test pattern. The `.mid` is written to JUCE's per-app temp folder (`$TMPDIR/DrumChimp/DrumChimp Export/`); Live ends the drag about 0.9 s after it starts. Route A (§3.6) works. |
| (c) Drop from Live's browser onto a pad | **Pass** (checked by hand). Live delivers a browser drag as an ordinary **file drop** (`filesDropped`, not text), with the full path, even for samples inside the app bundle: `…/Core Library/Samples/Multisamples/Synths/ARP Poly Lead/ARP Poly Lead - F#4.aif`. The pad label changed but the placeholder sound still played: Phase 0 has no sampler, so loading the dropped file is Phase 1. |
| (d) Library scan time | **Pass: 1.2 s** for the whole library (`Tools/scan_library.sh`). 9 roots from Library.cfg; 5,637 audio files (9,012 `.asd` skipped); 531 loops, 472 files too long and 0 unreadable were left out. That leaves 4,634 one-shots, 3,939 of them in drum folders. Headers take 0.1–0.2 ms each. Phase 2 notes: the `808`/`909` folders (from `Multisamples/Drum Machines`) and `Chops` hold mixed roles, so file-name rules matter there. The CSV list (`--csv`) is the starting point for the hand-labelled fixture. |
| (e) A Remote Script can find a DrumChimp's track | **Pass.** Live leaves non-automatable VST3 parameters out of the LOM: the first, non-automatable *Instance ID* didn't appear in `list_parameters`. The automatable one does, and its display text gives the ID (`12603`). **Duplicating the track copies the ID** (checked: both report 12603), so in Phase 7 the Remote Script resolves clashes itself: the parameter is automatable, so the script can write a new ID into the copy. Writing the clip itself (`ClipSlot.create_clip`, `Clip.add_new_notes`) is standard LOM, left to Phase 7. |

### Phase 1 (2026-10-28, Live 12.4.7): done

The sampler and pads. Dropping a sample on a pad now loads and plays it.

- **Engine** (`core/sampler/DrumEngine.h`): 32 voices with Hermite interpolation at the sample's
  own rate, Level, Pan (equal power), Tune ±24 st, Decay (−60 dB at the time; *Full* plays the
  whole sample), velocity sensitivity, four choke groups (the hats share A), and Mute/Solo. A
  retrigger, a choke or a sample swap fades the old voice over 2 ms. Kit banks reach the audio
  thread through an atomic handoff. An old bank retires only when no voice still plays a sample
  that only it has. Every sample is freed on the message thread.
- **Plug-in:** 67 parameters (8 per pad, plus Source, Note map and Instance ID). Note map is
  *GM* or *Chromatic*. Samples load on a background thread (`SampleLoader`: WAV, AIFF, FLAC, up to
  30 s). A new instance gets the Core Library 909 kit (`DefaultKit.h`), unless the host restores a
  state or a pad is set first. The state holds each pad's path and file size. A missing file
  shows as missing, keeps its size for Phase 3 to find it again, and its pad stays silent. The
  Instance ID is now set so that it is saved (the first version wasn't: the tests caught it).
- **Editor:** eight pads that light when they play (click: select and play; drop: load), and a
  panel for the selected pad: Load…, Clear, five knobs, Choke, Mute, Solo.
- **Tests:** `Tests/run_tests.sh`, 56 core checks. Hits land on their exact sample at any block
  size; Tune and the host rate, level, pan, velocity, decay, choke, retrigger and mute work; a
  bank swap allocates nothing on the audio thread. `Tests/run_host_tests.sh`, 29 processor checks.
  Loading (and a non-audio file reported), exact timing through `processBlock`, the note maps,
  the generator, state round trip and a missing sample, the default kit, and the editor. **No
  allocation in 400 blocks** of notes, generator, a sample swap, a loop jump and a note-map
  change. It also writes `Tests/build/editor.png`. `pluginval` strictness 10 passes.
- **In Live:** the default 909 kit plays (track meter 0.72). The generator's MIDI out, recorded
  through *MIDI From*, is still exact over 4 bars. A duplicated track plays the same kit restored
  from its state (original muted, master at one track's level).

### Phase 2 (2026-10-28): done, G2 and G3 pass

The library scanner and the classifier (§3.2 describes them as built).

- *(Phase 4 found that the 1,725 Factory Pack samples in these counts are protected: only Live
  can play them. They're now left out: 2,439 playable drums.)*
- **G2, finding the library:** the roots come from `Library.cfg` and the app (9 on this Mac). A
  cold scan with every header read takes 0.5–0.9 s; a warm one from the cache takes 0.3 s. The
  target was under 10 s. One scan is shared by every instance (`SharedResourcePointer`); the
  editor's status line shows *Library: 3829 drums in 5062 samples, 9 places*, and the pad panel
  shows where a pad's sample is in the library and how it was sorted.
- **G3, sorting by name: 300 / 300** on the hand-labelled fixture (`Tests/fixtures/
  classifier_labels.csv`). How it was made: a stratified sample of this Mac's library (8
  ambiguous hats, 52 from awkward folders (Chops, Bell, FX Hit, E-Perc, Drum Machines, Cymbal),
  35 non-drums, 205 at random). I labelled them from their names before writing the rules, with
  every acceptable role where a sound is honestly ambiguous (*Snare Clap Combo*: snare or clap).
  The score is high because Ableton's folders do most of the work. The name rules matter for
  third-party packs and user folders; 25 synthetic names cover those (`BD_909_01`, `OH 1`,
  `Open Hats/Hat 01`, `Bass Drums/…`, a `Bass` folder of notes, loops), with no real non-Ableton
  library to check against yet.
- **The whole library** (`Tools/classify.sh`): 621 kicks, 634 snares, 210 claps, 194 rims, 389
  closed and 220 open hats, 497 toms, 165 crashes, 109 rides, 790 perc; 1,233 Unsorted (939 in
  non-drum folders, 161 effects, 95 without a drum word, 32 too long, 6 break slices). 21 hats
  were decided by length; 166 of the 497 toms have a range in their name.
- **A fix found on the whole library:** the first length limit (4 s, 10 s for cymbals) dropped
  real acoustic floor toms (4–7 s) and 808 kicks (up to 10 s). Now kicks, snares, toms, cymbals and
  open hats may ring for 10 s.
- **Tests:** `test_classifier` (tokens, 25 synthetic names plus tom ranges, tags and confidence, the 300-path fixture with every miss
  listed) and `test_library` (a scan of a made-up library: what's found and skipped, the cache
  (a second scan reads no headers, a changed file is read again, a deleted one drops out), and
  cancelling) join the core tests. The host tests add the shared library: the scan finishes, the
  cache is written, the default kit's eight samples are found in their pads' roles, and a rescan
  works. `pluginval` strictness 10 passes.
- **Not yet checked in Live:** Live still has the Phase 1 build loaded. The scan runs the same way
  in any host, and the host tests run it in-process.

### Phase 3 (2026-10-28): done, G4, G5, G6 and G9 (for kits) pass in the tests

Kits: §3.3 describes them as built.

- **G4, a real-kit-style kit in one click:** **Randomise kit** fills each pad with a sample of
  its role. At Matched, all from one family, 200/200 times in the tests. On this Mac, Randomise
  from the default 909 kit changed all eight pads, each to its role.
- **G5, Randomise with locks, and undo:** locked pads never change. A pad's dice changes only that
  pad. Undo and Redo walk the last 20 kits.
- **G6, assigning samples by hand:** DrumChimp's own **browser**. It is filtered to the selected
  pad's role (or any role, or everything) and searchable. Clicking a sample plays it through a
  ninth *preview* voice, which no pad uses; double-clicking or dragging it onto a pad loads it.
  Drops from Live's browser and Finder still work.
- **G9 for kits:** roles and locks are saved with the set. A missing sample is looked for in the
  library: the same file name, then the same saved size, then the most matching folders. It's
  never a guess between two equally good matches. If found, it loads from there and the pad
  panel says *Found again: it was at …*. The tests move the default kick to a drive that doesn't
  exist and it comes back.
- **Kit presets:** saved and loaded from the *Kits* menu (samples, roles and sounds). A JSON
  that isn't a kit is refused.
- **Editor** (960 × 640): a kit bar (Randomise, Undo, Redo, Character, the family chosen, Kits),
  a lock and a dice on every pad, a role menu in the pad panel, and the browser.
- **Tests:**
  - `test_kit` (a made-up library): roles, Matched vs Wild over 200 seeds, no repeats, locks and
    their pull on the family, dice, tom ranges, the same seed giving the same kit, a role with no
    samples, history and relocation.
  - `test_sampler` adds the preview slot.
  - The host tests add everything above on the real library.
  - `pluginval` strictness 10 passes.
- **Found while building:** the first version let a pack be a family, which mixed kits. 56 of
  200 Matched kits weren't one family until tags came first.
- **Not yet checked in Live:** Live still has the Phase 1 build loaded.

### Phase 4 (2026-10-28, Live 12.4.7): done, G7 passes

Patterns: §3.4 describes them as built.

- **G7:**
  - **Ten styles:** Rock, Pop, Funk, Hip-Hop, Trap, House, Techno, Drum & Bass, Breakbeat,
    Reggae.
  - **Anchors:** every style's anchors play in every bar over 200 seeds × 9 Density/Complexity
    settings. Named checks: House's kick on every beat, Rock's snare on 2 and 4, Reggae's one
    drop (kick on 3, never on 1).
  - **Seeds:** the same seed gives the same pattern.
  - **Settings:** more Density only adds hits (10 styles × 100 seeds × 11 steps), and so does
    more Complexity.
  - **Fills:** only where asked, with a crash after.
- **The sequencer, on the grid:**
  - **Fake transport:** with House at Humanise 50%, every note-on landed on its exact sample
    through a steady run, a loop back to bar 1 and a tempo change (120 → 97 BPM).
  - **Block sizes 1–4096:** with rolls, swing and humanise, every hit plays once, never early.
  - **In Live:** House recorded through *MIDI From* at 124 BPM had the kick on every beat, the
    clap on 2 and 4, open hats on the off-beats and swung sixteenths at 0.27 beats (54%), each
    within a sample.
- **The drag-out** is now the pattern itself (Bars long, with swing and humanise), not the
  Phase 0 test pattern, which is gone.
- **The pattern goes with the set** as it is: hand edits and lane locks included, and not remade
  from its settings on load.
- **Editor** (960 × 760): pattern controls under the pads (Style, Bars, Fill, Generate, Density,
  Complexity, Swing, Humanise, Ghost notes), and a step grid across the bottom. The grid shows
  velocity as brightness, rolls as dots and the playhead; click a step to toggle it, and each
  lane has a lock.
- **Found on the way:**
  - **Pack samples are protected (§2, point 6).** A host test failed now and then: a moved
    sample was found in the library but wouldn't load. The cause was the Pack samples, which
    only Live can decode. In Phases 2 and 3 they were in the index, so Randomise and the browser
    could offer them. Fixed:
    - the header reader spots them;
    - the scan leaves them out (1,725 here) and counts them in the status line's tooltip, with
      the cache format bumped to 2;
    - a dropped one says why it can't load;
    - a new host test checks that every sample from 10 Randomise kits actually loads.

    The library is now 2,439 playable drums, all Core Library. Matched kits still come out as
    one family (an RKTD kit, a 606 kit).
  - **Relocation and copies:** two identical copies of a sample in two places no longer count
    as a guess when finding a missing sample.
  - **Exact saving:** humanise offsets are rounded to 0.1 ms, so a saved pattern restores
    exactly.
- **Tests:** `test_pattern` (style parsing, invariants, determinism, monotonic Density and
  Complexity, fills, locks, fallbacks, playback timing, swing, the loop point, MIDI, Handoff).
  The host generator test was rewritten as above. `pluginval` strictness 10 passes.

### Phase 5 (2026-10-28, Live 12.4.7): done, G8 passes

Getting patterns into Live (§3.6).

- **Route B, recording on another track:** checked automatically in Live
  (`Tests/live/run_live_tests.py`, 8 checks, all passing):
  - DrumChimp (House, 2 bars, Humanise 0, no fills) recorded through *MIDI From* for two loops
    at 120 BPM: 74 notes, every one on its sixteenth to within a sample, the kick on all 16
    beats, and the second loop identical to the first.
  - Funk at Swing 66%: 19 off-beat sixteenths exactly a third of a step late, 22 on the grid.
  - A duplicated DrumChimp track plays the original's pattern note for note (37 and 37). The
    pattern travels in the device's state; a new instance would have had a new seed.
- **No separate bridge:** the tests use ClaudeLiveBridge (the ableton-mcp project, port 9873)
  rather than a DrumChimpBridge copy, so nothing more needs installing in Live. They run in
  whatever set is open and leave it as it was: their own tracks are added and then deleted, armed
  tracks are disarmed and re-armed, and the tempo is put back.
- **Route A, the drag-out onto DrumChimp's own track:** the export is the pattern (Phase 4, with
  a host test that reads the file back). The drag itself worked by hand in Phase 0. New: if notes
  arrive while the generator plays, which means a dragged-out clip with Source still on
  *Generator* doubling every hit, the status line says so for 4 s. It suggests setting Source to
  *MIDI In*.
- **Route C** (Send to Clip) stays optional, for Phase 7.
- **Not automated, and why:** saving and reopening the set (the LOM has no save; duplicating
  covers the same state calls) and a clip on DrumChimp's track in *MIDI In* mode (the bridge
  can't write clips; the processor tests cover incoming notes).
- **What Live last ran:** the Phase 4 build. The Phase 5 build adds only the warning, which the
  host tests check. `pluginval` strictness 10 passes.

### Phase 6 (2026-10-28): done, G9 and G10 pass

UI and hardening.

- **Editor:**
  - **Resizable:** drag the corner, from 70% to 150%. The whole 960 × 760 layout scales, and the
    size is saved with the set.
  - **Settings:** shows the library's places with their sample and drum counts, marks the Packs
    as protected, adds and removes your own folders (kept in
    `~/Library/Application Support/DrumChimp/settings.json`, shared by every instance), and
    rescans.
  - **Step grid:** drag a hit up or down to set its velocity.
  - **Status line:** names the pads whose samples are missing or can't be played.
- **G9, everything survives:**
  - **Damaged states** restore to something sane that plays with no NaN: NaN, infinite and
    out-of-range values; a pattern with 9,999 bars and junk cells; unknown roles, pad numbers
    and elements; garbage bytes.
  - **Kit presets** skip non-finite values.
  - Saving and duplicating were already covered (Phases 3–5).
- **G10, real-time safe:** no allocation in 400 blocks that now also include Randomise, a
  preview, a hand edit and two new patterns while playing. Also checked:
  - a zero-length block, and a block 8 times the prepared size (with a hit on its exact sample);
  - a sample-rate change and a reset while notes sound;
  - 32 instances made and destroyed.
- **CPU:** Trap at full Density and Complexity (rolls on the hats), 48 kHz / 512, takes
  **0.095%** of one core. The target was under 2%.
- **UI snapshot test:**
  - The editor in a known state (the default kit, Rock from a fixed seed) is compared with
    `Tests/fixtures/snapshot/editor.png`, which is tied to this Mac's fonts.
  - The library status, which is this Mac's own, is masked.
  - `Tests/run_host_tests.sh --update-snapshot` writes a new reference after a deliberate change.
- **No library** (`DRUMCHIMP_LIBRARY=none`, run by `Tests/run_host_tests.sh` as a second pass):
  the library is empty, there's no default kit, Randomise does nothing and leaves nothing to
  undo, the generator runs silent, samples still load by hand, and the editor opens.
- **Clean-copy build:** the sources without any build output, in a new folder, pass the core
  tests, build, install and pass `pluginval` strictness 10. A second account or Mac wasn't
  available, so this stands in for a fresh clone.
- **Found on the way:** a pad's dice could keep its sample. When the first candidate group (the
  family, then the tom range) held only the pad's current sample, it was chosen again. The new
  host tests caught it; the made-up library in `test_kit` always had three per family. Now the
  first group with anything else wins, with a new test.
- **Not done:**
  - **Notarisation** needs a *Developer ID Application* certificate. This Mac has only *Apple
    Development*, so builds stay ad-hoc signed, which is fine for this Mac.
  - The editor's new pieces haven't been seen in Live: Live still runs the Phase 4 build.

### More styles (2026-10-28, after Phase 6)

The Style pull-down has 20 styles. The ten new ones are added at the end, so sets saved with the
first ten keep theirs. Live automation of Style stores a normalised value, though, so a Style
automation lane recorded before this change would now point at a different style.

- **Tests** (`test_pattern`), all passing:
  - every one of the 20 keeps its anchors in every bar;
  - every one plays its written groove exactly at Complexity 0;
  - named checks: Disco's off-beat open hats, Dembow's snare on 3, 6, 11 and 14, Jazz's 12-step
    grid with the ride on every beat, Bossa Nova's two-bar clave;
  - a 12-step grid plays triplet eighths, and Swing leaves it alone.
- **Editor:** a picture of Jazz in the step grid (`Tests/build/editor-jazz.png`) shows 12 steps a
  bar with beat lines every 3.
- **Checks:** `pluginval` strictness 10 passes. The Live tests' style list now has all 20.

**Next:** Phase 7, the optional extras: Send to Clip (route C), curated kits from Ableton's Drum
Rack presets, an audio-feature fallback for the classifier, more styles, multi-out, and 3/4 and
6/8.
