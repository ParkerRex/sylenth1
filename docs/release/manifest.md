# Native Release Manifest

Each successfully packaged candidate directory contains `manifest.json` (schema version 1), `SHA256SUMS.txt`, `ctest.xml`, and `core-summary.json`, alongside developer ZIPs or the distribution DMG.

The manifest records:

- Product/version and `implementation: native-only`.
- Source commit, dirty-tree status, UTC creation time, configuration, build OS/Xcode, pinned JUCE version, required architectures, and minimum macOS target.
- Bundle identifiers, format/version, factory file count, signing mode/kind, signature verification, and each bundled binary's SHA-256, architecture list, and deployment minimum per architecture.
- Passed local CTest, core render, and realtime/type-safety gates, plus the universal render executable's build metadata/hash.
- Accepted notary submission IDs/status when distribution mode completed. Raw Keychain credentials, private keys, certificate material, and notary logs are excluded.
- Deliverable filenames, byte lengths, and SHA-256 hashes. `SHA256SUMS.txt` also hashes the manifest and retained test reports using relative filenames.
- External host, Intel, and oldest-OS evidence still required; unresolved licensing/content review and owner authorization; `publication_approved: false`.
- Original image/audio comparisons remain external evidence. `reference_fidelity: not_verified` and `distribution_qualification: not_qualified` apply to developer packages and signed/notarized candidates. The local command does not verify or infer these external requirements.

`notarized: false` is expected for developer packages. `notarized: true` means both submissions were accepted and the final app/image stapling and Gatekeeper assessments succeeded. It does not imply host, reference fidelity, license approval, or completed distribution qualification. A separate human review must link the actual original/candidate files and comparison/host reports to this candidate's hashes before qualification can be completed.

The manifest does not claim that builds are bit-reproducible: timestamps, signing tickets, SDK/compiler versions, and ZIP/DMG container metadata can vary. It records the actual bytes reviewed. The selected package directory is reported only after successful validation and manifest/checksum creation; an interrupted directory without these final records is incomplete.
