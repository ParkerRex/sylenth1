# Native Rebuild Host Qualification - 2026-10-09

Status: preparation; no new host acceptance is claimed yet.

## Environment

- macOS: 27.0.1, build 26A434.
- Hardware: Apple M1 Max, arm64, 64 GiB RAM.
- Installed Ableton: Live 11 Suite 11.3.43 (2025-10-06_b466d4f56c).
- Candidate: integration worktree `codex/classic-sylenth-rebuild`; exact committed build identity will be recorded with candidate evidence.
- The Ableton Remote Script responds to the established session/track/browser API. Its legacy handshake does not support the newer `get_script_info` command.
- On initial inspection, Live opened an unmodified Untitled default set: two empty MIDI tracks, two empty audio tracks, two returns, 120 BPM, stopped. No existing music project is being edited.

## Historical Benchmark Boundary

The old `/Users/parkerrex/Desktop/testing-synth Project/demp.als` benchmark is absent. Targeted searches of Desktop, Music, Documents, and retained build directories did not find it. The historical nine-track bar-65 CPU result cannot be rerun or claimed from a different workload. New qualification sets and headless benchmark runs must identify their own notes, presets, instances, rates and buffers.

## Candidate Checks

- [ ] Complete optimized universal build and record executable hashes/UUIDs.
- [ ] Preserve recoverable copies of installed Synthia bundles before replacement.
- [ ] Install candidate, gracefully restart Live, and verify mapped executable identity.
- [ ] AU validation and AU scan/load/play.
- [ ] VST3 scan/load/play.
- [ ] Native oscillator, part, modulation and effect control changes in the host.
- [ ] Program/bank state save, close/reopen and restore.
- [ ] Host automation record/playback.
- [ ] Editor open/close while playing, without killing host notes.
- [ ] Stop, all-notes-off and panic behavior.
- [ ] Sample-rate and buffer changes.
- [ ] Offline/realtime host rendering comparison.
- [ ] Optimized headless and identified host performance evidence.

## Scope Limits

Universal slice validation does not establish native Intel execution. Live 12, minimum macOS 11.0, Intel hardware, and a clean user machine remain separate qualification environments. Missing original-plugin audio and lossless image captures also remain separate from host stability checks.
