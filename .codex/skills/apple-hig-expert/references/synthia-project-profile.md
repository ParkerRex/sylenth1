# Synthia project profile

Project documents reviewed: 2026-10-13. This profile translates the copied skill into Synthia's existing contract.

## Authority and scope

`SPEC.md` wins on product requirements. `CONTEXT.md` defines vocabulary; `docs/ARCHITECTURE.md` defines native/realtime boundaries; `docs/VALIDATION.md` defines evidence. Read `docs/CLASSIC_PARITY.md` for original-reference claims and `docs/SCREENSHOTS.md` for captures.

The active release recreates the classic Sylenth1 control surface with owned implementation/assets and Synthia identity. HIG guidance should improve usability and accessibility within that scope. A proposed conflict resolution that changes requirements belongs in `SPEC.md` when authorized, not only in this skill.

The platform is macOS, delivered as AU and VST3 with Standalone for inspection and validation. Browser/Wasm, mobile interfaces, AI generation, accounts, and conversational editing are deferred. There are no Worker panels, frontend tokens, CSS grids, browser zoom rules, or DOM measurement inputs.

## Instrument structure

- Preserve the 908 x 591 classic composition, Part A/B binding changes, central LCD program/arp/effect pages, modulation rows, mixer, and bottom keyboard/wheels/portamento.
- Page selection and module enablement are separate states. An effect's selected page must not imply it is enabled.
- Distinguish source routing from destination mute/solo. Accessible labels and feedback must describe the operation actually performed.
- Use the same parameter state for mouse, keyboard, accessibility, host automation, MIDI Learn, presets, and host restore. UI labels may improve; public IDs must remain stable.
- Presets are `.SynthiaPreset`; owned banks are `.SynthiaBank`. Do not imply proprietary `.fxp`/`.fxb` compatibility.

## Interaction and feedback

Prefer precise labels, current values with musical units, clear selection/enabled/disabled states, and predictable edits. For example, identify an independent oscillator as A1 or B2 and distinguish a part cutoff from shared filter control. A tooltip is supplementary help, not an accessible name.

Review fine editing, reset, section copy/paste, program operations, and MIDI Learn/Forget against the documented behavior. Keep errors specific to the failed operation. An invalid load must preserve valid live state; saving must distinguish a new file from an overwrite.

Keyboard, pitch bend, and modulation-wheel input use the processor's bounded event queue. Closing an editor must release only its own interactions. Do not propose a global panic as a substitute for fixing UI note ownership.

## Visual and performance constraints

Keep useful information density and the classic geometry. Do not add decorative cards/pills, faint subtitle rows, or continuously repainting decorative effects. Bound meter/readout refresh to real state changes.

Use uniformly scaled owned graphics with matching hit regions. Report text or target limitations at each tested scale rather than hiding them behind a screenshot comparison. Any accessibility recommendation that changes geometry needs an explicit contract decision.

Keep UI updates, file operations, accessibility work, and resource construction off the audio thread. If a UI change is claimed to improve CPU, use optimized builds and comparable open/closed-editor host measurements; a design estimate is not a measured result.

## Evidence paths

Use `docs/VALIDATION.md#headless-ui-snapshot` for the current snapshot variables and binding-report procedure. Store generated output under `build/`. Do not commit evidence snapshots unless requested.

Images establish rendered state only. Mouse hit testing, keyboard navigation, VoiceOver, automation, program restoration, and editor lifetime need runtime evidence. Passing Synthia tests or captures does not establish Sylenth1 equivalence, current AU/VST3 qualification, or distribution readiness.
