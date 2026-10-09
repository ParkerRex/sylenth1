# Native Rebuild Host Qualification - 2026-10-09

Status: optimized developer candidate `12a24a3` passes AU validation and AU/VST3 saved-set reopen/play in Live 11.3.43. Full host lifecycle/automation checks, comprehensive changed-state restore, original-reference fidelity, and public distribution remain unqualified.

## Environment and candidate identity

- macOS: 27.0.1, build 26A434.
- Hardware: Apple M1 Max, arm64, 64 GiB RAM.
- Ableton: Live 11 Suite 11.3.43 (2025-10-06_b466d4f56c).
- Developer package: `build/release-artifacts/synthia-native-0.1.0-developer-20261009T183920927525Z` in the integration worktree.
- Package manifest: clean source commit `1f246f929a5addfd141aef8c61480608d02119f8`, Release configuration, version 0.1.0, pinned JUCE 8.0.13, arm64/x86_64 slices, macOS 11.0 deployment floor, and 18 factory presets in each bundle.
- Signatures: verified ad-hoc developer signatures, not Developer ID distribution signatures; no notarization or publication approval.
- Current accessibility source: `12a24a3aaf286c941022ea8c0b715c00718aa189`. Its Debug quality gate passes 11/11 CTest entries and 17/17 core reports. Its optimized universal rebuild passes 11/11 Release CTest entries, including the core render suite. Current installed binaries and host observations are recorded separately below.
- On initial inspection, Live opened an unmodified Untitled default set. Dedicated qualification sets were used; no existing music project was edited.

The editor change adds writable macOS accessibility roles, values, and actions to custom controls, with focused processor-test coverage. The new Debug Pluck capture and checked-in gallery capture share SHA-256 `140a59dd1d53343d729757d51cbda751caa33567428ab75ddb20a37133a93828`, establishing unchanged pixels for that state. Native Standalone actions changed Master Volume from 0 to -18 dB, A1 waveform Saw to Sine, A1 Retrigger on to off, and B1 octave 0 to 1 with matching readback and independent Part A state. All values were restored before exit. `accessibility-native-actions.json` records this check and its limits.

Recoverable previous AU/VST3 bundles and their manifest are under `build/reports/host-qualification/previous-install`. `install.log` records source-bundle preflight, replacement, and verified installed signatures.

The initial installed executable hashes matched the earlier package manifest. Live's mapped executable records identify the actual loaded files, rather than relying on bundle filenames:

| Format | Installed executable SHA-256 | Mapped-file evidence |
| --- | --- | --- |
| AU | `39d3a786405d5d3444c3ca792bd433f9071290fd4a02b53862257c19f9e1c5f3` | `binary-identity-au.json`, Live PID 63451, inode 664494938 |
| VST3 | `5fc43150975b7d07acba5e2feea38f6f85a4c8a425734f4fd70583abf2d19b52` | `binary-identity-vst3.json`, Live PID 73650, inode 664494939 |

These identity files are under `build/reports/host-qualification`; the AU record also includes both architecture UUIDs. They qualify the recorded candidate, not a later rebuild.

After gracefully quitting Live, `scripts/install-local-plugins.sh build-native-release Release` installed the accessibility candidate and verified both signatures. Live PID 80084 then mapped the new executable inodes:

| Format | Current executable SHA-256 | Current mapped-file evidence |
| --- | --- | --- |
| AU | `8506da9098f980b3b604c0bc571d68b4be8ebb90fcfe6e7242d69f67f501ede9` | `binary-identity-accessibility-au.json`, inode 664522568 |
| VST3 | `793d9c00203d951a4d6732d4e0329f111ee9fd3bd74e16fff89fd680265f897a` | `binary-identity-accessibility-vst3.json`, inode 664522569 |

These records include both architecture UUIDs. `install-accessibility.log` records the replacement and verification. The initial developer package does not contain these later binaries.

The final developer archive is `build/release-artifacts/synthia-native-0.1.0-developer-20261009T193930274525Z`. Its manifest identifies clean source `5f9778a11a665cca26aea46cb618b91d46d63917`, which includes accessibility commit `12a24a3` and documentation. The canonical release pipeline passed 11/11 CTest entries and 17/17 core reports. All six checksum entries passed; packaged AU/VST3 executable hashes match the current mapped binaries above. It remains ad-hoc signed, unnotarized, and explicitly unqualified for public distribution.

## Recorded checks

The developer package contains `ctest.xml` with **11/11 Release CTest entries passing**, zero failures/skips, and `core-summary.json` with **17/17 standalone core reports passing**. Its manifest records passing realtime/type-safety validation, verified universal bundle contents, executable hashes, and package checksums.

`auval -v aumu SynA PkRx` ended with `AU VALIDATION SUCCEEDED` on both candidates; logs are `auval.log` and `auval-accessibility.log`. AUval exercised render sizes/rates, parameter scheduling, and MIDI. That does not replace Live-specific buffer, automation, or lifecycle checks.

| Host operation | Observation and limit |
| --- | --- |
| AU scan/load/play | Successful; mapped AU identity recorded. Bridge playback observation reports Live playing and nonzero stereo master meters. |
| AU save/close/reopen/play | Dedicated `native-au-validation Project/native-au-validation.als` reopened with the current candidate. Confirmed beat 256 before playback; stereo master meters reached 0.894818. `accessibility-au-reopen.json` records the snapshot. This is initial host save/reopen proof, not exhaustive program/bank or changed-parameter restore coverage. |
| VST3 scan/load/play | Successful in a new qualification set; mapped VST3 identity recorded. |
| VST3 save/close/reopen/play | Set saved as `native-vst3-validation Project/native-vst3-validation.als`, then Live quit and restarted. It reopened with the current candidate, preserved the two arrangement clips and device, and played after confirmed beat 256. Stereo master meters reached 0.771898; `accessibility-vst3-reopen.json` records the snapshot. This does not establish exhaustive changed-parameter restore. |
| Bridge parameter read/write | AU `Device On` changed from 1 to 0 and back to 1 with matching readback. Only this host wrapper parameter was configured in the recorded device list; this is not synth-control or automation-envelope proof. |
| AU stop and tail drain | Playback stopped at beat 283.69057 during the six-note workload. A later read showed all master meters at zero; `live-au-six-note-transport.json` records playing, immediate stop, and drained observations. This does not cover separate all-notes-off or panic actions. |
| AU and VST3 offline export | Both saved sets exported successfully through Live's native Export Audio/Video dialog. Each WAV contains 661,500 stereo frames at 44.1 kHz, exactly 15 seconds, with audible output and zero clipped samples. See the export evidence below. |

The saved AU set, bridge snapshot, install/identity records, and benchmark JSON remain disposable local evidence under `build/reports/host-qualification`. `workload.json` was prepared before installation; its `plugin_installed: false` field is historical preparation metadata, superseded by the install and mapped-binary records.

## Host workload and performance boundary

The dedicated workload is `native-six-note-chord`: 128 BPM, a 16-beat clip, MIDI pitches 48/55/60/64/67/72 at velocity 80, two 7.5-beat chords starting at beats 0 and 8, with arrangement copies starting at beats 256 and 272. Its notes and timing are recorded in `workload.json`. This identifies the qualification input, not a nine-instance stress test.

The old `/Users/parkerrex/Desktop/testing-synth Project/demp.als` benchmark is absent. Targeted searches of Desktop, Music, Documents, and retained build directories did not find it. The historical nine-track bar-65 CPU result cannot be rerun or claimed from this workload. No measured nine-track or sub-50-percent Live CPU result is asserted.

The final optimized headless benchmark, recorded in `headless-final-benchmark.json`, used:

- `Lead/LD - Supersaw Stack 01.SynthiaPreset`, six held notes 48/55/60/64/67/72, and six active voices.
- 48 kHz, 128-sample blocks, parameter publication on every block, five rendered seconds per repetition, 1.5 seconds warmup, three repetitions.
- Wall times 554.927, 563.058, and 562.638 ms; median **562.638 ms**, or **8.88671 times realtime**; best 9.01019 times realtime.
- Peak 0.0620938, zero invalid samples, and a passing report.

This is a single headless workload result, not Live process CPU or a verified before/after improvement. `headless-initial-benchmark.json` overlapped an incremental build and explicitly marks its performance evidence invalid; its block size and duration also differ. Do not use it as a paired baseline.

### Current host CPU observation

`live-au-six-note-cpu.json` records one current AU instance playing the identified six-note set from a confirmed beat 256. The loaded inode remained 664522568. Live's current-launch log records 44.1 kHz and 512-sample input/output buffers. The editor was closed. Over 10.115 seconds, cumulative Live process CPU time increased by 5.13 seconds, averaging **50.72% of one CPU core**; the individual `ps` readings ranged from 46.9% to 51.5%.

This includes Live and its two return devices. Earlier stopped readings varied from 15.3% to 47.7%; the stopped sample contained only one sampled Synthia processing path, while playback sampled oscillator, filter, voice, and effects work. These samples do not establish the exact source of Live's idle cost or isolate plugin CPU. No optimization or under-50-percent qualification is claimed. The historical nine-track workload remains unavailable. Raw records are `live-idle-cpu.json`, `live-idle.sample.txt`, and `live-au-six-note.sample.txt`.

### Current host offline exports

Both native export dialogs showed Master output, start 65.1.1, length 8.0.0, stereo, 44.1 kHz, 16-bit WAV, triangular dither, and normalization off. The existing MP3 and analysis-file options were left enabled. Local results:

| Format | WAV evidence | Peak | RMS | Clipped samples |
| --- | --- | --- | --- | --- |
| AU | `native-au-offline-12a24a3.wav` and matching JSON | 0.969940 | 0.168245 | 0 |
| VST3 | `native-vst3-offline-12a24a3.wav` and matching JSON | 0.398041 | 0.103235 | 0 |

Each JSON records the file SHA-256, exact frames, dialog settings, and limits. The two saved sets are not asserted to contain identical synth states; their audio levels are not an AU/VST3 equivalence comparison. No realtime loopback capture was made, so host offline/realtime equivalence remains unverified. Triangular dither also rules out a byte-identity claim from these exports.

## Ableton bridge boundary

The patched Remote Script **1.7.2** is installed and responds through protocol 1. `bridge-install.json` records source commit `6a81b52bb8beb5e74ef55fb0c3418581d30ee544`, installed SHA-256 `02ae657a80f55793fb1415c6b45ff3ff586993dc4d85e84aa34e731f8f590cec`, and [bridge PR 140](https://github.com/ahujasid/ableton-mcp/pull/140). `bridge-live-au.json` records the successful session snapshot, wrapper parameter read/write, and playback observation.

The current MCP client is still **1.4.5**, without [client PR 139](https://github.com/ahujasid/ableton-mcp/pull/139). It expects Remote Script 1.7.1; the observed protocol 1 operations succeeded with 1.7.2. Installed script proof does not establish that the client patch is installed or that every automation/GUI operation is available.

## Remaining candidate checks

- [x] Optimized universal developer build, executable hashes/UUIDs, and Release test evidence.
- [x] Recoverable copies of previous installed bundles.
- [x] Candidate installation and actual mapped executable identity.
- [x] AU validation and AU scan/load/play.
- [x] VST3 scan/load/play.
- [x] Initial AU set save, close/reopen, and playback.
- [x] Identified optimized headless workload result.
- [x] Accessibility source `12a24a3`: Debug quality gate 11/11 CTest and 17/17 core reports; unchanged Pluck capture pixels.
- [x] Updated optimized accessibility candidate build and Release checks: 11/11 CTest, including the core suite.
- [x] Install changed candidate, verify mapped identity, repeat AUval and both saved-set reopen/play checks.
- [x] Native Standalone accessible value, toggle, and Part B action checks.
- [x] VST3 set save/close/reopen/play.
- [x] AU transport stop followed by zero output meters for the identified workload.
- [x] AU and VST3 native offline exports with valid duration, audible output, and no clipping.
- [x] Current one-instance host CPU observation, with binary identity, sample rate, buffer size, and workload limits.
- [ ] Native oscillator, part, modulation, and effect control changes in the host.
- [ ] Comprehensive program/bank/changed-parameter restore in Live.
- [ ] Host automation record/playback.
- [ ] Editor open/close while playing, without killing host notes.
- [ ] Separate all-notes-off and panic behavior in Live.
- [ ] Live sample-rate and buffer changes.
- [ ] Offline/realtime host rendering comparison.
- [ ] Representative multi-instance host performance qualification; the historical nine-track set is unavailable.

Live's main device panel does not expose its editor-open wrench through the available accessibility tool; coordinate attempts return `AXError.notImplemented`, and Show Plug-In Windows did not open an initially closed editor. The [Live 11 manual](https://www.ableton.com/en/live-manual/11/working-with-instruments-and-effects/) documents that shortcut as showing/hiding already-open plugin windows. The bridge exposes only configured device parameters, currently the host's Device On control. Standalone accessibility and processor tests do not substitute for the remaining Live editor/automation checks.

## Scope limits

Universal slice validation does not establish native Intel execution. Live 12, minimum macOS 11.0, Intel hardware, and a clean user machine remain separate qualification environments. Missing original-plugin audio and lossless image captures remain separate from host stability checks. Distribution still needs licensing/content decisions, Developer ID signing, notarization, clean-machine proof, and owner publication authorization. This developer candidate is not a qualified public release.
