---
name: apple-hig-expert
description: Design or audit Synthia's native macOS JUCE interface using current Apple Human Interface Guidelines and the classic instrument contract. Use for control interaction, accessibility, typography, scaling, and AU/VST3 editor reviews. Excludes browser, mobile, AI-feature, and DSP-only work.
license: MIT
metadata:
  version: 2.1.0
  author: Alireza Rezvani
  adapted-by: Worker and Synthia contributors
  category: design
  updated: 2026-10-13
---

# Apple HIG Expert for Synthia

Use current Apple Human Interface Guidelines (HIG) to improve Synthia's native interface. Preserve the classic instrument contract in `SPEC.md`. Distinguish Apple recommendations, project requirements, measured defects, and untested behavior. An audit does not certify HIG compliance, original-plugin fidelity, or release readiness.

Adapted from `little-worker/.agents/skills/apple-hig-expert` version 2.0.0. The source's browser measurement scripts, web profile, AI guidance, and other-platform routes are outside Synthia's current release and are omitted. This skill uses native evidence and needs no Bun or browser dependencies.

## Establish the context

Read the repository's start-here documents as required by `AGENTS.md`, then [synthia-project-profile.md](references/synthia-project-profile.md). For visual claims, also read `docs/CLASSIC_PARITY.md` and `docs/SCREENSHOTS.md`.

Repository paths in code spans and catalog sources resolve from the project root. Markdown reference links resolve from the containing skill file.

Record:

- Surface: AU editor, VST3 editor, or Standalone. Name the host and format.
- Implementation: C++20 and JUCE Components, not SwiftUI or an HTML interface.
- Conditions: macOS/host version, display, editor scale, backing scale, current part, LCD page, preset/program, and playback state.
- Inputs: mouse/trackpad, keyboard, and relevant assistive technology. MIDI performance and host automation are separate interaction paths.
- Evidence: source, specification, rendered capture, running product, accessibility tree, or completed host task. Identify the revision and actual binary for runtime claims.

A question requests an answer or review. Apply code changes only within the user's requested scope.

## Route to relevant guidance

- Read [platform-specifics.md](references/platform-specifics.md) for macOS and plugin-host boundaries.
- Read [design-principles.md](references/design-principles.md) for purpose, control, clarity, and tradeoffs.
- Read [visual-design.md](references/visual-design.md) for classic geometry, type, color, and materials.
- Read [accessibility.md](references/accessibility.md) for target measurements, keyboard focus, VoiceOver, and display settings.

[hig-rubric.yaml](config/hig-rubric.yaml) records applicable rules, source authority, measurement units, and verification dates. Its project rules summarize existing requirements; they do not create new product requirements. Read only the references needed for the task.

Live Apple documentation owns current HIG claims. `SPEC.md` owns Synthia's product contract. If a recommendation conflicts with that contract, report both and propose a scoped resolution. Do not silently redesign the interface, change the contract, or treat accessibility gaps as acceptable merely because a reference image contains them.

## Design or audit

For a design, identify the musical task and control state first. Improve the authorized interaction within the classic canvas and real parameter model. Do not introduce dashboards, decorative containers, continuous animations, or a new frontend architecture.

For a review:

1. Define the requested surface, task, evidence, and observable outcome.
2. Select applicable rules. Retain `na` reasons for rules outside the task.
3. Measure what the evidence supports. Inspect real hit regions, effective scale, text size, rendered contrast, focus, accessible values, and host gestures as applicable.
4. Exercise the affected native task when source or images cannot prove it. Reuse repository validation procedures; record unavailable host or assistive-technology checks.
5. Report findings by user impact, with evidence, rule/source, the smallest useful correction, and any product-contract conflict.

Use [hig-audit-template.md](templates/hig-audit-template.md) when a structured report helps. Keep brief reviews brief. Outcomes are `pass`, `fail`, `untested`, or `na`; evidence is `measured`, `observed`, `needs runtime test`, or `assumption`. Do not produce a synthetic compliance score.

## Evidence and verification

Native measurements use macOS points. State how JUCE logical coordinates, editor scaling, and display backing pixels map to the measured region; do not equate screenshot pixels with points. Measure the interaction region, not only the painted knob or glyph.

Use `docs/VALIDATION.md#headless-ui-snapshot` for deterministic captures. Save generated captures and reports under `build/`. A snapshot proves rendering, not mouse behavior, keyboard navigation, VoiceOver operation, automation, or host restoration.

For before/after claims, use the same control, host/format, state, scale, units, source rule, and conditions. Preserve both measured values and evidence paths. Changed thresholds require a separate comparison. Missing evidence is `untested`.

For documentation-only work, check links, paths, YAML/frontmatter, and `git diff --check`. For code changes, run the repository quality gate and affected UI/host checks in `AGENTS.md` and `docs/VALIDATION.md`. Do not install plugins, replace a running host's binary, or restart a session merely to prepare an audit.

## Keep guidance current

The packaged macOS guidance was reviewed on **2026-10-13**. Apple updates the HIG throughout the year.

1. Check [What's new in design](https://developer.apple.com/design/whats-new/) and the applicable live HIG page before a consequential recommendation or when the rule predates newer design updates.
2. Review the actual text and platform tables. A reachable URL alone does not verify a rule. If Apple's page requires JavaScript, its official DocC JSON is available at `https://developer.apple.com/tutorials/data/design/human-interface-guidelines/<page>.json`.
3. Update only the rules actually reviewed, their `lastVerified` dates, and affected explanatory prose. Keep Apple summaries short and link to primary sources.

The latest reviewed design feed includes September 2026 layout/branding guidance and macOS 27 design resources. Their availability does not change Synthia's JUCE implementation, macOS 11 deployment floor, or classic-layout contract. Recheck after WWDC and September releases rather than relying on a fixed annual HIG edition.
