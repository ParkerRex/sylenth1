# Preset Schema

This document sketches the planned preset schema. `SPEC.md` remains the source of truth for required fields and behavior.

## Format

Preset files use a JSON `.SynthiaPreset` envelope:

- deterministic key order when written by tools,
- human-inspectable,
- versioned,
- migratable,
- no copied third-party data.

The canonical and only supported preset extension is `.SynthiaPreset`.

Current factory presets:

- `presets/factory/Init/Init.SynthiaPreset`
- `presets/factory/Lead/LD - Supersaw Stack 01.SynthiaPreset`
- `presets/factory/Lead/LD - Rolling Circuit.SynthiaPreset`
- `presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset`
- `presets/factory/Pluck/PL - Droplet Glass.SynthiaPreset`
- `presets/factory/Pluck/PL - Room Shine.SynthiaPreset`
- `presets/factory/Bass/BA - Bass Wub 01.SynthiaPreset`
- `presets/factory/Bass/BA - OTT Saw Lift.SynthiaPreset`
- `presets/factory/Bass/BA - Acid Thread.SynthiaPreset`
- `presets/factory/Pad/PD - Pad Wide 01.SynthiaPreset`
- `presets/factory/Pad/PD - Analog Silk Sweep.SynthiaPreset`
- `presets/factory/Pad/PD - Cosmic Aura.SynthiaPreset`
- `presets/factory/Pad/PD - Dark Space.SynthiaPreset`
- `presets/factory/Arp/ARP - Arp Motion 01.SynthiaPreset`
- `presets/factory/FX/FX - Space 01.SynthiaPreset`
- `presets/factory/Synth/SY - Broken Tape Keys.SynthiaPreset`
- `presets/factory/Synth/SY - Pixel Storm Poly.SynthiaPreset`

Factory patches are Synthia-native remixes authored from local reference library category/name targets. They do not embed third-party binary preset payloads.

Current user preset location:

- `~/Music/ParkerX/synthia/Presets`

The preset browser scans only the current Synthia user preset location.

The editor recursively scans factory presets from bundled plugin resources when running from an installed AU, VST3, or Standalone bundle, falling back to the source `presets/factory` directory for development tools. User presets are scanned from the user preset location. Factory presets are treated as read-only; editor Save As and Duplicate write schema-valid user `.SynthiaPreset` files.

Current validation command:

```bash
./build/SynthiaRender --validate-presets presets/factory --output build/reports/presets.json
```

Current patch recreation command:

```bash
./build/SynthiaRender --suite patch-recreation --output-dir build/reports/patch-recreation
```

## Required Top-Level Fields

`.SynthiaPreset` files carry a lightweight browser envelope:

- `fileType`: `SynthiaPreset`.
- `presetName`: display name.
- `presetAuthor`: author name.
- `presetDescription`: short description.
- `product`: `Synthia`.
- `productVersion`: plugin/project version.
- `vendor`: vendor string.
- `url`: vendor URL.
- `version`: envelope schema version.
- `bank`: browser bank.
- `category`: browser category.
- `tags`: browser-facing tag list.
- `preview`: preview/audition metadata.
- `preset`: the validated Synthia preset payload.

The nested `preset` payload keeps the render-state contract:

- `schema_version`: integer.
- `plugin_min_version`: semantic version string.
- `id`: stable lower-kebab-case preset ID.
- `display_name`: shipped user-facing name.
- `author`: string.
- `description`: string.
- `tags`: lowercase slug list.
- `parameters`: map of parameter ID to value.
- `mod_slots`: list of TransMod-style slot objects.
- `macros`: list of macro objects.
- `metadata`: optional object.

User preset writes include `metadata.program = "synthia_lab_rebuild"` and browser metadata under `metadata.browser`.

Phase 2 and Phase 3 may extend `metadata` with generation provenance, prompt text, seed, model/version identifiers, reference-analysis summaries, and reversible edit history. Those fields must not be required for normal audio rendering.

## Browser Metadata

Preset browser metadata is UI/library state, not realtime audio state. It does not create APVTS parameters and must not be read by the audio thread.

The current browser-facing scan summary exposes:

- `source`: `factory` or `user`.
- `bank`: display bank name, for example `Factory` or `User`.
- `category`: browser category, for example `Init`, `Plucks`, or `User`.
- `tags`: top-level tag strings from the preset JSON.
- `favorite_key`: stable local key. Factory presets use `<source>:<preset_id>`; user and legacy-user presets add the normalized local file path so duplicate user preset IDs do not share favorite state.
- `favorite`: local favorite state resolved from the sidecar favorites file.

The nested preset payload may provide browser metadata as:

```json
"metadata": {
  "program": "synthia_lab_rebuild",
  "browser": {
    "bank": "Factory",
    "category": "Plucks",
    "source": "factory"
  }
}
```

If `metadata.browser` is absent, scan summaries fall back to source-derived bank/category defaults so older valid presets remain visible. Envelope `bank`, `category`, and `tags` exist for browser/audition tooling; render state is still owned by the nested preset payload.

Favorites are stored outside preset JSON in:

- `~/Music/ParkerX/synthia/PresetFavorites.json`

The sidecar shape is:

```json
{
  "schema_version": 1,
  "favorite_keys": [
    "factory:pluck-core-01",
    "user:browser-favorite-test:/Users/example/Music/ParkerX/synthia/Presets/browser-favorite-test.SynthiaPreset"
  ]
}
```

Keeping favorites in a sidecar lets factory presets remain read-only and lets future browser UI toggle favorites without mutating sound patches.

## MIDI Controller Map

MIDI controller assignments are global user-library state, not preset sound state and not APVTS parameters. They must not be stored in factory preset JSON or read from the audio thread.

The current user MIDI map file is:

- `~/Music/ParkerX/synthia/MidiControllerMap.json`

The sidecar shape is:

```json
{
  "schema_version": 1,
  "mappings": [
    {
      "cc": 74,
      "parameter_id": "filter.cutoff_semitones"
    }
  ]
}
```

Map rules:

- `cc` must be `0` through `127`.
- `parameter_id` must name an automatable parameter from the registry.
- each CC may map to one parameter,
- each parameter may have one learned CC,
- later conflicting assignments replace earlier assignments during normalization,
- reserved performance/safety CCs such as sustain, all-sound-off, and all-notes-off are rejected by the processor assignment path.

The processor loads this file on construction, publishes fixed atomic CC-to-parameter indexes for realtime MIDI lookup, and applies learned/mapped CC changes on the message-thread timer. Writes happen only from the control path when the user learns or forgets an assignment.

## Parameter Values

Parameter values should be stored in physical/display domains when stable and clear:

- semitones as numbers,
- milliseconds as numbers,
- dB as numbers,
- enums as strings,
- normalized values only when the parameter is intentionally abstract.

The parameter registry owns conversion between physical values and host-normalized values.

When a preset is loaded through the plugin editor, the processor resets APVTS parameters to registry defaults before applying preset overrides. This prevents values from a previous preset from leaking into presets that intentionally omit optional fields.

Init, Reset, and Randomize commands are control-path state operations, not additional preset JSON fields. Init prepares registry-default APVTS state named `Init`. Reset reloads the current preset path when one exists and falls back to Init for transient states. Randomize prepares bounded, seed-repeatable APVTS state and names it `Randomized <seed>`. A randomized patch can be saved through the normal preset writer after the user accepts it; the seed is not serialized unless a future Phase 2 provenance field explicitly adds it under `metadata`.

Dirty state and A/B compare are also control-path/UI workflow state, not preset JSON fields. `PresetManager` fingerprints registry-ordered serialized parameters to compare the current APVTS state against an immutable baseline fingerprint, ignoring host bookkeeping properties such as current preset name/path. `PresetLoadResult` carries that fingerprint because APVTS `ValueTree` state can be shared after replacement and must not be treated as a frozen baseline. Local A/B compare slots capture APVTS state snapshots and can prepare those snapshots for replacement without mutating live parameters first. Safe save uses explicit create-new versus overwrite-existing write modes; create-new writes use a final no-clobber file create, and metadata-aware writes serialize display name, author, description, tags, bank, and category through the normal preset JSON shape.

Current FX and quality fields are ordinary serialized parameters. `fx.enabled` is the global FX bypass. The fixed rack uses `fx.saturation_enabled`, `fx.distortion_mode`, `fx.phaser_*`, `fx.chorus_*`, `fx.eq_*`, `fx.delay_*`, `fx.reverb_*`, and `fx.compressor_*` fields. Delay sync is stored as an enum string such as `1/8`; distortion mode is stored as `Soft`, `Clip`, or `Fold`. Realtime and offline quality are stored as `quality.realtime_mode` and `quality.offline_mode`.

The Step LFO table is ordinary serialized parameters: `lfo.shape` gains the `Step` choice, and `lfo.step_count` (2..16), `lfo.step_smooth` (0..1), and `lfo.step.1` through `lfo.step.16` (-1..1) store the drawn pattern. Presets that omit step fields load the registry-default descending ramp.

## Arp, Step, and Chord State

Phase 1 arp/chord state is ordinary serialized parameter state so host automation, saved presets, and future AI generation use the same contract.

Top-level arp fields:

- `arp.enabled`: boolean, default `false`.
- `arp.mode`: enum string: `Up`, `Down`, `UpDown`, or `AsPlayed`.
- `arp.rate`: enum string: `1/32`, `1/16`, `1/8`, `1/4`, or `1/2`.
- `arp.gate`: normalized gate, `0.02` through `1.0`, default `0.75`.
- `arp.octaves`: integer-like octave span, `1` through `4`.
- `arp.hold`: boolean hold/latch behavior for released input notes.
- `arp.swing`: normalized swing amount, `0.0` through `0.75`.
- `arp.step_count`: integer-like active step count, `1` through `16`.

Each fixed step lane is `arp.step.N.*` for `N = 1..16`:

- `arp.step.N.enabled`: boolean.
- `arp.step.N.pitch_semitones`: integer-like pitch offset, `-24` through `24`.
- `arp.step.N.velocity`: normalized velocity scale, `0.0` through `1.0`.
- `arp.step.N.gate`: normalized per-step gate scale, `0.02` through `1.0`.
- `arp.step.N.tie`: boolean; tied steps suppress the normal gate-off until the next step boundary.

Top-level chord fields:

- `chord.enabled`: boolean, default `false`.
- `chord.voice_count`: integer-like active chord voice count, `1` through `8`.

Each fixed chord voice is `chord.voice.N.*` for `N = 1..8`:

- `chord.voice.N.enabled`: boolean.
- `chord.voice.N.pitch_semitones`: integer-like pitch offset, `-24` through `24`.
- `chord.voice.N.velocity`: normalized velocity scale, `0.0` through `1.0`.

Defaults preserve existing behavior: arp and chord are disabled, step lanes are enabled with neutral pitch/velocity/gate, and chord voice 1 is the neutral root voice. Legacy presets that omit these fields load through registry defaults. Newly saved user presets include the full arp/step/chord parameter set.

## Layer and Oscillator Slot State

Phase 1 now serializes a Sylenth-style A/B layer backbone as ordinary parameter state. Layer indices are numeric in IDs:

- `layer.1.*`: Layer A.
- `layer.2.*`: Layer B.

Layer fields:

- `layer.N.enabled`: boolean.
- `layer.N.level_db`: dB, `-48.0` through `12.0`, default `0.0`.
- `layer.N.pan`: normalized bipolar pan, `-1.0` through `1.0`, default `0.0`.
- `layer.N.solo`: boolean.
- `layer.N.mute`: boolean.

Each layer owns two oscillator-slot records, giving four slots total: `layer.1.osc.1`, `layer.1.osc.2`, `layer.2.osc.1`, and `layer.2.osc.2`.

Oscillator-slot fields:

- `layer.N.osc.M.enabled`: boolean.
- `layer.N.osc.M.voices`: integer-like voice count, `0` through `8`; `0` is a disabled-slot-compatible value.
- `layer.N.osc.M.waveform`: enum string: `Saw`, `Pulse`, `Noise`, or `Sub`.
- `layer.N.osc.M.octave`: integer-like octave offset, `-4` through `4`.
- `layer.N.osc.M.note`: integer-like semitone offset, `-12` through `12`.
- `layer.N.osc.M.fine_cents`: cents, `-100.0` through `100.0`.
- `layer.N.osc.M.level`: normalized level, `0.0` through `1.0`.
- `layer.N.osc.M.phase_degrees`: degrees, `0.0` through `360.0`.
- `layer.N.osc.M.detune`: normalized detune, `0.0` through `1.0`.
- `layer.N.osc.M.stereo`: normalized stereo spread, `0.0` through `1.0`.
- `layer.N.osc.M.pan`: normalized bipolar pan, `-1.0` through `1.0`.
- `layer.N.osc.M.retrigger`: boolean.
- `layer.N.osc.M.invert`: boolean.

Defaults preserve the current sound path:

- Layer A is enabled.
- Layer B is disabled.
- Layer A oscillator 1 is enabled with `voices = 1`, `waveform = Saw`, and `level = 1.0`.
- The other three oscillator slots are disabled with `voices = 0` and `level = 0.0`.

The existing flat `osc.*`, `filter.*`, envelope, modulation, amp, and FX parameters remain host-stable. Layer A oscillator 1 gates and mixes the legacy `osc.*` compatibility source; A2/B1/B2 render from `layer.N.osc.M.*` slot fields through the current oscillator stack foundation. Legacy presets that omit `layer.*` fields load by registry defaults; host states that predate these fields are merged over registry defaults before restore. Newly saved user presets include the layer and oscillator-slot fields.

Legacy presets are not fully inferred into equivalent oscillator-slot state yet. For example, a preset that renders from flat stack, pulse, sub, and detune parameters will still show the default Layer A slot backbone unless it explicitly stores layer fields. UI should treat Layer A oscillator 1 as the compatibility source for the flat oscillator path, not as a complete visualization of every legacy `osc.*` field.

Layer display names are not automatable parameters. If custom names are added later, they should live in preset metadata or UI-local state with an explicit migration rule.

## Mod Slot Shape

Example:

```json
{
  "slot_id": 1,
  "enabled": true,
  "source": "LFO",
  "scaler": "Macro1",
  "depth_domain": "Physical",
  "depths": {
    "filter.cutoff_semitones": 12.0,
    "amp.pan": 0.25
  }
}
```

Rules:

- `slot_id` must be 1 through 8 for the v1 profile.
- `source` is required when `enabled` is true.
- `scaler` may be `None`.
- Unknown destinations should be preserved during migration when possible.
- Runtime UI/model reads use `ModulationRouteModel` to derive route rows from the flat TransMod parameters. The legacy normalized `transmod.N.depth` field remains a cutoff-depth contributor and is surfaced as a contributing route parameter when present.
- Runtime UI/model writes use `ModulationRouteModel` write requests that compile source, scaler, destination, and depth intent back into the existing flat `transmod.N.*` parameters. The current write adapter replaces a slot with one destination route and emits clear-slot edits for removal; per-route bypass/remove state and expanded destinations remain future schema work.

## Macro Shape

Example:

```json
{
  "id": "motion",
  "display_name": "Motion",
  "value": 0.5,
  "assignments": [
    {
      "target_id": "direct.filter_lfo_semitones",
      "min": 4.0,
      "max": 24.0,
      "curve": "linear"
    }
  ]
}
```

The current factory `space` macro declares assignments to `fx.delay_mix` and `fx.reverb_mix`. Runtime processing also maps `macro.space` into delay and reverb wetness so the preset remains useful even before a richer macro-assignment engine exists. The added phaser, EQ, compressor, and distortion-mode fields default to bypassed or no-op values so legacy presets keep their prior sound.

## Migration Rules

- Schema versions are monotonic integers.
- Every migration must be tested.
- Unknown fields should be preserved when possible.
- Unknown parameter IDs should warn, not crash.
- Invalid numeric values may be clamped only during explicit migration.
- Host state must include enough data to restore without external preset files.

## Factory Preset Naming

Shipped factory presets must avoid unlicensed third-party marks.

Internal research names may exist in `metadata` during development, but release builds should omit or sanitize them.

The current factory preset display names are `Init`, `Pluck Core 01`, `Supersaw Stack 01`, `Bass Wub 01`, `Pad Wide 01`, `Arp Motion 01`, and `FX Space 01`.
