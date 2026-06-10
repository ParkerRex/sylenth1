# synthia

![synthia lab software instrument hero](docs/assets/readme-hero.png)

synthia is a lab-built macOS software instrument project. The product goal is to rebuild everyone's favorite vintage VST, Sylenth1, optimized for today's macOS/Ableton workflow, then extend that foundation with AI-assisted sound design and conversational editing.

## Product Roadmap

Phase 1: recreate the Sylenth experience. The first milestone is a modern AU/VST3 instrument with Sylenth-level immediacy: A/B architecture, fast oscillator/filter/envelope/modulation access, a strong preset workflow, arpeggiator/effects coverage, and Ableton validation. The manual in `Sylenth1Manual.pdf`, the screenshot corpus in `research/sylenth1-screenshots/`, and `docs/modern-synthia-baseline.md` drive product decisions for this phase.

Phase 2: add AI-assisted sound and arpeggio creation. The plugin should be able to randomize and generate useful sounds, chord movement, and arpeggio ideas with the musical intent of tools like Xfer Records' Cthulhu, while keeping all generated state as normal editable presets and parameters.

Phase 3: make the VST conversational. Users should be able to ask for changes in plain language, for example "make the bass wubbier," or provide a reference sound and ask synthia to recreate the character with editable synth, modulation, arp, and FX settings.

## Current State

The repo builds a JUCE/CMake instrument scaffold with:

- AU, VST3, and standalone targets.
- A dry-core DSP path with oscillator, filter, envelopes, LFO, ramp, glide, velocity glide, amp drive, pan/spread, and performance MIDI sources.
- A bypassable post-voice FX path with saturation, tempo-synced delay, simple reverb, chorus, and realtime/offline quality settings.
- An 8-slot TransMod-style modulation layer with source/scaler routing and physical destination depths.
- A Phase 1 A/B layer and four oscillator-slot backbone in host/preset state; Layer A maps to the current sound path and Layer B is valid but disabled by default.
- Factory presets, preset schema validation, MIDI fixture rendering, and JSON report generation.
- A modern flat dark control surface (shown in the hero above) that keeps the Sylenth workflow density but drops the vintage skin: functional-zone colour coding (source cyan, shaping amber, performance blue, modulation magenta), knobs with lit value arcs, a performance strip with polyphony/voices, Part A/B selection, and preset navigation; oscillator modules (a PITCH sub-box plus a VOLUME/PHASE/DETUNE/STEREO/PAN knob row and an INV/WAVE/VOICES/RETRIG row); a bespoke filter module, vertical ADSR amp/mod envelopes, voice/amp/ramp/macros, and an arp/step/chord grid around a glass centre display that reports the live preset, program slot, dirty state, voice/CPU diagnostics, a live output scope, and echoes any touched control as "name = value".
- Serum/Massive-inspired modulation UX on the existing TransMod engine: a draggable MOD SOURCES chip strip routes any catalog source onto destination knobs (osc pitch, pulse width, filter cutoff, amp level, pan) by drag-and-drop, writing ordinary `transmod.*` slot state; routed knobs show halo rings covering the modulated range with a live dot for LFO routes, alt-drag edits depth, right-click clears the slot.
- A visual LFO module whose Step shape is a drawable 16-bar grid (`lfo.step.*` parameters with step count and smoothing) with a live playhead — the stepper workflow, with `BA - Wub Stepper 01` as its first factory patch.
- Sound, Modulation (read-only route overview plus eight TransMod slots), Effects (fixed-order FX rack), and Browser pages, the last presenting the preset/program workflow as one workspace: factory/user load-save-duplicate, visible invalid-preset browser errors, dirty/init/randomize/reset and A/B compare, metadata-aware Save New/Overwrite safe-save, and a global MIDI Learn surface. Every control binds to a real APVTS parameter; no DSP, parameters, or fake controls were added for the UI.
- Preset workflow model support for metadata-aware writes, no-clobber create-only safe-save checks, dirty-state baseline fingerprints, and local A/B compare slot state.
- Core validation for oscillator/filter behavior, modulation routing, voice allocation, dry/wet renders, standalone realtime/offline quality comparison, render determinism, preset loading, and APVTS automation exposure.

Phase 1 host validation in Ableton is underway: current proof covers AU/VST3 scan-load-play smoke, current VST3 rescan/create/play, AU/VST3 Live-set state restore, VST3 transport run/stop, VST3 offline bounce artifact creation, AU transport run/stop with the hosted AU editor visible, AU/VST3 hosted editor open/close/reopen while transport runs, VST3 learned-CC capture/persistence, VST3 continuous controller value application, VST3 host Forget/stepped controller playback, AU/VST3 all-notes-off/all-sound-off plus hosted Panic, AU seeded controller value application, AU in-editor MIDI Learn capture/persistence, AU global-panel MIDI Forget, AU/VST3 sample-rate/buffer change handling, AU/VST3 hosted `Arp Motion 01` preset editor-state proof, AU/VST3 playback after preset load, AU/VST3 parameter automation record/playback, Ableton offline-versus-realtime content comparison with negative controls, standalone rendered modulation route write/clear proof, and standalone realtime/offline quality comparison. No non-UI Phase 1 host-matrix gap remains open; Ableton audio-diff modulation comparison and strict offline/realtime waveform equivalence are not claimed.

## Build

Configure:

```bash
cmake -S . -B build -DSYNTHIA_ENABLE_TESTS=ON
```

Build:

```bash
cmake --build build --config Debug
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Build artifacts are written under:

- `build/SynthiaPlugin_artefacts/Standalone/synthia.app`
- `build/SynthiaPlugin_artefacts/AU/synthia.component`
- `build/SynthiaPlugin_artefacts/VST3/synthia.vst3`

The default build fetches JUCE `8.0.13`. Set `SYNTHIA_JUCE_PATH=/path/to/JUCE` during configure to use a local JUCE checkout. The previous `SYNTH_*` CMake options are still accepted as compatibility aliases.

## Validation

Run the default quality gate:

```bash
scripts/check-quality.sh
```

This runs repo whitespace checks, the realtime/type-safety C++ fitness gate, CMake configure with `compile_commands.json`, Debug build, CTest, and the standalone core render suite. The slower sweep commands are:

```bash
scripts/check-cpp-format.sh --all
scripts/check-quality.sh --with-tidy
```

Use `--exclude <path>` on the sweep commands when another agent owns a file, for example the active UI editor files. See `docs/QUALITY.md` for sanitizer builds, formatting policy, clang-tidy, and realtime-safety rules.

Run the full standalone core suite:

```bash
./build/SynthiaRender --suite core --output-dir build/reports/core
```

Focused validation commands:

```bash
./build/SynthiaRender --smoke --output build/reports/smoke.json
./build/SynthiaRender --list-parameters --output build/reports/parameters.json
./build/SynthiaRender --validate-presets presets/factory --output build/reports/presets.json
./build/SynthiaRender --voice-test --output build/reports/voice-core.json
./build/SynthiaRender --osc-test --notes C1,C3,C5,C7 --output build/reports/oscillator.json
./build/SynthiaRender --filter-test --output build/reports/filter.json
./build/SynthiaRender --modulation-test --fixture fixtures/midi/overlap-pluck.mid --output build/reports/modulation.json
./build/SynthiaRender --modulation-route-render-test --fixture fixtures/midi/overlap-pluck.mid --output build/reports/modulation-route-render.json
```

Render the factory dry-core pluck:

```bash
./build/SynthiaRender \
  --preset "presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset" \
  --fixture fixtures/midi/overlap-pluck.mid \
  --dry \
  --output build/renders/pluck-core-01-dry.wav \
  --report build/reports/pluck-core-01-dry.json
```

Render the factory wet pluck:

```bash
./build/SynthiaRender \
  --preset "presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset" \
  --fixture fixtures/midi/overlap-pluck.mid \
  --wet \
  --output build/renders/pluck-core-01-wet.wav \
  --report build/reports/pluck-core-01-wet.json
```

Current core validation covers:

- finite output and non-clipping dry renders,
- oscillator tuning, pulse width, sub octave, stack detune, noise, and hard sync,
- semitone-domain filter mapping and nonlinear filter stability,
- ramp timing, glide, velocity glide, and mono/legato/unison allocation edge cases,
- direct modulation and TransMod source/scaler/destination behavior,
- modulation route write audio proof, including audible route creation and deterministic clear-to-baseline restore,
- top-level preset `mod_slots` schema loading and strict depth validation,
- FX bypass equivalence, tempo-synced delay at test tempo, FX tail reporting, wet render finite-output safety, and serialized realtime/offline quality settings,
- deterministic render repeatability and LFO ablation metrics.

## Developer Handoff

Use this when continuing development on another Mac, especially one with Ableton installed.

Clone the private repo:

```bash
git clone https://github.com/ParkerRex/synthia.git synthia
cd synthia
```

Build and validate locally:

```bash
cmake -S . -B build -DSYNTHIA_ENABLE_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
./build/SynthiaRender --suite core --output-dir build/reports/core
scripts/check-plugin-bundles.sh build
```

Install the locally built AU and VST3 into the per-user macOS plug-in folders:

```bash
scripts/install-local-plugins.sh build
```

Preview or remove the local install:

```bash
scripts/uninstall-local-plugins.sh --dry-run
scripts/uninstall-local-plugins.sh
```

Local install and Ableton scan troubleshooting live in `docs/host-validation/local-install-troubleshooting.md`.

Open the Ableton smoke template and record the environment before testing:

```bash
open docs/host-validation/ableton-smoke.md
```

Fill in:

- date,
- machine,
- macOS version,
- Ableton version,
- repo commit from `git rev-parse --short HEAD`,
- build directory, usually `build`,
- sample rate,
- buffer size,
- plugin format tested: AU, VST3, or both.

In Ableton:

- Enable Audio Units and VST3 in Ableton's Plug-Ins settings.
- Rescan plug-ins after running `scripts/install-local-plugins.sh build`.
- Confirm `synthia` appears in the AU plug-in list.
- Confirm `synthia` appears in the VST3 plug-in list.
- Load the AU build on a MIDI track.
- Load the VST3 build on a separate MIDI track.
- Play `fixtures/midi/overlap-pluck.mid` or an equivalent overlapping-note pluck pattern.
- Confirm both formats produce finite audible output.
- Exercise mono, mono-legato, poly, unison, glide, velocity glide, ramp, and TransMod behavior.
- Record and replay one parameter automation lane.
- Save the Live set, close Ableton, reopen it, and confirm state restore.
- Export a short bounce if playback and restore pass.

If something fails, write it into `docs/host-validation/ableton-smoke.md` with:

- plugin format,
- exact step,
- expected result,
- actual result,
- whether it reproduces,
- Ableton log path or relevant message,
- linked fix commit once fixed.

## Repository Map

- `SPEC.md`: durable product requirements.
- `CONTEXT.md`: project vocabulary and decision lanes.
- `docs/ARCHITECTURE.md`: implementation architecture.
- `docs/VALIDATION.md`: validation strategy and report contract.
- `docs/modern-synthia-baseline.md`: Phase 1 Sylenth rebuild baseline and roadmap.
- `research/sylenth1-screenshots/SOURCE_INDEX.md`: traceable source map for the local Sylenth screenshot corpus.
- `src/dsp/`: DSP engine, oscillator, filter, envelopes, LFO, ramp, FX, and parameters.
- `src/voice/`: voice rendering and allocation.
- `src/plugin/`: JUCE processor/editor and parameter registry.
- `src/presets/`: preset schema validation.
- `src/validation/`: standalone render and report CLI.
- `tests/smoke/`: CTest smoke, contract, voice, and DSP coverage.
- `presets/factory/`: factory presets.
- `fixtures/`: MIDI and preset fixtures used by validation.
