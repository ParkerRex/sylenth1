# Validation Plan

Validation must prove the instrument behaves correctly as a plugin and as a sound engine. Passing unit tests alone is not enough; Ableton loading, host state, and audio renders are required.

## Native Rebuild Acceptance

The rebuild uses one native architecture. Its behavior checks prove the local
implementation responds to its controls. They do
not prove a Sylenth audio match or a pixel match.

`SynthiaDspCoreTest` includes these native cases:

- A1 octave changes the measured sine frequency by 12 semitones within 5 cents;
  waveform changes affect audio; zero voices silence the oscillator; eight
  detuned voices change the render.
- Stereo spread produces different left and right waveforms, with zero spread
  producing equal channels.
- Hard-panned A/B parts preserve A's samples when only B's filter or amp
  envelope changes. Filter input None silences its output while B oscillators
  can still feed Filter A through crossrouting.
- The second modulation envelope and LFO change the audible A1 pitch through
  native destinations. Changing the first source does not change that route;
  disabling the route restores the deterministic baseline.
- Arp Up, Down, UpDown, DownUp, both endpoint-repeat modes and AsPlayed produce
  specified note sequences. Wrap resets melodic position. Random is repeatable.
  StepSequence uses step transpose; ordinary melodic modes ignore it.
- Triplet and dotted arp divisions are checked against their rounded sample
  durations. Tied steps preserve common notes, order the new note before the
  old release, and release at rests.
- Five velocity modes produce the specified event velocities. StepChord emits
  matching note-offs for all generated chord notes. Step velocity reaches the
  native modulation path and clearing its route restores baseline audio.
- Fully wet delay has no immediate dry impulse, uses independent left/right
  sample timings, collapses to equal channels at zero width, and bypasses
  exactly. Reverb respects predelay, mono width and a longer declared tail.
- Compressor attack and release settings produce different transient and
  recovery behavior. Phaser/chorus Sync matches the equivalent free rate through
  a tempo change without resetting phase. Zero visible FX mixes remain dry even
  with hidden macro values; fixed distortion Amount remains independent of
  macro drive.
- Shared resonance modulation changes both A/B filter paths. Global phaser
  modulation selects the most recently triggered active voice through its
  release, then falls back; multiple voices do not attenuate its depth. Scalar
  and block processing preserve the typed control and audio samples. Part/master
  volume endpoints reach exact silence and restore a held note when raised.
- Normal portamento glides with overlapping keys and retriggers the mono ramp;
  it jumps between separated keys. Slide glides between separated keys.
- A rack render matches independently composed modules in the documented order:
  Distortion, Phaser, Chorus, EQ, Reverb, Delay, Compressor.

`SynthiaContractTest` checks native APVTS round-trip, independent B filter/amp
and second envelope/LFO values, and a selected route with zero amount. Native
preset preparation and host state preserve the current parameter values. XML
round-trip checks protect numeric values represented as strings by JUCE.

`SynthiaNativeProcessorTest` uses the real processor to check 512 program slots,
sub-bank boundaries, copy/paste, insertion/deletion, invalid indices, program values/names across
host-state restore, bank-file persistence, rejection of malformed banks without changing the selected
program, and create-only no-clobber behavior.
It also submits UI MIDI to the fixed queue, proves note-on renders audio,
note-off releases it, rejects queue overflow and accepts events after draining. A note-off rejected
because the queue is full must trigger the processor panic handoff and leave
no active voice. Pitch/mod wheel updates coalesce while the FIFO is full; their
final center/zero values must still reach the audio processor.
Reset is checked against the stored original after its file is changed or
deleted. Host restore must preserve edited values and the original dirty
baseline, and Reset must return to that original state.

It checks that closing the native editor preserves a held DAW note, and that a
six-second free-running arp interval keeps scheduling after all voices are
silent. FREE LFO clocks are compared after equal audio callback time with and
without active notes. Silent rate edits must preserve phase and change its
slope. Both LFOs produce a different next-note render with FREE on after a
silent gap; ordinary note-reset mode returns identical audio. This measures
delivered audio callbacks, and does not force a host to process a suspended
plugin. This covers the processor handoff; manual keyboard mouse interaction still
requires native UI/host proof.

The core render suite writes `native-preset-loading-contract.json`, which loads
a native preset through the normal preset preparation path and measures the
sine pitch after A1 octave and a selected modulation destination are applied.
The renderer loads independent filters/envelopes, both LFOs, native destinations,
selected routes and expanded FX controls from that same prepared state.

`native-global-routing.json` compiles a phaser center route through the actual
write adapter, loads the parameter state, checks an audible wet-render change,
preserves the visible base frequency and restores the deterministic baseline
after clearing the route.

The core render suite also writes `wet-difference-contract.json`. Audible distortion
with delay and reverb disabled must pass. The same rendered audio with zero
declared tail metadata must also pass the audibility check. Identical samples with a claimed
tail and disabled effects must fail the meaningful wet-difference contract.
The existing max/RMS difference thresholds remain unchanged. A time-based FX
tail is checked separately where the patch actually enables delay or reverb.

These checks must pass on the integrated build before packaging. Earlier
Ableton evidence records earlier binaries. Fresh AU/VST3 scan, playback,
automation, save/reopen, offline/realtime, buffer/sample-rate and UI checks are
still required for this rebuild.

## Original Reference Evidence

Original Sylenth recordings and original lossless captures are not available in
this worktree. `tests/references/sylenth-reference-manifest.json` records that
absence explicitly. No local synthia render is used as a Sylenth golden, and
passing patch-recreation renders does not establish external fidelity.

The checked-in reference manifest is an acquisition checklist, not a passing
fixture. Populate every required case with an original-plugin capture and its
provenance before evaluating fidelity. Preserve the original version, host,
patch settings, fixed MIDI input, sample rate, buffer size and tempo. Reference
paths are relative to the manifest directory. Each original requires its
SHA-256, `product: Sylenth1`, `capture_origin: original_plugin` and `captured_by`.
Audio cases require explicit approved max-absolute and RMS difference limits
with an approver and reason. No audio tolerance is inferred from local tests.

```bash
python3 scripts/compare-reference-audio.py \
  --manifest tests/references/sylenth-reference-manifest.json \
  --output build/reports/reference/audio.json
python3 scripts/compare-reference-images.py \
  --manifest tests/references/sylenth-reference-manifest.json \
  --output-dir build/reports/reference/images
python3 tests/tools/ReferenceComparisonTest.py
```

Both reference commands currently return exit status 1 and write per-case
missing-reference errors. Missing files, provenance, hashes, capture settings
or audio limit approval also fail. Self-tests use small synthetic files solely
to validate comparison behavior; they are not reference evidence.

Audio comparison accepts uncompressed integer PCM WAV, requires identical
sample rate/channel count/frame count, and records per-channel correlation,
RMS and maximum sample differences. It never aligns, trims, resamples or gain
matches. Exact timing and amplitude remain observable. Reference capture must
include release and effects tails; comparing a shortened file fails.

Visual comparison accepts non-interlaced 8-bit RGBA PNG at identical resolution.
Convert other lossless source formats explicitly without resizing before
comparison, while retaining the source capture and documenting conversion.
There is no visual tolerance. All RGBA channels are compared, including alpha.
Only an explicitly approved branding rectangle may be excluded: each mask
requires integer `x`, `y`, `width`, `height`, `purpose: branding`, `approved_by`
and `reason`. There are no masks in the checked-in manifest. A mask that
excludes the whole image fails. Controls, panels and spacing must remain in the
comparison.

Reference and candidate must be independent files. Same paths, symlinks and
hardlinks to the same file fail. Report and difference-image destinations must
not alias any capture or the manifest. Musical audio captures must be non-silent;
a deliberately silent case requires `silence_approval` with an approver and
reason.

Image reports include changed/compared/masked pixel counts, changed fraction,
maximum/mean/RMS channel differences, both file hashes and the approved mask
list. A difference PNG is written for each comparable case; blue pixels mark
excluded branding. Resolution mismatch or any unmasked pixel difference fails
with exit status 1. These metrics expose differences; they do not certify
perceptual or interaction equivalence.

`SynthiaReferenceComparisonTest` exercises exact matches, one-channel and alpha
changes, timing changes, frame/sample-rate/resolution mismatch, approved mask
boundaries, invalid masks, aliases, capture overwrite protection, silent audio and
missing-reference command failures. It uses only
Python's standard library.

## Native Validation Commands

Browser/Wasm, AI generation and conversational editing are deferred. The current
acceptance contract is `SPEC.md` and `CLASSIC_PARITY.md`. Original-product fidelity,
local behavior, current host execution and distribution proof remain separate.

Current commands:

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

- unique parameter IDs (the current inventory is reported by `--list-parameters`),
- valid defaults and ranges,
- APVTS state round-trip,
- layer and oscillator-slot defaults, arp/chord defaults, saved preset serialization, inactive-slot no-op behavior, audible A2/B1/B2 rendering, and layer mute/solo behavior,
- factory preset files and six curated Phase 1 patch-recreation render cases,
- no unknown preset parameter IDs,
- TransMod slot objects use valid slot IDs, source/scaler choices, depth domains, and destination IDs.
- Init, Reset, and seedable Randomize commands prepare ordinary APVTS state without mutating live state before replacement.
- Preset workflow helpers cover metadata-aware writes, no-clobber create-only safe-save rejection, dirty-state comparison against immutable baseline fingerprints, and local A/B compare slot capture/recall without mutating live state before replacement.
- The classic editor has deterministic captures for all eight LCD pages, Part A/B and 2x scale. Adjacent binding reports check every visible control against the registry and APVTS. These captures do not establish pixel identity with the original; mouse interaction and host lifecycle are separate checks.
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
- modulation route model catalogs and derived route rows are covered by `SynthiaContractTest`, including source/scaler IDs, destination IDs, active route filtering, and normalized `transmod.N.depth` cutoff contribution.
- `SynthiaRender --modulation-route-render-test` compiles a route write through `ModulationRouteModel`, applies it to the unused `transmod.8.*` slot, proves the rendered audio changes, then applies a clear-slot edit and proves the render returns to baseline within deterministic tolerances.
- preset browser metadata validation, saved user preset browser metadata, factory/user source summaries, sidecar favorite add/remove behavior, search/category/tag/favorite filtering, and browser catalog facets are covered by `SynthiaContractTest`.
- APVTS automation-readiness is covered by `SynthiaContractTest`: every registry parameter must be exposed by APVTS, remain host-automatable when marked automatable, match its declared float/bool/choice type, and accept host-notifying default writes. The 2026-06-07 automation record is historical; this candidate needs its own AU/VST3 host proof.
- MIDI controller-map persistence is covered by `SynthiaContractTest`; learned CC capture, persistence, value application, and Forget behavior in AU and VST3 are described in historical Ableton records; current-candidate host proof is tracked separately.
- FX bypass stays null-equivalent to dry rendering when globally bypassed, disabled expanded-rack modules are dry-equivalent, phaser/EQ/compressor/distortion-mode processing is finite and measurably audible when enabled, tempo-synced delay reports exact sample timing at test tempo, FX tail length is reported from the active time-based FX parameters, and wet output remains finite, non-clipping, and measurably different from its dry reference.
- `SynthiaRender --offline-realtime-compare-test` renders the same native `FX Space 01` state with 64- and 512-sample processing blocks, splits blocks at exact MIDI event samples, checks finite non-clipping output, and requires deterministic waveform equivalence. This is a standalone processing-boundary check. Fresh Ableton offline bounce versus realtime resampling remains separate; the 2026-06-07 host proof in `docs/host-validation/ableton-smoke.md` records an earlier binary.
- `SynthiaRender --randomize-test` prepares seedable bounded randomized APVTS state through `PresetManager`, renders each seed through the standalone engine as prepared, rejects malformed seed lists, and checks finite non-silent non-clipping output.
- `SynthiaRender --suite core` runs the standalone smoke, parameter, preset, voice, oscillator, filter, modulation, modulation route render, offline/realtime compare, randomize render, dry pluck, wet pluck, LFO ablation, and determinism reports in one command.
- `SynthiaRender --suite patch-recreation` renders `Pluck Core 01`, `Supersaw Stack 01`, `Bass Wub 01`, `Pad Wide 01`, `Arp Motion 01`, and `FX Space 01`, writes WAV/report artifacts for each, and checks finite non-clipping output plus meaningful wet-versus-dry FX differences. The pad uses `fixtures/midi/held-pad-triad.mid`, a staggered sustained triad lasting 3.75 seconds, so its slow envelopes reach a useful level. Other patches use the overlap-pluck fixture. An explicitly supplied non-default fixture is used for all patches. Per-patch reports record the actual fixture. The arp/chord patch also asserts that `SynthRender` applies preset-loaded `arp.*` and `chord.*` state.
- `SynthiaRenderCoreSuite` runs the core suite under CTest.
- `SynthiaRandomizeRenderTest` runs the randomized preset render proof under CTest.
- `SynthiaModulationRouteRenderTest` runs the modulation route write/render/clear proof under CTest.
- `SynthiaOfflineRealtimeCompareTest` runs the standalone same-state processing-block comparison under CTest.
- `SynthiaPatchRecreationSuite` runs the patch-recreation suite under CTest.

Preset render validation is expected to fail if the preset file is missing, the `.SynthiaPreset` envelope or nested preset payload is invalid, the MIDI fixture is missing, the fixture is not a valid MIDI file, or the fixture has no note events.

## Validation Profiles

### DSP Primitive Checks

The low-level test library includes additional oscillator/filter primitives that are not public classic controls. Their tests do not imply those controls are exposed by the editor.

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
- `pluck-core-01-wet.json`: wet factory pluck metrics plus WAV artifact path, FX mode, delay division, tempo-synced delay samples, tail length, post-event render length, and wet-versus-dry difference metrics.
- `randomize.json`: seed list, per-seed prepared/rendered status, prepared FX-enabled state, peak, RMS, nonzero sample count, invalid sample count, and pass/fail for bounded randomized state render proof.
- `modulation-route-render.json`: route write edits, clear-slot edits, active route count, baseline/routed/cleared peak and RMS, routed audio-difference thresholds, clear-to-baseline tolerances, and pass/fail for route creation plus clear-slot restore.
- `offline-realtime-compare.json`: 64/512-sample block sizes, both render peaks, max/RMS waveform differences, explicit deterministic tolerances and `same_state_equivalent` pass/fail (report schema 2).
- `lfo-ablation.json`: compares per-voice LFO and mono LFO renders using note-local LFO spread and audio difference.
- `determinism.json`: renders the dry pluck twice and compares `max_abs_diff`, `rms_diff`, and `peak_delta` against fixed tolerances.
- `artifacts/*.wav`: retained dry/per-voice/mono render WAVs.
- `failures/*.wav`: written only when deterministic repeat comparison fails.

Current patch-recreation-suite artifacts:

- `summary.json`: aggregate pass/fail for the curated Phase 1 patch set.
- `<preset-id>-wet.json`: per-preset render metrics, FX mode, note-local LFO spread where applicable, and wet-versus-dry difference metrics.
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
- MIDI Learn and Forget from per-control context menus, plus mapped CC playback against at least one continuous and one stepped parameter.

Historical Ableton proof includes AU/VST3 `Layer A Level` automation record/playback plus Master offline bounce versus realtime resampling content comparison with envelope alignment, per-channel filtered-band correlation thresholds, and negative controls. Strict waveform/null-test equivalence is not claimed.

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
- `fx_mode`, `delay_division_beats`, `tempo_synced_delay_samples`, `fx_tail_seconds`, `post_last_event_seconds`, `wet_dry_max_abs_diff`, `wet_dry_rms_diff`, and `wet_meaningful_passed`: wet-render proof for the onboard fixed-order FX path.
- `mod_slot_schema_passed`: modulation harness check that canonical preset `mod_slots` objects are loaded into runtime TransMod slots.
- `routed_max_abs_diff`, `routed_rms_diff`, `cleared_max_abs_diff`, and `cleared_rms_diff`: modulation route render proof metrics for created-route audibility and clear-slot determinism.
- `realtime_block_samples`, `offline_block_samples`, `max_absolute_tolerance`, `rms_tolerance`, `both_finite` and `same_state_equivalent`: standalone same-state block-boundary comparison fields.
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
preset first to capture its native controls, and
`SYNTHIA_UI_SNAPSHOT_QUIT=1` exits the standalone after writing the file:

```bash
SYNTHIA_UI_SNAPSHOT=/tmp/ui.png \
SYNTHIA_UI_SNAPSHOT_QUIT=1 \
SYNTHIA_UI_SNAPSHOT_PRESET="presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset" \
./build/SynthiaPlugin_artefacts/Debug/Standalone/Synthia.app/Contents/MacOS/Synthia
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

Reports use `schema_version: 1` and a per-report `suite` field, except the
same-state realtime/offline block comparison, which uses schema 2. Core-suite reports live under the supplied output directory and are disposable build artifacts.

Failed renders should keep audio artifacts and JSON reports for inspection.

## First Validation Milestone

The first implementation milestone is not complete until:

- standalone renders the factory pluck fixture,
- oscillator/filter/envelope/LFO tests pass,
- AU loads in Ableton,
- VST3 loads in Ableton,
- Ableton state restore preserves the preset,
- per-voice LFO ablation test passes.

## Integrated Evidence - 2026-10-09

The integrated Debug quality gate passed all 11 CTest entries and all 17 standalone core reports. The current reference-tool test has seven synthetic positive/negative cases; these are tool correctness tests, not original-plugin fidelity evidence.

All eight LCD pages plus Part B and 2x exports have valid binding reports and clean capture logs. Repeated explicit Init captures are byte-identical to one another. This is a deterministic local-capture result, not equality to Sylenth1. A missing supplied snapshot preset exits with failure and produces no PNG.

Snapshot mode initializes program 0/Init when `SYNTHIA_UI_SNAPSHOT_PRESET` is absent. Supply an explicit current preset path for any other state. Normal standalone/host restore behavior is unchanged outside snapshot mode.

Current host qualification is tracked in `host-validation/native-rebuild-2026-10-09.md`. Optimized packaging, real-host acceptance, original-reference fidelity, and public distribution remain separate gates.
