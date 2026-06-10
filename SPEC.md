# synthia Instrument Specification

Status: Draft v1 (research turn)

Purpose: Define a lab-built macOS/Ableton software instrument whose Phase 1 goal is to recreate the Sylenth1 vintage VST experience for modern AU/VST3 hosts, then extend it with AI-assisted sound and arpeggio generation and conversational sound editing.

## Normative Language

The key words `MUST`, `MUST NOT`, `REQUIRED`, `SHOULD`, `SHOULD NOT`, `RECOMMENDED`, `MAY`, and `OPTIONAL` in this document are to be interpreted as described in RFC 2119.

`Implementation-defined` means the behavior is part of the implementation contract, but this specification does not prescribe one universal policy. Implementations MUST document the selected behavior.

## Evidence Language

This specification separates product requirements from research confidence:

- `Observed` means the statement is grounded in vendor documentation, manuals, host documentation, or directly inspected local material.
- `Inferred` means the statement is a reasoned design conclusion from observed material.
- `Unknown` means the statement is unresolved and MUST NOT be treated as confirmed behavior.

The Phase 1 target is an approved Sylenth recreation direction for modern hosts, grounded in `Sylenth1Manual.pdf`, `research/sylenth1-screenshots/`, and `docs/modern-synthia-baseline.md`. Older Strobe/pluck research remains supporting background for the current engine, not the product destination.

## 1. Problem Statement

synthia is a software instrument for producers, sound designers, and coding agents in a lab context. The product mission is to rebuild the Sylenth1 virtual analog workflow that producers still love, make it run well on today's macOS/Ableton systems, and then add AI-native workflows that legacy VSTs never had.

The instrument solves these problems:

- Sylenth1 remains a favorite vintage VST workflow, but it is no longer an actively evolving modern macOS-first production dependency.
- Producers want the immediacy of the original Sylenth architecture: A/B parts, four oscillator slots, fast filter/envelope/modulation access, integrated arp/effects, and direct preset workflows.
- Modern users also expect richer modulation inspection, browser metadata, high-DPI UI behavior, deterministic host restore, and strong Ableton AU/VST3 proof.
- The next product leap is AI-assisted sound design: useful randomized patches, generated arpeggios/chord movement, reference-sound recreation, and natural-language edits.

The product boundary:

- synthia owns synthesis, modulation, preset state, host integration, rendering behavior, validation tools, UI/UX, AI-assisted generation, and conversational control.
- synthia does not own DAW arrangement, mastering chains, external sample libraries, or user-provided copyrighted reference audio outside local analysis and preset-generation workflows.
- synthia MAY include onboard FX and arpeggiator/chord-generation features, but the core synth MUST remain strong enough to validate without hiding weak oscillator/filter/envelope behavior behind effects.

Success means:

- The plugin loads and behaves correctly in Ableton Live on supported macOS systems as AU and VST3.
- A competent user recognizes the Phase 1 workflow as a faithful modern rebuild of the vintage Sylenth experience.
- A user can browse, edit, save, automate, and restore presets with the speed expected from a production VST.
- Phase 2 AI generation creates musically useful patches and arp/chord ideas as editable parameter state.
- Phase 3 conversational control can translate text and reference-sound intent into safe, reversible synth changes.
- The codebase remains testable, realtime-safe, and portable enough for future Windows/Linux plugin targets.

## 2. Goals and Non-Goals

### 2.1 Goals

- Phase 1: recreate the Sylenth-style virtual analog architecture and workflow as the main product baseline.
- Support A/B parts, four oscillator slots, per-slot voices/unison, tuning, detune, phase/retrigger/invert, pan/stereo spread, mixer, filters, modulation, arpeggiator, effects, preset banks, and performance controls.
- Keep the current oscillator stack, nonlinear filter, modulation, and validation harness as useful scaffolding until the Sylenth-level architecture replaces or absorbs them.
- Provide polyphonic, monophonic, legato, and unison voice modes.
- Provide per-voice amp envelope, mod envelope, LFO, ramp, glide, velocity glide, random-on-note, voice index, and unison index sources.
- Provide an 8-slot TransMod-style modulation layer where each slot routes one source, optionally scaled by another source, to many destinations.
- Implement semitone-domain pitch and filter-cutoff modulation.
- Implement a nonlinear OTA-cascade-style multimode filter with diode-feedback clipping behavior and drive/resonance interaction.
- Provide amp-stage waveshaping, voice pan, unison pan, and analog-style instability controls.
- Provide a compact production UI that intentionally recreates Sylenth's visual hierarchy, density, panel rhythm, and control workflow as closely as the current real model-backed controls allow.
- Phase 2: add AI-assisted patch randomization, sound generation, chord movement, and arpeggio generation.
- Phase 3: add conversational VST control for text edits and reference-audio recreation workflows.
- Ship AU and VST3 plugin targets for macOS.
- Support Universal macOS binaries for Apple Silicon and Intel Macs.
- Provide a standalone target for validation and preset/render tooling.
- Provide deterministic preset serialization, migration, and host state restore.
- Provide automated validation for oscillators, envelopes, LFOs, filters, modulation, arpeggiator timing, preset generation, AI edit safety, host state, and reference renders.

### 2.2 Non-Goals

- Bit-identical binary emulation of any third-party plugin.
- Requiring a licensed Sylenth install at runtime.
- Shipping third-party factory presets, samples, marks, logos, screenshots, manual content, or product assets as synthia-owned content without explicit legal approval.
- Turning the product into a generic wavetable workstation before the Phase 1 Sylenth rebuild is complete.
- VST2 support in the first release.
- AAX support in the first release.
- Built-in DAW project templates.
- Cloud sync, accounts, copy protection, telemetry, or network services in v1.

## 3. System Overview

### 3.1 Main Components

1. `Plugin Shell`
   - Hosts the audio processor and editor.
   - Exposes AU, VST3, and standalone targets through JUCE/CMake.
   - Owns host state serialization and plugin metadata.

2. `Parameter Registry`
   - Defines stable parameter IDs, ranges, units, display text, smoothing policy, automation flags, and preset serialization behavior.
   - Owns migration between preset/schema versions.

3. `Voice Allocator`
   - Maps incoming MIDI notes to voices.
   - Handles mono, legato, poly, unison, retrigger, release, stealing, sustain pedal, all-notes-off, and panic behavior.

4. `Voice Engine`
   - Owns per-voice oscillator phases, envelopes, LFO, ramp, glide, random source, modulation values, filter state, and pan.

5. `Oscillator Stack`
   - Produces band-limited saw and pulse waveforms, sub waveforms, noise, stack detune, hard sync, and gain compensation.

6. `Mixer`
   - Mixes main, pulse, noise, and sub sources before filter drive.
   - Applies loudness compensation so stack and waveform changes do not create uncontrolled gain jumps.

7. `Filter and Drive`
   - Implements required filter modes, nonlinear feedback clipping, self-oscillation, semitone cutoff, keytracking, drive, and resonance compensation.

8. `Amp and Stereo Stage`
   - Applies amp envelope, amp drive, level, voice pan, unison pan, macro spread, and final denormal protection.

9. `Modulation System`
   - Evaluates direct modulation and TransMod slots at the defined control/audio rates.
   - Produces deterministic per-voice modulation for overlapping notes.

10. `FX Section`
   - Provides OPTIONAL onboard saturation, delay, reverb, and chorus.
   - MUST be bypassable and excluded from dry-core validation.

11. `Preset System`
   - Reads, writes, validates, migrates, and restores presets.
   - Provides factory patches and user patches without third-party proprietary data.

12. `Validation Harness`
   - Renders offline tests from the standalone target.
   - Computes aliasing, timing, tuning, modulation, stereo width, loudness, and regression metrics.

13. `UI Editor`
   - Presents synthesis controls, modulation slots, macro controls, preset navigation, meters/scopes, and diagnostics.

### 3.2 Architecture Layers

1. `Host Adapter Layer`
   - JUCE plugin wrappers, DAW interaction, buses, MIDI, automation, preset/state chunks.

2. `Realtime Audio Layer`
   - Audio-thread safe DSP and event scheduling.
   - MUST NOT allocate, block, lock, perform file I/O, log synchronously, or call network APIs on the realtime audio thread.

3. `Control Layer`
   - Parameter changes, smoothing, host automation, MIDI learn, macro mapping, UI gestures.

4. `Synthesis Layer`
   - Oscillator, mixer, filter, amp, voice state, FX, oversampling, denormal handling.

5. `Modulation Layer`
   - Source evaluation, gate modes, direct modulation, TransMod routing, source scaling, voice/unison variation.

6. `Persistence Layer`
   - Plugin state, presets, migration, factory/user library scanning.

7. `Validation Layer`
   - Offline renders, fixtures, spectral analysis, host smoke tests, CI checks.

### 3.3 External Dependencies

- JUCE and CMake for plugin build targets.
- Steinberg VST3 SDK through JUCE-supported VST3 builds.
- Apple Audio Unit infrastructure for AU builds.
- Ableton Live as the primary DAW validation host.
- macOS code signing and notarization tooling for distributable builds.
- Optional DAWs for compatibility validation: Logic Pro, Reaper, Bitwig Studio, Cubase.
- Optional reference systems for licensed Strobe v1 / Strobe2 comparison, when legally available.
- Optional Python or C++ analysis tooling for FFT, envelope, and reference-render metrics.

## 4. Core Domain Model

### 4.1 Entities

#### 4.1.1 Plugin Build

One compiled distributable artifact for one plugin format and architecture set.

Fields:

- `format` (enum)
  - Values: `AU`, `VST3`, `Standalone`.
- `architectures` (list)
  - Required macOS values: `arm64`, `x86_64`.
- `version` (semantic version string)
  - MUST match plugin metadata and preset schema compatibility rules.
- `bundle_identifier` (string)
  - MUST be globally unique and MUST NOT use third-party brand identifiers.
- `manufacturer_code` (4-character string)
  - REQUIRED for AU compatibility.
- `plugin_code` (4-character string)
  - REQUIRED and MUST remain stable after public release.
- `signed` (boolean)
- `notarized` (boolean)

#### 4.1.2 Parameter

Stable automation and preset field exposed by the plugin.

Fields:

- `id` (string)
  - Stable lower-kebab-case or lower-dot-case ID.
  - MUST NOT be changed or reused after public release.
- `name` (string)
  - Human-readable UI/host label.
- `group` (enum)
  - Examples: `voice`, `osc`, `mixer`, `filter`, `amp`, `lfo`, `ramp`, `env`, `transmod`, `fx`, `macro`, `global`.
- `unit` (enum)
  - Examples: `normalized`, `dB`, `Hz`, `semitones`, `cents`, `milliseconds`, `percent`, `enum`, `boolean`.
- `range` (object)
  - Physical minimum, maximum, default, step, skew curve.
- `automatable` (boolean)
- `smooth_ms` (number)
  - Audio/control smoothing time.
- `audio_rate_safe` (boolean)
  - Whether audio-rate modulation MAY target this parameter.
- `preset_serialized` (boolean)

Validation rules:

- Every automatable parameter MUST have a stable ID, range, default, display conversion, and parse conversion.
- Discrete enum parameters MUST define all values and migration behavior for removed values.
- Parameters that can cause zipper noise SHOULD use smoothing unless the click is musically intentional and documented.

#### 4.1.3 Preset

Serializable instrument state outside host-specific project state.

Fields:

- `schema_version` (integer)
- `plugin_min_version` (string)
- `id` (string)
  - Stable lower-kebab-case identifier.
- `display_name` (string)
  - MUST NOT include unlicensed third-party marks in shipped factory content.
- `author` (string)
- `description` (string)
- `tags` (list of strings)
- `parameters` (map `parameter_id -> value`)
- `mod_slots` (list of TransMod slot objects)
- `macros` (list of macro objects)
- `metadata` (object)
  - MAY include reference notes for internal development presets.

Validation rules:

- Missing parameters use current defaults only during preset migration, not silently at runtime for invalid presets.
- Unknown parameter IDs MUST be preserved in a forward-compatibility blob when possible.
- Invalid numeric values MUST be clamped only during explicit migration and MUST emit a validation warning.

#### 4.1.4 Voice

One active or reusable synthesis voice.

Fields:

- `state` (enum)
  - Values: `Idle`, `Starting`, `Held`, `Releasing`, `Stolen`, `Draining`.
- `voice_id` (integer)
- `midi_note` (integer)
- `velocity` (float `0..1`)
- `channel` (integer `1..16`)
- `start_sample_offset` (integer)
- `release_sample_offset` (integer or null)
- `unison_index` (integer)
- `unison_count` (integer)
- `voice_index_uni` (float `0..1`)
- `voice_index_bi` (float `-1..1`)
- `unison_index_uni` (float `0..1`)
- `unison_index_bi` (float `-1..1`)
- `random_on_note` (float `-1..1`)
- `oscillator_state` (OscillatorStack state)
- `filter_state` (Filter state)
- `amp_env_state` (Envelope state)
- `mod_env_state` (Envelope state)
- `lfo_state` (LFO state)
- `ramp_state` (Ramp state)
- `glide_state` (Glide state)
- `pan` (float `-1..1`)

Lifecycle notes:

- Voices MUST reset note-local modulation according to the selected gate mode.
- Voices MUST support overlapping notes with independent LFO/envelope/filter trajectories.
- Voice stealing MUST avoid clicks as much as practical through fade, release-priority, or zero-crossing strategies.

#### 4.1.5 Voice Mode

Policy for note allocation and retrigger behavior.

Fields:

- `mode` (enum)
  - Values: `Mono`, `MonoLegato`, `Poly`, `Unison`.
- `polyphony` (integer)
  - Required range: `1..32`.
- `unison_count` (integer)
  - Required range: `1..8`; Phase 1 voice math SHOULD make per-slot voices, note unison, and polyphony explicit.
- `steal_policy` (enum)
  - Values: `Oldest`, `Quietest`, `ReleaseFirst`, `FarthestPitch`.
- `retrigger` (boolean)
- `legato_glide` (boolean)
- `sustain_pedal_enabled` (boolean)

#### 4.1.6 Oscillator Stack

One super-oscillator per voice with multiple stacked phase accumulators.

Fields:

- `stack_count` (integer `1..5` REQUIRED for v1 profile)
- `stack_detune` (float `0..1`)
- `pitch_semitones` (float)
- `fine_cents` (float)
- `saw_level` (float `0..1`)
- `pulse_level` (float `0..1`)
- `noise_level` (float `0..1`)
- `pulse_width` (float `0.05..0.95`)
- `sub_wave` (enum)
  - Values: `Sine`, `Triangle`, `Saw`, `Pulse`, `Mixed`.
- `sub_octave` (enum)
  - Values: `-1`, `-2`, `-3`.
- `sub_level` (float `0..1`)
- `sub_pulse_width` (float `0.05..0.95`)
- `sync_amount` (float `0..1`)
- `phase_reset` (enum)
  - Values: `Off`, `Note`, `Voice`, `Random`.
  - `Off` SHOULD be the v1-like default unless a later extension profile requires phase reset.

Validation rules:

- Saw and pulse MUST be band-limited at normal sample rates.
- Noise source MUST be deterministic under seeded offline tests.
- Stack detune MUST be symmetric around the center pitch unless an extension explicitly adds chord/spread modes.

#### 4.1.7 Filter

Nonlinear multimode filter after the oscillator mixer.

Fields:

- `enabled` (boolean)
- `mode` (enum)
  - Required v1 modes: `L2`, `L4`, `B2`, `B4`, `H2`, `H4`, `Peak2`, `Notch2`, `Notch4`.
  - Full profile SHOULD include compound octave-spaced modes.
- `cutoff_semitones` (float)
  - Musical cutoff coordinate; converted to Hz internally.
- `resonance` (float `0..1`)
- `drive` (float `0..1`)
- `keytrack_depth` (float)
- `self_oscillation_enabled` (boolean)
- `oversampling` (enum)
  - Values: `Off`, `2x`, `4x`, `8x`; realtime defaults SHOULD balance CPU and aliasing.

Invariants:

- Cutoff modulation MUST be accumulated in semitone space before Hz conversion.
- Cutoff Hz MUST be clamped to a stable range below Nyquist in the active oversampled rate.
- Drive MUST interact with effective resonance; a clean linear SVF alone is not conformant.

#### 4.1.8 Envelope

Gateable ADSR source.

Fields:

- `attack_ms` (float)
- `decay_ms` (float)
- `sustain` (float `0..1`)
- `release_ms` (float)
- `curve` (enum)
  - Values: `Exponential`, `Linear`, `Snappy`.
- `gate_mode` (enum)
  - Values: `Poly`, `PolyOn`, `Mono`, `Song`.
- `stage` (enum)
  - Values: `Idle`, `Attack`, `Decay`, `Sustain`, `Release`.
- `value` (float)

Required envelopes:

- `amp_env`
- `mod_env`

#### 4.1.9 LFO

Gateable low-frequency modulation source.

Fields:

- `shape` (enum)
  - Required values: `Sine`, `Triangle`, `SawUp`, `SawDown`, `Square`, `SampleHold`, `Noise`, `Step`.
  - Recommended values: `Arc`, `TriS`, `TriC`.
- `rate_mode` (enum)
  - Values: `Hz`, `Sync`.
- `rate_hz` (float)
- `sync_division` (enum/string)
  - Examples: `1/16`, `1/8`, `1/8D`, `1/4`, `1/2`, `1 bar`.
- `phase_degrees` (float `0..360`)
- `gate_mode` (enum)
  - Values: `Poly`, `PolyOn`, `Mono`, `Song`.
- `mono` (boolean)
- `swing` (float `0..1`)
- `step_count` (integer `2..16`)
- `step_smooth` (float `0..1`)
- `step.1` through `step.16` (float `-1..1`)
- `value` (float `-1..1`)

Invariants:

- Per-voice LFO mode MUST maintain independent phase for overlapping voices.
- Gated modes MUST define whether phase resets on note-on, gate-on, or transport.
- Mono LFO mode MUST be audible as a distinct behavior in validation.
- `Step` shape MUST spread one LFO cycle across the active `step_count` steps in
  order, holding each step's value; `step_smooth` crossfades only the trailing
  fraction of each step into the next so low values keep a gated character.
- Editing step values, count, or smooth while notes are held MUST NOT reset LFO
  phase (the running cycle keeps its position).

#### 4.1.10 Ramp

One-shot or looping ramp modulation source.

Fields:

- `enabled` (boolean)
- `mode` (enum)
  - Values: `OneShot`, `Loop`, `Sync`.
- `delay_ms` (float)
- `rise_ms` (float)
- `curve` (enum)
- `gate_mode` (enum)
- `value` (float `0..1`)

#### 4.1.11 Modulation Source

A normalized source that can drive direct routes or TransMod slots.

Required sources:

- `LFO`
- `Ramp`
- `ModEnv`
- `AmpEnv`
- `Keytrack`
- `Velocity`
- `VelocityGlide`
- `PitchBend`
- `ModWheel`
- `Aftertouch`
- `VoiceUni`
- `VoiceBi`
- `UnisonUni`
- `UnisonBi`
- `RandomOnNote`
- `Macro1`
- `Macro2`
- `Macro3`
- `Macro4`

Validation rules:

- Each source MUST define polarity, range, update rate, reset behavior, and voice/global scope.
- Voice and unison sources MUST be stable for a voice lifetime.

#### 4.1.12 TransMod Slot

One source-to-many modulation route.

Fields:

- `slot_id` (integer `1..8` REQUIRED for v1 profile)
- `enabled` (boolean)
- `source` (Modulation Source)
- `scaler` (Modulation Source or `None`)
- `depths` (map `parameter_id -> depth`)
- `depth_domain` (enum)
  - Values: `Physical`, `Normalized`.

Invariants:

- One slot MAY target many parameters.
- A scaler multiplies the source before applying destination depths.
- Slot evaluation MUST be deterministic and sample-accurate where the destination requires it.

#### 4.1.13 Direct Modulation Route

Dedicated fast route from known sources to high-value destinations.

Required routes:

- `keytrack -> oscillator pitch`
- `lfo -> oscillator pitch`
- `mod_env -> oscillator pitch`
- `keytrack -> pulse width`
- `lfo -> pulse width`
- `mod_env -> pulse width`
- `keytrack -> filter cutoff`
- `lfo -> filter cutoff`
- `mod_env -> filter cutoff`

#### 4.1.14 Macro

Performance control mapped to parameters and modulation depths.

Fields:

- `macro_id` (string)
- `display_name` (string)
- `value` (float `0..1`)
- `assignments` (list)
  - Each assignment contains `target_id`, `min`, `max`, `curve`, and `polarity`.

Required macros for factory pluck profile:

- `motion`
- `width`
- `drive`
- `space`

#### 4.1.15 FX Chain

Optional post-synth processing chain.

Fields:

- `enabled` (boolean)
- `saturation` (module)
- `delay` (module)
- `reverb` (module)
- `chorus` (module OPTIONAL)
- `order` (list)

Rules:

- FX MUST be bypassable as a group.
- Dry-core validation MUST run with FX disabled.
- FX parameter modulation MAY be added after core conformance.

### 4.2 Stable Identifiers and Normalization Rules

- Product working name: `synthia`.
- Factory preset IDs MUST be lower-kebab-case.
- Parameter IDs MUST be stable after public release and MUST NOT encode UI placement.
- Internal research preset names MAY reference research targets, but shipped display names MUST avoid unlicensed third-party names and marks.
- Plugin bundle IDs MUST use an owned reverse-DNS prefix.
- File paths MUST be normalized to absolute paths before use in tests and packaging scripts.
- Preset tags MUST be lowercase ASCII slugs.
- Audio fixture names SHOULD include sample rate, tempo, and profile.

### 4.3 Derived Data and Invariants

- `effective_pitch_hz` derives from MIDI note, pitch bend, glide, oscillator pitch, fine tune, unison detune, stack detune, and modulation.
- `effective_cutoff_hz` derives from semitone cutoff after all cutoff modulation and clamping.
- `effective_resonance` derives from resonance and drive compensation.
- `voice_pan` derives from base pan, voice spread, unison spread, random/voice sources, and macro width.
- The realtime audio path MUST be deterministic for the same MIDI, parameters, preset, sample rate, block segmentation, and random seed.
- No DSP module may emit NaN or infinity; invalid states MUST be sanitized.
- Silence/release tails MUST avoid denormal CPU spikes.

## 5. User, Actor, and Permission Model

### 5.1 Actors

- `Producer`
  - Loads plugin in a DAW, plays MIDI, tweaks controls, saves presets, renders audio.
- `Sound Designer`
  - Builds factory/user presets and reference patches.
- `Plugin Host`
  - Calls lifecycle, process, parameter, state, and UI methods.
- `Installer`
  - Places plugin bundles and preset content in the expected local folders.
- `Developer`
  - Builds, tests, signs, notarizes, packages, and debugs the plugin.
- `Validation Harness`
  - Runs deterministic offline tests and generates render reports.
- `Support Operator`
  - Reads diagnostics and helps users resolve installation or host issues.

### 5.2 Permissions

- Local users MAY create, edit, duplicate, delete, import, and export user presets.
- Factory presets SHOULD be treated as read-only by default.
- The plugin MUST NOT require network credentials, accounts, or cloud permissions in v1.
- The standalone validation tool MAY write reports and renders to configured local output directories.
- Installer scripts MUST NOT modify DAW project files.

### 5.3 Trust Boundaries

- DAW host input is trusted only as API input, not as valid musical state.
- Preset files are untrusted user input and MUST be parsed defensively.
- Filesystem paths supplied by users or tests MUST be normalized and constrained by tool configuration.
- No audio-thread code may depend on UI thread state without lock-free synchronization.
- Source-use and release boundaries apply to all third-party historical references.

## 6. Workflow Specification

### 6.1 Install and Load in Ableton

Trigger: User installs a macOS build and opens Ableton Live.

Preconditions:

- The build contains AU and/or VST3 plugin bundles.
- The plugin is signed for local development or distribution as appropriate.
- Ableton plugin sources are enabled.

Steps:

1. Installer places AU in a supported Components folder and VST3 in a supported VST3 folder.
2. Ableton scans plugin folders.
3. Plugin reports metadata, format, category, buses, MIDI input, and parameters.
4. User creates a MIDI track and loads the instrument.
5. Plugin initializes with default preset and no active voices.

Postconditions:

- Plugin appears as a playable instrument.
- MIDI note input produces audio.
- Host automation list contains stable parameter names.

Failure behavior:

- If scan fails, plugin MUST provide enough logs or validation output to diagnose signature, architecture, missing dependency, or metadata issues.
- If an unsupported architecture is loaded, plugin MUST fail host scan cleanly.

### 6.2 Play Target Pluck

Trigger: Producer plays MIDI notes using the factory pluck preset or an equivalent user patch.

Preconditions:

- Plugin is loaded.
- Voice mode, oscillator, filter, envelopes, LFO, and modulation parameters are valid.

Steps:

1. Host sends MIDI note-on with sample offset.
2. Voice allocator assigns one or more voices.
3. Each voice initializes oscillator phases, envelopes, LFO/ramp gate states, glide, random-on-note, voice/unison indices, and pan.
4. Control layer computes parameter values and modulation sources for the block.
5. Oscillator stack renders band-limited waveforms.
6. Mixer applies waveform levels and gain compensation.
7. Filter computes semitone cutoff with keytrack, LFO, mod envelope, direct routes, and TransMod routes.
8. Nonlinear filter applies drive, feedback clipping, resonance, and selected mode.
9. Amp applies amp envelope, amp drive, pan, and level.
10. Voice sum is optionally processed by FX.
11. Output is returned to host.

Postconditions:

- Each overlapping note has note-local envelope and per-voice LFO movement unless a mono/global mode is selected.
- The dry signal remains musically usable without FX.

Failure behavior:

- If CPU budget is exceeded, plugin MUST avoid emitting invalid audio and SHOULD expose a CPU/voice diagnostic.
- If voice stealing occurs, plugin SHOULD minimize clicks.

### 6.3 Edit Sound

Trigger: User changes a UI control, host automation value, MIDI learn mapping, or macro.

Preconditions:

- Parameter exists and is writable.

Steps:

1. Control layer validates and normalizes input.
2. Parameter registry updates base value or modulation depth.
3. Smoother applies control changes according to parameter policy.
4. UI reflects actual effective value.
5. Host is notified of gesture begin/end for automation when applicable.

Postconditions:

- Automation and plugin state remain consistent.
- No zipper noise occurs on smoothed parameters.

Failure behavior:

- Invalid values are clamped or rejected according to parameter policy.
- Unknown parameter IDs from old presets enter migration handling.

### 6.4 Configure TransMod

Trigger: User assigns a source/scaler and depth from a TransMod slot to one or more destinations.

Preconditions:

- Slot ID is valid.
- Source and destination are compatible.

Steps:

1. User selects source.
2. User optionally selects scaler.
3. User adjusts one or more destination depths.
4. Modulation layer evaluates slot output per voice or globally as source scope requires.
5. UI indicates active modulation amount and destination polarity.

Postconditions:

- The slot can modulate many destinations from one source.
- Preset serialization records slot state.

Failure behavior:

- Incompatible audio-rate destinations MUST reject audio-rate source routing or downgrade to control-rate with visible indication.

### 6.5 Save, Load, and Migrate Preset

Trigger: User saves or loads a preset, or host restores plugin state.

Preconditions:

- Preset schema is parseable.

Steps:

1. Preset system parses JSON or host state.
2. Schema version is checked.
3. If older, migration transforms fields and records warnings.
4. Parameter values are validated.
5. Plugin applies state atomically on the control thread.
6. Host state dirty flag and UI update.

Postconditions:

- The same preset produces equivalent sound after reload within validation tolerance.

Failure behavior:

- Invalid preset MUST NOT crash the host.
- User-visible error describes parse, schema, unknown field, or invalid value class.

### 6.6 Validate Reference Render

Trigger: Developer or validation harness runs offline render tests.

Preconditions:

- Standalone target or test host is available.
- Fixture MIDI, tempo, preset, and sample rate are defined.

Steps:

1. Load preset and fixture.
2. Render dry core and optional FX variants.
3. Measure spectral, timing, tuning, modulation, loudness, and stereo metrics.
4. Compare against golden ranges.
5. Write report with pass/fail and measured values.

Postconditions:

- Regression report identifies which DSP or host behavior changed.

Failure behavior:

- Missing fixtures fail validation explicitly.
- Non-deterministic output above tolerance fails validation.

## 7. State Machines and Lifecycle

### 7.1 Plugin Lifecycle States

- `Constructed`
  - Object exists; no sample rate or block size.
- `Prepared`
  - Sample rate, block size, buses, oversampling, and buffers are initialized.
- `Processing`
  - Host may call realtime process.
- `Suspended`
  - Host has suspended audio processing.
- `Released`
  - Resources are released.

Transitions:

- `Constructed -> Prepared`: host calls prepare.
- `Prepared -> Processing`: first process call or explicit resume.
- `Processing -> Suspended`: host suspends or sample rate changes.
- `Suspended -> Prepared`: host prepares again.
- `Any -> Released`: host destroys plugin.

### 7.2 Voice Lifecycle States

- `Idle`
  - Available for allocation.
- `Starting`
  - Assigned note; sample-offset initialization pending or in progress.
- `Held`
  - Gate is active.
- `Releasing`
  - Note-off received; amp envelope release active.
- `Stolen`
  - Voice selected for stealing and fading/reinitializing.
- `Draining`
  - Voice tail finishing after all notes off or panic.

Terminal behavior:

- Voice returns to `Idle` when amplitude and filter tail fall below threshold or a maximum drain timeout is reached.

### 7.3 Envelope States

- `Idle`
- `Attack`
- `Decay`
- `Sustain`
- `Release`

Rules:

- Note-on in retrigger mode restarts envelope from the configured retrigger policy.
- Note-on in legato mode MAY avoid retriggering selected envelopes.
- Release from any active stage moves to `Release`.

### 7.4 LFO Gate States

- `FreeRunning`
- `WaitingForGate`
- `Gated`
- `Released`

Rules:

- `PolyOn` resets phase on note-on.
- `Poly` gates while note is held.
- `Mono` uses global phase for all voices.
- `Song` follows transport/tempo and is not note-local.

### 7.5 Preset Lifecycle States

- `Discovered`
- `Parsed`
- `Validated`
- `Migrated`
- `Loaded`
- `Invalid`

### 7.6 Idempotency and Concurrency Rules

- Repeated all-notes-off and panic events MUST be idempotent.
- Host state restore MUST be atomic from the audio thread perspective.
- Preset load during audio playback MUST crossfade, suspend, or apply lock-free parameter handoff without unsafe locks.
- UI edits and host automation conflicts MUST resolve by latest timestamp/order supplied by host and parameter gesture semantics.
- Offline render with identical inputs MUST be deterministic within floating-point tolerance.

## 8. Interfaces and Contracts

### 8.1 User Interface Contract

The UI MUST expose the instrument as a working synth, not a marketing surface.

Required views:

- `Header`
  - Preset selector, save/duplicate, undo/redo if implemented, output meter, panic, settings.
- `Voice`
  - Mono/poly/unison, polyphony, unison count, detune/spread, glide, retrigger, steal policy.
- `Oscillator`
  - Stack, detune, pitch, fine tune, sync, phase mode, saw, pulse, pulse width, noise, sub wave, sub octave, sub level.
- `Filter`
  - Mode, cutoff, resonance, drive, keytrack, envelope amount, LFO amount, oversampling.
- `Envelopes`
  - Amp ADSR and mod ADSR with compact visual curves.
- `LFO`
  - Shape, rate mode, rate/sync division, phase, gate mode, mono/per-voice, swing,
    step count, step smooth, and a live shape display. The `Step` shape MUST expose a
    drawable step grid bound to the real `lfo.step.*` parameters, and the display
    SHOULD animate a playhead from realtime engine state while voices sound.
- `Ramp`
  - Enable, mode, delay, rise, sync, curve.
- `TransMod`
  - Eight slots with source, scaler, destination depths, active modulation indicators.
- `Modulation assignment`
  - A drag-and-drop path from a modulation-source strip onto destination-bound
    knobs MUST compile into ordinary TransMod slot state (no hidden routing).
    Knobs whose parameter is a TransMod destination SHOULD show a halo ring for
    each active route covering the modulated range, and SHOULD offer in-place
    depth editing and route clearing that write the same `transmod.*` parameters
    as the TransMod slot editors.
- `Amp and Stereo`
  - Amp drive, level, pan, voice spread, unison spread, analog.
- `FX`
  - Saturation, delay, reverb, chorus bypass and mix controls.
- `Macros`
  - Motion, Width, Drive, Space.
- `Diagnostics`
  - Sample rate, format, architecture, CPU estimate, active voices, oversampling, denormal/NaN guard counters in debug builds.

UI rules:

- The UI SHOULD use the exact approved reference set, `Sylenth1Manual.pdf` plus every image listed in `research/sylenth1-screenshots/SOURCE_INDEX.md`, to recreate Sylenth's workflow, density, functional hierarchy, panel rhythm, control relationships, and preset interaction as faithfully as possible with the current real model-backed controls.
- The UI MUST use original native controls and source-use-safe assets. It MUST NOT copy protected third-party trade dress, logos, raster assets, screenshot backplates, product marks, or other source material unless explicit legal approval exists; unrelated references such as the white SH-101-like historical plugin look, Roland SH-101 panel, and FXpansion Strobe UI are also out of scope.
- Controls SHOULD use compact Sylenth-style production language: knobs/sliders for continuous values, menus for modes, toggles for binary options, tabs or panel selectors for views, and clear modulation badges.
- Text MUST remain readable at all supported scales.
- Every control MUST have a host parameter or documented local-only state.
- Automation gestures MUST be correct for mouse, keyboard, and controller edits.

### 8.2 Plugin Host Contract

Required plugin formats:

- AU for macOS.
- VST3 for macOS.
- Standalone for validation.

Audio/MIDI contract:

- Plugin type: instrument/synth.
- Audio input: none for v1 instrument profile.
- Audio output: stereo required; mono output MAY be supported.
- MIDI input: required.
- MIDI output: not required.
- Sidechain: not required.
- Reported latency: zero unless a future module introduces lookahead.
- Tail length: implementation-defined but MUST account for release, filter tails, delay, and reverb when FX are enabled.

Host state:

- Plugin MUST serialize complete state in host project chunks.
- Host state MUST include schema version and migration support.
- Host state MUST restore without requiring preset files to remain present.

### 8.3 MIDI Contract

Required messages:

- Note on/off with velocity.
- Pitch bend.
- Mod wheel CC1.
- Sustain pedal CC64.
- All notes off.
- All sound off.

Recommended messages:

- Channel pressure.
- Poly pressure.
- Expression CC11.
- MIDI learn for selected parameters.

Rules:

- Zero-velocity note-on MUST be treated as note-off.
- Sustain pedal MUST hold release transitions until pedal is lifted.
- Panic MUST silence all voices and clear held/sustained notes.

### 8.4 Preset File Contract

Preset files SHOULD use JSON for human-inspectable, deterministic state.

Required top-level fields:

- `schema_version`
- `plugin_min_version`
- `id`
- `display_name`
- `author`
- `tags`
- `parameters`
- `mod_slots`
- `macros`

Unknown top-level fields:

- MUST be preserved when possible.
- MUST NOT affect audio unless implemented and validated.

Error classes:

- `preset_file_missing`
- `preset_parse_error`
- `preset_schema_unsupported`
- `preset_validation_error`
- `preset_migration_warning`
- `preset_unknown_parameter`

### 8.5 Reference Render Contract

Reference renders are validation artifacts, not product audio assets.

Required metadata:

- `fixture_id`
- `preset_id`
- `plugin_version`
- `sample_rate`
- `block_size`
- `tempo_bpm`
- `host_or_runner`
- `seed`
- `git_commit` when available
- `metrics`

## 9. Configuration Specification

### 9.1 Build Configuration

Required build options:

- `SYNTHIA_BUILD_AU`
- `SYNTHIA_BUILD_VST3`
- `SYNTHIA_BUILD_STANDALONE`
- `SYNTHIA_ENABLE_TESTS`
- `SYNTHIA_ENABLE_DEBUG_DIAGNOSTICS`
- `SYNTHIA_ENABLE_COPY_AFTER_BUILD`
- `SYNTHIA_STRICT_RELEASE_NAMES`

Defaults:

- AU: enabled on macOS.
- VST3: enabled.
- Standalone: enabled for development.
- Tests: enabled in development/CI.
- Copy after build: disabled unless explicitly requested.

### 9.2 Runtime Settings

Runtime settings MUST be stored in plugin state when user-visible.

Core settings:

- `oversampling_realtime`
- `oversampling_offline`
- `quality_mode`
- `ui_scale`
- `preset_library_paths`
- `midi_learn_mappings`
- `random_seed_mode`
- `diagnostics_enabled`

Rules:

- Offline quality MAY exceed realtime quality if documented and deterministic.
- Realtime oversampling changes SHOULD avoid audio clicks through safe reinitialization.
- Invalid settings MUST fall back to defaults and emit diagnostics.

### 9.3 Default Patch Values

The default patch SHOULD be quiet, playable, and safe:

- Poly mode.
- 8 voices.
- 1 unison voice.
- Saw enabled.
- Filter open enough to hear pitch.
- Amp envelope with short attack and moderate release.
- FX disabled.
- Output level below clipping.

Factory pluck profile defaults SHOULD follow Appendix B.

## 10. Data Management

### 10.1 Persistence Model

State surfaces:

- Host project state chunk.
- Factory preset files.
- User preset files.
- MIDI learn map.
- Validation renders and reports.
- Debug logs in development builds.

### 10.2 Storage Locations

Implementation-defined locations MUST be documented.

Recommended macOS locations:

- Factory presets: app support directory under an owned vendor/product path.
- User presets: user Music, Documents, or app support directory under an owned vendor/product path.
- Validation reports: repository `build/reports` or configured output directory.

### 10.3 Migrations

- Preset schema versions MUST be monotonic integers.
- Every schema migration MUST have unit tests.
- Public parameter IDs MUST not be renamed without migration aliases.
- Removed parameters MUST be ignored with warning or mapped to replacement behavior.

### 10.4 Retention and Deletion

- The plugin MUST NOT create large caches by default.
- Validation renders MAY be deleted by normal build cleanup.
- User preset deletion MUST require explicit user action.
- Factory preset updates MUST NOT delete user presets.

### 10.5 Audit History

- Runtime plugin use does not require audit history.
- Development validation SHOULD record render metadata for regression diagnosis.

## 11. Integration Contracts

### 11.1 JUCE and CMake

Purpose: Build plugin formats from one C++ codebase.

Requirements:

- Use JUCE CMake plugin targets for AU, VST3, and standalone.
- VST2 MUST NOT be enabled unless a future explicit legacy requirement is accepted.
- AU target MUST use MusicDevice-style instrument behavior.
- Build files MUST keep plugin metadata centralized.

Current source: JUCE CMake API documents plugin `FORMATS` values including `Standalone`, `VST3`, `AU`, and others, and documents AU/VST3 copy directories and plugin metadata options: https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md

### 11.2 Ableton Live on macOS

Purpose: Primary host compatibility target.

Requirements:

- Validate in Ableton Live 11.3+ and Live 12 where available.
- AU and VST3 builds MUST be scanned and playable.
- Host automation MUST record and replay main parameters.
- Plugin state MUST save and restore inside Ableton Sets.
- Do not mix AU and VST3 instances of the same plugin in a single test set when validating host state.

Current source: Ableton documents macOS support for AU, VST2, and VST3, AUv2/AUv3 support as of Live 11.3, default system plugin folders, and Apple Silicon native plugin notes: https://help.ableton.com/hc/en-us/articles/209068929-Using-AU-and-VST-plug-ins-on-macOS

### 11.3 VST3 Format

Purpose: Cross-DAW plugin format.

Requirements:

- VST3 bundle MUST report correct category and factory information.
- VST3 output MUST be a macOS `.vst3` bundle.
- Universal binary packaging MUST be validated with `lipo` or equivalent.

Current source: Steinberg VST3 developer documentation describes macOS VST3 bundles and universal binary placement: https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BFormat.html

### 11.4 Strobe/Strobe2 Reference Material

Purpose: Historical and architectural reference only.

Rules:

- Licensed Strobe v1/Strobe2 installs MAY be used for black-box measurement if legally available.
- Reference measurements MUST NOT involve decompilation or copying proprietary presets/assets.
- Strobe2 comparisons MUST be marked as non-identical lineage reference, because ROLI documents Strobe2 as not a drop-in replacement for Strobe v1.
- On Apple Silicon, Strobe2 native behavior MUST be treated cautiously because ROLI documents a native oscillator issue and recommends Rosetta for correct sound.

Current sources:

- FXpansion DCAM Synth Squad legacy/support FAQ: https://www.fxpansion.com/support/faq/dcamsynthsquad/
- FXpansion DCAM product page: https://www.fxpansion.com/products/dcamsynthsquad/
- ROLI Strobe2 FAQ: https://support.roli.com/support/solutions/articles/36000396689-strobe2-faqs
- ROLI Apple Silicon compatibility note: https://support.roli.com/support/solutions/articles/36000582347-cypher2-strobe2-compatibility-with-apple-silicon

## 12. Security, Privacy, and Safety

### 12.1 Source Authorization And Legal Safety

- Implementation MUST be original.
- Developers MUST NOT decompile, disassemble, patch, or inspect proprietary binaries for algorithmic extraction.
- The Sylenth manual and screenshot corpus in this repo are approved Phase 1 rebuild references. Visual parity with Sylenth is an explicit product goal, not a constraint to avoid. Reference use does not permit copied presets, logos, screenshot backplates, raster UI assets, manual text, or bundled source material in shipped content unless explicit legal approval exists.
- Developers MUST NOT copy third-party presets, UI assets, manuals beyond fair-use notes, screenshots, product names, logos, or trade dress into the implementation or shipped content unless explicit legal approval exists.
- Shipped UI and factory content MUST avoid unlicensed marks including `FXpansion`, `Strobe`, `ROLI`, `Roland`, `SH-101`, `Deadmau5`, and song titles unless legal approval exists.
- Internal research notes MAY cite sources and use third-party names for nominative reference.

### 12.2 Runtime Safety

- Audio thread MUST NOT perform file I/O, allocation, network calls, blocking locks, or synchronous logging.
- Plugin MUST sanitize NaN, infinity, denormals, and invalid parameter states.
- Output MUST be protected from runaway feedback through bounded resonance, clipping, or limiter-style safety in debug/validation paths.
- Panic action MUST silence active voices.

### 12.3 Privacy

- v1 MUST NOT send telemetry.
- v1 MUST NOT require an account.
- Debug logs MUST NOT include user file contents beyond paths and error classes unless explicitly enabled.

### 12.4 Supply Chain

- Third-party dependencies MUST be pinned or versioned.
- New dependencies MUST be reviewed for license compatibility with the intended distribution.
- Builds SHOULD produce reproducible metadata where practical.

## 13. Failure Model and Recovery Strategy

### 13.1 Failure Classes

- `host_scan_failure`
- `unsupported_architecture`
- `invalid_plugin_metadata`
- `preset_parse_error`
- `preset_migration_error`
- `midi_state_error`
- `voice_allocation_overflow`
- `cpu_overload`
- `nan_or_infinity_detected`
- `denormal_spike`
- `oversampling_reinit_failure`
- `filesystem_permission_error`
- `codesign_or_notarization_failure`

### 13.2 Recovery Behavior

- Host scan failure: expose validation target and logs to diagnose metadata, signing, or architecture.
- Invalid preset: reject load, keep current state, show error.
- CPU overload: degrade only through documented quality settings or voice stealing; never emit invalid samples.
- NaN/infinity: sanitize sample/block, increment diagnostic counter, fail debug validation.
- Denormal: apply denormal guards globally in DSP path.
- MIDI stuck note: all-notes-off and panic MUST recover.

### 13.3 Partial State Recovery

- Host state restore failure MUST leave plugin in a valid default state.
- Preset migration warnings MUST be visible in development diagnostics.
- Validation failures MUST preserve reports and render artifacts for inspection.

### 13.4 Operator Intervention Points

- User panic button.
- Preset reset to init.
- Rescan plugin in Ableton.
- Clear/rebuild preset index.
- Disable FX.
- Reduce quality/oversampling/polyphony.
- Run standalone diagnostic render.

## 14. Observability and Operations

Required development diagnostics:

- Active voice count.
- Peak/RMS output meter.
- MIDI event counters.
- NaN/infinity guard counter.
- Denormal guard counter if measurable.
- Current sample rate, block size, oversampling, format, architecture.
- CPU/render time estimate.
- Preset schema/migration warnings.
- Last plugin state restore result.

Required validation outputs:

- JSON report per render fixture.
- Audio render artifact for failed reference tests.
- FFT/metric summaries for oscillator and filter tests.
- Host smoke test logs.

Logging rules:

- Realtime thread MUST write only to lock-free counters or buffers.
- File logs MUST be development/diagnostic only unless user explicitly enables them.
- Release builds SHOULD keep diagnostics lightweight.

## 15. Performance, Capacity, and Limits

### 15.1 Realtime Targets

Targets are implementation-defined baselines and MUST be measured on at least one Apple Silicon and one Intel Mac if available.

Recommended targets:

- 48 kHz, 128-sample buffer, realtime 2x oversampling.
- 16 poly voices with 2 unison voices SHOULD run without dropouts on a current Apple Silicon Mac.
- 8 poly voices with 2 unison voices MUST run comfortably on the minimum supported Mac.
- UI repaint MUST NOT starve audio processing.
- Parameter automation at host control rates MUST NOT create zipper noise.

### 15.2 Audio Limits

- Polyphony hard maximum: at least 32 logical voices.
- Unison hard maximum: at least 8 voices per note.
- Stack hard maximum: at least 5 oscillator copies for v1 profile.
- Sample rates: 44.1, 48, 88.2, 96 kHz REQUIRED; 176.4 and 192 kHz RECOMMENDED.
- Block sizes: 32, 64, 128, 256, 512, 1024 REQUIRED.
- Output should maintain headroom; factory presets SHOULD not clip at default velocity.

### 15.3 Degradation Behavior

- When voices exceed capacity, use configured stealing policy.
- When oversampling is too expensive, user MAY select lower quality; plugin MUST NOT silently reduce quality without visible state.
- Offline render MAY use higher quality than realtime if deterministic and documented.

## 16. Deployment and Host Lifecycle

### 16.1 Supported Environments

Required first release:

- macOS.
- Apple Silicon native.
- Intel native.
- Ableton Live 11.3+ and Live 12 validation.

Recommended:

- Logic Pro AU validation.
- Reaper VST3/AU validation.
- Bitwig VST3 validation.

### 16.2 Install Locations

Default macOS install locations SHOULD follow host conventions:

- AU: `/Library/Audio/Plug-Ins/Components/`
- VST3: `/Library/Audio/Plug-Ins/VST3/`

Per-user install locations MAY be supported:

- AU: `~/Library/Audio/Plug-Ins/Components/`
- VST3: `~/Library/Audio/Plug-Ins/VST3/`

### 16.3 Startup Sequence

1. Host loads plugin bundle.
2. Plugin initializes metadata and parameter registry.
3. Plugin creates default state.
4. Host calls prepare with sample rate/block size.
5. DSP allocates buffers and oversamplers off the audio path.
6. Processing starts.

### 16.4 Shutdown Sequence

1. Host stops processing or destroys instance.
2. Plugin releases voices and buffers.
3. UI closes safely.
4. No background worker threads remain.

### 16.5 Upgrade and Rollback

- New plugin versions MUST load old host states and presets through migration.
- Downgrade compatibility is not required unless explicitly documented.
- Public plugin IDs MUST remain stable across upgrades.

## 17. Reference Algorithms

### 17.1 Audio Block Processing

```text
function process_block(audio_out, midi_events, block_size):
  clear(audio_out)
  schedule_midi_events_by_sample_offset(midi_events)

  for sample_index in 0..block_size-1:
    handle_events_at(sample_index)
    update_global_modulators_if_needed()

    left = 0
    right = 0

    for voice in active_voices:
      update_voice_modulators(voice)
      frame = render_voice_sample(voice)
      left += frame.left
      right += frame.right

    frame = process_fx_if_enabled(left, right)
    audio_out.left[sample_index] = sanitize(frame.left)
    audio_out.right[sample_index] = sanitize(frame.right)
```

### 17.2 Voice Note-On

```text
function note_on(note, velocity, channel, sample_offset):
  allocated = allocator.allocate(note, velocity, channel)

  for voice in allocated:
    voice.state = Starting
    voice.midi_note = note
    voice.velocity = normalize_velocity(velocity)
    voice.random_on_note = seeded_random_bipolar()
    voice.voice_indices = compute_voice_indices(voice)
    voice.unison_indices = compute_unison_indices(voice)
    reset_or_continue_glide(voice)
    gate_amp_env(voice)
    gate_mod_env(voice)
    gate_lfo(voice)
    gate_ramp(voice)
    initialize_pan(voice)
```

### 17.3 Stack Detune

```text
function stack_detune_cents(index, count, detune_norm):
  if count == 1:
    return 0

  center = (count - 1) / 2
  spread = (index - center) / max(center, 1)
  shaped = pow(detune_norm, 2.2)
  return spread * max_detune_cents * shaped
```

### 17.4 Cutoff Computation

```text
function compute_cutoff_hz(voice):
  cutoff_semi =
    base_filter_cutoff_semi
    + direct_keytrack_depth * ((voice.midi_note - keytrack_root) / 12)
    + direct_lfo_depth_semi * voice.lfo.value
    + direct_mod_env_depth_semi * voice.mod_env.value
    + transmod_sum_for(filter_cutoff, voice)

  cutoff_hz = 440 * pow(2, (cutoff_semi - 69) / 12)
  return clamp(cutoff_hz, 8, 0.45 * oversampled_sample_rate)
```

### 17.5 TransMod Evaluation

```text
function transmod_sum_for(parameter_id, voice):
  total = 0

  for slot in transmod_slots:
    if not slot.enabled:
      continue

    source = eval_mod_source(slot.source, voice)
    scaler = 1

    if slot.scaler != None:
      scaler = eval_mod_source(slot.scaler, voice)

    total += slot.depths[parameter_id] * source * scaler

  return total
```

### 17.6 Nonlinear Filter Sketch

This sketch is not a claim about FXpansion internals. It is a conformant implementation direction for a nonlinear cascade with feedback clipping.

```text
function process_l4(input, cutoff_hz, resonance, drive):
  g = tan(pi * cutoff_hz / sample_rate)
  G = g / (1 + g)

  drive_gain = db_to_gain(max_drive_db * drive)
  resonance_effective = resonance / (1 + 0.65 * drive + 0.25 * drive * drive)
  feedback = 4 * resonance_effective * diode_clip(stage4)
  u = tanh(drive_gain * (input - feedback))

  stage1 = tpt_one_pole(u, stage1, G)
  stage2 = tpt_one_pole(stage1, stage2, G)
  stage3 = tpt_one_pole(stage2, stage3, G)
  stage4 = tpt_one_pole(stage3, stage4, G)

  return select_filter_output(mode, stage1, stage2, stage3, stage4)
```

### 17.7 Preset Migration

```text
function load_preset(json):
  parsed = parse_json(json)
  validate_top_level(parsed)

  while parsed.schema_version < current_schema_version:
    migration = migrations[parsed.schema_version]
    parsed = migration(parsed)

  validate_parameters(parsed.parameters)
  validate_mod_slots(parsed.mod_slots)
  return atomic_plugin_state(parsed)
```

## 18. Test and Validation Matrix

### 18.1 Core Conformance

- Plugin metadata loads in standalone.
- AU and VST3 builds are produced on macOS.
- Universal binary contains `arm64` and `x86_64`.
- Parameter registry has no duplicate IDs.
- Host state round trip preserves all parameters and modulation slots.
- Factory presets validate against schema.

### 18.2 Oscillator Tests

- Saw FFT aliasing at C1, C3, C5, C7.
- Pulse duty cycle at 10%, 25%, 50%, 75%, 90%.
- Stack detune symmetry for counts 1 through 5.
- Sub oscillator octave accuracy for -1, -2, -3.
- Hard sync harmonic behavior and pitch stability.
- Noise determinism under seeded tests.

### 18.3 Filter Tests

- Cutoff semitone sweep maps exponentially.
- Self-oscillation tracks pitch within target tolerance when keytrack is calibrated.
- Drive increases harmonic content.
- Drive reduces effective resonance according to documented implementation law.
- L2/L4/B2/B4/H2/H4 modes produce distinct expected responses.
- Oversampled and non-oversampled paths remain stable.

### 18.4 Envelope and LFO Tests

- Amp and mod envelope attack/decay/release timing.
- Exponential and linear curve behavior if both implemented.
- LFO Hz rates at multiple sample rates.
- LFO sync divisions at 120, 124, 128, and 130 BPM.
- Phase reset in `PolyOn`.
- Per-voice LFO independence for overlapping notes.
- Mono/global LFO behavior differs from per-voice mode.
- Swing preserves expected cycle timing.

### 18.5 Modulation Tests

- Direct routes affect expected destinations.
- TransMod one source to many destinations.
- TransMod scaler multiplication.
- Voice/unison sources vary per voice.
- Automation and modulation combine without discontinuities.

### 18.6 Musical Reference Tests

Fixtures:

- 128 BPM pluck phrase with overlapping 1/8 and 1/16 notes.
- Velocity alternating 70 through 120.
- Dry core render.
- Core plus FX render.
- Mono LFO ablation.
- Per-voice retriggered LFO render.

Metrics:

- Fundamental tuning within 5 cents for oscillator tests.
- Self-oscillating filter tracking within 10 cents after calibration.
- Cutoff modulation period within 1%.
- Envelope decay time within 15%.
- Spectral centroid envelope within implementation-defined tolerance.
- Stereo correlation within target profile range.
- Loudness after gain match within 0.5 LU for regression renders.

### 18.7 Host Integration Tests

- Ableton AU scan/load/play/save/restore.
- Ableton VST3 scan/load/play/save/restore.
- Host automation record/playback.
- Buffer size changes during playback.
- Sample rate changes.
- Offline bounce versus realtime render tolerance.
- Transport stop/all-notes-off.
- Plugin UI open/close during playback.

### 18.8 Security And Source-Use Tests

- Factory preset display names avoid third-party marks.
- Bundle metadata avoids third-party marks.
- No network permissions or calls in v1.
- Dependency license review passes.
- Preset parser rejects malformed files without crash.

### 18.9 Performance Tests

- CPU baseline at 44.1, 48, 96 kHz.
- 8, 16, and 32 voice stress tests.
- Unison 1, 2, 4, 8 stress tests.
- Oversampling off/2x/4x/8x tests.
- Denormal silence/release-tail test.
- UI repaint under active playback.

## 19. Implementation Checklist

### 19.1 Required for Phase 1 Sylenth Rebuild Conformance

- JUCE/CMake project with AU, VST3, and standalone targets.
- Central parameter registry.
- Stable host state serialization and preset JSON schema.
- A/B part architecture with layer select, solo/mute, mixer, master output, and host-state restore.
- Four oscillator slots across the A/B parts, with per-slot waveform, voices/unison, tuning, detune, phase/retrigger/invert, level, pan, and stereo spread.
- Per-part filter, amp envelope, modulation envelope, LFO routing, and mixer behavior.
- Arpeggiator and 16-step pattern workflow with host-tempo sync, gate, octave/wrap, velocity, hold/tie, and deterministic render proof.
- Sylenth-level preset workflow: bank/folder browsing, previous/next, search/tags, favorites, author/notes, dirty state, init/randomize/reset, safe save/overwrite, and host restore.
- FX rack coverage for distortion/saturation, phaser, chorus/flanger, EQ, delay, reverb, compressor, bypass state, tail reporting, and finite wet renders.
- Sylenth-faithful UI that reaches the original's speed, density, panel rhythm, and workflow while preserving readability and Ableton stability.
- Ableton AU/VST3 proof for scan/load/playback, automation, save/reopen restore, bounce, buffer/sample-rate changes, and UI open/close while playing.
- Release-safe product names, preset names, UI, and metadata.

### 19.2 Required for Current Core Scaffold

- Poly/mono/legato/unison voice allocator.
- Band-limited saw and pulse oscillator.
- Noise and sub oscillator with required waveforms.
- Stack count 1 through 5 and detune.
- Hard sync.
- Mixer with gain compensation.
- Nonlinear multimode filter with required modes.
- Semitone-domain cutoff modulation.
- Amp envelope and mod envelope.
- LFO with Hz/sync, shapes, phase, gate modes, mono/per-voice behavior.
- Ramp generator.
- Direct modulation routes.
- Eight TransMod slots.
- Voice/unison/random/velocity/performance modulation sources.
- Amp drive, voice pan, unison pan, analog instability.
- Optional FX section with bypass.
- Factory init preset and factory pluck preset.
- Automated DSP, preset, and host validation.

### 19.3 Recommended Extensions

- Full compound filter mode family.
- 16 TransMod slots in an extended profile.
- MPE.
- Microtuning.
- Preset browser tags/search/favorites.
- Built-in oscilloscope/spectrogram.
- More FX processors.
- Windows VST3 build.
- Linux CLAP/LV2 build.
- Audio input/effect version.

### 19.3 Production Readiness

- Code signing and notarization pipeline.
- Installer package and uninstall docs.
- Ableton Live validation on Apple Silicon and Intel.
- Logic AU validation.
- Reaper AU/VST3 validation.
- Crash report and diagnostic procedure.
- CPU/performance report.
- Preset migration test suite.
- Legal/brand review for names, UI, docs, and factory content.

## 20. Open Questions

- Final product name and owned bundle identifier prefix.
- Minimum supported macOS version.
- Whether v1 should ship only macOS or include Windows VST3.
- Whether the first public release should include onboard delay/reverb or keep FX disabled/internal until the dry core is validated.
- Whether an audio-input/effect version is desired.
- Whether to use GPL/commercial JUCE terms or another framework path.
- Which exact reference audio clips, if any, are approved for internal audio measurement beyond the approved Sylenth manual and screenshot corpus.
- Whether older Strobe research remains useful once the Phase 1 Sylenth rebuild is underway.
- Exact release-safe branding, mark treatment, and source-use approvals for the Sylenth-faithful UI.
- Whether factory preset names may use indirect nods or must be completely generic.

## Appendix A. Factory Pluck Profile

Internal profile ID: `legacy-pluck-core-01`

Shipped display name recommendation: `Pluck Core 01`

Internal research alias: `FB_StrobeLike_Pluck_01` MAY be used in development metadata only.

```json
{
  "schema_version": 1,
  "plugin_min_version": "0.1.0",
  "id": "legacy-pluck-core-01",
  "display_name": "Pluck Core 01",
  "author": "Factory",
  "tags": ["pluck", "analog", "poly", "wide"],
  "parameters": {
    "voice.mode": "poly",
    "voice.polyphony": 8,
    "voice.unison_count": 2,
    "osc.saw_level": 0.92,
    "osc.pulse_level": 0.08,
    "osc.noise_level": 0.0,
    "osc.sub_wave": "saw",
    "osc.sub_level": 0.08,
    "osc.sub_octave": -1,
    "osc.stack_count": 2,
    "osc.stack_detune": 0.025,
    "osc.pulse_width": 0.5,
    "osc.sync_amount": 0.0,
    "filter.mode": "L4",
    "filter.cutoff_semitones": 58.0,
    "filter.resonance": 0.24,
    "filter.drive": 0.32,
    "direct.filter_keytrack": 0.45,
    "direct.filter_lfo_semitones": 14.0,
    "direct.filter_mod_env_semitones": 28.0,
    "amp_env.attack_ms": 2.0,
    "amp_env.decay_ms": 280.0,
    "amp_env.sustain": 0.05,
    "amp_env.release_ms": 180.0,
    "mod_env.attack_ms": 0.5,
    "mod_env.decay_ms": 430.0,
    "mod_env.sustain": 0.0,
    "mod_env.release_ms": 160.0,
    "lfo.shape": "SawDown",
    "lfo.rate_mode": "sync",
    "lfo.sync_division": "1/4",
    "lfo.phase_degrees": 0.0,
    "lfo.mono": false,
    "lfo.gate_mode": "PolyOn",
    "amp.drive": 0.12,
    "amp.level_db": -6.0,
    "amp.pan_spread": 0.65,
    "amp.analog": 0.08,
    "fx.saturation_mix": 0.05,
    "fx.delay_mix": 0.12,
    "fx.delay_time": "1/8D",
    "fx.reverb_mix": 0.08
  },
  "mod_slots": [],
  "macros": [
    {
      "id": "motion",
      "display_name": "Motion"
    },
    {
      "id": "width",
      "display_name": "Width"
    },
    {
      "id": "drive",
      "display_name": "Drive"
    },
    {
      "id": "space",
      "display_name": "Space"
    }
  ]
}
```

## Appendix B. Historical Research Source Map

Observed current/vendor facts:

- FXpansion documents DCAM Synth Squad as a legacy product and describes Strobe as a fast performance synth with single-oscillator architecture, polyphony, TransMod, parallel waveforms, oscillator stacking, and many filter modes: https://www.fxpansion.com/products/dcamsynthsquad/
- FXpansion support documents DCAM Synth Squad as legacy/superseded by Strobe2 and no longer actively tested/developed: https://www.fxpansion.com/support/faq/dcamsynthsquad/
- ROLI documents Strobe2 as inspired by SH-101, OB-1, and Pro-One, with single super-oscillator, stacked detuned copies, TransMod, built-in effects, and 16 TransMod slots as a Strobe2 feature: https://support.roli.com/support/solutions/articles/36000396689-strobe2-faqs
- ROLI documents Strobe2 as not a drop-in replacement for Strobe v1 and notes synthesis-engine differences for imported v1 presets: https://support.roli.com/support/solutions/articles/36000396689-strobe2-faqs
- ROLI documents Strobe2/Cypher2 Universal Binaries and an Apple Silicon native oscillator issue with Rosetta recommended for correct sound: https://support.roli.com/support/solutions/articles/36000582347-cypher2-strobe2-compatibility-with-apple-silicon
- Ableton documents macOS AU/VST/VST3 support and plugin install folders: https://help.ableton.com/hc/en-us/articles/209068929-Using-AU-and-VST-plug-ins-on-macOS
- JUCE CMake documentation lists plugin target formats including Standalone, VST3, and AU: https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
- Steinberg VST3 documentation describes `.vst3` macOS bundles and universal binary placement: https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BFormat.html

Observed manual-derived architecture facts from the supplied research brief:

- Strobe v1 manual material describes Strobe as a single-oscillator analog-modelled performance synth inspired by Roland SH-series instruments, especially SH-09/SH-101, plus OB-1 and Pro-One.
- Strobe oscillator material describes stack/detune, saw/pulse/noise, sub oscillator waveforms, hard sync, pulse width, and direct modulation.
- Strobe filter material describes an OTA cascaded core with diode clipping in the feedback path, semitone cutoff, self-oscillation, drive/resonance interaction, and many multimode responses.
- Strobe amp material describes amp overload/waveshaping, voice pan modulation, level, and an analog instability control.
- Strobe modulation material describes gateable modulators, LFO sync/Hz modes, phase reset on gating, mono versus per-voice behavior, TransMod, glide, velocity glide, voice/unison sources, and modulation scaling.

Inferred target:

- The target sound should prioritize per-voice retriggered modulation, semitone cutoff movement, nonlinear filter drive, and voice/unison variation rather than wavetable synthesis or a generic monophonic SH-101 clone.

Unknowns:

- Exact historical binary identity.
- Exact preset values.
- Exact filter mode used on canonical references.
- Exact external effects chain.
- Any private parameter scaling or custom modifications.
