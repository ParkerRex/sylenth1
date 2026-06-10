# Architecture

synthia is planned as a JUCE/CMake C++ instrument with AU, VST3, and standalone targets.

## Product Phases

Phase 1 rebuilds the Sylenth virtual analog instrument workflow for modern macOS/Ableton AU and VST3 use. The current single-core pluck engine is scaffolding toward a fuller A/B architecture, four oscillator slots, Sylenth-level preset/arp/effects workflow, and a Sylenth-faithful UI based on the approved screenshot corpus.

Phase 2 adds AI-assisted sound and arpeggio creation. Generated patches, chord movement, and arp ideas must compile down to ordinary editable parameters, presets, modulation routes, and sequencer state.

Phase 3 adds conversational VST control. Text requests and reference-sound analysis should produce reversible parameter, modulation, arp, and FX edits without touching the realtime audio thread.

Current scaffold:

- `CMakeLists.txt` configures JUCE `8.0.13` through CMake `FetchContent`, unless `SYNTHIA_JUCE_PATH` points at a local checkout.
- `SynthiaPlugin` builds shared plugin code plus AU, VST3, and Standalone formats with host-facing product name `synthia`.
- `SynthiaRender` is the command-line validation executable.
- `SynthiaSmokeTest` is the first CTest target.
- `SynthiaContractTest` validates parameter registry, layer/oscillator-slot defaults, preset files, MIDI controller-map persistence, and APVTS state round-trip.
- `SynthiaVoiceCoreTest` validates envelope, LFO reset, voice allocation, and engine note release.
- `SynthiaDspCoreTest` validates oscillator, filter, arp/chord scheduling, ramp, glide, velocity glide, direct routes, TransMod scalers, voice/unison/random/performance modulation sources, layer slot rendering, and FX bypass, delay/tail, panic-clear, and reverb-state safety.
- `SynthiaRenderCoreSuite` runs the standalone core render harness and writes disposable JSON/WAV artifacts under `build/reports/ctest-core`, including dry and wet factory pluck reports.

## Component Stack

1. `Plugin Shell`
   - JUCE processor/editor wrappers.
   - AU/VST3/standalone target metadata.
   - Host buses, MIDI, parameters, state chunks.

2. `Parameter Registry`
   - Stable parameter IDs.
   - Ranges, units, defaults, display conversion, smoothing, automation flags.
   - Preset and host-state migration anchors.

3. `Voice Allocator`
   - Mono, mono legato, poly, unison.
   - Sustain pedal, all-notes-off, panic.
   - Voice stealing and release draining.

4. `Voice Engine`
   - Per-voice oscillator, envelopes, LFO, ramp, glide, filter, pan, random-on-note.
   - Must preserve independent note-local motion for overlapping notes.

5. `DSP Modules`
   - Band-limited oscillator stack.
   - Mixer and gain compensation.
   - Nonlinear multimode filter.
   - Arpeggiator, step, and chord event generation.
   - Amp drive and stereo stage.
   - Optional FX.

6. `Modulation System`
   - Direct routes.
   - Eight TransMod-style slots.
   - Voice/unison/random/velocity/performance sources.

7. `Persistence`
   - Host state.
   - Factory/user presets.
   - Schema migrations.

8. `Validation Harness`
   - Standalone render fixtures.
   - Audio metrics.
   - Host smoke tests.

9. `AI Orchestration Layer`
   - Planned Phase 2/3 layer outside the realtime audio path.
   - Converts text prompts, randomization intent, chord/arp intent, or reference-sound analysis into validated parameter and preset edits.
   - Must publish changes through the normal control/preset path.

## Realtime Boundary

The audio thread may:

- read atomically published parameter state,
- process MIDI events,
- render DSP,
- update lock-free diagnostic counters,
- write output buffers.

The audio thread must not:

- allocate memory,
- block on locks,
- perform file or network I/O,
- call synchronous logging,
- parse presets,
- rebuild UI,
- scan directories,
- create or destroy heavyweight resources.

## Planned Source Layout

This is the expected code layout once implementation begins:

- `CMakeLists.txt`: top-level JUCE/CMake project.
- `src/plugin/`: processor, editor, parameters, state.
- `src/dsp/`: oscillator, filter, envelopes, LFO, ramp, amp, FX.
- `src/voice/`: allocator and per-voice render engine.
- `src/modulation/`: source evaluation, direct routes, TransMod slots.
- `src/presets/`: schema, migration, factory preset loader.
- `src/validation/`: standalone render/test helpers.
- `src/ai/`: planned Phase 2/3 prompt, generation, reference-analysis, and reversible-edit orchestration.
- `tests/`: unit and render tests.
- `presets/factory/`: factory presets.
- `fixtures/`: MIDI and render fixtures.
- `docs/`: project docs.

Existing scaffold files:

- `src/plugin/PluginProcessor.h`
- `src/plugin/PluginProcessor.cpp`
- `src/plugin/PluginEditor.h`
- `src/plugin/PluginEditor.cpp`
- `src/plugin/ParameterRegistry.h`
- `src/plugin/ParameterRegistry.cpp`
- `src/midi/MidiControllerMap.h`
- `src/midi/MidiControllerMap.cpp`
- `src/presets/PresetManager.h`
- `src/presets/PresetManager.cpp`
- `src/dsp/Envelope.h`
- `src/dsp/Envelope.cpp`
- `src/dsp/Arpeggiator.h`
- `src/dsp/Arpeggiator.cpp`
- `src/dsp/Lfo.h`
- `src/dsp/Lfo.cpp`
- `src/dsp/Ramp.h`
- `src/dsp/Ramp.cpp`
- `src/dsp/SynthEngine.h`
- `src/dsp/SynthEngine.cpp`
- `src/dsp/fx/FxChain.h`
- `src/dsp/fx/FxChain.cpp`
- `src/presets/PresetValidator.h`
- `src/presets/PresetValidator.cpp`
- `src/voice/Voice.h`
- `src/voice/Voice.cpp`
- `src/voice/VoiceAllocator.h`
- `src/voice/VoiceAllocator.cpp`
- `src/validation/SynthRender.cpp`
- `tests/smoke/SynthSmokeTest.cpp`
- `tests/smoke/SynthContractTest.cpp`
- `tests/smoke/SynthVoiceCoreTest.cpp`

## Parameter Strategy

Parameter IDs must be stable after public release. UI labels may change; IDs should not.

Recommended ID style:

- `voice.mode`
- `osc.stack_count`
- `osc.saw_level`
- `filter.cutoff_semitones`
- `direct.filter_lfo_semitones`
- `transmod.1.source`
- `macro.motion`

Each parameter needs:

- default,
- physical range,
- normalized conversion,
- display conversion,
- automation flag,
- smoothing policy,
- preset serialization flag.

Current registry status:

- 364 parameter IDs.
- Voice, A/B layer state, four oscillator-slot state, legacy oscillator, filter, amp, envelopes, LFO (including the Step shape's `lfo.step_count`, `lfo.step_smooth`, and `lfo.step.1..16` table), ramp, arp/step/chord state, direct modulation, fixed-order FX rack state, realtime/offline quality modes, macros, and eight TransMod-style slots with physical destination depths are represented.
- Host state uses `AudioProcessorValueTreeState` with schema metadata.
- `ModulationRouteModel` exposes a non-realtime source catalog, destination catalog, route view, and write adapter over the existing TransMod slots. It is the model for future source tiles, halos, matrix rows, validation reports, and AI edit summaries; the audio thread still consumes the fixed `SynthParameters::transMod` snapshot.

Current editor and preset status:

- The editor skin is a flat midnight-graphite theme with functional-zone colour coding (source cyan, shaping amber, performance blue, modulation magenta, utility slate); knobs draw lit value arcs in their zone hue. The Sylenth-derived layout and density are retained; the brushed-tan hardware skin is not.
- Modulation UX is drag-and-drop on top of the existing TransMod state: a `MOD SOURCES` chip strip on the Sound page drags any catalog source onto destination-bound knobs (osc pitch, pulse width, filter cutoff, amp level, pan). Drops compile through the `ModulationRouteModel` write adapter into a free `transmod.*` slot; halo rings on those knobs show each route's modulated range, alt-drag edits the first route's depth, and right-click clears a slot. No UI-only routing state exists.
- The LFO is a visual module: bound `lfo.*` controls plus a live display that renders classic shapes and, for the `Step` shape, a drawable 16-bar grid bound to `lfo.step.*`. A playhead/value dot animates from the processor's lock-free visual feed (first active voice, or the shared mono LFO when mono/Song gating applies), and the centre display draws a live output scope from a wait-free sample ring.
- `PluginProcessor::publishUiVisuals` publishes the scope ring and LFO phase/value atomics from the audio thread after rendering; the editor only reads atomics. `SYNTHIA_UI_SNAPSHOT` (with optional `SYNTHIA_UI_SNAPSHOT_PRESET` / `SYNTHIA_UI_SNAPSHOT_QUIT`) renders the editor offscreen to PNG for scripted UI proof without screen-recording permissions.
- `PluginEditor` is a compact, fixed-shell control surface modeled on the Sylenth workflow: a header (preset prev/next/load/save/duplicate, dirty state, output meter, active/max voices, model-backed patch-load estimate, panic, and an always-visible SR/block/peak/MIDI/invalid/architecture diagnostics footer), a persistent Layer A/B selector exposing the `layer.*` mix state, and a `Sound` / `Modulation` / `Effects` tabbed workspace. The `Sound` tab exposes the live core controls, a scrollable APVTS-bound arp/step/chord sequencer row, a preset workflow panel for Init/Random/Reset plus local A/B compare, a preset metadata/save panel with explicit no-clobber Save New and Overwrite actions, a preset browser drawer, and a compact global MIDI Learn panel; `Modulation` holds the destination-grouped direct routes plus the eight TransMod slots and a read-only route overview; `Effects` exposes the fixed-order rack modules with master/quality. Controls should move toward Sylenth-faithful knobs, panel bars, dense labels, top-strip rhythm, preset/LCD behavior, and A/B part grouping, while every visible control stays bound to real state. Render-boundary honesty is encoded in the UI: Layer A oscillator 1 remains the legacy flat `osc.*` compatibility source and is badged `LIVE`; A2/B1/B2, layer mix controls, arp/step/chord controls, preset workflow controls, and FX rack controls bind to real state.
- Editor controls are constructed against `ParameterRegistry` IDs and use APVTS attachments for sliders, combo boxes, and toggles so UI edits reach the same host-automatable parameters as presets and host state.
- `PresetManager` recursively scans bundled `.SynthiaPreset` factory presets with a source-directory development fallback, scans user presets from `~/Music/ParkerX/synthia/Presets`, validates the required `.SynthiaPreset` envelope and nested preset payload before load, prepares defaults-plus-overrides APVTS state for one-shot replacement, prepares Init and bounded seed-randomized APVTS states for processor preset commands, maps canonical `mod_slots` objects into flat TransMod parameters, writes schema-valid user `.SynthiaPreset` files, and exposes browser-facing summaries with source, bank, category, tags, favorite keys, sidecar favorite state, search/filter helpers, and catalog facets.
- `MidiControllerMap` reads and writes the global user CC sidecar at `~/Music/ParkerX/synthia/MidiControllerMap.json`. `PluginProcessor` loads it on the message/control path, publishes fixed atomic CC-to-parameter indexes for audio-thread MIDI lookup, captures MIDI Learn events through atomics, and applies mapped CC values to APVTS parameters from its message-thread timer. The audio thread never writes the sidecar, locks the map, or mutates APVTS parameters directly.
- Processor diagnostics expose sample rate, block size, active voices, model-backed patch cost, MIDI event count, invalid sample count, peak, current preset, and binary architecture to the editor without filesystem or UI work on the audio thread.

Current layer and oscillator-slot status:

- `SynthParameters` has two `LayerParameters` records and two `LayerOscillatorParameters` records per layer: A1, A2, B1, and B2.
- Layer A defaults are enabled; Layer B defaults are disabled.
- Layer A oscillator 1 defaults to the primary enabled slot with one saw voice at full level. The other three slots default disabled with zero voices.
- `PluginProcessor` caches the namespaced `layer.*` atomics and publishes them into the realtime parameter snapshot.
- `SynthEngine::setParameters` clamps the layer and slot state at the audio boundary.
- Current audio rendering uses Layer A/B mix state and A1/A2/B1/B2 oscillator slots. Layer A oscillator 1 remains the compatibility gate for the legacy flat `osc.*` source; A2/B1/B2 render through preallocated oscillator stacks mapped from `layer.N.osc.M.*`. One filter, envelope pair, amp, modulation, and FX path are still shared until the later per-layer filter/envelope slice.

Current voice-core status:

- `SynthEngine` consumes note-on, note-off, all-notes-off, and all-sound-off from the plugin processor.
- `VoiceAllocator` supports basic poly allocation, note release, panic, and deterministic random-on-note values.
- Voice allocation-shape changes, held-note rebuilds, and surplus-voice removals use a short de-click fade instead of hard-resetting audible voices.
- `Envelope` and `Lfo` provide deterministic per-voice modulation primitives.
- `OscillatorStack` renders polyBLEP saw/pulse, deterministic noise, sub waveforms, stack detune, and hard sync.
- `Filter` renders semitone-domain L2/L4/B2/B4/H2/H4/Peak2/Notch2/Notch4 nonlinear responses with drive/resonance compensation and interpolated oversampling sub-steps.
- `Voice` applies direct pitch/pulse/cutoff routes, ramp, glide, velocity glide, TransMod scalers, synced or Hz LFO rates, per-voice/mono LFO behavior, amp envelope, amp drive, level, pan spread, unison spread, analog variation, performance MIDI sources, and macro influence.
- `ModulationRouteModel` translates enabled TransMod slots into explicit route summaries with source/scaler IDs, destination IDs, physical depth parameters, and legacy `transmod.N.depth` cutoff-depth visibility. Its write adapter compiles source/destination/depth requests back into existing `transmod.N.*` APVTS parameters, including deterministic clear-slot edits. `SynthAudioProcessor::getModulationRouteView()`, `writeModulationRoute()`, and `clearModulationSlot()` expose this model to the editor without adding hidden UI-only state.
- `Arpeggiator` keeps fixed-size held-note, step, and chord candidate state. `SynthEngine` also tracks physical MIDI input notes in fixed arrays so held notes can move deterministically between direct and arp routing when `arp.enabled` changes. When `arp.enabled` is true, external MIDI notes become held input state and generated note events feed the existing `VoiceAllocator`. When `chord.enabled` is true with arp off, direct note-on captures the generated chord outputs for that source note and deduplicates overlapping output pitches. Direct note-off releases the captured outputs, and chord parameter changes release stale chord outputs before rebuilding still-held physical input notes under the new chord configuration. The scheduler uses host tempo from `SynthParameters::tempoBpm`, fixed arrays, and no audio-thread allocation.
- `FxChain` applies a fixed post-voice rack: distortion/saturation, phaser, chorus/flanger-style modulation, simple EQ, tempo-synced delay, simple reverb, and compressor. Delay, chorus, phaser, EQ, and reverb state is prepared or reset outside allocation-sensitive processing; `process` performs no heap allocation. `quality.realtime_mode` and `quality.offline_mode` select conservative processing variations without changing audio-thread resource allocation.
- `SynthiaRender` can write oscillator, filter, modulation, voice, preset validation, dry-core factory pluck, wet factory pluck, LFO ablation, determinism, and core-suite summary reports using the requested preset and MIDI fixture.

## DSP Priorities

Highest priority:

- Phase 1 Sylenth rebuild roadmap and Ableton AU/VST3 proof.
- Per-layer filters/envelopes, layer mixer/master polish, and deeper oscillator-slot parity on top of the delivered layer/slot renderer.
- Preset browser, fine-grained step/arp UI, effects, and modulation UX that support the Sylenth-level workflow.

Lower priority:

- Phase 2 AI generation,
- Phase 3 conversational/reference-sound editing,
- extended modulation processors beyond the Phase 1 workflow.
