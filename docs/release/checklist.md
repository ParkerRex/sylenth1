# Native Release Checklist

The local release process produces reviewable engineering candidates. Public release requires the owner to resolve these items and explicitly authorize publication. No release tool creates a GitHub release, tag, scheduled CI job, or installer in system plugin folders.

## Owner decisions

- Choose Synthia's owned-code license. The repository has no top-level license grant; tooling must not choose terms for the owner.
- Resolve JUCE 8 commercial licensing or AGPLv3 compliance for the intended distribution. The dependency's [pinned license](https://github.com/juce-framework/JUCE/blob/8.0.13/LICENSE.md) is separate from the owned-code decision. Obtain qualified review for the intended release terms.
- Approve an original-content audit of factory presets, marks, screenshots, manual extracts, audio references, and UI assets. Research files must not become release resources without the relevant rights. The release script packages bundles and original factory files, not the repository's research folders.
- Confirm version/tag policy, stable AU manufacturer `PkRx`, subtype `SynA`, bundle ID `com.parkerx.synthia`, and any migration promises. No public release exists to infer a policy from.
- Authorize use of a Developer ID Application signing identity and an existing Keychain notary profile. Certificate availability and notary-profile availability are distinct. Tooling does not obtain credentials or private material.

These decisions do not block local engineering builds or developer packages. Signing and notarization must wait for credential authorization; publication must wait for the license/content decisions and explicit owner approval.

## Candidate acceptance

- Build a committed clean source tree in Release mode with the one-command release process.
- Retain the manifest, CTest XML, core render summary, final archive/image, and checksums.
- Confirm all expected factory files match the exact source bytes, all bundle metadata matches the version, and every code item contains native Intel and Apple Silicon slices targeting macOS 11 or earlier.
- Require all local gates to pass. Do not package a Debug, sanitizer, stale-config, or unchecked binary as a release candidate.
- Run current AU/VST3 Ableton scan, load, playback, automation, save/reopen/restore, panic, sample-rate/buffer changes, bounce comparison, and UI-open/close proof. Use the host record template and identify exact candidate bytes.
- Require the selected original-plugin image and audio evidence for the requested classic rebuild. Review the original/candidate capture provenance, file hashes, matching state/settings, approved branding masks, approved audio limits, and actual comparison reports. Synthetic tool tests, comparisons against Synthia itself, a missing-reference manifest, or a manually inserted `passed` flag are not fidelity proof.
- Test actual Intel hardware and the oldest advertised macOS version. An ARM test run, Rosetta run, or Mach-O inspection alone does not prove native Intel host operation.
- Install the final downloaded/quarantined DMG on a clean macOS account or machine; assess and load the standalone and both plugin formats. Confirm the notarized package works without development cache or certificate trust.
- Record supported Ableton/macOS versions and any limitations. Missing historical validation sets and unrun host/hardware checks remain open evidence gaps.

## Publication handoff

- Verify `shasum -a 256 -c SHA256SUMS.txt` in the candidate directory.
- Confirm Developer ID signatures, accepted notary submission IDs, stapled tickets, and successful app/DMG Gatekeeper assessment.
- Review release notes and licensing notices with the owner.
- Obtain explicit publication authorization and choose the approved version tag.
- Publish only the final DMG and its matching evidence/checksums for distribution mode. Developer ZIPs must retain their local-only labeling.

The local packaging manifest always states `publication_approved: false`, `reference_fidelity: not_verified`, and `distribution_qualification: not_qualified`. Signing, notarization, and local validation do not establish the missing original-reference and current-host proof. Software cannot infer the human license decision or publication authorization. Maintain a separate reviewed qualification/release record linked to the actual reports and candidate file hashes.
