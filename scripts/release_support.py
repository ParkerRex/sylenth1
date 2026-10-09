#!/usr/bin/env python3
"""Shared native macOS release checks. Uses only the Python standard library."""
from __future__ import annotations

import hashlib
import json
import plistlib
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MINIMUM_MACOS = "11.0"
ARCHITECTURES = {"arm64", "x86_64"}
BUNDLES = {"Standalone": "Synthia.app", "AU": "Synthia.component", "VST3": "Synthia.vst3"}
MACHO_MAGIC = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
               b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
               b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}


def run(*arguments: object, cwd: Path | None = None) -> str:
    result = subprocess.run([str(arg) for arg in arguments], cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        # Do not echo arguments: signing and notary configuration stays out of logs.
        raise RuntimeError(f"{arguments[0]} failed ({result.returncode}): {result.stderr.strip()}")
    return result.stdout + result.stderr


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def project_version() -> str:
    match = re.search(r"\bVERSION\s+(\d+\.\d+\.\d+)\b", (ROOT / "CMakeLists.txt").read_text())
    if match is None:
        raise RuntimeError("CMake project semantic version is missing")
    return match.group(1)


def artifact_directory(build: Path, config: str | None) -> Path:
    root = build / "SynthiaPlugin_artefacts"
    # An explicitly requested config must never resolve another config's output.
    candidates = [root / config] if config else [root] + [root / name for name in
                                                              ("Release", "RelWithDebInfo", "Debug", "MinSizeRel")]
    complete = [path for path in candidates if all((path / fmt / name).is_dir()
                                                   for fmt, name in BUNDLES.items())]
    if not complete:
        raise RuntimeError(f"complete AU/VST3/Standalone artifacts missing under {root}")
    return complete[0]


def factory_hashes(factory: Path) -> dict[str, str]:
    files = {path.relative_to(factory).as_posix(): sha256(path) for path in factory.rglob("*")
             if path.is_file() and path.name != ".DS_Store"}
    if not files or not any(name.endswith(".SynthiaPreset") for name in files):
        raise RuntimeError(f"factory preset library is empty: {factory}")
    return files


def is_macho(path: Path) -> bool:
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as source:
        return source.read(4) in MACHO_MAGIC


def version_tuple(value: str) -> tuple[int, ...]:
    parts = tuple(int(part) for part in value.split("."))
    return parts + (0,) * (3 - len(parts))


def check_binary(path: Path) -> dict[str, object]:
    architectures = set(run("lipo", "-archs", path).strip().split())
    if architectures != ARCHITECTURES:
        raise RuntimeError(f"{path}: expected arm64 and x86_64, found {sorted(architectures)}")
    minimums: dict[str, str] = {}
    for architecture in sorted(architectures):
        commands = run("otool", "-arch", architecture, "-l", path)
        match = re.search(r"cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform (?:1|macos)\s+minos ([\d.]+)", commands)
        if match is None:
            match = re.search(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", commands)
        if match is None or version_tuple(match.group(1)) > version_tuple(MINIMUM_MACOS):
            raise RuntimeError(f"{path}: {architecture} must target macOS {MINIMUM_MACOS} or earlier")
        minimums[architecture] = match.group(1)
    return {"architectures": sorted(architectures), "minimum_macos": minimums, "sha256": sha256(path)}


def check_bundles(artifacts: Path, mode: str = "developer", team_id: str | None = None) -> list[dict[str, object]]:
    version = project_version()
    expected_presets = factory_hashes(ROOT / "presets/factory")
    reports: list[dict[str, object]] = []
    for fmt, name in BUNDLES.items():
        bundle = artifacts / fmt / name
        with (bundle / "Contents/Info.plist").open("rb") as source:
            metadata = plistlib.load(source)
        expected = {"CFBundleIdentifier": "com.parkerx.synthia", "CFBundleName": "Synthia",
                    "CFBundleExecutable": "Synthia", "CFBundleShortVersionString": version,
                    "CFBundleVersion": version, "CFBundlePackageType": "APPL" if fmt == "Standalone" else "BNDL"}
        for key, value in expected.items():
            if metadata.get(key) != value:
                raise RuntimeError(f"{fmt}: {key} must be {value!r}; found {metadata.get(key)!r}")
        if "LSMinimumSystemVersion" in metadata and version_tuple(metadata["LSMinimumSystemVersion"]) > version_tuple(MINIMUM_MACOS):
            raise RuntimeError(f"{fmt}: Info.plist minimum system version exceeds {MINIMUM_MACOS}")
        if fmt == "AU":
            components = metadata.get("AudioComponents", [])
            major, minor, patch = (int(part) for part in version.split("."))
            if len(components) != 1 or any(components[0].get(key) != value for key, value in
                {"manufacturer": "PkRx", "subtype": "SynA", "type": "aumu",
                 "version": (major << 16) | (minor << 8) | patch}.items()):
                raise RuntimeError("AU component manufacturer/subtype/type/version metadata is invalid")
        actual_presets = factory_hashes(bundle / "Contents/Resources/factory")
        if actual_presets != expected_presets:
            raise RuntimeError(f"{fmt}: bundled factory files differ from the source library")
        executable = bundle / "Contents/MacOS/Synthia"
        if not is_macho(executable):
            raise RuntimeError(f"{fmt}: missing Mach-O Synthia executable")
        binaries = {path.relative_to(bundle).as_posix(): check_binary(path)
                    for path in sorted(bundle.rglob("*")) if is_macho(path)}
        signature = "unchecked"
        if mode != "unsigned":
            run("codesign", "--verify", "--deep", "--strict", "--all-architectures", bundle)
            signature = run("codesign", "--display", "--verbose=4", "--all-architectures", bundle)
            if mode == "distribution":
                if not team_id or not re.fullmatch(r"[A-Z0-9]{10}", team_id):
                    raise RuntimeError("distribution bundle checks require a ten-character team ID")
                for architecture in sorted(ARCHITECTURES):
                    details = run("codesign", "--display", "--verbose=4", "--arch", architecture, bundle)
                    if (f"TeamIdentifier={team_id}" not in details or "Authority=Developer ID Application:" not in details
                            or "Timestamp=" not in details or not re.search(r"flags=0x[\da-fA-F]+\([^\n]*\bruntime\b", details)):
                        raise RuntimeError(f"{fmt}/{architecture}: Developer ID, team, secure timestamp or hardened runtime missing")
                    entitlements = run("codesign", "--display", "--entitlements", ":-", "--arch", architecture, bundle)
                    start = entitlements.find("<?xml")
                    if start >= 0:
                        end = entitlements.find("</plist>", start) + len("</plist>")
                        if plistlib.loads(entitlements[start:end].encode()).get("com.apple.security.get-task-allow"):
                            raise RuntimeError(f"{fmt}: debug entitlement is forbidden for distribution")
        reports.append({"format": fmt, "bundle": name, "bundle_identifier": metadata["CFBundleIdentifier"],
                        "version": version, "binaries": binaries, "factory_file_count": len(actual_presets),
                        "factory_files_sha256": actual_presets,
                        "signature_verified": mode != "unsigned", "signing_mode": mode,
                        "signature_kind": "developer-id" if "Authority=Developer ID Application:" in signature else
                                          "ad-hoc" if "Signature=adhoc" in signature else "other"})
    return reports


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
