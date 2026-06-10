# Validation Plan

Validation must prove the instrument behaves correctly as a plugin and as a sound engine. Passing unit tests alone is not enough; Ableton loading, host state, and audio renders are required.

## Roadmap Validation Phases

Phase 1 validation proves the Sylenth rebuild: AU/VST3 scan/load/play in Ableton, host state restore, automation, preset browsing, A/B architecture, oscillator/filter/envelope/modulation behavior, arpeggiator timing, FX tails, UI open/close while playing, and screenshot/manual QA against `docs/modern-synthia-baseline.md`.

Phase 2 validation proves AI-assisted generation: randomize/generate commands must produce finite audio, valid preset state, bounded parameter values, deterministic or intentionally seeded results, and editable arp/chord/modulation data rather than opaque output.

Phase 3 validation proves conversational editing: text prompts and reference-sound workflows must produce reversible parameter diffs, preserve realtime safety, avoid invalid states, and include reports that explain which synth, modulation, arp, and FX controls changed.

Current scaffold commands:

```bash
cmake -S . -B build -DSYNTHIA_ENABLE_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
./build/SynthiaRender --smoke --output build/reports/smoke.json
./build/SynthiaRender --list-parameters --output build/reports/parameters.json
./build/SynthiaRender --validate-presets presets/factory --output build/reports/presets.json
./build/SynthiaRender --voice-test --output build/reports/voice-core.json
./build/SynthiaRender --osc-test --notes C1,C3,C5,C7 --output build/reports/oscillator.json
./build/SynthiaRender --filter-test --output build/reports/filter.json
./build/SynthiaRender --modulation-test --fixture fixtures/midi/overlap-pluck.mid --output build/reports/modulation.json
./build/SynthiaRender --modulation-route-render-test --fixture fixtures/midi/overlap-pluck.mid --output build/reports/modulation-route-render.json
./build/SynthiaRender --offline-realtime-compare-test --fixture fixtures/midi/overlap-pluck.mid --output build/reports/offline-realtime-compare.json
scripts/compare-ableton-bounce-realtime.py --self-test --output build/reports/ableton/bounce-compare/strong-compare-self-test.json
./build/SynthiaRender --randomize-test --seeds 1,42,12345,67890 --fixture fixtures/midi/overlap-pluck.mid --output build/reports/randomize.json
./build/SynthiaRender --preset "presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset" --fixture fixtures/midi/overlap-pluck.mid --dry --output build/renders/pluck-core-01-dry.wav --report build/reports/pluck-core-01-dry.json
./build/SynthiaRender --preset "presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset" --fixture fixtures/midi/overlap-pluck.mid --wet --output build/renders/pluck-core-01-wet.wav --report build/reports/pluck-core-01-wet.json
./build/SynthiaRender --suite core --output-dir build/reports/core
./build/SynthiaRender --suite patch-recreation --output-dir build/reports/patch-recreation
```

The current smoke render is intentionally note-less and proves initialization, finite output, report writing, and command shape.

The current contract validation proves:

- 346 unique parameter IDs,
- valid defaults and ranges,
- APVTS state round-trip,
- old host-state default merge for new layer, oscillator-slot, arp, step, and chord fields,
- layer and oscillator-slot defaults, arp/chord defaults, legacy preset defaulting, saved preset serialization, inactive-slot no-op behavior, audible A2/B1/B2 rendering, and layer mute/solo behavior,
- seven factory preset JSON files, including six curated Phase 1 patch-recreation cases,
- no unknown preset parameter IDs,
- TransMod slot objects use valid slot IDs, source/scaler choices, depth domains, and destination IDs.
- Init, Reset, and seedable Randomize commands prepare ordinary APVTS state without mutating live state before replacement.
- Preset workflow helpers cover metadata-aware writes, no-clobber create-only safe-save rejection, dirty-state comparison against immutable baseline fingerprints, and local A/B compare slot capture/recall without mutating live state before replacement.
- Standalone UI smoke covers the preset workflow and metadata-save controls at normal and compact sizes: dirty-state pill, Init, Random, Reset, A Store/A Load, B Store/B Load, Preset name, Author, Bank, Category, Tags, Notes, Save New, Overwrite, and the adjacent preset browser/MIDI panels. The A Store manual control smoke confirms A Load becomes actionable after capture. Invalid-preset browser rows are model-backed in the editor and require manual browser-bottom QA until JUCE viewport scrolling is reliable under automation.
- MIDI controller-map normalization rejects invalid assignments, resolves CC/parameter conflicts deterministically, and round-trips through the global user sidecar JSON shape.

The current voice-core validation proves:

- envelope attack/release behavior,
- LFO phase reset,
- poly voice allocation and release,
- engine note-on/note-off voice lifecycle,
- audio render remains finite while voices are active.

The current DSP validation proves:

- oscillator tuning for C1, C3, C5, and C7 within the 5-cent target,
- high-band oscillator spectral metrics are recorded for C1, C3, C5, and C7,
- pulse duty cycle at 10, 25, 50, 75, and 90 percent widths,
- sub octave accuracy for -1, -2, and -3,
- deterministic noise, stack detune symmetry, and hard-sync finite output,
- semitone filter cutoff mapping,
- nonlinear filter mode response for L2, L4, B2, B4, H2, H4, Peak2, Notch2, and Notch4,
- finite resonant impulse behavior,
- drive changes filter response,
- `Pluck Core 01` dry render loads the requested preset and MIDI fixture, writes a finite non-clipping WAV/report, and records note-local LFO spread during overlapping notes.
- ramp timing, glide, velocity glide, direct keytrack/LFO/envelope routes, TransMod scaler multiplication to many physical destinations, performance MIDI sources, voice/unison/random source spread, and fixture trace ranges.
- direct chord expansion, overlapping chord-output release ownership, chord parameter-change note-off symmetry, arp up-mode timing, host-tempo step duration, gate-off timing, octave wrapping, step pitch and velocity scaling, tie/hold behavior, arp enable/disable while input notes are held, hold-disable latch clearing, and panic clearing generated notes.
- voice-mode and polyphony cap changes keep the most recent held notes and drain surplus voices through a bounded de-click fade instead of hard-resetting them.
- top-level `mod_slots` preset schema objects are applied by render loading, including schema-only modulation fixtures that omit flat APVTS-style `transmod.*` parameters.
- modulation route model catalogs and derived route rows are covered by `SynthiaContractTest`, including source/scaler IDs, destination IDs, active route filtering, and legacy `transmod.N.depth` cutoff contribution.
- `SynthiaRender --modulation-route-render-test` compiles a route write through `ModulationRouteModel`, applies it to `transmod.3.*`, proves the rendered audio changes, then applies a clear-slot edit and proves the render returns to baseline within deterministic tolerances.
- preset browser metadata validation, saved user preset browser metadata, factory/user/legacy source summaries, sidecar favorite add/remove behavior, search/category/tag/favorite filtering, and browser catalog facets are covered by `SynthiaContractTest`.
- APVTS automation-readiness is covered by `SynthiaContractTest`: every registry parameter must be exposed by APVTS, remain host-automatable when marked automatable, match its declared float/bool/choice type, and accept host-notifying default writes. Ableton AU/VST3 automation record/playback is covered by the 2026-06-07 host proof in `docs/host-validation/ableton-smoke.md`.
- MIDI controller-map persistence is covered by `SynthiaContractTest`; learned CC capture, persistence, value application, and Forget behavior in AU and VST3 are covered by the current Ableton host proof in `docs/host-validation/ableton-smoke.md`.
- FX bypass stays null-equivalent to dry rendering when globally bypassed, disabled expanded-rack modules are dry-equivalent, phaser/EQ/compressor/distortion-mode processing is finite and measurably audible when enabled, tempo-synced delay reports exact sample timing at test tempo, FX tail length is reported from the active time-based FX parameters, and wet output remains finite, non-clipping, and measurably different from its dry reference.
- `SynthiaRender --offline-realtime-compare-test` renders `FX Space 01` in realtime Normal quality and offline High quality, verifies both renders are finite and non-clipping, and records a bounded meaningful audio difference caused by the quality-mode switch. Ableton offline bounce versus realtime resampling is covered by the 2026-06-07 host proof in `docs/host-validation/ableton-smoke.md` and the stronger `scripts/compare-ableton-bounce-realtime.py` content-match report.
- `SynthiaRender --randomize-test` prepares seedable bounded randomized APVTS state through `PresetManager`, renders each seed through the standalone engine as prepared, rejects malformed seed lists, and checks finite non-silent non-clipping output.
- `SynthiaRender --suite core` runs the standalone smoke, parameter, preset, voice, oscillator, filter, modulation, modulation route render, offline/realtime compare, randomize render, dry pluck, wet pluck, LFO ablation, and determinism reports in one command.
- `SynthiaRender --suite patch-recreation` renders `Pluck Core 01`, `Supersaw Stack 01`, `Bass Wub 01`, `Pad Wide 01`, `Arp Motion 01`, and `FX Space 01` against the overlap-pluck fixture, writes WAV/report artifacts for each, and checks finite non-clipping output plus meaningful wet-versus-dry FX differences. The arp/chord patch also asserts that `SynthRender` applies preset-loaded `arp.*` and `chord.*` state.
- `SynthiaRenderCoreSuite` runs the core suite under CTest.
- `SynthiaRandomizeRenderTest` runs the randomized preset render proof under CTest.
- `SynthiaModulationRouteRenderTest` runs the modulation route write/render/clear proof under CTest.
- `SynthiaOfflineRealtimeCompareTest` runs the standalone realtime/offline quality-mode comparison under CTest.
- `SynthiaPatchRecreationSuite` runs the patch-recreation suite under CTest.

Preset render validation is expected to fail if the preset file is missing, the `.SynthiaPreset` envelope or nested preset payload is invalid, the MIDI fixture is missing, the fixture is not a valid MIDI file, or the fixture has no note events.

## Validation Profiles

### Core DSP

- oscillator aliasing,
- pulse duty cycle,
- sub oscillator tuning,
- stack detune symmetry,
- filter cutoff mapping,
- filter drive/resonance behavior,
- envelope timing,
- LFO sync and phase reset,
- per-voice modulation independence,
- TransMod source/scaler/destination behavior.

### Musical Render

Use fixed MIDI fixtures:

- 128 BPM,
- overlapping 1/8 and 1/16 pluck phrase,
- velocity variation from 70 to 120,
- dry core render,
- FX render,
- mono-LFO ablation,
- per-voice-LFO render.

Required checks:

- overlapping notes keep note-local cutoff motion in per-voice mode,
- mono LFO produces a measurably different movement profile,
- output is deterministic within tolerance,
- factory pluck does not clip at default level.

Current standalone core-suite artifacts:

- `summary.json`: aggregate pass/fail for all core reports.
- `pluck-core-01-dry.json`: dry factory pluck metrics plus WAV artifact path.
- `pluck-core-01-wet.json`: wet factory pluck metrics plus WAV artifact path, FX mode, delay division, tempo-synced delay samples, tail length, post-event render length, active quality mode, and wet-versus-dry difference metrics.
- `randomize.json`: seed list, per-seed prepared/rendered status, prepared FX-enabled state, peak, RMS, nonzero sample count, invalid sample count, and pass/fail for bounded randomized state render proof.
- `modulation-route-render.json`: route write edits, clear-slot edits, active route count, baseline/routed/cleared peak and RMS, routed audio-difference thresholds, clear-to-baseline tolerances, and pass/fail for route creation plus clear-slot restore.
- `offline-realtime-compare.json`: realtime/offline quality modes, peak/RMS metrics for each render, audio-difference metrics, and pass/fail for bounded meaningful quality-mode difference.
- `lfo-ablation.json`: compares per-voice LFO and mono LFO renders using note-local LFO spread and audio difference.
- `determinism.json`: renders the dry pluck twice and compares `max_abs_diff`, `rms_diff`, and `peak_delta` against fixed tolerances.
- `artifacts/*.wav`: retained dry/per-voice/mono render WAVs.
- `failures/*.wav`: written only when deterministic repeat comparison fails.

Current patch-recreation-suite artifacts:

- `summary.json`: aggregate pass/fail for the curated Phase 1 patch set.
- `<preset-id>-wet.json`: per-preset render metrics, FX mode, quality mode, note-local LFO spread where applicable, and wet-versus-dry difference metrics.
- `summary.json` patch rows include `arp_chord_state_passed`; this must be `true` for `Arp Motion 01`.
- `artifacts/<preset-id>-wet.wav`: retained render WAVs for listening and regression inspection.

### Host Integration

Required Ableton checks:

- AU scan/load/play,
- VST3 scan/load/play,
- parameter automation record/playback,
- host state save/restore,
- offline bounce versus realtime render,
- buffer-size changes,
- sample-rate changes,
- transport stop and all-notes-off,
- UI open/close while playing.
- MIDI Learn and Forget from the global controller panel, plus mapped CC playback against at least one continuous and one stepped parameter.

Current Ableton proof includes AU/VST3 `Layer A Level` automation record/playback plus Master offline bounce versus realtime resampling content comparison with envelope alignment, per-channel filtered-band correlation thresholds, and negative controls. Strict waveform/null-test equivalence is not claimed.

Recommended additional hosts:

- Logic Pro for AU,
- Reaper for AU/VST3,
- Bitwig Studio for VST3.

### Performance

Measure:

- 44.1, 48, 96 kHz,
- 32, 64, 128, 512 sample buffers,
- 8, 16, 32 voices,
- unison 1, 2, 4, 8,
- oversampling off, 2x, 4x,
- UI open and closed,
- silence/release-tail denormal behavior.

## Metrics

Recommended targets:

- oscillator tuning: within 5 cents,
- self-osc filter tracking: within 10 cents after calibration,
- LFO sync period: within 1%,
- envelope decay/release timing: within 15%,
- regression loudness after gain match: within 0.5 LU,
- no NaN or infinity in any render,
- no denormal CPU spike on silence tails.

Implemented standalone metrics:

- `invalid_samples`: finite-output guard; must be `0`.
- `peak`: maximum absolute sample; dry pluck must remain below `1.0`.
- `rms`: linear RMS of stereo audio.
- `rms_dbfs`: `20 * log10(rms)`, used as a lightweight loudness proxy until LUFS exists.
- `crest_db`: peak-to-RMS relationship.
- `dc_offset`: average mono offset.
- `spectral_centroid_hz`: bounded-window DFT centroid for coarse spectral regression.
- `stereo_correlation`: left/right correlation, useful for spread regressions.
- `note_local_lfo_spread`: spread of per-voice LFO values while notes overlap.
- `fx_mode`, `delay_division_beats`, `tempo_synced_delay_samples`, `fx_tail_seconds`, `post_last_event_seconds`, `quality_mode`, `wet_dry_max_abs_diff`, `wet_dry_rms_diff`, and `wet_meaningful_passed`: wet-render proof for the onboard fixed-order FX path.
- `mod_slot_schema_passed`: modulation harness check that canonical preset `mod_slots` objects are loaded into runtime TransMod slots.
- `routed_max_abs_diff`, `routed_rms_diff`, `cleared_max_abs_diff`, and `cleared_rms_diff`: modulation route render proof metrics for created-route audibility and clear-slot determinism.
- `realtime_quality_mode`, `offline_quality_mode`, `quality_modes_differ`, `difference_meaningful`, and `difference_bounded`: standalone realtime/offline quality comparison fields.
- `max_abs_diff`, `rms_diff`, `peak_delta`: deterministic repeat comparison metrics.

## Headless Performance Bench

`SynthiaRender --bench` renders a sustained chord through the full engine
(parameter snapshot per block, 512-sample blocks by default) and reports wall
time plus realtime multiple, for fast DSP perf A/B without a host. Use an
optimized build; Debug timings are invalid for performance decisions.

```bash
cmake --build build-perf-release --target SynthiaRender
./build-perf-release/SynthiaRender --bench
```

Options: `--bench-preset <id-or-path>`, `--bench-notes <list>`,
`--bench-seconds <s>`, `--bench-reps <n>`, `--bench-block <samples>`,
`--output <report.json>`. Rep-to-rep spread is roughly half a percent, so
changes of about one percent and larger are resolvable. The JSON report
records wall-ms per rep, best/median realtime multiples, active voices, and a
peak/invalid-sample sanity check.

The bench complements, not replaces, host validation: confirm wins in Ableton
with the bar-65 protocol, ideally as a paired same-session A/B (install old
binary, measure, install new binary, measure) because single cross-session
CPU windows carry thermal and session-state confounds of a few percentage
points.

## Headless UI Snapshot

The editor renders itself offscreen to PNG when `SYNTHIA_UI_SNAPSHOT=<path.png>`
is set, so UI states can be captured from scripts without macOS
screen-recording permission. `SYNTHIA_UI_SNAPSHOT_PRESET=<preset path>` loads a
preset first (e.g. to capture the Step-LFO grid or modulation halos), and
`SYNTHIA_UI_SNAPSHOT_QUIT=1` exits the standalone after writing the file:

```bash
SYNTHIA_UI_SNAPSHOT=/tmp/ui.png \
SYNTHIA_UI_SNAPSHOT_QUIT=1 \
SYNTHIA_UI_SNAPSHOT_PRESET="presets/factory/Bass/BA - Wub Stepper 01.SynthiaPreset" \
./build/SynthiaPlugin_artefacts/Standalone/synthia.app/Contents/MacOS/synthia
```

The hook is a no-op without the environment variable and adds no UI state.
`SynthiaRender --validate-presets` reports now include per-preset `errors`
arrays so invalid presets are diagnosable from the JSON alone.

## Render Artifact Contract

Each validation render should record:

- fixture ID,
- preset ID,
- plugin version,
- sample rate,
- block size,
- tempo,
- seed,
- architecture,
- format or standalone runner,
- metric results,
- pass/fail summary.

Current report schema uses `schema_version: 1` and a per-report `suite` field. Core-suite reports live under the supplied output directory and are disposable build artifacts.

Failed renders should keep audio artifacts and JSON reports for inspection.

## First Validation Milestone

The first implementation milestone is not complete until:

- standalone renders the factory pluck fixture,
- oscillator/filter/envelope/LFO tests pass,
- AU loads in Ableton,
- VST3 loads in Ableton,
- Ableton state restore preserves the preset,
- per-voice LFO ablation test passes.
