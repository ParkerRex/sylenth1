# Preset and Program State

`SPEC.md` owns the product requirements. This document defines the current native state and file formats. The instrument uses one architecture: four oscillator slots, independent A/B filters and amplitude envelopes, two modulation envelopes and two LFOs.

## Owned Formats

A single patch uses `.SynthiaPreset`, a JSON envelope whose `fileType` is `SynthiaPreset`. The envelope carries `presetName`, `presetAuthor`, `presetDescription`, product/version/vendor fields, browser `bank`, `category`, `tags`, optional preview metadata, and a nested `preset` payload.

The payload contains:

- `schema_version`: `2` for newly written patches.
- `plugin_min_version`, `id`, `display_name`, `author`, `description`, `tags`.
- `parameters`: registered parameter IDs with physical values; choices use their exact registered strings and switches use booleans.
- `mod_slots`: optional physical or normalized advanced modulation depths.
- `macros`: optional macro metadata.
- `metadata`: optional authoring and browser information.

Unknown parameter IDs, invalid choices and out-of-range preset values are validation errors. There is no old-patch sound compatibility profile. Loading a valid patch starts from current registry defaults before applying its values, so omitted controls cannot inherit another patch's values.

Factory Init has an empty parameter map and therefore produces exactly the same sound state as the processor's Init command. The 18 owned factory patches use the native engine. They were authored around independent part filters/envelopes and visible modulation routes; their sound has changed from the previous shared-filter scaffold. They are not third-party factory presets and are not claimed to match a proprietary instrument's binary output.

A program bank uses `.SynthiaBank`, an owned JSON format:

```json
{
  "fileType": "SynthiaBank",
  "schema_version": 1,
  "subbanks": 4,
  "programs_per_subbank": 128,
  "program_state_xml": "<PROGRAM_BANK schema_version=\"1\" current_program=\"0\">...</PROGRAM_BANK>"
}
```

The XML is a JUCE ValueTree. `PROGRAM_BANK` contains exactly 512 `PROGRAM` records with unique zero-based `index` and nonempty `name` fields. A populated record contains its `SYNTHIA_STATE` sound state and a `BASELINE` child holding the original program state for Reset. An empty record represents registry-default Init. Bank loading validates the envelope, version, slot count, indices, names, state types, finite numeric values and registered parameter ranges before replacing the bank. Files above 64 MB are rejected.

**Proprietary `.fxp` and `.fxb` files are not supported.** Those extensions identify the reference instrument's files, not the owned Synthia format. Bank import/export rejects them explicitly. There is no documented proprietary mapping or imported third-party payload.

Create-new saves use exclusive creation and reject an existing destination. Overwrite saves write a temporary file and replace the target through JUCE's temporary-file operation. Both patch and bank saves run outside the audio thread. External user files are not rewritten during factory authoring or startup.

## Program Workflow

The host sees 512 programs, divided into four subbanks of 128. Display numbering may be one-based; the processor API and persisted indices are zero-based. Startup slot 0 is Init, subsequent first-subbank slots contain owned factory patches, and the remaining slots are Init.

Program navigation stores the current edited sound in its program slot before selecting another slot. Changes remain available when returning to that program. The original baseline remains unchanged across navigation, allowing Reset to restore the program's original sound.

- Rename changes the program name, capped at 128 characters.
- Copy captures a separate clipboard sound.
- Paste replaces the selected program with the copied sound.
- Insert puts the copied sound at the selected index and shifts subsequent entries within that subbank; the last entry in that subbank is discarded.
- Delete shifts later entries within the selected subbank left and places Init at its last slot.
- Init creates registry-default sound state.
- Randomize prepares a bounded, seed-repeatable native sound state.
- Reset restores the selected program's original stored baseline without consulting an external file.

Host `setCurrentProgram` and incoming MIDI program changes publish one fixed atomic request. The latest pending request wins. A 60 Hz control timer applies it outside the audio thread; the sound becomes available to the next stable audio block after application. Notes arriving before application use the previous sound. This handoff is asynchronous, not sample-accurate.

As documented for the original workflow, MIDI program changes select the first 128 programs. Subbank selection and access to all 512 programs are available through the editor and host program API. CC bank-selection extensions are not implemented. Authoritative bank and host-state restoration clear any older pending program request.

## Host State

Host state uses a `SYNTHIA_STATE` ValueTree with `schema_version = 2`, plugin version, current patch name/path, the full registered parameter state and a `PROGRAM_BANK` child. It restores all program sounds, original baselines, names and selection without requiring external preset files.

The processor brackets control-path APVTS state replacement with an atomic sequence counter. The audio callback clears a block if a replacement overlaps its parameter snapshot. It never parses bank XML/JSON, mutates APVTS, accesses program storage or takes program/metadata locks. Program changes request an audio-thread panic after state replacement.

Selected part is atomic UI state and persists as the host root `selected_part` field. It survives editor close and is excluded from sound fingerprints and ordinary patch JSON. Local comparison snapshots, clipboard state and browser favorites are control/UI state. They are not DSP parameters. Dirty comparison uses registry-ordered sound fingerprints rather than mutable ValueTree identity.

## Native Parameter Families

Parts use `layer.1` for A and `layer.2` for B. Each part has `level_db`, `pan`, optional `solo`/`mute` controls, its own `filter.*` and `amp_env.*`, and two `osc.M.*` records.

Each oscillator stores:

- `voices` (0..8), `waveform`, `octave`, `note`, `fine_cents`.
- `level`, `phase_degrees`, `detune`, `stereo`, `pan`, `retrigger`, `invert`.

The eight native waveform choices are `Saw`, `Pulse`, `Noise`, `Sine`, `Triangle`, `SawTriangle`, `HalfPulse`, `QuarterPulse`. All four slots default to level 1; A1 has one voice and the other three have zero voices. A zero voice count switches a slot off, so raising its Voices control makes it audible without an additional hidden enable operation.

Part filters store `enabled`, `mode`, `cutoff_semitones`, `resonance`, `drive`, `keytrack`, `oversampling`, and `input`. Input choices are `None`, `A`, `B`, `A+B`; A defaults to input A and B to input B. Filter modes combine response and slope: `L2`, `L4`, `B2`, `B4`, `H2`, `H4`. Bypass is the filter Enabled switch. Internal filter tests may exercise additional algorithms, which are not exposed as native part modes.

Part amplitude envelopes use `layer.N.amp_env.attack_ms`, `decay_ms`, `sustain`, `release_ms`. The shared filter controls use `filter_control.cutoff_semitones`, `resonance`, `keytrack`, `warm_drive`. Warm Drive is a switch for the filter saturation quality path. `filter_control.drive` is an additional shared drive amount. Master output uses `master.level_db`.

Modulation envelope 1 uses `mod_env.*`; envelope 2 uses `mod_env.2.*`. LFO 1 uses `lfo.*`; LFO 2 uses `lfo.2.*`. Each LFO stores waveform, Hz rate, sync division, phase, gate mode, gain, offset and `free`, plus the extended step table. Global Sync and Free select timing; there is no additional hidden rate-mode parameter. Free selects free-running Hz behavior and prevents note retrigger. Eligible global LFO clocks keep advancing during silent processor callbacks. The silent path consumes the current parameter snapshot and pending panic request, advances the clocks, and skips voice and FX rendering. `global.sync` controls host-synchronized timing. Portamento uses `voice.glide_ms` and `voice.portamento_mode`, with `Normal` and `Slide` choices. `voice.pitch_bend_range` stores semitones.

Arp state remains ordinary parameter state: `arp.enabled`, `mode`, `rate`, `time_ms`, `velocity_mode`, `wrap`, `gate`, `octaves`, `hold`, `swing`, `step_count`, and 16 `arp.step.N.*` records. Chord voices use `chord.*`. Native free timing uses `time_ms`; synchronized timing uses the registered division. Delay, phaser and chorus use Global Sync as their only timing-mode switch; there is no hidden delay-sync gate. The plugin effects rack is always available; its seven individual module switches own bypass. There is no public global FX gate. Internal render tools may still bypass the rack for dry proof. Init starts with all modules disabled. FX fields use `fx.*` and are fully saved, including 18 musical delay divisions, synchronized phaser/chorus rates, independent delay timings/divisions, stereo controls, filter cuts, smear, reverb predelay/damping and compressor timing. Warm Drive and part filter oversampling define the implemented quality path; there are no public quality-profile controls.

## Modulation Routes

Eight slots represent the original source panels:

- Slots 1 and 2: modulation envelopes 1 and 2.
- Slots 3 and 4: LFOs 1 and 2.
- Slots 5..8: the four miscellaneous selectable sources.

Each slot uses `transmod.N.source` and optional `scaler`. Two visible routes store `transmod.N.route.M.destination` and `amount` for M = 1 or 2. Destination index 0 means None; subsequent indices follow `modulationDestinationCatalog()` order. Amount is bipolar -1..1 and scales into the destination's physical depth domain. Selection persists even at amount zero. A selected destination activates that slot, so ordinary classic controls do not require an additional enable switch.

The catalog includes shared pitch/cutoff/level/pan routes and 38 native destinations: pitch, volume, pan, detune and phase for each oscillator; cutoff, resonance and drive for each part filter; each part's level and pan; rate, gain and offset for each LFO; shared A+B resonance; and phaser center frequency. Phaser center modulation uses the most recently triggered active synthesis voice through its release, then the next latest active voice, and returns to zero when no voice remains. This last-note priority is the owned global-effect policy; exact proprietary polyphonic aggregation is not claimed. Sources include both modulation envelopes, both LFOs, both amplitude envelopes, velocity, keytrack, aftertouch, pitch bend, mod wheel and extended sources.

Advanced physical depths remain available as `transmod.N.native.<target_parameter_id>`. These add to the two visible route amounts. The optional `mod_slots` JSON objects use registered source/scaler choice strings, `depth_domain` (`Physical` or `Normalized`), and a `depths` map keyed by native destination parameter IDs or the shared routing aliases `osc.pitch_semitones`, `filter.cutoff_semitones`, `amp.level_db`, `amp.pan`. A shared alias describes modulation without inventing a nonexistent base knob. Runtime route writes can replace existing destinations or retain them explicitly. Clearing a slot removes its source and physical depths.

## MIDI Learn and UI Performance

Per-control Learn, Cancel and Forget use the processor's MIDI assignment API. Learned CC maps remain in `~/Music/ParkerX/synthia/MidiControllerMap.json`, with `schema_version: 1` and `mappings` records containing `cc` and `parameter_id`. Each CC controls one automatable parameter and each parameter has one learned CC. Sustain and safety CCs are reserved. The map is published as fixed atomic indices; learned values are applied through the control timer.

The UI keyboard and wheels submit short MIDI messages to a fixed 256-entry queue. The audio callback drains them before rendering. Unsupported messages and a full queue return failure. If overflow would lose a note release or panic message, the next block discards the queued UI events and panics the voices. Pitch and modulation wheels publish their latest values separately from the queue, so a full note queue cannot discard the final pitch-center event. Host SysEx and other messages longer than three bytes are skipped before constructing owning MIDI messages, avoiding payload allocation on the audio thread.

Favorites remain in `~/Music/ParkerX/synthia/PresetFavorites.json`; user patches remain under `~/Music/ParkerX/synthia/Presets`. Neither sidecar is copied into factory patches. Factory resources are read-only in the browser, with source-directory fallback for development.
