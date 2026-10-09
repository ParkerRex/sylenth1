#!/usr/bin/env python3
"""Fail closed on incomplete or inconsistent native universal bundles."""
import argparse
import sys
from pathlib import Path
from release_support import ROOT, artifact_directory, check_bundles, write_json


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", nargs="?", default="build")
    parser.add_argument("config", nargs="?")
    parser.add_argument("--mode", choices=("developer", "distribution", "unsigned"), default="developer")
    parser.add_argument("--team-id")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    build = Path(args.build_dir)
    if not build.is_absolute():
        build = ROOT / build
    artifacts = artifact_directory(build, args.config)
    report = check_bundles(artifacts, args.mode, args.team_id)
    if args.output:
        write_json(args.output, {"schema_version": 1, "passed": True, "bundles": report})
    for bundle in report:
        print(f"{bundle['format']}: universal, metadata/presets valid, signature {bundle['signature_kind']}")
    print(f"bundle checks passed: {artifacts}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
