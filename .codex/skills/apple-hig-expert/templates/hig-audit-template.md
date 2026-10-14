# Synthia native HIG review

Use this structure when the requested review needs a durable report. Keep a narrow review brief. Repository paths below resolve from the project root.

## Context

- Date and reviewer:
- Scope and musical task:
- Surface: AU / VST3 / Standalone; host and format:
- Revision and actual binary:
- macOS version and display:
- Editor scale and backing scale:
- Part, LCD page, preset/program, and playback state:
- Sample rate and buffer size when relevant:
- Input methods and accessibility settings:
- Evidence paths and source verification dates:

This records evidence for the stated scope. It does not certify HIG compliance, original-plugin fidelity, all-host support, or release readiness.

## Outcomes and evidence

Use one outcome per catalog rule: `pass`, `fail`, `untested`, or `na`. Keep a reason for rules outside this review. Missing runtime or measurement evidence is `untested`, never partial credit.

Evidence labels are `measured`, `observed`, `needs runtime test`, or `assumption`. A fail may come from an observed or measured defect; a full task pass requires enough evidence to exercise that task.

| Outcome | Count |
| --- | ---: |
| Pass | |
| Fail | |
| Untested | |
| N/A | |

Counts describe the reviewed scope, not a compliance score. State the most important user impact and unresolved evidence in plain language.

## Measurements

| Control/state | Editor/backing scale | Measured value and units | Rule/source | Result | Evidence |
| --- | --- | --- | --- | --- | --- |

Record effective hit regions in macOS points, not only painted sizes or screenshot pixels. Include coordinate conversion, spacing, type size/weight, foreground/background, and measurement method when applicable. The catalog records macOS-specific recommendations; distinguish them from project requirements and broad button guidance.

For before/after claims, retain both values and evidence paths under identical conditions. A new threshold or context needs a separate comparison.

## Rule results

Use `config/hig-rubric.yaml` from this skill for stable IDs, authority, and sources. Project paths in that catalog resolve from the repository root.

| Rule ID | Authority | Outcome | Evidence or N/A reason | Finding |
| --- | --- | --- | --- | --- |

Review the areas that apply to the task:

- Classic layout, Part A/B and LCD state, typography, scaling, and hit alignment.
- Keyboard, pointer, focus, accessible names/values/actions, and host gesture paths.
- Contrast, state without color, relevant display settings, and bounded feedback.
- Program/file recovery and UI note ownership when affected.

A screenshot proves rendered state. Keyboard, mouse targeting, VoiceOver, automation, restore, and editor lifetime require native runtime evidence. Keep host and Standalone results separate.

## Findings

Order findings by impact. Use severity 1 for minor clarity issues, 2 for a material task impediment, 3 for a blocked task, and 4 for loss of work or consequential playback failure. Severity describes observed impact; explain assumptions.

For each finding, record:

- Stable finding ID, impact, and affected task/control.
- Observation or measurement with exact state and evidence.
- Rule/source and whether it is Apple guidance or a project requirement.
- Smallest useful correction and any conflict with the classic contract.
- Repeatable check that could disprove the finding or verify a correction.

## Verification and limits

List exact commands or native task steps, results, tested revision/binary, and unavailable checks. Follow `AGENTS.md`, `docs/VALIDATION.md`, and `docs/SCREENSHOTS.md`; keep generated artifacts under `build/`.

Name the evidence still needed for each applicable `untested` rule. Source inspection, older screenshots, and historical host records do not qualify a current binary. Do not install or replace plugins, restart a host session, or change product requirements without the task's authorization.
