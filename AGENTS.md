# Synthia Agent Guide

Synthia is a C++20/JUCE instrument for macOS AU, VST3, and Standalone. The active release recreates the classic Sylenth1 control surface with owned code and assets. Use simple solutions and plain language.

## Start here

For meaningful work, read these in order:

1. [SPEC.md](SPEC.md): product requirements. This wins when documents disagree.
2. [CONTEXT.md](CONTEXT.md): vocabulary and decision lanes.
3. [docs/index.md](docs/index.md): document map.
4. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): component and realtime boundaries.
5. [docs/VALIDATION.md](docs/VALIDATION.md): tests, renders, and host proof.

Read [docs/PRESET_SCHEMA.md](docs/PRESET_SCHEMA.md) for persistence changes, [docs/CLASSIC_PARITY.md](docs/CLASSIC_PARITY.md) for reference claims, and [docs/BUILD_RELEASE.md](docs/BUILD_RELEASE.md) for packaging. [CONTRIBUTING.md](CONTRIBUTING.md) covers submitting a PR; [docs/SCREENSHOTS.md](docs/SCREENSHOTS.md) covers UI captures.

## Scope and ownership

- Work on one focused purpose. Preserve unrelated user and agent changes.
- A question asks for an answer, not implementation. Discuss proposed changes before implementing when the user only asks for an opinion.
- Keep one native architecture. Old Synthia patch compatibility is out of scope. Browser/Wasm, AI generation, and conversational editing are deferred.
- Current patches use `.SynthiaPreset` with payload schema 2; host state uses schema 2. Owned `.SynthiaBank` files use schema 1 and 512 programs. Do not imply proprietary `.fxp`/`.fxb` support.
- Change product requirements in `SPEC.md`, then update the relevant durable document with code. Do not hide requirements in tests or comments.
- Do not add abstractions or tests merely because code changed. Test observable behavior and plausible failures with the cheapest reliable check.
- Use parallel agents only when breadth warrants it or the user requests it. State file ownership before parallel edits. Respect another agent's ownership, especially `src/plugin/PluginEditor.cpp` and `.h` during non-UI cleanup.
- Use precise names, type-safe code, and plain explanations. Avoid decorative UI chrome and continuous repaint animations.

## Source map

| Path | Responsibility |
| --- | --- |
| `src/dsp/`, `src/voice/` | Sound engine, prepared resources, voice and note scheduling |
| `src/plugin/PluginProcessor.*` | Host lifecycle, MIDI handoff, program bank and state |
| `src/plugin/ParameterRegistry.*` | Canonical parameter IDs, types, ranges and defaults |
| `src/plugin/PluginEditor.*` | Classic native controls and host gestures |
| `src/modulation/`, `src/midi/` | Modulation routing and MIDI controller models |
| `src/presets/`, `presets/factory/` | Owned preset validation, persistence and factory content |
| `src/validation/`, `tests/`, `scripts/` | Renders, behavior tests, comparisons and build tooling |

Keep public parameter IDs centralized and stable. UI labels may change; released automation/preset IDs must not be reused for another meaning.

## Realtime rules

The audio thread may render DSP, process bounded MIDI, read atomically published state, update lock-free diagnostics, and write output buffers.

It must not allocate, wait on locks, touch files or the network, log synchronously, parse presets, scan directories, rebuild UI, or create/destroy heavyweight resources. Prepare buffers and delay storage before processing; use bounded resets and queues.

Guard DSP against NaN, infinity, denormals, unstable feedback, invalid parameters, out-of-bounds blocks, and uncontrolled gain jumps. Add deterministic regression coverage for audible DSP bugs and consequential MIDI, routing, state, and lifecycle failures.

## Build and verification

Default quality gate for code changes:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=6 scripts/check-quality.sh
```

This runs whitespace hygiene, realtime/type-safety fitness checks, Debug configure/build, CTest, and the standalone core render suite. For documentation-only changes, verify links, command paths, and `git diff --check`; do not rebuild unchanged DSP just to update prose.

For a focused build and test loop:

```sh
cmake -S . -B build -DSYNTHIA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -j 6
ctest --test-dir build -C Debug --output-on-failure
./build/SynthiaRender --suite core --output-dir build/reports/core
```

CMake fetches pinned JUCE 8.0.13 by default. Use `-DSYNTHIA_JUCE_PATH=/absolute/path/to/JUCE` for an existing checkout. See [docs/VALIDATION.md](docs/VALIDATION.md) for focused test selection. A current factory render example is:

```sh
./build/SynthiaRender \
  --preset "presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset" \
  --fixture fixtures/midi/overlap-pluck.mid \
  --dry \
  --output build/renders/pluck-core-01-dry.wav \
  --report build/reports/pluck-core-01-dry.json
```

Optional format/tidy sweeps can expose historical debt. Keep cleanup scoped and exclude files owned elsewhere:

```sh
scripts/check-cpp-format.sh --all --exclude src/plugin/PluginEditor.cpp --exclude src/plugin/PluginEditor.h
scripts/check-quality.sh --with-tidy --exclude src/plugin/PluginEditor.cpp --exclude src/plugin/PluginEditor.h
```

Build outputs and generated reports/WAVs stay under `build/`. Commit evidence snapshots only when requested, with provenance.

## Native UI and host proof

For UI changes, capture the affected Part A/B, LCD page, or scale and inspect the result. Follow [docs/SCREENSHOTS.md](docs/SCREENSHOTS.md). A capture proves that state renders; it does not prove mouse interaction or original-product fidelity.

Plugin-facing changes need current AU/VST3 host proof: scan/load/playback, automation record/playback, save/close/reopen/restore, offline versus realtime where practical, sample-rate/buffer changes, stop/all-notes-off/panic, and editor open/close during playback. Record unavailable checks explicitly.

Use `scripts/install-local-plugins.sh build Debug --dry-run` for preflight and `scripts/install-local-plugins.sh build Debug` for an authorized local install. This replaces installed Synthia AU/VST3 bundles. Do not replace a running host's plugin or clear its caches without coordinating the validation session. Debug source bundles are:

- `build/SynthiaPlugin_artefacts/Debug/AU/Synthia.component`
- `build/SynthiaPlugin_artefacts/Debug/VST3/Synthia.vst3`
- `build/SynthiaPlugin_artefacts/Debug/Standalone/Synthia.app`

Identify the tested revision and actual binary. Record host failures with Ableton/macOS versions, format, sample rate, buffer size, preset/state, and reproduction steps. Earlier host records qualify only their named builds.

## Evidence and contribution discipline

Owned implementation and passing local tests do not prove Sylenth1 audio or pixel equivalence. Required original-reference checks must fail if captures are missing. Never substitute Synthia renders for original-plugin evidence or invent a successful reproduction.

Use owned or explicitly licensed shipped assets and presets. Vendor screenshots/manuals are research references, not runtime backplates, logos, or factory content. Do not inspect/disassemble proprietary binaries or bypass licensing.

- Do not push unless explicitly asked. Do not merge without the requested disposition.
- Use conventional commits and short conventional PR titles, for example `fix: release held notes when arp stops`.
- Check `git status -sb` before staging and stage only intended files. Read every line of the final diff and PR description.
- Rebase onto the latest upstream default branch, currently `master`, before opening a real PR. Do not open a draft.
- Follow [CONTRIBUTING.md](CONTRIBUTING.md) and the PR template: concrete problem/result/cause, observed baseline reproduction and source location for bugs, exact checks/results/tested revision/limits, and brief accurate model/harness disclosure.
- For performance claims, report measured before/after conditions and tradeoffs. A target is not a result.
- When asked to monitor a PR, check new bot findings against source, fix real issues, explain false positives, and stay quiet when nothing changes. Stop when review bots are green on the latest commit; merge only if authorized.
