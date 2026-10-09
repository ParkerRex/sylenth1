# Local Install And Host Troubleshooting

Use this note for local Ableton validation builds. It is not a distribution or notarization guide.

## Build And Check

From the repo root:

```bash
scripts/build-release-bundles.sh --config RelWithDebInfo --build-dir build-release
scripts/check-plugin-bundles.sh build-release RelWithDebInfo
```

The release command runs all CTest tests and the standalone core suite before ad-hoc signing. The bundle checker fails on missing native Intel/Apple Silicon code, an executable targeting newer than macOS 11, wrong metadata, any changed/missing factory file, or an invalid sealed signature. It checks the exact configuration requested and never selects another configuration's stale bundles.

## Install

Install the local AU and VST3 into the current user's plugin folders:

```bash
scripts/install-local-plugins.sh build-release RelWithDebInfo
```

Installed paths:

- AU: `~/Library/Audio/Plug-Ins/Components/Synthia.component`
- VST3: `~/Library/Audio/Plug-Ins/VST3/Synthia.vst3`

The install script checks the source bundles before copying and ad-hoc signs the copied bundles for local host scanning. A distribution candidate must retain its Developer ID signature and must not pass through this development installer. To preserve the already verified source signature during debugging:

```bash
SYNTHIA_SKIP_ADHOC_SIGN=1 scripts/install-local-plugins.sh build-release RelWithDebInfo RelWithDebInfo
```

Run AU validation after install:

```bash
auval -v aumu SynA PkRx
```

## Uninstall

Preview removal:

```bash
scripts/uninstall-local-plugins.sh --dry-run
```

Remove only Synthia's per-user AU and VST3 bundles:

```bash
scripts/uninstall-local-plugins.sh
```

This script does not remove Ableton caches, user presets, or unrelated plugins.

## Ableton Rescan

After install or uninstall:

1. Open Ableton preferences.
2. Go to Plug-Ins.
3. Enable Audio Units and VST3.
4. Rescan plug-ins.
5. Confirm `Synthia` appears under both AU and VST3 when installed.

If Synthia does not appear after a normal rescan, quit Ableton and inspect Ableton's plugin scan logs before deleting any cache files.

Common user cache locations to inspect or move aside for a targeted rescan:

- `~/Library/Preferences/Ableton/Live 11.0.12/`
- `~/Library/Caches/Ableton/`

Do not delete broad `~/Library` folders or unrelated plugin folders. Move suspected Ableton cache files to a temporary folder first so they can be restored if the issue is unrelated.

## Local Signing Versus Distribution Signing

Ad-hoc signing is enough for local development validation on this machine. It does not produce distributable, notarized artifacts.

The [release workflow](../BUILD_RELEASE.md) implements a separate distribution mode using an authorized existing Developer ID Application identity, team ID, and Keychain notary profile. It signs with secure timestamps and hardened runtime, requires accepted notarization, staples the app/final DMG, and assesses them. It fails if credentials are missing and never falls back to ad-hoc signing.

Licensing/content decisions, current Ableton proof, native Intel/oldest-OS proof, clean-machine installation, and publication authorization remain explicit items in the [release checklist](../release/checklist.md). Local engineering does not require choosing public license terms.
