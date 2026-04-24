#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

build_type="${CMAKE_BUILD_TYPE:-Release}"
build_dir="${DOWNSPOUT_BUILD_DIR:-$repo_root/build/release}"
dist_dir="${DOWNSPOUT_DIST_DIR:-$repo_root/dist}"
staging_dir="${DOWNSPOUT_STAGING_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/downspout-stage.XXXXXX")}"
package_dir="${DOWNSPOUT_PACKAGE_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/downspout-package.XXXXXX")}"
run_tests="${DOWNSPOUT_RUN_TESTS:-1}"
strip_binaries="${DOWNSPOUT_STRIP:-1}"

version="${DOWNSPOUT_VERSION:-}"
if [[ -z "$version" ]]; then
  version="$(sed -nE 's/^[[:space:]]*project\(downspout[[:space:]]+VERSION[[:space:]]+([^[:space:]\)]+).*/\1/p' "$repo_root/CMakeLists.txt" | head -n 1)"
fi

if [[ -z "$version" ]]; then
  echo "Unable to determine release version. Set DOWNSPOUT_VERSION." >&2
  exit 1
fi

platform="${DOWNSPOUT_RELEASE_PLATFORM:-}"
if [[ -z "$platform" ]]; then
  case "$(uname -s):$(uname -m)" in
    Linux:x86_64|Linux:amd64) platform="linux-x86_64" ;;
    Linux:aarch64|Linux:arm64) platform="linux-arm64" ;;
    Darwin:x86_64) platform="macos-x86_64" ;;
    Darwin:arm64) platform="macos-arm64" ;;
    *)
      platform="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
      ;;
  esac
fi

fail_if_non_empty_dir() {
  local dir="$1"

  if [[ -d "$dir" ]] && find "$dir" -mindepth 1 -print -quit | grep -q .; then
    echo "Refusing to use non-empty directory: $dir" >&2
    echo "Set DOWNSPOUT_STAGING_DIR or DOWNSPOUT_PACKAGE_DIR to an empty path." >&2
    exit 1
  fi
}

fail_if_non_empty_dir "$staging_dir"
fail_if_non_empty_dir "$package_dir"

cmake_args=(
  -S "$repo_root"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE="$build_type"
  -DDOWNSPOUT_BUILD_CADENCE=ON
  -DDOWNSPOUT_BUILD_DRUMGEN=ON
  "-DCMAKE_INSTALL_PREFIX=$staging_dir"
)

if [[ -n "${DPF_ROOT:-}" ]]; then
  cmake_args+=("-DDPF_ROOT=$DPF_ROOT")
fi

echo "Configuring downspout $version ($build_type)"
cmake "${cmake_args[@]}"

build_command=(cmake --build "$build_dir" --config "$build_type")
if [[ -n "${DOWNSPOUT_BUILD_JOBS:-}" ]]; then
  build_command+=(--parallel "$DOWNSPOUT_BUILD_JOBS")
else
  build_command+=(--parallel)
fi

echo "Building downspout"
"${build_command[@]}"

if [[ "$run_tests" != "0" ]]; then
  echo "Running tests"
  ctest --test-dir "$build_dir" --build-config "$build_type" --output-on-failure
fi

echo "Installing release payload to $staging_dir"
cmake --install "$build_dir" --config "$build_type" --prefix "$staging_dir"

required_bundles=(bassgen.vst3 p_mix.vst3 drumgen.vst3 cadence.vst3)
for bundle in "${required_bundles[@]}"; do
  if [[ ! -d "$staging_dir/$bundle" ]]; then
    echo "Missing expected VST3 bundle: $staging_dir/$bundle" >&2
    exit 1
  fi
done

if [[ "$strip_binaries" != "0" && "$platform" == linux-* ]]; then
  strip_tool="${STRIP:-strip}"
  if command -v "$strip_tool" >/dev/null 2>&1; then
    echo "Stripping Linux shared objects"
    while IFS= read -r -d '' shared_object; do
      "$strip_tool" --strip-unneeded "$shared_object"
    done < <(find "$staging_dir" -type f -name '*.so' -print0)
  else
    echo "Skipping strip; '$strip_tool' was not found" >&2
  fi
fi

cmake -E make_directory "$dist_dir"
cmake -E copy_directory "$staging_dir" "$package_dir"
cmake -E copy "$repo_root/LICENSE" "$package_dir/LICENSE"
cmake -E copy "$repo_root/README.md" "$package_dir/README.md"

artifact_base="downspout-$version-$platform-vst3.zip"
artifact_path="$dist_dir/$artifact_base"

echo "Creating $artifact_path"
(
  cd "$package_dir"
  cmake -E tar cf "$artifact_path" --format=zip "${required_bundles[@]}" LICENSE README.md
)

if command -v sha256sum >/dev/null 2>&1; then
  (
    cd "$dist_dir"
    sha256sum "$artifact_base" > "$artifact_base.sha256"
  )
elif command -v shasum >/dev/null 2>&1; then
  (
    cd "$dist_dir"
    shasum -a 256 "$artifact_base" > "$artifact_base.sha256"
  )
fi

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  echo "artifact=$artifact_path" >> "$GITHUB_OUTPUT"
  if [[ -f "$artifact_path.sha256" ]]; then
    echo "checksum=$artifact_path.sha256" >> "$GITHUB_OUTPUT"
  fi
fi

echo "Release artifact:"
echo "$artifact_path"
if [[ -f "$artifact_path.sha256" ]]; then
  echo "$artifact_path.sha256"
fi
