# Synthia Instrument Specification

Status: classic native rebuild and release qualification in progress, 2026-10-09.

This is the product contract. Historical research, old implementation checklists, and completed plans do not override it. MUST, MUST NOT, and REQUIRED identify release requirements; SHOULD identifies a recommended behavior that needs an explicit reason if omitted.

## 1. Current Release Contract


The 2026-10-09 product direction is a faithful recreation of the classic Sylenth1 interface, controls, signal path, and sound behavior. This contract supersedes older descriptions of Phase 1 as a modernized interpretation, an incremental pluck-engine expansion, or a UI polish exercise. Browser/Wasm work, AI generation, conversational editing, and additional modern synthesis features are deferred from this release.

The provisional visual reference is `research/sylenth1-screenshots/official-lennardigital-main-ui.jpg`, the classic v3.0.2 view at 908 by 591 pixels. `Sylenth1Manual.pdf` defines documented interactions and signal flow. Other skins are supporting research, not interchangeable acceptance images. A reference-version change requires an explicit update to this contract.

Conformance has four independent dimensions:

1. **Visual:** the complete classic one-screen composition, dimensions, part states, LCD pages, labels, control geometry, and interaction states. Rendering MUST use owned implementation/assets and the Synthia identity. Branding differences MUST be explicit in reference comparisons. Third-party screenshot backplates and vendor logos MUST NOT be bundled as the implementation.
2. **Behavior:** every exposed control MUST change the corresponding real parameter or documented local state. A control whose parameter is serialized but ignored by the selected sound path is a failure. Both parts, all four oscillators, all modulation sources/destinations, arp modes, FX controls, and program operations require behavior proof.
3. **Audio:** source-owned DSP MUST be compared against authorized captures from the selected original version before sound equivalence is claimed. Self-render tests prove Synthia behavior and safety; they do not prove Sylenth equivalence. Bit-identical audio is an unverified requirement until reference phase/randomization, host, sample rate, and capture conditions are fixed and measured. Missing references MUST fail a requested equivalence check rather than produce a passing report.
4. **Persistence:** complete Synthia host-state and preset restore, an owned versioned 512-program bank, and documented compatibility behavior. Original proprietary `.fxp`/`.fxb` compatibility MUST NOT be implied by support for Synthia banks; it requires its own documented mapping and validated reference files.

The native architecture MUST provide:

- Four equivalent stereo oscillators, A1/A2/B1/B2, with eight documented waveform choices, 0 through 8 stacked voices, pitch, phase/retrigger, invert, detune, stereo, pan, and level.
- Independent stereo Part A/B filters and amplitude envelopes, filter input selection (none, A, B, AB), shared cutoff/resonance/keytrack/warm-drive controls, part output mixing, and master volume after effects.
- Two modulation envelopes and two LFOs, including LFO gain/offset/free behavior and shared sync, plus source-to-destination modulation matching the original control surface.
- Ten arpeggiator modes, five velocity modes, wrap/transpose/hold behavior, step velocity as a modulation source, and deterministic chord/step note ownership.
- Distortion (overdrive, foldback, clip, decimate, bitcrush), phaser, chorus, EQ, reverb, delay, and compressor with model-backed original control coverage. The native rack order follows the manual's signal-flow diagram: distortion, phaser, chorus, EQ, reverb, delay, compressor.
- Four sub-banks of 128 programs, program selection and MIDI program changes, program editing and bank persistence, and per-control MIDI Learn/Forget.
- Onscreen notes, pitch bend, modulation wheel, bend range, mono/legato, and portamento controls, with MIDI entering through a bounded realtime-safe queue.

The user explicitly removed compatibility with old Synthia patches on 2026-10-09. The instrument MUST use one native architecture. Legacy sound preservation, architecture selection, and conversion UI are not requirements and MUST NOT constrain implementation. New presets and host states MUST round-trip correctly; unsupported old formats MAY be rejected with a clear message. User files outside the repository MUST NOT be deleted or rewritten as part of this change.

Release acceptance requires the integrated current commit to pass the repository quality gate, focused control/persistence tests, visual and audio reference checks for every claimed fidelity level, current AU/VST3 host checks, optimized performance checks, and the distribution checklist. Public release MUST NOT be inferred from a successful developer build. Signing/notarization, licensed assets/dependencies, supported-host coverage, and missing external references remain separate evidence requirements.

`docs/CLASSIC_PARITY.md` tracks control coverage and proof. `docs/exec-plans/active/2026-10-09-complete-classic-native-rebuild.md` tracks execution and unresolved dependencies.

## 2. Product Boundary

Synthia owns its original implementation, synthesis and MIDI behavior, native controls, current preset/program state, host integration, validation, and release artifacts. The first release targets macOS AU and VST3; Standalone supports inspection and validation. It does not require an account or a network connection at runtime.

The classic reference defines the primary user experience. Modern modulation halos, scopes, browser dashboards, AI controls, and other extensions MUST NOT displace the original control surface. Future work must be separately scoped after native conformance.

## 3. Parameter and Control Contract

- Each public parameter MUST have one precise purpose, a stable current identifier, type, range, default, choices, display conversion, and host automation behavior.
- Public controls MUST affect the native sound path or a documented local operation. Serialized no-op parameters from the old scaffold MUST NOT remain as apparent instrument controls.
- Parameter IDs MUST NOT encode UI coordinates. After public release, IDs MUST not be reused for different meanings.
- New Init and all shipped factory presets MUST use the current native contract. Old Synthia patch compatibility is explicitly not required.
- Every control gesture MUST notify the host correctly. Fine editing, resetting, section copy/paste, touched-control value display, and per-control MIDI Learn/Forget MUST use the same state as host automation and presets.
- Continuous sound controls SHOULD be smoothed where a sudden transition creates unintended zipper noise or gain jumps. Changes MUST remain finite and bounded.
- UI selection, the current part, LCD page, and display scale MAY be local state, but MUST NOT conceal unbound or nonfunctional controls.

## 4. Synthesis and Routing

### Oscillators and Voices

Each note has two parts with two stereo oscillator slots each. Each slot supports eight reference waveform families and zero through eight stacked voices. Zero voices is the visible off state. Octave, note, fine tune, level, phase, retrigger, invert, detune, stereo separation, and pan MUST apply independently to all four slots, including A1.

Saw and pulse families MUST be band-limited. Deterministic test mode MUST define noise and initial-phase behavior. Stereo spread MUST separate oscillator voices, not average their pan into one mono bus. Unused sources MUST skip unnecessary rendering.

Polyphonic, monophonic, legato, pitch-bend, sustain, voice stealing, and portamento behavior MUST be explicit. Normal/Slide portamento and velocity movement MUST follow the documented control modes. Held/generated note ownership MUST survive overlapping notes and relevant parameter changes without stuck notes.

### Filters, Envelopes, and Mixer

Each part has its own stereo filter and amp ADSR. Filter input selection chooses none, source A, source B, or both. Part output mute/solo controls destination output; source routing is separately determined by oscillator voices and filter input selection.

The primary filter controls expose bypass, lowpass, bandpass, highpass, 12/24 dB response, cutoff, resonance, and drive. Shared cutoff/resonance/keytrack and Warm Drive affect both part filters. Self-oscillation, drive behavior, and tuning require focused measured tests and later original-reference comparison.

The two part amp envelopes independently shape the filtered output. A releasing voice MUST remain alive until the required part envelopes and bounded drain behavior finish. Part levels mix after filtering and envelopes. Master level applies after master effects.

### Modulation

Two modulation envelopes and two LFOs MUST be independently configurable. LFO controls include shape, rate, gain, offset, free-running behavior, and host sync. Selected-version waveform coverage MUST be verified from actual references; unverified names or counts MUST NOT be fabricated.

Modulation includes the original performance/miscellaneous sources and signed destination amounts. The routing model, host state, preset state, and actual realtime destinations MUST agree. Both source slots of each original panel MUST be independently editable. Global/free LFO phase advances during silence and changes to rate/sync/free state MUST take effect without unintended resets.

### Arpeggiator

The arp supports the ten reference mode families, five velocity modes, sync/free timing, octave span, wrapping, and step transpose/velocity/hold. Step sequence, step chord, and ordered/random behaviors MUST be distinct and deterministic when seeded for tests. Triplet and dotted rates MUST have correct tempo ratios.

Held steps MUST preserve common notes and allow legato transitions where documented. Disabling arp/hold, changing relevant chord configuration, transport discontinuities, all-notes-off, and panic MUST release the correct generated notes. A silent gate gap MUST NOT stop the clock of a pending arp event.

### Master Effects

The fixed native order is distortion, phaser, chorus, EQ, reverb, delay, compressor, following the supplied manual's signal-flow diagram. Every visible module control and bypass MUST affect real state.

Distortion supports overdrive, foldback, clip, decimate, and bitcrush with actual oversampled nonlinear processing. Phaser uses six stereo stages; chorus supports the documented dual configuration. Native phaser/chorus processing uses the documented doubled processing rate. Reconstruction filtering MUST be explicit; a quality label alone is not oversampling.

EQ exposes bass/treble frequency and gain. Delay exposes independent left/right timing, feedback, low/high filtering, smear, ping-pong, and width. Reverb exposes pre-delay, damp, size, width, and mix. Compressor exposes attack, release, threshold, ratio, and the supported gain/mix behavior.

The original module switches are the user-facing enable controls. There MUST NOT be an invisible global false gate that makes a checked effect inactive. The standalone renderer MAY bypass the entire rack explicitly for dry validation.

Tail reporting MUST include finite effect/envelope drain behavior. An effect need not produce a decay tail to be audibly different from its dry input. No module may allocate or rebuild delay storage while processing.

## 5. Programs, Presets, and State

The owned program bank contains four sub-banks of 128 slots. It supports selection, previous/next, renaming, copy/paste, inserting copied content, deletion, Init, Reset, bounded randomization, and current bank load/save. Reset restores the appropriate saved program baseline rather than whichever state happens to be current.

MIDI program changes select the first 128 programs as documented by the original manual. Program-change requests arriving on the audio thread MUST be published through a bounded handoff; parsing, bank editing, and parameter-tree replacement run outside the audio callback. Pending older requests MUST NOT overwrite a newer host restore.

Host project state MUST be self-contained, including current program/bank data needed for restore. Current state MUST round-trip without relying on external preset files. Bank/preset validation MUST reject malformed or unsupported input without replacing valid live state. Writes MUST distinguish create-new from overwrite and avoid partial files.

Old Synthia patches need not load or retain sound. External user files MUST remain untouched by development cleanup. The exact current owned format is documented in `docs/PRESET_SCHEMA.md`. Original Sylenth `.fxp`/`.fxb` files are a separate interoperability requirement, not an alias for the owned format.

## 6. MIDI and Host Lifecycle

The instrument accepts MIDI note on/off, pitch bend, modulation wheel, sustain, all-notes-off, all-sound-off, pressure where supported, and program changes. Zero-velocity note-on is note-off. Control input MUST be bounded and sanitized.

Onscreen keyboard and wheel events use a bounded queue. Overflow MUST NOT leave notes stuck or pitch permanently bent. Closing the editor MUST release only interactions it owns; it MUST NOT silence DAW notes or discard unrelated effect tails.

AU and VST3 MUST scan, load, play, automate, save/close/reopen/restore, bounce, change sample rate/buffer size, and open/close their editor during playback on the qualified hosts. Tests must identify the exact binary. A prior build's successful scan or an installed filename is not proof of the candidate currently mapped by the host.

The instrument provides stereo output with no required audio input or MIDI output. Reported latency MUST reflect any actual fixed processing delay. Tail behavior must match host rendering needs.

## 7. Realtime and Resource Safety

The audio thread may render DSP, schedule bounded MIDI, read atomically published state, update lock-free diagnostics, and write buffers. It MUST NOT allocate, wait on locks, parse JSON, touch files/network, log synchronously, or create/destroy heavyweight resources.

Buffers and delay storage MUST be prepared in advance. Logical resets during processing must be fixed-cost state resets, not buffer allocation or unbounded clearing. Cached coefficient calculations and any required per-sample wrapping must be justified and checked by the fitness gate.

NaN, infinity, denormals, invalid parameters, unstable feedback, and uncontrolled gain jumps MUST be guarded. Supported sample rates and blocks MUST not expose out-of-bounds buffers. Public release limits must be validated rather than inferred from a dropdown or universal binary header.

Preset/bank/host writers MUST serialize their control-path updates so the audio snapshot never treats an in-progress multi-writer change as complete. The audio callback MUST not take the writers' lock.

## 8. Visual Acceptance

The classic 908 x 591 reference establishes the logical canvas: compact top performance strip; two oscillators surrounding the selected part's amp envelope; part/global filters on the left; central LCD programs/arp/effects; mixer on the right; two modulation envelopes, two LFOs and miscellaneous routing below; keyboard, wheels, bend range and portamento at the bottom.

Part A/B changes both bindings and appropriate visual state. LCD pages, menus, knob/fader/toggle states, selected values, and disabled states MUST be inspectable in deterministic captures. Uniform high-DPI scaling MUST keep graphics and hit targets aligned. Continuous decorative animation is excluded.

No single global image score can excuse a missing control. Reference comparisons must report dimensions, state, masks, and pixel differences. Missing external goldens MUST fail the requested check. Deliberate owned-brand differences must be stated separately from geometry or functional mismatches.

## 9. Verification and Release

The default quality gate is `scripts/check-quality.sh`. Tests must protect observable behavior and consequential failures rather than mirror implementation fields. Add regression coverage for audible A1 edits, independent stereo/part paths, routing, note scheduling, current persistence, UI lifecycle, and release rejection conditions.

Original-reference audio tests must state original version, patch settings, MIDI, tempo, sample rate, phase/randomization, host, alignment, and explicit error tolerance. The self-authored patch suite and realtime/offline comparison are not original-product sound evidence.

Performance uses optimized builds, real voice/stack loads, relevant buffer/sample rates, UI open/closed, and tail/silence behavior. Do not improve CPU by silently lowering quality or capping requested capability. The historical bar-65 Ableton benchmark is valid only with its actual set and verified current plugin image; a replacement workload must be identified as different evidence.

The build targets macOS 11.0 and later, Apple Silicon and Intel, with AU/VST3 and standalone artifacts. The deployment floor and universal slices are build properties, not runtime qualification. Current Ableton 11.3+ and Live 12, Intel, minimum-OS, and clean-machine proof must be tracked explicitly. Other hosts may be added with named evidence.

Developer artifacts must be marked as such. Public distribution requires a clean versioned candidate, passing validation, complete bundled presets, signatures and notarization, checksums/manifest, install/uninstall guidance, and the applicable licensing/content decisions. Missing credentials or proof MUST fail distribution gates. The release tooling MUST NOT publish remotely without authorization.

The detailed procedures and actual evidence live in `docs/VALIDATION.md`, `docs/BUILD_RELEASE.md`, `docs/CLASSIC_PARITY.md`, and the active execution plan.

## 10. Source and Identity

Implementation and shipped content MUST be original or explicitly licensed for that use. Developers MUST NOT extract proprietary algorithms by inspecting/disassembling binaries or bypass product licensing. Supplied manuals and screenshots are approved research references, not runtime resources or factory presets.

Synthia uses its own product identity and an owned bundle identifier. Third-party logos, screenshot backplates, factory presets, and manual content must not be represented as Synthia assets. The source/dependency licensing mode and any broader visual-asset approval remain explicit public-release decisions.

No runtime telemetry, accounts, cloud sync, or network service is required for this release. User-facing errors should explain the failed operation without exposing unrelated private data.
