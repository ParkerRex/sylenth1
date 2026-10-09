---
title: Complete Classic Native Rebuild
status: active
created_at: 2026-10-09
completed_at: null
summary: Complete native classic instrument behavior, editor, reference validation, and release qualification.
post_build_recap: null
read_when:
  - Continuing the native classic rebuild.
  - Checking ownership, integration order, or release evidence.
---

# Complete Classic Native Rebuild

## Purpose / Big Picture

Deliver Synthia's native AU/VST3 instrument against the classic Sylenth1 control and visual reference, with working original behaviors and explicit release proof. The user authorized full parallel implementation on 2026-10-09 and deferred the browser initiative. Keep changes local; pushing or public publication was not requested.

## Progress

- [x] Audit baseline code, manual, screenshots, GitHub, fresh builds, tests, and control behavior.
- [x] Preserve the original checkout and five unpushed commits; create managed worktree `sylenth-native-rebuild` from `3c33275`, branch `codex/classic-sylenth-rebuild`.
- [x] Assign six GPT-6.1 Sol high agents exclusive file ownership.
- [x] Establish classic-reference requirements and evidence boundaries in SPEC and CLASSIC_PARITY.
- [x] Implement the native engine, state/program contracts, classic editor, and documented arp/effects surfaces. Selected-version LFO waveform coverage remains a reference-dependent gap.
- [x] Integrate focused behavioral tests and correct obsolete effects-tail/bypass assumptions without weakening audibility or residual-silence checks.
- [x] Complete reference comparison tools that fail honestly on missing inputs, aliases and invalid approval metadata.
- [x] Build integrated universal bundles and pass the repository quality gate: 11/11 CTest and 17/17 core reports.
- [x] Inspect ten deterministic editor captures and fix bindings, glyphs, labels, version text and state initialization.
- [x] Run adversarial reviews of realtime, current state, routing, UI, reference tooling and packaging boundaries; fix verified findings.
- [ ] Obtain selected-version original waveform/control and audio/image evidence; close the unverified fidelity gaps.
- [x] Build an optimized developer candidate; pass Release 11/11 CTest and 17/17 core reports, AUval, AU/VST3 scan/load/play with mapped binary identity, initial AU save/reopen/play, and an identified headless workload.
- [x] Commit writable native accessibility controls and focused processor coverage as `12a24a3`; pass Debug 11/11 CTest and 17/17 core reports and confirm unchanged Pluck snapshot pixels.
- [x] Verify optimized accessibility candidate: 11/11 Release CTest, AUval, new mapped AU/VST3 identities, both saved-set reopen/play checks, and native Standalone control actions.
- [ ] Complete host automation, comprehensive changed-state restore, lifecycle, buffer/rate, host bounce, and host CPU coverage.
- [x] Produce and verify ad-hoc-signed developer AU/VST3/Standalone artifacts with universal slices, owned presets, manifest, and checksums.
- [ ] Qualify public distribution only with required licensing decisions, credentials, reference captures, supported-environment evidence, and publication authorization.
- [ ] Reconcile all durable docs with the final implementation and outstanding external proof.

## Surprises & Discoveries

- The baseline UI stores A1 octave but the legacy oscillator ignores it; a one-octave edit rendered identical WAV bytes.
- The baseline effects test treats zero decay tail as inaudible effects, rejecting valid distortion/EQ/compression.
- Legacy parts sum before one nonlinear filter. The user explicitly removed old-patch compatibility, so there is no need to preserve that sound path.
- Real JUCE XML state serializes numbers as strings; rejecting those strings lost restored values. The shared strict parser now accepts finite full numeric strings.
- Dirty-state fingerprinting must use actual JUCE step indices and normalized values rather than wide-range physical float round trips.
- Processor idle skipping initially froze FREE LFOs and long arp intervals. Bounded idle LFO advancement and pending-event checks now have real-processor regression proof.
- Repeated bank-state scans caused excessive test/restore time; indexed control-state operations and lazy empty slots avoid that work.
- The historical nine-track Ableton set is absent. A separate six-note qualification set was exercised without claiming historical benchmark equivalence.
- The initial package manifest and host executable hashes identify source `1f246f9`; accessibility source `12a24a3` has separate passing Debug/Release checks and current mapped host identities.
- Remote Script 1.7.2 is patched and installed, but the MCP client remains unpatched 1.4.5. Script installation does not establish client PR installation.

## Decision Log

- Use the classic v3.0.2 908x591 view as the working visual reference; keep Synthia-owned identity/assets.
- Superseding user decision, 2026-10-09: old Synthia patch compatibility is not required. Use one native architecture; remove legacy selection/conversion UI and do not spend effort preserving old renders. User files outside the repository remain untouched.
- Use one isolated integration worktree and exclusive file ownership instead of six independently diverging schema branches. The parent coordinates builds; agents do not concurrently rebuild shared output directories.
- Keep reference fidelity, safety tests, host execution, and signing evidence distinct.

## Outcomes & Retrospective

In progress. Accessibility source `12a24a3` passes the Debug quality gate, 11/11 Release CTest, AUval, both saved-set reopen/play checks with current mapped AU/VST3 identity, and native Standalone control actions. It preserves the inspected Pluck pixels. Comprehensive host state restore, automation/lifecycle/render comparison, and measured Live CPU coverage remain open. Live's inaccessible editor-open control limits further automated host interaction. External original-plugin references, licensing decisions, notarization credentials, and untested host/hardware targets remain unresolved. These results do not qualify a public release.

Initial developer artifacts are under `build/release-artifacts/synthia-native-0.1.0-developer-20261009T183920927525Z`. The manifest records clean source `1f246f929a5addfd141aef8c61480608d02119f8`, verified ad-hoc signatures, 18 factory presets per universal bundle, 11 passing Release CTest entries, and 17 passing core reports. Initial installed AU/VST3 hashes match that manifest. The later accessibility build has separate mapped-binary evidence. See [the host qualification record](../../host-validation/native-rebuild-2026-10-09.md).

Final developer artifacts are under `build/release-artifacts/synthia-native-0.1.0-developer-20261009T193930274525Z`, from clean source `5f9778a` with accessibility changes included. The release pipeline again passed 11/11 CTest and 17/17 core reports. All six checksum entries pass, and AU/VST3 executable hashes match the updated mapped host binaries. Public distribution remains unqualified.

The final optimized headless benchmark reports a median 562.638 ms for five rendered seconds (8.88671 times realtime) using the supersaw preset, six held notes, 48 kHz, 128-sample blocks, per-block parameter publication, 1.5 seconds warmup, and three repetitions. It records zero invalid samples. It is not a paired before/after result or the unavailable nine-track Live benchmark.

## Context and Orientation

The C++20 JUCE/CMake project has a preallocated realtime voice engine, APVTS parameter registry, custom preset format, standalone render runner, eleven CTest entries, and historic plus initial current-candidate Ableton proof. The current requirement at the start of SPEC supersedes earlier modernized-UI directions.

### In Scope

Native architecture, complete original-control bindings, classic editor, current program/preset/host workflows, focused tests, reference comparison, optimized host qualification, developer/distribution packaging, and honest documentation.

### Out Of Scope

Browser/Wasm, AI/conversational features, old Synthia patch compatibility, copying proprietary code/assets, and unrequested publication.

## Plan of Work

Engine and contract owners agree on structs and IDs before editor and validation consume them. Arp/effects submits all shared parameter changes to the engine and contract owners. The parent then builds one coherent integrated tree, returns failures to owners, inspects screenshots, and conducts a review pass before host testing and release packaging.

## Milestones

1. Native Init has independent real A/B paths and all original control families, without a parallel legacy profile.
2. Classic editor exposes real behavior at deterministic reference geometry and all program/LCD states.
3. Existing and focused tests pass, with failed reference checks distinguished from missing external evidence.
4. Optimized current binaries pass available host checks and package validation; public distribution gates remain explicit.

## Concrete Steps

Work from `/Users/parkerrex/.codex/worktrees/sylenth-native-rebuild/synthia`.

Use the original checkout's pinned JUCE source at `/Users/parkerrex/Developer/synthia/build/_deps/juce-src` to avoid redundant downloads. Configure fresh worktree-local output directories; never reuse the original checkout's CMake cache. Run one coordinated build at a time with bounded parallelism.

## Validation and Acceptance

Every new DSP/control behavior requires a focused observable check. Include negative controls for no-op parameters, missing references, invalid state, bank operations, module bypass, and signing failures. Current preset and host-state round trips must be deterministic. Exact matching cannot be accepted from Synthia-versus-Synthia renders.

### Test Commands

```sh
cmake -S . -B build -DSYNTHIA_ENABLE_TESTS=ON -DSYNTHIA_JUCE_PATH=/Users/parkerrex/Developer/synthia/build/_deps/juce-src
CMAKE_BUILD_PARALLEL_LEVEL=6 scripts/check-quality.sh
scripts/check-plugin-bundles.sh build Debug
```

Use the final release script's documented options for optimized developer and distribution candidates. Host profiling follows `.codex/skills/profile-synthia-ableton/SKILL.md`, using the exact installed binary and preserving the user's set.

## Idempotence and Recovery

The original checkout stays intact. Build artifacts remain disposable. Do not reset or revert other owners' edits. Keep external user presets/maps intact. Before replacing installed bundles, preserve a recoverable copy and record hashes. Stop host mutation if a user project has unsaved edits that cannot safely be preserved.

## Artifacts and Notes

Baseline audit reports remain in the original checkout under `build/reports/audit-2026-10-09`. New evidence belongs under the integration worktree's `build/reports`. Do not commit generated WAVs, binaries, or capture images unless explicitly requested.

## Interfaces and Dependencies

| Owner | Exclusive files |
| --- | --- |
| engine | SynthParameters, SynthEngine, oscillator/filter/envelope/LFO/ramp and voice sources |
| contracts | ParameterRegistry, PluginProcessor, presets, MIDI, modulation, PRESET_SCHEMA |
| editor | PluginEditor and new plugin/ui sources |
| arp_effects | Arpeggiator and dsp/fx sources |
| validation | tests, SynthRender, VALIDATION, reference comparison/capture scripts |
| release | CMake, remaining build/release scripts, BUILD_RELEASE, local-install guidance |
| parent | SPEC, CONTEXT, CLASSIC_PARITY, architecture, README, program/plan indexes, integration and host evidence |
