#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

build_type="${CMAKE_BUILD_TYPE:-Release}"
build_dir="${DOWNSPOUT_SCREENSHOT_BUILD_DIR:-$repo_root/build/screenshots}"
output_dir="${DOWNSPOUT_SCREENSHOT_OUTPUT_DIR:-$repo_root/docs/pages/assets/plugins}"
skip_build="${DOWNSPOUT_SCREENSHOT_SKIP_BUILD:-0}"
wait_seconds="${DOWNSPOUT_SCREENSHOT_WAIT_SECONDS:-15}"
settle_seconds="${DOWNSPOUT_SCREENSHOT_SETTLE_SECONDS:-1}"
screen_geometry="${DOWNSPOUT_SCREENSHOT_SCREEN:-1920x1400x24}"
start_jack="${DOWNSPOUT_SCREENSHOT_START_JACK:-1}"
app_args=()
if [[ -n "${DOWNSPOUT_SCREENSHOT_APP_ARGS:-}" ]]; then
  read -r -a app_args <<< "$DOWNSPOUT_SCREENSHOT_APP_ARGS"
fi

plugins=(
  "bassgen:bassgen"
  "p-mix:p_mix"
  "e-mix:e_mix"
  "m-mix:m_mix"
  "melgen:melgen"
  "rift:rift"
  "orchid:orchid"
  "drumgen:drumgen"
  "drumkit:drumkit"
  "xoxolo:xoxolo"
  "cadence:cadence"
  "arpgen:arpgen"
  "counterpointer:counterpointer"
  "sidecar:sidecar"
  "gremlin:gremlin"
  "gremlin-driver:gremlin_driver"
  "ground:ground"
  "ambo:ambo"
  "floozy:floozy"
  "basilico:basilico"
  "canticle:canticle"
  "luma:luma"
  "paunchlad:paunchlad"
  "lifeform:lifeform"
)

usage() {
  cat <<EOF
Usage: $(basename "$0") [options] [plugin...]

Build standalone DPF UI apps and capture plugin screenshots for GitHub Pages.
Plugin names may use either README slugs such as p-mix or executable names such
as p_mix. With no plugin arguments, all plugins are captured.

Options:
  --build-dir DIR      CMake build directory. Default: build/screenshots
  --output-dir DIR     Screenshot output directory. Default: docs/pages/assets/plugins
  --skip-build         Reuse an existing screenshot build.
  --list               List known plugins and exit.
  -h, --help           Show this help.

Environment:
  CMAKE_BUILD_TYPE                    Build type. Default: Release
  DPF_ROOT                            Optional external DPF checkout
  DOWNSPOUT_SCREENSHOT_WAIT_SECONDS   Window wait timeout. Default: 15
  DOWNSPOUT_SCREENSHOT_SETTLE_SECONDS Delay before capture. Default: 1
  DOWNSPOUT_SCREENSHOT_SCREEN         Xvfb screen. Default: 1920x1400x24
  DOWNSPOUT_SCREENSHOT_START_JACK     Start jackd -d dummy if needed. Default: 1
  DOWNSPOUT_SCREENSHOT_APP_ARGS       Extra standalone app args.
EOF
}

list_plugins() {
  local entry slug exe
  for entry in "${plugins[@]}"; do
    IFS=: read -r slug exe <<< "$entry"
    printf '%-16s %s\n' "$slug" "$exe"
  done
}

selected=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      build_dir="$2"
      shift 2
      ;;
    --output-dir)
      output_dir="$2"
      shift 2
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --list)
      list_plugins
      exit 0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      while [[ $# -gt 0 ]]; do
        selected+=("$1")
        shift
      done
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      selected+=("$1")
      shift
      ;;
  esac
done

require_command() {
  local command_name="$1"
  local package_hint="$2"

  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Missing required command: $command_name" >&2
    echo "Install $package_hint and rerun this script." >&2
    exit 1
  fi
}

find_plugin_entry() {
  local requested="$1"
  local entry slug exe

  for entry in "${plugins[@]}"; do
    IFS=: read -r slug exe <<< "$entry"
    if [[ "$requested" == "$slug" || "$requested" == "$exe" ]]; then
      printf '%s:%s\n' "$slug" "$exe"
      return 0
    fi
  done

  echo "Unknown plugin: $requested" >&2
  echo "Known plugins:" >&2
  list_plugins >&2
  return 1
}

capture_list=()
if [[ ${#selected[@]} -eq 0 ]]; then
  capture_list=("${plugins[@]}")
else
  for requested in "${selected[@]}"; do
    capture_list+=("$(find_plugin_entry "$requested")")
  done
fi

if [[ "$skip_build" != "1" ]]; then
  cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE="$build_type"
    -DDOWNSPOUT_BUILD_SCREENSHOT_APPS=ON
  )

  if [[ -n "${DPF_ROOT:-}" ]]; then
    cmake_args+=("-DDPF_ROOT=$DPF_ROOT")
  fi

  echo "Configuring screenshot build"
  cmake "${cmake_args[@]}"

  build_targets=()
  for entry in "${capture_list[@]}"; do
    IFS=: read -r _slug exe <<< "$entry"
    build_targets+=("$exe-jack")
  done

  echo "Building standalone UI targets"
  cmake --build "$build_dir" --config "$build_type" --target "${build_targets[@]}"
fi

if [[ -z "${DISPLAY:-}" && "${DOWNSPOUT_SCREENSHOT_UNDER_XVFB:-0}" != "1" ]]; then
  require_command xvfb-run "the xvfb package"
  exec xvfb-run -a -s "-screen 0 $screen_geometry" \
    env DOWNSPOUT_SCREENSHOT_UNDER_XVFB=1 "$0" \
    --build-dir "$build_dir" \
    --output-dir "$output_dir" \
    --skip-build \
    "${selected[@]}"
fi

require_command import "ImageMagick"
if ! command -v xdotool >/dev/null 2>&1; then
  require_command xwininfo "the x11-utils package"
  require_command xprop "the x11-utils package"
fi

mkdir -p "$output_dir"

current_pid=""
jack_pid=""
cleanup_current() {
  if [[ -n "$current_pid" ]] && kill -0 "$current_pid" >/dev/null 2>&1; then
    kill "$current_pid" >/dev/null 2>&1 || true
    sleep 0.2
    kill -9 "$current_pid" >/dev/null 2>&1 || true
  fi
}
cleanup_all() {
  cleanup_current
  if [[ -n "$jack_pid" ]] && kill -0 "$jack_pid" >/dev/null 2>&1; then
    kill "$jack_pid" >/dev/null 2>&1 || true
    wait "$jack_pid" >/dev/null 2>&1 || true
  fi
}
trap cleanup_all EXIT INT TERM

ensure_jack_server() {
  if [[ "$start_jack" != "1" ]]; then
    return 0
  fi

  if command -v jack_wait >/dev/null 2>&1 && jack_wait -c -t 1 >/dev/null 2>&1; then
    return 0
  fi

  require_command jackd "the jackd2 package"

  local log_file="${TMPDIR:-/tmp}/downspout-screenshot-jackd.log"
  echo "Starting temporary JACK dummy server"
  jackd -r -d dummy -r 48000 -p 256 >"$log_file" 2>&1 &
  jack_pid="$!"

  if command -v jack_wait >/dev/null 2>&1; then
    if ! jack_wait -w -t 10 >/dev/null 2>&1; then
      echo "Timed out waiting for temporary JACK server." >&2
      if [[ -s "$log_file" ]]; then
        echo "Last jackd output:" >&2
        tail -n 40 "$log_file" >&2
      fi
      return 1
    fi
  else
    sleep 2
  fi
}

ensure_jack_server

wait_for_window() {
  local pid="$1"
  local exe="$2"
  local deadline=$((SECONDS + wait_seconds))
  local ids id

  while (( SECONDS < deadline )); do
    if ! kill -0 "$pid" >/dev/null 2>&1; then
      return 1
    fi

    if command -v xdotool >/dev/null 2>&1; then
      ids="$(xdotool search --pid "$pid" 2>/dev/null || true)"
      if [[ -z "$ids" ]]; then
        ids="$(xdotool search --name "$exe" 2>/dev/null || true)"
      fi
    else
      ids="$(
        xwininfo -root -tree 2>/dev/null \
          | sed -nE 's/^[[:space:]]*(0x[0-9a-fA-F]+).*/\1/p' \
          | while IFS= read -r id; do
              if xprop -id "$id" _NET_WM_PID 2>/dev/null | grep -Eq "= ${pid}$"; then
                printf '%s\n' "$id"
              fi
            done
      )"
    fi

    for id in $ids; do
      if command -v xdotool >/dev/null 2>&1; then
        xdotool getwindowgeometry "$id" >/dev/null 2>&1 || continue
      else
        xwininfo -id "$id" >/dev/null 2>&1 || continue
      fi

        printf '%s\n' "$id"
        return 0
    done

    sleep 0.2
  done

  return 1
}

capture_plugin() {
  local slug="$1"
  local exe="$2"
  local app="$build_dir/bin/$exe"
  local output="$output_dir/$slug.png"
  local log_file="${TMPDIR:-/tmp}/downspout-screenshot-$slug.log"
  local window_id

  if [[ ! -x "$app" ]]; then
    echo "Missing standalone app: $app" >&2
    echo "Run without --skip-build or build target $exe-jack first." >&2
    return 1
  fi

  echo "Capturing $slug"
  "$app" "${app_args[@]}" >"$log_file" 2>&1 &
  current_pid="$!"

  if ! window_id="$(wait_for_window "$current_pid" "$exe")"; then
    echo "Timed out waiting for $slug UI window." >&2
    if [[ -s "$log_file" ]]; then
      echo "Last output from $app:" >&2
      tail -n 40 "$log_file" >&2
    fi
    cleanup_current
    current_pid=""
    return 1
  fi

  if command -v xdotool >/dev/null 2>&1; then
    xdotool windowraise "$window_id" >/dev/null 2>&1 || true
    xdotool windowactivate "$window_id" >/dev/null 2>&1 || true
  elif command -v wmctrl >/dev/null 2>&1; then
    wmctrl -ia "$window_id" >/dev/null 2>&1 || true
  fi
  sleep "$settle_seconds"

  import -window "$window_id" "$output"
  if command -v convert >/dev/null 2>&1; then
    convert "$output" -strip "$output"
  fi

  cleanup_current
  current_pid=""
}

for entry in "${capture_list[@]}"; do
  IFS=: read -r slug exe <<< "$entry"
  capture_plugin "$slug" "$exe"
done

echo "Screenshots written to $output_dir"
