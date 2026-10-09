# Classic Native Conformance

Status: integrated native implementation through `12a24a3` passes the Debug quality gate, 11/11 Release CTest entries, AU validation, and AU/VST3 saved-set reopen/play in Live 11.3.43, 2026-10-09. Full host qualification, original-reference conformance, and public distribution remain open. An implemented control is not automatically a measured match to Sylenth1.

## Reference and Release Scope

The working visual reference is the classic v3.0.2 screenshot at `research/sylenth1-screenshots/official-lennardigital-main-ui.jpg` (908 x 591). The supplied `Sylenth1Manual.pdf` defines controls and signal flow. Its older screenshots must not silently override the selected skin geometry. Source-index images of other skins are research only.

The release includes native macOS AU, VST3, and a standalone validation application. Browser/Wasm, AI generation, conversational editing, and new modern synthesis features are deferred.

The interface uses Synthia's own identity and code-drawn assets. Any reference-image mask must identify a deliberate owned-branding difference. Masking missing controls, layout errors, entire panels, or dynamic states merely to make a comparison pass is not permitted.

## Control and Signal-Path Checklist

| Area | Required behavior | Implementation/proof status |
| --- | --- | --- |
| Oscillators | Four equivalent stereo slots; eight waveforms; 0-8 voices; pitch/fine, phase/retrigger/invert, detune, stereo/pan/level; section copy/paste | Implemented; native frequency, audibility, silence and stereo tests pass |
| Part paths | Filter A/B input none/A/B/AB; independent stereo filters and amp ADSR; part selection/solo; shared filter controls; post-part mixer | Implemented; isolation, crossrouting, release lifetime and zero-volume tests pass |
| Modulation | Mod Env 1/2, LFO 1/2 with gain/offset/free/sync, miscellaneous sources, original destination coverage and signed depth | Implemented routes include Step Velocity, combined resonance and Phaser CenterFreq; selected-version LFO waveform coverage remains incomplete/unverified |
| Arpeggiator | Ten modes, five velocity modes, sync/free timing, octaves, wrap, step transpose/velocity/hold, step-velocity source | Implemented; sequence, held-note, timing and release tests pass |
| Effects | Five distortions; six-stage phaser; dual chorus; EQ; reverb then delay; compressor; all original controls and bypass | Implemented source-owned DSP; module behavior and rack order tests pass; original audio equivalence unverified |
| Programs | Four sub-banks x128 slots, prev/next/select, host/MIDI selection, rename/copy/paste/insert/delete, Init/Reset/Randomize, owned bank load/save | Implemented; real-processor bank boundaries, persistence and Reset tests pass |
| Performance | Keyboard, pitch/mod wheels, bend range, mono/legato/portamento, global sync, MIDI Learn/Forget | Model/queue tests pass; native mouse and current-host proof still required |
| Persistence | Self-contained current host-state/program-bank restore; old Synthia compatibility explicitly out of scope | Implemented; XML numeric values, dirty fingerprints, saved baselines and restore tests pass |
| Visual states | Entire classic canvas, Part A/B, every LCD page, menus, selected/disabled controls, scale behavior | Ten inspected captures have valid bindings; repeated Init export is deterministic; original pixel identity remains unverified |

Recorded integrated result: all 11 CTest entries and all 17 standalone core reports pass under the Debug quality gate. The optimized developer package `build/release-artifacts/synthia-native-0.1.0-developer-20261009T183920927525Z` records the same 11/11 and 17/17 passing results in Release, verified ad-hoc signatures, universal slices, and 18 owned presets per bundle. Its manifest identifies clean source `1f246f9`; these results do not qualify later unbuilt edits.

Current host observations establish AU/VST3 saved-set reopen/play after installing the optimized accessibility candidate `12a24a3`, with new mapped executable hashes recorded separately from the earlier package. Native macOS accessibility actions in Standalone successfully edited volume, oscillator waveform/retrigger, and the independent Part B octave. Host synth-control coverage, automation, editor lifecycle, buffer/rate changes, comprehensive state restore, and host bounce comparison remain open. See [the current host record](host-validation/native-rebuild-2026-10-09.md) for exact identities, conditions, and limits.

The recorded Release headless supersaw benchmark rendered five seconds at 48 kHz/128 samples with six held notes, per-block parameter publication, 1.5 seconds warmup, and three repetitions. Median wall time was 562.638 ms (8.88671 times realtime), with zero invalid samples. This is not a historical nine-track Live CPU measurement or a paired performance improvement. Accessibility source `12a24a3` passes 11/11 Debug CTest entries and 17/17 core reports, plus 11/11 Release CTest entries. Its new Pluck snapshot is byte-identical to the gallery image (SHA-256 `140a59dd1d53343d729757d51cbda751caa33567428ab75ddb20a37133a93828`).

The two native LFOs currently expose eight implemented shapes. The supplied older manual describes ten, while current official specifications describe eleven. No verified selected-version list is available. Do not label waveform coverage complete or invent names to satisfy a count.

Global Phaser CenterFreq modulation uses the most recently triggered active voice, retains that voice through release, then falls back to the next active voice. This is an explicit implementation policy pending reference verification, not a verified original algorithm.

## Evidence Levels

1. Build and model tests establish that the implementation compiles, validates data, and renders safely.
2. Control tests establish that each exposed control produces the intended audible or state change.
3. Host proof establishes that the exact candidate binary scans, plays, automates, restores, and survives lifecycle changes in an identified host/environment.
4. Reference-image and reference-audio comparisons establish only the fidelity dimensions and tolerances actually measured.
5. Distribution proof establishes signatures, notarization, package contents, installation, and the released artifact identity.

No level substitutes for another. Claims must include the build commit, configuration, reference identity, environment, and actual comparison result.

## Audit Baseline

Before this rebuild, commit `3c33275` built universal bundles and passed 8/9 CTest tests. The patch-recreation failure required an effects tail even for a preset using only distortion/EQ/compression. The original slot-A1 octave parameter did not change rendered WAV bytes. Both findings require durable regression coverage.

The June host records used Live 11.0.12 on Apple Silicon and precede later code changes. They are historical evidence, not release qualification for this rebuild. The source tree had no original-plugin reference-audio fixtures. Standard local AU/VST/VST3 directories contained Synthia but no Sylenth1 installation during the audit.

The user subsequently removed the old-patch compatibility requirement. The release uses one native architecture; there is no legacy mode or conversion workflow. Existing user files remain untouched.

## External Proof and Distribution Dependencies

- A selected original-version installation and authorized deterministic audio captures are needed before audio equivalence can pass.
- Pixel identity requires canonical lossless captures at specified sizes/scales and states. The compressed reference JPG is useful layout evidence but cannot establish lossless source pixels or runtime interactions.
- Original proprietary preset compatibility needs documented mappings and authorized test files. The owned Synthia bank format does not establish it.
- Public distribution needs the applicable JUCE/project licensing decision, reviewed shipped assets/branding, Developer ID credentials, a notarization profile, and clean-machine installation proof.
- Host and hardware coverage must reflect actual tested systems. Universal binary inspection alone is not Intel-host execution proof.

These dependencies must remain open until evidence exists. Do not replace them with self-comparisons or silently relax acceptance.
