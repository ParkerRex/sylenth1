#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="build"
config=""
product_bundle="Synthia"
dry_run=0
positional_count=0
skip_sign="${SYNTHIA_SKIP_ADHOC_SIGN:-0}"

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) dry_run=1 ;;
    -h|--help)
      printf 'usage: scripts/install-local-plugins.sh [build-dir] [config] [--dry-run]\n'
      printf 'Validate source bundles, copy AU/VST3, then ad-hoc sign and verify installed bundles.\n'
      printf 'SYNTHIA_SKIP_ADHOC_SIGN=1 preserves valid source AU/VST3 signatures; unsigned sources fail.\n'
      exit 0
      ;;
    --*) fail "unknown option: $1" ;;
    *)
      case "$positional_count" in
        0) build_dir="$1" ;;
        1) config="$1" ;;
        *) fail "too many positional arguments" ;;
      esac
      positional_count=$((positional_count + 1))
      ;;
  esac
  shift
done

case "$skip_sign" in
  0|1) ;;
  *) fail "SYNTHIA_SKIP_ADHOC_SIGN must be 0 or 1" ;;
esac
command -v codesign >/dev/null 2>&1 || fail "codesign is required for a verified local installation"

case "$build_dir" in
  /*) build_path="$build_dir" ;;
  *) build_path="$root_dir/$build_dir" ;;
esac

# Raw build output may be unsigned: developer packaging signs separate staged
# copies. Structural checks remain mandatory before any installed files change.
checker_args=("$build_path")
[[ -z "$config" ]] || checker_args+=("$config")
"$root_dir/scripts/check-plugin-bundles.sh" "${checker_args[@]}" --mode unsigned
artifact_dir="$(python3 - "$root_dir/scripts" "$build_path" "$config" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from release_support import artifact_directory
print(artifact_directory(Path(sys.argv[2]), sys.argv[3] or None))
PY
)"

au_src="$artifact_dir/AU/$product_bundle.component"
vst3_src="$artifact_dir/VST3/$product_bundle.vst3"
au_dest="$HOME/Library/Audio/Plug-Ins/Components"
vst3_dest="$HOME/Library/Audio/Plug-Ins/VST3"

verify_signature() {
  codesign --verify --deep --strict --all-architectures "$1" >/dev/null
}

if [[ "$skip_sign" == "1" ]]; then
  # Refuse unsigned or damaged sources before copying when preserving signatures.
  verify_signature "$au_src"
  verify_signature "$vst3_src"
fi

if [[ "$dry_run" == "1" ]]; then
  printf 'install preflight passed: %s\n' "$artifact_dir"
  if [[ "$skip_sign" == "1" ]]; then
    printf 'dry run: copy AU/VST3, preserve verified source signatures, verify installed copies\n'
  else
    printf 'dry run: copy AU/VST3, ad-hoc sign installed copies, verify both native slices\n'
  fi
  exit 0
fi

mkdir -p "$au_dest" "$vst3_dest"
rsync -a --delete "$au_src" "$au_dest/"
rsync -a --delete "$vst3_src" "$vst3_dest/"

sign_installed_bundle() {
  local label="$1"
  local bundle="$2"
  if [[ "$skip_sign" == "0" ]]; then
    codesign --force --deep --sign - "$bundle" >/dev/null
  fi
  # Skipping signature creation never skips verification of the installed bytes.
  verify_signature "$bundle"
  printf '%s codesign: installed signature verified for both native slices\n' "$label"
}

sign_installed_bundle "AU" "$au_dest/$product_bundle.component"
sign_installed_bundle "VST3" "$vst3_dest/$product_bundle.vst3"

printf 'installed AU: %s/%s.component\n' "$au_dest" "$product_bundle"
printf 'installed VST3: %s/%s.vst3\n' "$vst3_dest" "$product_bundle"
printf 'rescan plug-ins in Ableton before validation.\n'
printf 'AU validation command: auval -v aumu SynA PkRx\n'
