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
  -DDOWNSPOUT_BUILD_BASSGEN=ON
  -DDOWNSPOUT_BUILD_GATER=ON
  -DDOWNSPOUT_BUILD_PMIX=ON
  -DDOWNSPOUT_BUILD_EMIX=ON
  -DDOWNSPOUT_BUILD_MMIX=ON
  -DDOWNSPOUT_BUILD_TMIX=ON
  -DDOWNSPOUT_BUILD_MIXGEN=ON
  -DDOWNSPOUT_BUILD_LOOPDELAY=ON
  -DDOWNSPOUT_BUILD_LIGHTVERB=ON
  -DDOWNSPOUT_BUILD_MELGEN=ON
  -DDOWNSPOUT_BUILD_RIFT=ON
  -DDOWNSPOUT_BUILD_ORCHID=ON
  -DDOWNSPOUT_BUILD_AMBO=ON
  -DDOWNSPOUT_BUILD_CADENCE=ON
  -DDOWNSPOUT_BUILD_ARPGEN=ON
  -DDOWNSPOUT_BUILD_COUNTERPOINTER=ON
  -DDOWNSPOUT_BUILD_SIDECAR=ON
  -DDOWNSPOUT_BUILD_DRUMGEN=ON
  -DDOWNSPOUT_BUILD_DRUMKIT=ON
  -DDOWNSPOUT_BUILD_SYRINX=ON
  -DDOWNSPOUT_BUILD_GREMLIN=ON
  -DDOWNSPOUT_BUILD_GREMLIN_DRIVER=ON
  -DDOWNSPOUT_BUILD_GROUND=ON
  -DDOWNSPOUT_BUILD_FLOOZY=ON
  -DDOWNSPOUT_BUILD_BASILICO=ON
  -DDOWNSPOUT_BUILD_CANTICLE=ON
  -DDOWNSPOUT_BUILD_LUMA=ON
  -DDOWNSPOUT_BUILD_PAUNCHLAD=ON
  -DDOWNSPOUT_BUILD_LIFEFORM=ON
  -DDOWNSPOUT_BUILD_XOXOLO=ON
  -DDOWNSPOUT_BUILD_TUNEY_VST=ON
  -DDOWNSPOUT_BUILD_HARMONIC_ATLAS=ON
  -DDOWNSPOUT_BUILD_CONDUCTOR=ON
  -DDOWNSPOUT_BUILD_DRIFT=ON
  -DDOWNSPOUT_BUILD_MNEMOSYNE=ON
  -DDOWNSPOUT_BUILD_POLYMETER=ON
  -DDOWNSPOUT_BUILD_ORACLE=ON
  -DDOWNSPOUT_BUILD_MOSAIC=ON
  -DDOWNSPOUT_BUILD_RESONANCE_GARDEN=ON
  -DDOWNSPOUT_BUILD_ORBIT=ON
  -DDOWNSPOUT_BUILD_GUARDIAN=ON
  -DDOWNSPOUT_BUILD_FLUES_SYNTH_DRIVER=ON
  -DDOWNSPOUT_BUILD_MIDISCRIBE=ON
  -DDOWNSPOUT_BUILD_CAMPIONE=ON
  -DDOWNSPOUT_BUILD_DAMIANO=ON
  -DDOWNSPOUT_BUILD_SKREAM=ON
  "-DCMAKE_INSTALL_PREFIX=$vst3_dir"
)

if [[ -n "${DPF_ROOT:-}" ]]; then
  cmake_args+=("-DDPF_ROOT=$DPF_ROOT")
fi

needs_configure=1
if [[ -f "$build_dir/CMakeCache.txt" ]]; then
  if ! find "$repo_root" -name "CMakeLists.txt" -newer "$build_dir/CMakeCache.txt" -print -quit | grep -q .; then
    needs_configure=0
  fi
fi

if [[ "$needs_configure" == "1" ]]; then
  echo "Configuring downspout"
  cmake "${cmake_args[@]}"
else
  echo "CMake cache is up to date, skipping configure"
fi

echo "Building downspout"
cmake --build "$build_dir" --parallel "$(nproc)"

if [[ -d "$repo_root/third_party/DPF" || -n "${DPF_ROOT:-}" ]]; then
  echo "DPF available for wrapper targets"
else
  echo "DPF not available; only portable core targets will build"
fi

if [[ "$run_tests" != "0" ]]; then
  echo "Running tests"
  test_exit=0
  ctest --test-dir "$build_dir" --output-on-failure || test_exit=$?
  if [[ "$test_exit" != "0" ]]; then
    echo "WARNING: $test_exit test(s) failed — installation will continue."
    echo "Run 'ctest --test-dir $build_dir --output-on-failure' for details."
  fi
fi

echo "Installing to $vst3_dir"
cmake --install "$build_dir" --prefix "$vst3_dir"
# cmake install(DIRECTORY) does not preserve execute bits; set them so hosts
# (Carla, REAPER, Ardour) can dlopen the shared libraries.
find "$vst3_dir" -name "*.so" -exec chmod +x {} \;

if compgen -G "$vst3_dir/*.vst3" > /dev/null; then
  echo "Installed VST3 bundles:"
  find "$vst3_dir" -maxdepth 1 -type d -name "*.vst3" | sort
  cat <<EOF

If a DAW still shows old names, makers, or categories, force a plugin rescan
or clear that DAW's VST3 cache. This installer updates the bundles but does
not edit DAW-specific caches such as REAPER's reaper-vstplugins64.ini or
Ardour's ~/.cache/ardour*/vst entries.
EOF
else
  cat <<EOF
No .vst3 bundles were installed.

Check that DPF is available and that the VST3 wrapper targets are enabled.
EOF
fi
