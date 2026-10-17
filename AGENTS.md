# BuildApp

## Purpose
Psytrance buildup effect (VST3 / AU, JUCE 8). Nine knobs on one chain: pitch (0..+12 st), low cut (left = off), high cut (right = off), white noise volume and pitch, a one-knob buildup reverb, stereo narrowing (100 % default), level, dry/wet. Every knob at rest passes the signal bit for bit. The editor shows the live input/output spectrum with the cuts' exact response and the noise band.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Targets: `buildapp_core` (static lib, no JUCE), `BuildApp` plugin, `buildapp_snapshot`, `buildapp_demo`, `buildapp_tests`. Options `BUILDAPP_BUILD_PLUGIN`, `BUILDAPP_BUILD_TESTS`, `BUILDAPP_COPY_PLUGIN` (off in CI), cache var `BUILDAPP_VERSION` (release tag sets it). UI sources are listed once in `BUILDAPP_UI_SOURCES` |
| `src/core/fx/Buildup.cpp` | The chain; skips every stage at rest |
| `src/core/fx/Mapping.h` | What each knob position means (Hz, dB, width), shared by DSP, parameter texts and display |
| `src/core/fx/CutFilters.cpp` | 2 x trapezoidal SVF per cut (Q 0.60 + 2.1, +3 dB corner), oversampled 4x/2x/1x by rate, log-domain cutoff glide, smoothstep on/off fades, halfbands primed from a 512-sample history on wake |
| `src/core/dsp/Svf.h`, `Halfband.cpp` | Simper/Zavalishin SVF (double); HIIR-style polyphase IIR halfband (designer included) |
| `src/core/fx/PitchShifter.cpp` | Delay-line shifter with correlation-matched, sub-sample splices; anti-alias lowpass in front |
| `src/core/fx/NoiseSource.cpp` | Per-channel xorshift noise, resonant band, RMS held by an integrated compensation gain |
| `src/core/fx/Reverb.cpp` | Diffuser + 8-line FDN; `decaySeconds`, `dryGain`, `wetGain` are the one-knob mapping |
| `src/core/analysis/Spectrum.cpp`, `Tap.h` | Lock-free taps (audio thread writes) and the 1/12-octave display spectrum (message thread) |
| `src/plugin/PluginProcessor.cpp` | Parameters and their texts, mono/stereo layouts, taps and peaks |
| `src/plugin/PluginEditor.cpp` | 880x540 sheet scaled as a whole; header, A spectrum, B..F knob groups, footer help line |
| `.github/workflows/release.yml` | `v*` tag: macOS universal build + tests + pluginval + auval + sign/notarize (when secrets set) + pkg/zip, Windows VST3 + tests + Inno Setup, GitHub release |

## For AI Agents

### Working In This Directory
- Core loop (seconds): `cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILDAPP_BUILD_PLUGIN=OFF && cmake --build build-core && ./build-core/buildapp_tests`
- Full plugin: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` (installs into `~/Library/Audio/Plug-Ins`). A local JUCE: `-DFETCHCONTENT_SOURCE_DIR_JUCE=...`.
- Never include JUCE from `src/core`. New `.cpp` files go into `CMakeLists.txt` by hand.
- Audio thread: no allocation or locks in `processBlock` / `Buildup::process`. Buffers are sized in `prepare`.
- A stage's smoothers only advance while it runs: a silent stage snaps its targets (`setTargets`), so it wakes up where the knob is. Keep that when adding stages.
- At-rest exactness is tested (`Chain: every knob at its default passes the loop bit for bit`): a stage at rest must return early, not multiply by 1.
- JUCE 8 API: text widths via `theme::textWidth`; numbers via `fmt::` (`src/plugin/Format.h`), never `juce::String (x, 0)`.

### Testing Requirements
- After core changes: `./build-core/buildapp_tests` must pass (halfband rejection, filter response vs analog, click-free switching, pitch intervals and splice residual, noise level, reverb RT60 and drop, bit-exact rest, all rates).
- After UI changes: build `buildapp_snapshot`, render `<dir>` and read the PNGs.
- Before a release: `auval -v aufx BdAp Jgru` and pluginval at strictness 5 on both formats.

### Common Patterns
- Namespaces: `ba` (core), `ba::dsp` (primitives), `ba::map` (knob meanings), `theme` (UI tokens and helpers), `fmt` (number text). Plugin/UI classes are global.
- JUCE-style formatting: space before call parens `foo (x)`, 4-space indent, braces on their own line.
- Look ("night sheet", PhaseFucker's sibling): sheet `#131211`, bone ink `#ECE7DB`, one acid accent `#C8F03C` only for what builds tension (cut curve, build-knob rings); no gradients, shadows or glow; corner radius 2 px; IBM Plex Sans Condensed labels, IBM Plex Mono numbers.

## Dependencies
- JUCE 8.0.15 (AGPLv3), Catch2 v3.7.1, C++20, CMake >= 3.24, macOS deployment target 11.0
- IBM Plex fonts (OFL), bundled in `resources/fonts`

## Manual Notes

Notes under this heading are written by humans and preserved on regeneration.
