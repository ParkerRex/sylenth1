#!/usr/bin/env python3
"""Build, validate and package native AU/VST3/Standalone candidates on macOS."""
from __future__ import annotations

import argparse
import datetime
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from release_support import (ROOT, MINIMUM_MACOS, BUNDLES, artifact_directory,
                             check_binary, check_bundles, is_macho, project_version, run, sha256, write_json)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("Release", "RelWithDebInfo"), default="RelWithDebInfo")
    parser.add_argument("--build-dir", type=Path, default=Path("build-release-current"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--juce-path", type=Path, help="Optional local pinned JUCE 8.0.13 checkout")
    parser.add_argument("--install-local", action="store_true", help="Install developer AU/VST3 after all gates pass")
    parser.add_argument("--package", action="store_true", help="Create developer ZIPs or a distribution DMG")
    parser.add_argument("--signing-mode", choices=("developer", "distribution"), default="developer")
    parser.add_argument("--identity", help="Existing Developer ID Application signing identity")
    parser.add_argument("--team-id", help="Ten-character Apple team ID, checked against every signed slice")
    parser.add_argument("--notary-profile", help="Existing notarytool Keychain profile; no raw credentials")
    parser.add_argument("--dry-run", action="store_true", help="Validate options/cache and describe stages without build/sign/upload/install")
    return parser.parse_args()


def cache_values(build: Path) -> dict[str, str]:
    cache = build / "CMakeCache.txt"
    if not cache.exists():
        return {}
    values = {}
    for line in cache.read_text().splitlines():
        match = re.match(r"([^#/:][^:]*):[^=]+=(.*)", line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def preflight(args: argparse.Namespace) -> dict[str, str]:
    if args.jobs < 1:
        raise RuntimeError("--jobs must be positive")
    args.build_dir = (ROOT / args.build_dir).resolve()
    if args.build_dir == ROOT:
        raise RuntimeError("build directory must be outside the source root")
    if args.juce_path:
        args.juce_path = args.juce_path.resolve()
        header = args.juce_path / "modules/juce_core/system/juce_StandardHeader.h"
        if not header.is_file() or not re.search(r"#define JUCE_MAJOR_VERSION\s+8\b", header.read_text()):
            raise RuntimeError("--juce-path must point at a JUCE 8 checkout")
        for name, value in (("MINOR", 0), ("BUILDNUMBER", 13)):
            if not re.search(rf"#define JUCE_{name}_VERSION\s+{value}\b" if name == "MINOR" else
                             rf"#define JUCE_{name}\s+{value}\b", header.read_text()):
                raise RuntimeError("release tooling requires pinned JUCE 8.0.13")
    cache = cache_values(args.build_dir)
    cached_source = cache.get("CMAKE_HOME_DIRECTORY")
    if cached_source and Path(cached_source).resolve() != ROOT:
        raise RuntimeError("CMake cache belongs to another checkout; choose a fresh --build-dir")
    if cache.get("SYNTHIA_ENABLE_ASAN", "OFF") != "OFF" or cache.get("SYNTHIA_ENABLE_UBSAN", "OFF") != "OFF":
        raise RuntimeError("release candidates cannot reuse a sanitizer build directory")
    if args.signing_mode == "distribution":
        if args.config != "Release" or not args.package:
            raise RuntimeError("distribution mode requires --config Release --package")
        if args.install_local:
            raise RuntimeError("distribution mode cannot use --install-local (local installer replaces signatures)")
        if not args.identity or not args.identity.startswith("Developer ID Application:"):
            raise RuntimeError("distribution mode requires an explicit Developer ID Application --identity")
        if not args.team_id or not re.fullmatch(r"[A-Z0-9]{10}", args.team_id):
            raise RuntimeError("distribution mode requires a ten-character --team-id")
        if not args.notary_profile or not args.notary_profile.strip():
            raise RuntimeError("distribution mode requires an existing --notary-profile")
        if run("git", "status", "--porcelain", cwd=ROOT).strip():
            raise RuntimeError("distribution mode requires a clean committed source tree")
    if platform.system() != "Darwin":
        raise RuntimeError("native release tooling requires macOS")
    for tool in ("cmake", "ctest", "git", "lipo", "otool", "codesign", "ditto"):
        if shutil.which(tool) is None:
            raise RuntimeError(f"required tool missing: {tool}")
    if args.signing_mode == "distribution":
        for tool in ("xcrun", "hdiutil", "spctl"):
            if shutil.which(tool) is None:
                raise RuntimeError(f"required distribution tool missing: {tool}")
        run("xcrun", "--find", "notarytool")
        run("xcrun", "--find", "stapler")
    return cache


def sign(bundle: Path, args: argparse.Namespace) -> None:
    options = ["--force", "--sign", args.identity if args.signing_mode == "distribution" else "-"]
    if args.signing_mode == "distribution":
        options += ["--timestamp", "--options", "runtime"]
    else:
        options += ["--timestamp=none"]
    # Sign from the inside out; --deep is verification only, never a signing shortcut.
    main_executable = bundle / "Contents/MacOS/Synthia"
    binaries = [path for path in bundle.rglob("*") if is_macho(path) and path != main_executable]
    for path in sorted(binaries, key=lambda path: len(path.parts), reverse=True):
        run("codesign", *options, path)
    nested = [path for path in bundle.rglob("*") if path.is_dir() and
              path.suffix in (".app", ".framework", ".bundle", ".xpc", ".component", ".vst3") and not path.is_symlink()]
    for path in sorted(nested, key=lambda path: len(path.parts), reverse=True):
        run("codesign", *options, path)
    run("codesign", *options, bundle)


def notarize(path: Path, args: argparse.Namespace, report: Path) -> dict[str, str]:
    print(f"notarization: {path.name}", flush=True)
    result = json.loads(run("xcrun", "notarytool", "submit", path, "--keychain-profile",
                            args.notary_profile, "--wait", "--output-format", "json"))
    status = {"id": result.get("id", ""), "status": result.get("status", "")}
    write_json(report, status)
    if status["status"] != "Accepted" or not status["id"]:
        raise RuntimeError(f"notarization was not accepted; inspect submission {status['id']}")
    return status


def executable(build: Path, config: str) -> Path:
    paths = [build / "SynthiaRender", build / config / "SynthiaRender"]
    for path in paths:
        if path.is_file() and os.access(path, os.X_OK):
            return path
    raise RuntimeError("SynthiaRender executable missing after build")


def main() -> None:
    args = arguments()
    cache = preflight(args)
    source_commit = run("git", "rev-parse", "HEAD", cwd=ROOT).strip()
    print(f"native build: {args.config}, arm64+x86_64, macOS {MINIMUM_MACOS} minimum", flush=True)
    stages = ["configure optimized universal targets", "build", "whitespace and realtime/type-safety gates",
              "CTest", "standalone core renders", "sign bundles", "strict bundle checks"]
    if args.package:
        stages += ["stage bundles", "Developer ID notarization and DMG assessment" if args.signing_mode == "distribution"
                   else "developer ZIP archives", "release manifest and SHA-256 checksums"]
    if args.install_local:
        stages += ["install developer AU/VST3"]
    if args.dry_run:
        print("dry run: " + " -> ".join(stages))
        return
    configure = ["cmake", "-S", ROOT, "-B", args.build_dir, "-DSYNTHIA_ENABLE_TESTS=ON",
                 "-DSYNTHIA_ENABLE_COPY_AFTER_BUILD=OFF", "-DSYNTHIA_ENABLE_ASAN=OFF", "-DSYNTHIA_ENABLE_UBSAN=OFF",
                 "-DSYNTHIA_JUCE_GIT_TAG=8.0.13", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                 f"-DCMAKE_BUILD_TYPE={args.config}", "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64",
                 f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MINIMUM_MACOS}"]
    juce_path = args.juce_path or (Path(cache["SYNTHIA_JUCE_PATH"]) if cache.get("SYNTHIA_JUCE_PATH") else None)
    if juce_path:
        # Reuse the same version validation for a previously cached local dependency.
        args.juce_path = juce_path
        preflight(args)
        configure.append(f"-DSYNTHIA_JUCE_PATH={juce_path}")
    for label, command in (("configure", configure),
                           ("build", ["cmake", "--build", args.build_dir, "--config", args.config, "-j", args.jobs]),
                           ("whitespace", ["git", "diff", "--check"]),
                           ("staged whitespace", ["git", "diff", "--cached", "--check"]),
                           ("realtime/type safety", [sys.executable, ROOT / "scripts/check-cpp-fitness.py"])):
        print(label, flush=True)
        # Stream build output so failures remain reviewable without buffering a full build.
        subprocess.run([str(arg) for arg in command], cwd=ROOT, check=True)
    reports = args.build_dir / "reports/release"
    reports.mkdir(parents=True, exist_ok=True)
    print("CTest", flush=True)
    subprocess.run(["ctest", "--test-dir", str(args.build_dir), "-C", args.config, "--output-on-failure",
                    "--no-tests=error", "--output-junit", str(reports / "ctest.xml")], cwd=ROOT, check=True)
    renderer = executable(args.build_dir, args.config)
    check_binary_report = check_binary(renderer)
    print("standalone core render suite", flush=True)
    subprocess.run([str(renderer), "--suite", "core", "--output-dir", str(reports / "core")], cwd=ROOT, check=True)
    summary = json.loads((reports / "core/summary.json").read_text())
    if (summary.get("suite") != "core" or summary.get("failed_count") != 0
            or not summary.get("report_count") or summary.get("passed_count") != summary.get("report_count")):
        raise RuntimeError("standalone core summary is incomplete or failed")
    artifacts = artifact_directory(args.build_dir, args.config)
    # Check structure before invoking any signing service.
    check_bundles(artifacts, "unsigned")
    version = project_version()
    dirty = bool(run("git", "status", "--porcelain", cwd=ROOT).strip())
    commit = run("git", "rev-parse", "HEAD", cwd=ROOT).strip()
    if args.signing_mode == "distribution" and (dirty or commit != source_commit):
        raise RuntimeError("source changed during the distribution build; commit changes and rebuild")
    if not args.package:
        for fmt, name in BUNDLES.items():
            sign(artifacts / fmt / name, args)
        check_bundles(artifacts, args.signing_mode, args.team_id)
    else:
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        package = ROOT / "build/release-artifacts" / f"synthia-native-{version}-{args.signing_mode}-{stamp}"
        package.mkdir(parents=True)
        with tempfile.TemporaryDirectory(prefix="synthia-release-", dir=args.build_dir) as temporary:
            stage = Path(temporary)
            for fmt, name in BUNDLES.items():
                destination = stage / fmt / name
                destination.parent.mkdir(parents=True)
                run("ditto", artifacts / fmt / name, destination)
                sign(destination, args)
            bundle_report = check_bundles(stage, args.signing_mode, args.team_id)
            submissions = []
            deliverables: list[Path] = []
            if args.signing_mode == "distribution":
                # Notarize all three bundles, then staple the standalone before building the final DMG.
                plugin_zip = stage / "notary-all.zip"
                payload = stage / "Payload"
                payload.mkdir()
                for fmt, name in BUNDLES.items():
                    run("ditto", stage / fmt / name, payload / name)
                run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", payload, plugin_zip)
                submissions.append(notarize(plugin_zip, args, reports / "notary-bundles.json"))
                standalone = stage / "Standalone/Synthia.app"
                run("xcrun", "stapler", "staple", standalone)
                run("xcrun", "stapler", "validate", standalone)
                run("spctl", "--assess", "--type", "execute", "--verbose=2", standalone)
                run("ditto", standalone, payload / "Synthia.app")
                (payload / "INSTALL.txt").write_text("Copy Synthia.app to /Applications.\nCopy Synthia.component to ~/Library/Audio/Plug-Ins/Components/.\nCopy Synthia.vst3 to ~/Library/Audio/Plug-Ins/VST3/.\nQuit and rescan your audio host after installation.\n")
                image = package / f"Synthia-Native-{version}.dmg"
                run("hdiutil", "create", "-volname", f"Synthia {version}", "-srcfolder", payload,
                    "-format", "UDZO", "-ov", image)
                run("codesign", "--force", "--sign", args.identity, "--timestamp", image)
                run("codesign", "--verify", "--strict", image)
                submissions.append(notarize(image, args, reports / "notary-image.json"))
                run("xcrun", "stapler", "staple", image)
                run("xcrun", "stapler", "validate", image)
                run("spctl", "--assess", "--type", "open", "--context", "context:primary-signature", "--verbose=2", image)
                deliverables.append(image)
                bundle_report = check_bundles(stage, args.signing_mode, args.team_id)
            else:
                for fmt, name in BUNDLES.items():
                    archive = package / f"Synthia-Native-{fmt}-{version}-{args.config}-Developer.zip"
                    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", stage / fmt / name, archive)
                    deliverables.append(archive)
            for report in (reports / "ctest.xml", reports / "core/summary.json"):
                if not report.is_file():
                    raise RuntimeError(f"required validation report missing: {report}")
                shutil.copy2(report, package / ("core-summary.json" if report.suffix == ".json" else "ctest.xml"))
                deliverables.append(package / ("core-summary.json" if report.suffix == ".json" else "ctest.xml"))
            manifest = {"schema_version": 1, "product": "Synthia", "version": version,
                        "source_commit": commit, "source_dirty": dirty, "created_utc": stamp,
                        "implementation": "native-only", "configuration": args.config,
                        "minimum_macos": MINIMUM_MACOS, "required_architectures": ["arm64", "x86_64"],
                        "signing_mode": args.signing_mode, "notarized": bool(submissions),
                        "reference_fidelity": "not_verified", "distribution_qualification": "not_qualified",
                        "notary_submissions": submissions, "juce_version": "8.0.13",
                        "build_macos": platform.mac_ver()[0], "xcode": run("xcodebuild", "-version").strip(),
                        "bundles": bundle_report, "validation": {"ctest": "passed", "core": "passed",
                        "realtime_type_safety": "passed", "render_binary": check_binary_report,
                        "ableton": "external evidence required", "intel_hardware": "external evidence required",
                        "original_reference_audio": "external evidence required",
                        "original_reference_images": "external evidence required",
                        "oldest_supported_macos": "external evidence required"},
                        "publication_approved": False,
                        "external_prerequisites": ["Owner chooses Synthia license and JUCE commercial/AGPLv3 compliance",
                                                   "Third-party asset/content review", "Current AU/VST3 host proof",
                                                   "Selected original image/audio captures and reviewed comparison evidence",
                                                   "Clean-machine and supported macOS/Intel proof", "Owner authorizes public release"],
                        "artifacts": [{"file": path.name, "bytes": path.stat().st_size, "sha256": sha256(path)}
                                      for path in deliverables]}
            write_json(package / "manifest.json", manifest)
            deliverables.append(package / "manifest.json")
            (package / "SHA256SUMS.txt").write_text("".join(f"{sha256(path)}  {path.name}\n" for path in sorted(deliverables)))
        print(f"package: {package}", flush=True)
        print("Distribution prerequisites remain in docs/release/checklist.md; publication is a separate authorized action.")
    if args.install_local:
        for fmt, name in BUNDLES.items():
            sign(artifacts / fmt / name, args)
        check_bundles(artifacts, args.signing_mode, args.team_id)
        subprocess.run([str(ROOT / "scripts/install-local-plugins.sh"), str(args.build_dir), args.config], cwd=ROOT, check=True)
    print(f"validated artifacts: {artifacts}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
