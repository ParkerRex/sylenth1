#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
product_bundle="Synthia"

case "$build_dir" in
  /*) artifact_root="$build_dir/SynthiaPlugin_artefacts" ;;
  *) artifact_root="$root_dir/$build_dir/SynthiaPlugin_artefacts" ;;
esac

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

resolve_artifact_dir() {
  local config_dir
  local config_dirs=(Release Debug RelWithDebInfo MinSizeRel "")

  if [[ -n "${2:-}" ]]; then
    config_dirs=("$2" Release Debug RelWithDebInfo MinSizeRel "")
  fi

  for config_dir in "${config_dirs[@]}"; do
    [[ -n "$config_dir" ]] || {
      printf '%s\n' "$1"
      return
    }

    [[ -d "$1/$config_dir" ]] || continue
    [[ -d "$1/$config_dir/AU/$product_bundle.component" ]] || continue
    [[ -d "$1/$config_dir/VST3/$product_bundle.vst3" ]] || continue

    printf '%s\n' "$1/$config_dir"
    return
  done
}

artifact_dir="$(resolve_artifact_dir "$artifact_root" "${2:-}")"
[[ -n "$artifact_dir" ]] || fail "could not resolve plugin artifact directory under $artifact_root"

"$root_dir/scripts/check-plugin-bundles.sh" "$build_dir" "${2:-}"

au_src="$artifact_dir/AU/$product_bundle.component"
vst3_src="$artifact_dir/VST3/$product_bundle.vst3"
au_dest="$HOME/Library/Audio/Plug-Ins/Components"
vst3_dest="$HOME/Library/Audio/Plug-Ins/VST3"

[[ -d "$au_src" ]] || fail "AU bundle missing: $au_src"
[[ -d "$vst3_src" ]] || fail "VST3 bundle missing: $vst3_src"

mkdir -p "$au_dest" "$vst3_dest"

rsync -a --delete "$au_src" "$au_dest/"
rsync -a --delete "$vst3_src" "$vst3_dest/"

sign_installed_bundle() {
  local label="$1"
  local bundle="$2"

  if [[ "${SYNTHIA_SKIP_ADHOC_SIGN:-0}" == "1" ]]; then
    printf '%s codesign: skipped by SYNTHIA_SKIP_ADHOC_SIGN=1\n' "$label"
    return
  fi

  if ! command -v codesign >/dev/null 2>&1; then
    printf '%s codesign: skipped because codesign is unavailable\n' "$label"
    return
  fi

  codesign --force --deep --sign - "$bundle" >/dev/null
  codesign --verify --deep --strict "$bundle" >/dev/null
  printf '%s codesign: ad-hoc signed for local host scanning\n' "$label"
}

sign_installed_bundle "AU" "$au_dest/$product_bundle.component"
sign_installed_bundle "VST3" "$vst3_dest/$product_bundle.vst3"

printf 'installed AU: %s/%s.component\n' "$au_dest" "$product_bundle"
printf 'installed VST3: %s/%s.vst3\n' "$vst3_dest" "$product_bundle"
printf 'rescan plug-ins in Ableton before validation.\n'
printf 'AU validation command: auval -v aumu SynA PkRx\n'
