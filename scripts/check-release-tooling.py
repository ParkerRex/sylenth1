#!/usr/bin/env python3
"""Exercise native release rejection paths in disposable fixtures, never installed plugins."""
from __future__ import annotations

import importlib.util
import plistlib
import shutil
import tempfile
from pathlib import Path
from collections.abc import Callable

from release_support import ROOT, BUNDLES, artifact_directory, check_bundles, project_version, run


def expect_failure(label: str, operation: Callable[[], object]) -> None:
    try:
        operation()
    except (OSError, ValueError, RuntimeError):
        print(f"PASS: rejects {label}")
        return
    raise RuntimeError(f"accepted invalid fixture: {label}")


def main() -> None:
    spec = importlib.util.spec_from_file_location("release_builder", ROOT / "scripts/build-release-bundles.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    with tempfile.TemporaryDirectory(prefix="synthia-release-test-") as temporary:
        directory = Path(temporary)
        source = directory / "minimal.c"
        source.write_text("int main(void) { return 0; }\n")
        executable = directory / "universal"
        run("xcrun", "clang", "-arch", "arm64", "-arch", "x86_64", "-mmacosx-version-min=11.0", source, "-o", executable)
        artifacts = directory / "SynthiaPlugin_artefacts/Release"
        version = project_version()
        for fmt, name in BUNDLES.items():
            bundle = artifacts / fmt / name
            (bundle / "Contents/MacOS").mkdir(parents=True)
            shutil.copy2(executable, bundle / "Contents/MacOS/Synthia")
            factory = bundle / "Contents/Resources/factory"
            factory.parent.mkdir(parents=True)
            shutil.copytree(ROOT / "presets/factory", factory)
            metadata = {"CFBundleIdentifier": "com.parkerx.synthia", "CFBundleName": "Synthia",
                        "CFBundleExecutable": "Synthia", "CFBundleShortVersionString": version,
                        "CFBundleVersion": version, "CFBundlePackageType": "APPL" if fmt == "Standalone" else "BNDL"}
            if fmt == "AU":
                major, minor, patch = map(int, version.split("."))
                metadata["AudioComponents"] = [{"manufacturer": "PkRx", "subtype": "SynA", "type": "aumu",
                                                "version": (major << 16) | (minor << 8) | patch}]
            (bundle / "Contents/Info.plist").write_bytes(plistlib.dumps(metadata))
            run("codesign", "--force", "--sign", "-", "--timestamp=none", bundle)
        check_bundles(artifacts)
        print("PASS: universal ad-hoc signed native fixtures, metadata and all factory bytes")
        standalone = artifacts / "Standalone/Synthia.app"
        plist = standalone / "Contents/Info.plist"
        original = plist.read_bytes()
        changed = plistlib.loads(original)
        changed["CFBundleIdentifier"] = "com.invalid.instrument"
        plist.write_bytes(plistlib.dumps(changed))
        expect_failure("wrong product identity", lambda: check_bundles(artifacts))
        plist.write_bytes(original)
        run("codesign", "--force", "--sign", "-", standalone)
        preset = next((standalone / "Contents/Resources/factory").rglob("*.SynthiaPreset"))
        original_preset = preset.read_bytes()
        preset.write_bytes(original_preset + b"\n")
        expect_failure("modified factory preset", lambda: check_bundles(artifacts))
        preset.write_bytes(original_preset)
        # A non-preset resource invalidates the code signature without changing factory expectations.
        resource = standalone / "Contents/Resources/release-test.txt"
        resource.write_text("fixture")
        expect_failure("invalid sealed signature", lambda: check_bundles(artifacts))
        resource.unlink()
        main_binary = standalone / "Contents/MacOS/Synthia"
        run("xcrun", "clang", "-arch", "arm64", "-mmacosx-version-min=11.0", source, "-o", main_binary)
        expect_failure("missing Intel slice", lambda: check_bundles(artifacts, "unsigned"))
        run("xcrun", "clang", "-arch", "arm64", "-arch", "x86_64", "-mmacosx-version-min=12.0", source, "-o", main_binary)
        expect_failure("raised minimum macOS", lambda: check_bundles(artifacts, "unsigned"))
        expect_failure("fallback to a stale configuration", lambda: artifact_directory(directory, "RelWithDebInfo"))
        shutil.copy2(executable, main_binary)
        run("codesign", "--force", "--sign", "-", standalone)
        expect_failure("ad-hoc signature presented as distribution", lambda: check_bundles(artifacts, "distribution", "ABCDEFGHIJ"))
        import argparse
        options = argparse.Namespace(jobs=1, build_dir=directory, juce_path=None, signing_mode="distribution",
                                     config="Release", package=True, install_local=False, identity=None,
                                     team_id=None, notary_profile=None)
        expect_failure("missing distribution credentials", lambda: builder.preflight(options))
        options.signing_mode = "developer"
        (directory / "CMakeCache.txt").write_text("CMAKE_HOME_DIRECTORY:INTERNAL=/unrelated/checkout\n")
        expect_failure("foreign-checkout CMake cache", lambda: builder.preflight(options))
    print("Release tooling fixtures passed; no installed plugin or credential was accessed.")


if __name__ == "__main__":
    main()
