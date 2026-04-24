#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${DOWNSPOUT_BUILD_DIR:-$repo_root/build}"
vst3_dir="${DOWNSPOUT_VST3_DIR:-$HOME/.vst3}"
build_type="${CMAKE_BUILD_TYPE:-Release}"
run_tests="${DOWNSPOUT_RUN_TESTS:-1}"

cmake_args=(
  -S "$repo_root"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE="$build_type"
  -DDOWNSPOUT_BUILD_DRUMGEN=ON
  "-DCMAKE_INSTALL_PREFIX=$vst3_dir"
)

if [[ -n "${DPF_ROOT:-}" ]]; then
  cmake_args+=("-DDPF_ROOT=$DPF_ROOT")
fi

echo "Configuring downspout"
cmake "${cmake_args[@]}"

echo "Building downspout"
cmake --build "$build_dir"

if [[ -d "$repo_root/third_party/DPF" || -n "${DPF_ROOT:-}" ]]; then
  echo "DPF available for wrapper targets"
else
  echo "DPF not available; only portable core targets will build"
fi

if [[ "$run_tests" != "0" ]]; then
  echo "Running tests"
  ctest --test-dir "$build_dir" --output-on-failure
fi

echo "Installing to $vst3_dir"
cmake --install "$build_dir"

if compgen -G "$vst3_dir/*.vst3" > /dev/null; then
  echo "Installed VST3 bundles:"
  find "$vst3_dir" -maxdepth 1 -type d -name "*.vst3" | sort
else
  cat <<EOF
No .vst3 bundles were installed.

Check that DPF is available and that the VST3 wrapper targets are enabled.
EOF
fi
