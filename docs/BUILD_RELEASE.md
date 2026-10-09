# Build and Release Workflow

Synthia builds native AU, VST3, and standalone macOS binaries. The local release command builds optimized universal code, runs the existing validation gates, checks every bundled factory file and product identifier, and signs the candidate. It never publishes, tags, uploads to GitHub, or starts a host.

## Supported build target

Release candidates target macOS **11.0 or later**, with both `arm64` and `x86_64` in every Mach-O code item. This is a conservative shared deployment floor: Apple Silicon starts at macOS 11 and JUCE 8.0.13 supports this target. CMake sets the floor before `project()` so it cannot silently inherit the build machine's OS version. The release command explicitly sets both the floor and architectures.

The floor is a build contract. Native Intel hardware, the oldest supported OS, and current Ableton AU/VST3 validation remain release acceptance checks. A universal binary and passing tests on one Apple Silicon Mac do not prove these other systems.

The dependency remains pinned to JUCE `8.0.13`. A clean checkout fetches that tag. `--juce-path /absolute/path/to/JUCE` allows an existing local checkout with the same version; the release tool checks its version macros. CMake 3.24+, Python 3, Xcode command-line tools, and a macOS SDK are required.

JUCE's documented build requirements and deployment targets are in the [pinned README](https://github.com/juce-framework/JUCE/blob/8.0.13/README.md). Synthia's C++20 build and native Apple Silicon target deliberately use the newer shared floor above.

## Developer host-validation build

```bash
scripts/build-release-bundles.sh --config RelWithDebInfo --install-local
```

`RelWithDebInfo` provides optimized code with symbols for profiling. `Release` provides optimized candidate binaries. The release command does not accept Debug or sanitizer builds as candidates. Use `scripts/check-quality.sh` for the normal Debug quality gate.

The command configures with tests enabled and copy-after-build disabled, builds, runs whitespace and realtime/type-safety checks, runs all CTest tests, runs the standalone core suite, verifies the universal render executable, ad-hoc signs the bundles, and checks their sealed signatures. `--install-local` runs only after these gates pass. Installation is explicit because it replaces Synthia's per-user AU/VST3 bundles.

Full profiling and current-plugin image verification are in `.codex/skills/profile-synthia-ableton/SKILL.md`. Use a validated set, the bar-65 playback protocol, and the installed/mapped executable inode before trusting CPU measurements. A missing historical set must be recorded, with replacement measurements described accurately.

## Developer packages

```bash
scripts/build-release-bundles.sh --config Release --package
```

This creates three ad-hoc signed ZIP files under `build/release-artifacts/synthia-native-<version>-developer-<timestamp>/`. Each filename contains `Native` and `Developer`. The directory includes `manifest.json`, `SHA256SUMS.txt`, CTest XML, and the core render summary. Developer packages are local engineering artifacts. Ad-hoc signatures do not establish Developer ID trust or notarization.

Inspect the planned operation without building, signing, uploading, or installing:

```bash
scripts/build-release-bundles.sh --config Release --package --dry-run
```

There is no skip-validation packaging flag. Cache reuse is allowed only for the same source checkout. An explicitly requested configuration never falls back to another configuration's artifacts. Release reports stay in the selected build directory rather than a different checkout's `build/` directory.

## Distribution candidate

The owner must authorize use of existing signing and notarization credentials. Configure a `notarytool` Keychain profile outside this repository. The script accepts the profile name, an existing Developer ID Application identity, and its ten-character team ID; it does not accept passwords, private keys, certificate files, or API tokens.

```bash
scripts/build-release-bundles.sh \
  --config Release --package --signing-mode distribution \
  --identity 'Developer ID Application: OWNER NAME (TEAMID1234)' \
  --team-id TEAMID1234 \
  --notary-profile synthia-release
```

The identity and team above are placeholders, not credentials supplied by this project. Missing options, an unsuitable identity, a dirty source tree, missing tools, or a source change during build fail before submission. A missing or invalid certificate/profile causes the signing/notary command to fail. The tool never downgrades distribution signing to ad-hoc signing.

Distribution mode stages copies, signs nested code from the inside out, and applies Developer ID secure timestamps and hardened runtime to bundles. It verifies both native slices, the team identity, sealed resources, and absence of the debug `get-task-allow` entitlement. The current instrument requires no permissive entitlements, JIT, unsigned executable memory, or library-validation exceptions. Any future embedded helper/framework requires an explicit review of its signing requirements.

The staged bundles are submitted together as a ZIP to Apple's notary service. The command waits for `Accepted`, staples and validates the standalone app, and assesses the app with Gatekeeper. It then creates and signs a read-only compressed disk image containing the app, AU, VST3, and installation instructions; submits that final image; waits for acceptance; staples/validates its ticket; and assesses the final image. Only the resulting DMG is a distribution deliverable. The temporary submission ZIP is removed with the staging directory. AU/VST3 bundles do not support the same app stapling path; they are carried by the notarized, stapled DMG, and their code is covered by the notary submissions.

Apple describes these container and ticket steps in [customizing notarization](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow) and [packaging Mac software](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution). Plugin loading and clean-machine validation still require a real host.

Distribution mode never installs locally: the local installer uses development signing and would replace the distribution signature. Signing/notarization establishes a technical candidate. Its manifest still states `reference_fidelity: not_verified` and `distribution_qualification: not_qualified`: selected original image/audio evidence and current host proof require separate review against the actual report and artifact hashes. Missing references cannot be replaced by local tests or an asserted JSON flag. Follow [the release checklist](release/checklist.md) before any public release.

## Checks and manifest

```bash
scripts/check-plugin-bundles.sh build-release-current Release
scripts/check-release-tooling.py
```

The bundle checker defaults to sealed-signature verification. `--mode unsigned` checks newly built structural artifacts before signing, and cannot establish release readiness. `--mode distribution --team-id TEAMID1234` additionally verifies Developer ID, secure timestamp, hardened runtime, and team metadata per architecture. `--output <path.json>` writes its structured result.

The isolated tooling check compiles tiny universal binaries in a temporary directory and uses real Apple tools. It exercises rejection paths without accessing certificates, notarization credentials, installed plugins, or Ableton.

The [manifest contract](release/manifest.md) distinguishes build gates from external host/hardware evidence and approval. Verify package bytes from its directory with:

```bash
shasum -a 256 -c SHA256SUMS.txt
```
