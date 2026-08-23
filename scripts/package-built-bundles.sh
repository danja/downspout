#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin_dir="${DOWNSPOUT_BIN_DIR:-$repo_root/bin}"
dist_dir="${DOWNSPOUT_DIST_DIR:-$repo_root/dist}"
version="${DOWNSPOUT_VERSION:?DOWNSPOUT_VERSION must be set}"
platform="${DOWNSPOUT_RELEASE_PLATFORM:?DOWNSPOUT_RELEASE_PLATFORM must be set}"
sidecar_build="${DOWNSPOUT_BUILD_SIDECAR:-OFF}"

# Reference set used to report what a complete build should contain.
# Kept in step with scripts/package-release.sh, which gates the Linux build.
required_bundles=(
  campione.vst3 bassgen.vst3 p_mix.vst3 e_mix.vst3 m_mix.vst3 t_mix.vst3
  mixgen.vst3 loopdelay.vst3 lightverb.vst3 melgen.vst3 rift.vst3
  orchid.vst3 ambo.vst3 drumgen.vst3 drumkit.vst3 syrinx.vst3 cadence.vst3
  arpgen.vst3 counterpointer.vst3 gremlin.vst3 gremlin_driver.vst3
  ground.vst3 floozy.vst3 basilico.vst3 canticle.vst3 luma.vst3
  paunchlad.vst3 lifeform.vst3 xoxolo.vst3 tuney_vst.vst3
  harmonic_atlas.vst3 conductor.vst3 drift.vst3 mnemosyne.vst3
  polymeter.vst3 oracle.vst3 mosaic.vst3 resonance_garden.vst3 orbit.vst3
  guardian.vst3
)
if [[ "$sidecar_build" == "ON" ]]; then
  required_bundles=(
    campione.vst3 bassgen.vst3 p_mix.vst3 e_mix.vst3 m_mix.vst3 t_mix.vst3
    mixgen.vst3 loopdelay.vst3 lightverb.vst3 melgen.vst3 rift.vst3
    orchid.vst3 ambo.vst3 drumgen.vst3 drumkit.vst3 syrinx.vst3 cadence.vst3
    arpgen.vst3 counterpointer.vst3 sidecar.vst3 gremlin.vst3
    gremlin_driver.vst3 ground.vst3 floozy.vst3 basilico.vst3 canticle.vst3
    luma.vst3 paunchlad.vst3 lifeform.vst3 xoxolo.vst3 tuney_vst.vst3
    harmonic_atlas.vst3 conductor.vst3 drift.vst3 mnemosyne.vst3
    polymeter.vst3 oracle.vst3 mosaic.vst3 resonance_garden.vst3 orbit.vst3
    guardian.vst3
  )
fi

# Best effort: this script packages the cross-built (macOS/Windows) bundles,
# where a single plugin failing to compile must not discard the rest. Package
# whatever landed in bin/, and report the delta against required_bundles
# instead of aborting. Only a completely empty build is treated as fatal.
if [[ ! -d "$bin_dir" ]]; then
  echo "Build output directory not found: $bin_dir" >&2
  exit 1
fi

found_bundles=()
while IFS= read -r bundle; do
  found_bundles+=("$bundle")
done < <(cd "$bin_dir" && find . -maxdepth 1 -type d -name '*.vst3' -printf '%f\n' | sort)

if [[ ${#found_bundles[@]} -eq 0 ]]; then
  echo "No VST3 bundles found in $bin_dir" >&2
  exit 1
fi

missing_bundles=()
for bundle in "${required_bundles[@]}"; do
  [[ -d "$bin_dir/$bundle" ]] || missing_bundles+=("$bundle")
done

echo "Packaging ${#found_bundles[@]} bundle(s) for $platform: ${found_bundles[*]}"
if [[ ${#missing_bundles[@]} -gt 0 ]]; then
  echo "::warning::$platform build is missing ${#missing_bundles[@]} expected bundle(s): ${missing_bundles[*]}"
  echo "Missing expected bundles: ${missing_bundles[*]}" >&2
fi
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    echo "### $platform bundles"
    echo
    echo "- packaged: ${#found_bundles[@]}"
    echo "- missing: ${#missing_bundles[@]}"
    [[ ${#missing_bundles[@]} -gt 0 ]] && echo "- missing bundles: \`${missing_bundles[*]}\`"
    echo
  } >> "$GITHUB_STEP_SUMMARY"
fi

package_dir="$(mktemp -d "${TMPDIR:-/tmp}/downspout-package.XXXXXX")"
trap 'rm -rf "$package_dir"' EXIT

for bundle in "${found_bundles[@]}"; do
  cmake -E copy_directory "$bin_dir/$bundle" "$package_dir/$bundle"
done
cmake -E copy "$repo_root/LICENSE" "$package_dir/LICENSE"
cmake -E copy "$repo_root/README.md" "$package_dir/README.md"
cmake -E make_directory "$dist_dir"

artifact_base="downspout-$version-$platform-vst3.zip"
artifact_path="$dist_dir/$artifact_base"
(
  cd "$package_dir"
  cmake -E tar cf "$artifact_path" --format=zip "${found_bundles[@]}" LICENSE README.md
)

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$dist_dir" && sha256sum "$artifact_base" > "$artifact_base.sha256")
elif command -v shasum >/dev/null 2>&1; then
  (cd "$dist_dir" && shasum -a 256 "$artifact_base" > "$artifact_base.sha256")
fi

echo "Release artifact:"
echo "$artifact_path"
[[ -f "$artifact_path.sha256" ]] && echo "$artifact_path.sha256"
