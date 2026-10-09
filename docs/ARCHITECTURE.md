# Architecture

Synthia is a C++20 JUCE instrument with native macOS AU, VST3, and Standalone targets. The active release follows the classic-reference contract in `SPEC.md`. The October 2026 implementation has one native architecture; old Synthia patch compatibility, browser/Wasm, and AI extensions are outside this release.

## Boundaries

| Component | Responsibility |
| --- | --- |
| PluginProcessor | JUCE lifecycle, MIDI ingestion, bounded UI MIDI handoff, parameter snapshots, host state, program requests, diagnostics |
| ParameterRegistry | Parameter IDs, types, ranges, defaults, choices, and display/automation contracts |
| SynthEngine and VoiceAllocator | Prepared resources, note ownership, sustain, voice allocation, arp/chord scheduling, master output |
| Voice and DSP modules | Four stereo oscillator slots, independent part filters/amp envelopes, two modulation envelopes/LFOs, routing, glide |
| FxChain | Fixed post-mix distortion, phaser, chorus, EQ, reverb, delay, and compressor |
| PresetManager and processor program bank | Current owned preset/bank validation, safe writes, program editing and self-contained restore |
| PluginEditor | Fixed logical classic canvas and real host-parameter gestures; no independent synthesis state |
| SynthRender and tests | Parameter/preset loading, behavioral renders, audio metrics, processor/program tests, reference tooling |

## Native signal path

Each note owns four oscillator stacks, with up to eight oscillator voices per slot and independent stereo distribution. A1/A2 form source bus A; B1/B2 form source bus B. Each part filter independently selects no input, A, B, or A+B. The selected stereo signal passes through that part's filter and amp envelope. Part output level/pan/mute/solo apply before the parts are mixed.

Source selection and destination output controls have different roles: muting Part B's output does not prevent its oscillators feeding Filter A through explicit cross-routing. Zero oscillator voices disables that source. There is no hidden secondary enabled switch that makes a visible nonzero voice count silent.

The mixed voice output enters the fixed master rack: distortion, phaser, chorus, EQ, reverb, delay, compressor. Master level applies after effects. Native synthesis is not normalized into a legacy shared-filter path. Output guards protect against invalid/runaway values without presenting normal above-unity floating-point output as an automatic fault.

Both part envelopes participate in note release lifetime. Transport/panic handling must clear held and generated notes and effect state deliberately. An active arpeggiator can have zero currently sounding voices during a gate gap; that is not permission to stop its scheduler clock.

## Modulation and timing

Two modulation envelopes, two LFOs, performance inputs, and miscellaneous sources feed signed destination depths. The route model and the realtime fixed arrays describe the same routes. Original panels select source/destination controls directly; parameter IDs do not encode screen position.

Global sync and LFO free-running behavior are evaluated live without accidental phase resets. Shared/free LFOs continue advancing while no voice sounds. The arpeggiator supports step/chord event batches in fixed storage. Note-off ownership must survive parameter changes and overlapping generated pitches.

Source-owned DSP implements documented control behavior. Matching original algorithms, phase response, and timbre is a separate reference-measurement obligation; this architecture description does not establish audio equivalence.

## Realtime boundary

The audio callback may consume bounded MIDI, atomically published values, preallocated voice/effect state, and lock-free diagnostic/UI feeds. It must not allocate, wait on locks, parse JSON, access files, log synchronously, or rebuild the editor.

Heavy preparation happens before processing. Hot DSP operations and logical resets are covered by the repository fitness checker; narrowly permitted operations must state their bounded behavior. Large unsupported MIDI messages must be rejected before constructing allocating JUCE message objects.

Program-change MIDI and UI program selection publish bounded requests. Bank editing, preset parsing, state replacement, and sidecar writes run on the control path. UI MIDI queue overflow must not leave notes stuck. Closing the editor releases only notes/wheel gestures it owns; it must not panic host playback.

## Parameters and persistence

`ParameterRegistry` is the canonical list. The processor caches its atomic values and builds `SynthParameters` snapshots. Current native Init and source-owned factory files use that same contract. Current host-state and program-bank round trips must be self-contained and tested; no old-Synthia preservation layer is required.

The program bank has four sub-banks of 128 slots. MIDI program changes use the original first-128-program behavior. Program edits and reset baselines must survive selection changes and host restore. The owned Synthia bank format does not imply support for proprietary original-plugin bank files.

See `PRESET_SCHEMA.md` for the implemented versioned format and error rules.

## Classic editor

The editor uses a 908 x 591 logical canvas with deterministic uniform scaling. Part A/B changes the oscillator/filter/amp-envelope bindings. A central LCD selects arp and effect pages while modulation and performance controls remain visible. Each control uses real ranged-parameter values and correct host gestures, including fine edits and reset actions.

Keyboard/pitch/modulation interaction enters the same processor event boundary as incoming MIDI. Meter/readout refresh is bounded and driven by actual diagnostics. Decorative continuously repainting effects are not part of the design.

The interface is drawn from owned code and uses Synthia identity. Reference masks and fidelity claims must report the deliberate branding differences rather than hiding control/layout mismatches.

## Build and evidence

CMake pins JUCE 8.0.13, targets both `arm64` and `x86_64`, and specifies a macOS 11.0 deployment floor. `BUILD_RELEASE.md` defines developer versus distribution artifacts and their strict validation requirements.

`CLASSIC_PARITY.md` separates implementation, behavioral tests, original-reference comparison, current host execution, and distribution proof. `VALIDATION.md` lists the actual checks. Build success and universal slice inspection do not establish Intel runtime or original-sound equivalence.
