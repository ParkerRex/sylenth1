# Synthia

Synthia is a native macOS software instrument built with C++20, JUCE, and CMake. The active release recreates the classic Sylenth1 control layout and documented instrument behavior as AU, VST3, and a standalone application.

The October 2026 rebuild replaces the modern dark interface and shared sound path with a classic one-screen editor and independent stereo parts. Old Synthia patch compatibility is explicitly out of scope. Browser/Wasm, AI generation, and conversational editing are deferred.

## Release status

Implementation and integrated validation are in progress. This is not yet a qualified public release. Source-owned DSP and drawn controls do not establish byte-identical audio or pixel-identical rendering of a third-party product. Read [the conformance record](docs/CLASSIC_PARITY.md) for measured results and missing original-reference evidence.

The current contract is [SPEC.md](SPEC.md); execution progress is recorded in [the native rebuild plan](docs/exec-plans/active/2026-10-09-complete-classic-native-rebuild.md).

## Instrument

- Two independent stereo parts with four equivalent oscillators, per-part filters and amp envelopes, filter input routing, shared filter controls, and a post-effects master level.
- Two modulation envelopes, two LFOs, miscellaneous sources, and editable modulation destinations.
- A step arpeggiator, distortion, phaser, chorus, EQ, reverb, delay, and compressor.
- Four sub-banks of 128 programs, program editing, owned preset/bank files, MIDI program changes, and self-contained host state.
- A fixed 908 x 591 logical classic canvas with Part A/B switching, central arp/effect pages, onscreen keyboard, pitch/modulation wheels, and per-control MIDI Learn/Forget.

These describe the implemented target surfaces. Refer to validation evidence before assuming every original-product behavior has been reproduced exactly.

## Build and validate

```sh
cmake -S . -B build -DSYNTHIA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -j 6
ctest --test-dir build -C Debug --output-on-failure
```

The default configure fetches pinned JUCE `8.0.13`. An existing checkout can be supplied using `-DSYNTHIA_JUCE_PATH=/absolute/path/to/JUCE`.

Run the repository quality gate after changes:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=6 scripts/check-quality.sh
```

Build outputs stay under the selected build directory. With a Debug configuration, plugin bundles are under `build/SynthiaPlugin_artefacts/Debug/{AU,VST3,Standalone}`. The executable is named `Synthia` with matching bundle capitalization.

The build targets universal Apple Silicon/Intel binaries and an explicit macOS 11.0 deployment floor. This is a build target, not proof that every architecture/OS combination has been exercised.

## Reference and host validation

[VALIDATION.md](docs/VALIDATION.md) documents standalone renders, current preset/program tests, image/audio comparison tools, and required Ableton checks. Original-reference comparisons must fail when required captures are missing. Self-comparisons do not prove original-plugin fidelity.

[Historical Ableton records](docs/host-validation/ableton-smoke.md) identify their specific earlier builds and environments. A changed candidate needs its own host proof.

For headless editor captures, set `SYNTHIA_UI_SNAPSHOT` to an absolute PNG path and `SYNTHIA_UI_SNAPSHOT_QUIT=1`; see the validation document for Part A/B, LCD-page, and scale options.

## Packaging

[BUILD_RELEASE.md](docs/BUILD_RELEASE.md) describes developer and distribution modes. Developer packages are explicitly marked for testing. Distribution mode requires passing validation, Developer ID signing, notarization, and artifact verification; it does not publish to GitHub automatically.

The public release also needs the applicable project/JUCE licensing decision, reviewed shipped identity/assets, original-reference evidence for fidelity claims, and clean-machine installation/host coverage.

## Source map

- `src/dsp/`, `src/voice/`: realtime sound engine and scheduling.
- `src/plugin/`: host processor, parameter registry, and classic editor.
- `src/modulation/`, `src/midi/`: routing and controller models.
- `src/presets/`: current preset/program contracts.
- `src/validation/`, `tests/`: rendering, behavior tests, and comparison-tool tests.
- `presets/factory/`: source-owned factory presets.
- `scripts/`: quality checks, reference comparisons, build/install/release tooling.
- `docs/`: durable requirements, architecture, validation, and execution records.

The supplied Sylenth manual and screenshot collection are research references. They are not runtime resources or factory content.
