# Documentation Index

This directory holds implementation-facing documentation for synthia. `../SPEC.md` remains the source of truth for product requirements.

## Start Here

- `../README.md`: project orientation.
- `../SPEC.md`: full product specification.
- `../CONTEXT.md`: vocabulary and decision lanes.
- `../AGENTS.md`: workspace rules for coding agents.

## Engineering Docs

- `CLASSIC_PARITY.md`: current native classic release scope, reference policy, control coverage, and proof gaps.
- `exec-plans/active/2026-10-09-complete-classic-native-rebuild.md`: active implementation and release qualification plan.
- `ARCHITECTURE.md`: component model, planned code layout, realtime boundaries.
- `BUILD_RELEASE.md`: optimized macOS bundle builds, local Ableton install, packaging, and release upload handoff.
- `PRESET_SCHEMA.md`: preset and host-state shape.
- `QUALITY.md`: local formatting, linting, realtime-safety, and build-loop quality gates.
- `VALIDATION.md`: tests, render fixtures, host checks, and acceptance metrics.
- `modern-synthia-baseline.md`: Phase 1 Sylenth rebuild baseline, current gaps, and Phase 2/3 AI roadmap.
- `PLANS.md`: routing for Programs and ExecPlans.
- `host-validation/`: Ableton and other host validation notes/templates.
  - `host-validation/local-install-troubleshooting.md`: local AU/VST3 install, uninstall, ad-hoc signing, AU validation, and Ableton rescan troubleshooting.
- `programs/`: active multi-slice initiative packets.
- `exec-plans/`: active and completed implementation-slice plans.

## Research

- `research/source-map.md`: vendor docs, host docs, manual-derived facts, inferred target behavior, and unknowns.
- `../Sylenth1Manual.pdf`: local manual used for Phase 1 feature and workflow evidence.
- `../research/sylenth1-screenshots/SOURCE_INDEX.md`: traceable source map for the local Sylenth screenshot corpus.

## Maintenance Rule

When docs disagree:

1. `SPEC.md` wins on product requirements.
2. `docs/programs/active/*/program.md` wins on initiative-level progress and slice order.
3. `docs/exec-plans/active/*.md` wins on one-slice execution details while that slice is active.
4. `ARCHITECTURE.md`, `VALIDATION.md`, and `PRESET_SCHEMA.md` are implementation guidance and must be updated to match spec changes.
