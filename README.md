# BuildApp

A buildup effect for psytrance. Put it on the loop or the group you want to
build, ride the knobs over the last bars, and pull them back for the drop.
Nine knobs, one chain:

![BuildApp halfway into a buildup](docs/screenshot.png)

```
in ─┬─ pitch ─ low cut ─ high cut ─ + noise ─ reverb ─ stereo ─┐ wet
    └──────────────────────────────────────────────────────────┤ dry
                                                 dry/wet ─ level ─ out
```

VST3 and Audio Unit for macOS (universal), VST3 for Windows. JUCE 8.

## The knobs

| Knob | What it does |
|------|--------------|
| **Low cut** | 24 dB/oct highpass, 20 Hz to 8 kHz. All the way left: off. |
| **High cut** | 24 dB/oct lowpass, 20 kHz down to 100 Hz. All the way right: off. |
| **Pitch** | Up to +12 semitones, continuous, so it can be ridden through a build. |
| **Noise volume** | White noise on top of the signal (after the cuts), wide stereo. Left: off. |
| **Noise pitch** | Where the noise band sits, 80 Hz to 15 kHz. Its resonant top edge gives it a pitch; the level stays put while it sweeps. |
| **Reverb** | One knob for the classic buildup wash: more send, a longer tail (0.8 s to 12 s), less dry, until the loop dissolves. Back to zero cuts the tail within ~30 ms: the dry drop. |
| **Stereo** | 100 % is the image as it is; turning it up narrows it towards mono. |
| **Level** | Output gain, −36 to +12 dB. |
| **Dry/Wet** | The untouched input against the whole effect. |

With every knob at rest (low cut left, high cut right, everything else at
zero, stereo 100 %, level 0 dB, dry/wet 100 %) the signal passes bit for bit:
each stage at rest is skipped, not run at unity.

Every knob drags up/down (Shift for fine), takes the mouse wheel, types a value
on double-click (`340`, `4.2k`, `-6`, `off`) and resets on Alt/Cmd-click. The
cut markers in the spectrum can be dragged sideways too; drag one past the edge
of the plot, or double-click it, to switch that filter off. Hover over anything
and the footer says what it does.

### The display

- **A — Spectrum**: the input (faint line) and the output (filled), with the
  cuts' response in green on its own scale (0 dB near the top) and what they
  take away hatched. The noise band shows dotted where it sits.
- **B–F**: the knobs. The ring around a knob says what kind it is: green from
  the rest position to the value on the knobs that build tension (dark when
  the knob is at rest; the high cut rests on the right, so it lights to the
  left), one tick for noise pitch (a place, not an amount), bone around 0 dB
  for level, grey for dry/wet.

## How it sounds the way it does

**Filters** (`src/core/fx/CutFilters.cpp`). Each cut is two trapezoidal
state-variable filters in series (Simper / Zavalishin zero-delay feedback):
the bilinear transform of the analog 2-pole with the cutoff prewarped, so the
cutoff is exact, and with a state that stays smooth when the coefficients
change every sample. The second stage is resonant (Q 0.60 + 2.1), which puts a
+3 dB bump on the corner: the sweep sings without whistling. The cutoff glides
per sample in log frequency (20 ms), so automation steps and fast moves sweep
evenly. The filters run oversampled with polyphase IIR halfbands (HIIR-style
elliptic allpass chains, 130 dB stopband): 4× at 44.1/48 kHz, 2× at 88.2/96 kHz.
The tests measure the response against the analog prototype: within 0.25 dB
wherever it is above −24 dB, up to 17 kHz (0.3 dB at 18 kHz), at every rate.
Switching a filter on or off is a smoothstep crossfade, and a filter wakes up
settled on the signal, so the tests find nothing above the noise floor when it
happens.

**Pitch** (`PitchShifter.cpp`). A delay line read faster than it is written.
When the read head catches up it splices back 10–30 ms, to the point where
the material best matches itself (normalised correlation, refined to a
fraction of a sample), so splices land in phase with the kick, bass or lead
underneath. The crossfade moves from equal gain to equal power with how well
it matched. On tonal material the splices measure better than −85 dB residual.
A 4-pole lowpass at 0.45 fs / ratio in front keeps the top octave from folding
back. Above 0 semitones the signal runs 10–40 ms late; no delay-line shifter
avoids that, so expect some flamming against the dry signal at dry/wet below 100 %.

**Noise** (`NoiseSource.cpp`). Independent white noise per channel through a
resonant 2-pole lowpass with a highpass two octaves under it. The gain that
keeps its RMS at −12 dBFS (full volume) wherever the band sits is computed
from the filter response.

**Reverb** (`Reverb.cpp`). The send is cut below 160 Hz and above 9 kHz, so
the wash never muddies kick and bass. Four diffusion steps (8 channels each:
delays, Hadamard mix, polarity flips) feed an 8-line feedback delay network
with a Householder matrix, slowly modulated lines (no metallic ringing) and a
high shelf in the loop so the highs die first. The overall level stays within
about 1.5 dB from 0 to 100 % while the wash rises to ~6 dB above what is sent
into it.

The whole chain with everything on uses about 1 % of one core at 48 kHz.

## Building

Requirements: macOS 11+, CMake 3.24+, Ninja, Xcode command line tools.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This installs the VST3 and AU into `~/Library/Audio/Plug-Ins`. JUCE 8 is
downloaded automatically; pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`
to use a local checkout.

### Core only

The DSP doesn't depend on JUCE and builds in seconds:

```sh
cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILDAPP_BUILD_PLUGIN=OFF
cmake --build build-core
./build-core/buildapp_tests
```

### UI snapshots

`build/buildapp_snapshot_artefacts/Release/buildapp_snapshot <dir>`
renders the editor to PNGs (rest, build, peak, cuts) with the built-in loop
playing through it, no DAW needed. `... <dir> bench` times the paints instead.

### Trying it without a DAW

```sh
./start.sh               # the built-in 145 BPM psytrance loop
./start.sh track.wav     # your file, looped
```

`start.sh` builds and opens the demo host: the plugin in a window with the
loop running through it and out of your speakers. **BYPASS** plays it
untouched for comparison; space plays and stops.

### Releases

Push a version tag and GitHub Actions builds, tests and validates the plugins
(pluginval at strictness 5, auval) and attaches the installers to a new
release:

```sh
git tag v1.2.0 && git push origin v1.2.0
```

You get a macOS `.pkg` (universal, VST3 + AU), a Windows setup `.exe` (VST3)
and a zip of the bare plug-ins for each. A suffix such as `v1.2.0-beta.1` makes
it a pre-release. The installer scripts are in `packaging/`.

The macOS build is signed with a Developer ID and notarized when these
repository secrets are set (Settings > Secrets and variables > Actions):

| Secret | Value |
|--------|-------|
| `MACOS_CERTIFICATES_P12` | base64 of a `.p12` holding both the *Developer ID Application* and the *Developer ID Installer* certificate with their keys |
| `MACOS_CERTIFICATES_PASSWORD` | the `.p12` password |
| `NOTARY_APPLE_ID` | Apple ID of the developer account |
| `NOTARY_PASSWORD` | an app-specific password for that Apple ID |

Without them the macOS build is unsigned and macOS blocks the `.pkg` the first
time (System Settings > Privacy & Security > Open Anyway). The Windows
installer is not signed, so SmartScreen asks once (More info > Run anyway).

## Project layout

```
src/core/dsp       trapezoidal SVF, polyphase halfbands, smoothers, FFT
src/core/fx        cut filters, pitch shifter, noise, reverb, the chain, knob mappings
src/core/analysis  lock-free audio taps, live spectrum
src/plugin         JUCE processor and editor, number formatting
src/ui             theme, knobs and meters, spectrum view
tests/             Catch2 tests for the core, test signals (incl. the psy loop)
tools/             offscreen UI snapshot tool, demo host
packaging/         macOS pkg and Windows installer scripts
```

## License

The plugin links JUCE 8 under the AGPLv3. IBM Plex fonts under the SIL Open
Font License (`resources/fonts/IBMPlex-OFL.txt`).
