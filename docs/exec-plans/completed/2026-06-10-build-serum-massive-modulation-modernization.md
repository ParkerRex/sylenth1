---
title: Build Serum/Massive Modulation Modernization
status: completed
created_at: 2026-06-10
completed_at: 2026-06-10
summary: Move the surface past the vintage Sylenth skin: a Step LFO with a drawable grid, drag-and-drop modulation with knob halo rings on the TransMod engine, a live output scope, and a flat midnight reskin with zone colour coding.
post_build_recap: Added the Step LFO shape with a 16-slot table/count/smooth (18 new parameters, DSP in Lfo/Voice/VoiceAllocator with engine-boundary clamps and a regression test), a lock-free processor visual feed (scope ring plus LFO phase/value atomics), a full flat-dark reskin with zone hues and lit knob arcs, a visual LFO module with a drawable step grid and live playhead, drag-and-drop modulation from a source chip strip onto destination knobs with Serum-style halo rings (alt-drag depth, right-click clear) compiled through ModulationRouteModel, a live scope in the centre display, an offscreen UI snapshot hook, a Wub Stepper factory preset, per-preset error arrays in validate-presets reports, an LCD stale-preset-name fix, and stale patch-cost contract-test expectations corrected after 0457fe4.
program_id: synthia-lab-rebuild
read_when:
  - Reviewing the Step LFO model, parameters, or smoothing semantics.
  - Extending drag-and-drop modulation, halo rendering, or the source chip strip.
  - Auditing the realtime UI visual feed (scope ring, LFO phase/value atomics).
  - Regenerating UI screenshots headlessly via SYNTHIA_UI_SNAPSHOT.
---

# Build Serum/Massive Modulation Modernization

This ExecPlan modernizes the instrument surface and modulation workflow: the
parts of Serum (drawable stepper LFO, live modulation visuals, drag-and-drop
routing with knob rings) and Massive (colour-coded sections, stepper source,
chip-style macro/source handles) that fit the existing TransMod architecture,
replacing the vintage Sylenth skin while keeping its layout density.

This ExecPlan must be maintained in accordance with `docs/exec-plans/PLANS.md`.

## Purpose / Big Picture

Phase 1 baselined the Sylenth workflow. The product direction moved from
pixel-faithful recreation to a modern instrument: the surface looked dated and
the modulation workflow required visiting the TransMod slot forms. This slice
makes modulation direct (drag a source onto a knob), visible (halo rings, live
playhead, output scope), and sonically current (step-sequenced LFO), without
adding any UI-only state: every gesture compiles into existing `lfo.*` and
`transmod.*` parameters.

## Progress

- [x] 2026-06-10 EDT: Step LFO DSP: `LfoShape::Step` with held/crossfaded steps, `lfo.step_count`/`lfo.step_smooth`/`lfo.step.1..16` parameters, processor wiring, per-block step pushes in `Voice`/`VoiceAllocator` (no phase reset on edits), engine-boundary clamps, and a DSP core regression test.
- [x] 2026-06-10 EDT: Realtime visual feed: `publishUiVisuals` writes a wait-free output-scope ring and first-active-voice (or mono) LFO phase/value atomics after rendering; editor reads atomics only.
- [x] 2026-06-10 EDT: Flat midnight reskin: repointed the stable palette, rewrote knob/toggle/panel/caption painting, reinstated functional-zone ticks, zone-tinted knob arcs, restyled the centre display.
- [x] 2026-06-10 EDT: Visual LFO module: bound controls plus display; classic shapes render waveforms, Step mode is a drawable grid with gesture-correct `setValueNotifyingHost` writes and a live playhead.
- [x] 2026-06-10 EDT: Drag-and-drop modulation: MOD SOURCES chip strip, `ModTargetSlider` halo knobs for the five TransMod destinations, drops into free slots through the route write adapter, alt-drag depth editing, right-click slot clearing, live LFO dot on rings.
- [x] 2026-06-10 EDT: Proof and hardening: offscreen `SYNTHIA_UI_SNAPSHOT` hook (+preset/+quit variants), `BA - Wub Stepper 01` factory preset, validate-presets error arrays, LCD preset-name refresh on processor-side loads, README hero regenerated, SPEC/ARCHITECTURE/PRESET_SCHEMA/VALIDATION updates.

## Surprises & Discoveries

- The stale `build/` cache still pointed at the pre-rename `sylenth-ai` path, so
  CTest had not run since the rename. Reconfiguring exposed two pre-existing
  failures from the perf sprint: stale patch-cost expectations in the contract
  test after `0457fe4` changed the default filter oversampling to Off (fixed
  here), and `bass-wub-01` failing its wet-meaningful render check (left open;
  tracked separately).
- `SynthEngine::setParameters` clamps `lfo.shape` at the audio boundary; the
  clamp ceiling silently demoted the new `Step` choice to `Noise`, which read as
  an all-zero LFO. The DSP regression test caught it; the clamp now tracks
  `LfoShapeChoice::Step` and the step fields get their own boundary clamps.
- The mono/Song-gated LFO lives in `VoiceAllocator`, not `Voice`, so the step
  table had to be pushed in `syncMonoLfoConfig` too — and ahead of its
  config-change early-return so live step edits never reset phase.
- The LCD preset name only refreshed through editor-initiated preset actions;
  processor-side loads (and host restores) left it stale. The workflow refresh
  now re-syncs the LCD readout.
- macOS screen-recording TCC is unavailable in scripted sessions, which
  motivated the offscreen snapshot hook; it doubles as the new way to keep the
  README hero and UI QA artifacts reproducible.

## Decision Log

- Append `Step` to `lfo.shape` rather than adding a separate mode parameter:
  the shape list is the product surface for it (Serum/Sylenth precedent), the
  plugin is pre-release, and presets serialize choices by name. The AU version
  hint policy was left unchanged pending the next host-matrix sweep.
- Default step table is a descending ramp so the Step shape is audible
  immediately at any step count.
- Knob halo "remove" clears the whole TransMod slot via the existing
  deterministic clear adapter; adapter-written slots carry one destination, and
  the menu labels the action as a slot clear to stay honest for hand-built
  multi-destination slots.
- The scope ring uses relaxed atomics per sample (visualization tolerates
  tearing); the LFO dot publishes from voice 0 or the mono LFO, which is the
  same value the audible path uses.

## Validation Evidence

- `ctest` green except the pre-existing `SynthiaPatchRecreationSuite`
  `bass-wub-01` failure (predates this slice; verified by stash-testing the
  pristine tree, tracked as separate work).
- New `testStepLfoTracksStepTable` covers step values, boundaries, and
  mid-note smooth edits without phase reset.
- `SynthiaRender --validate-presets presets/factory` passes including the new
  Wub Stepper preset.
- Offscreen snapshots verify the reskin, STEP badge, drawn step grid, halo ring
  on the routed cutoff knob, mod source strip, and live preset-name LCD fix;
  the README hero is regenerated from the same hook.
