#!/usr/bin/env bash
# Usage: inspect-plugin.sh [--fix] <plugin-name>
#
# Validates a VST3 bundle and reports metadata.
# Flags anything that would cause the plugin to be invisible in Reaper.
#
# --fix : attempt to clear the stale Reaper cache entry and touch the .so
#         so that Reaper will rescan on next startup / manual rescan.

set -uo pipefail

BOLD='\033[1m'
RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[0;33m'
CYN='\033[0;36m'
RST='\033[0m'

pass()   { printf "  ${GRN}PASS${RST}  %s\n" "$*"; }
fail()   { printf "  ${RED}FAIL${RST}  %s\n" "$*"; FAILURES=$((FAILURES+1)); }
warn()   { printf "  ${YLW}WARN${RST}  %s\n" "$*"; }
info()   { printf "        %s\n" "$*"; }
action() { printf "  ${CYN}FIX ${RST}  %s\n" "$*"; }

FAILURES=0
DO_FIX=0

# ---------------------------------------------------------------------------
# Args
# ---------------------------------------------------------------------------
if [[ $# -lt 1 ]]; then
  echo "Usage: $(basename "$0") [--fix] <plugin-name>" >&2
  echo "  e.g.  $(basename "$0") bubbles" >&2
  echo "        $(basename "$0") --fix bubbles" >&2
  exit 1
fi

if [[ "$1" == "--fix" ]]; then
  DO_FIX=1
  shift
fi

name="$1"
bare="${name%.vst3}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${DOWNSPOUT_BUILD_DIR:-$repo_root/build}"
vst3_dir="${DOWNSPOUT_VST3_DIR:-$HOME/.vst3}"

bundle_names=( "${bare}.vst3" "${bare//-/_}.vst3" )

bundle=""
bundle_source=""
for candidate in "${bundle_names[@]}"; do
  if [[ -d "$vst3_dir/$candidate" ]]; then
    bundle="$vst3_dir/$candidate"
    bundle_source="installed ($vst3_dir)"
    break
  fi
  if [[ -d "$build_dir/bin/$candidate" ]]; then
    bundle="$build_dir/bin/$candidate"
    bundle_source="build output ($build_dir/bin)"
    break
  fi
done

printf "\n${BOLD}${CYN}=== Downspout plugin inspector: %s ===${RST}\n\n" "$bare"

if [[ -z "$bundle" ]]; then
  fail "Bundle not found. Searched:"
  info "  $vst3_dir/${bundle_names[0]}"
  info "  $build_dir/bin/${bundle_names[0]}"
  exit 1
fi

printf "${BOLD}Bundle:${RST} %s\n" "$bundle"
printf "${BOLD}Source:${RST} %s\n\n" "$bundle_source"

# ---------------------------------------------------------------------------
# 1. Bundle structure
# ---------------------------------------------------------------------------
printf "${BOLD}[1] Bundle structure${RST}\n"

arch="x86_64-linux"
contents="$bundle/Contents"
so_path="$contents/$arch/${bare//-/_}.so"
[[ ! -f "$so_path" ]] && so_path="$contents/$arch/${bare}.so"

[[ -d "$contents" ]]       && pass "Contents/ directory exists"      || fail "Contents/ directory missing"
[[ -d "$contents/$arch" ]] && pass "$arch/ subdirectory exists"      || fail "$arch/ subdirectory missing"

if [[ -f "$so_path" ]]; then
  pass "Shared library: $(basename "$so_path")"
  info "Size:  $(du -h "$so_path" | cut -f1)"
  info "Mtime: $(stat -c '%y' "$so_path" | cut -d. -f1)"
else
  fail "Shared library not found (expected $so_path)"
  echo ""
  echo "Files under Contents/:"; find "$contents" -type f 2>/dev/null | sed 's/^/    /'
  exit 1
fi

# ---------------------------------------------------------------------------
# 2. ELF sanity
# ---------------------------------------------------------------------------
printf "\n${BOLD}[2] Binary${RST}\n"

file_out=$(file "$so_path" 2>&1)
echo "$file_out" | grep -q "ELF 64-bit LSB shared object" \
  && pass "ELF 64-bit shared object" \
  || fail "Unexpected file type: $file_out"

# ---------------------------------------------------------------------------
# 3. Shared library dependencies
# ---------------------------------------------------------------------------
printf "\n${BOLD}[3] Shared library dependencies${RST}\n"

missing=$(ldd "$so_path" 2>&1 | grep "not found" || true)
if [[ -z "$missing" ]]; then
  pass "All dependencies satisfied"
else
  while IFS= read -r line; do fail "Missing dep: $line"; done <<< "$missing"
fi

# ---------------------------------------------------------------------------
# 4. Required VST3 entry-point symbols
# ---------------------------------------------------------------------------
printf "\n${BOLD}[4] VST3 entry-point symbols${RST}\n"

all_syms=$(nm -D "$so_path" 2>/dev/null || true)
for sym in GetPluginFactory ModuleEntry ModuleExit; do
  # Match the symbol name as a whole word on a line with type T (text/code)
  if echo "$all_syms" | grep -qw "T $sym"; then
    pass "$sym exported"
  else
    fail "$sym NOT exported — host will not load this plugin"
  fi
done

# ---------------------------------------------------------------------------
# 5. DistrhoPluginInfo.h (source tree)
# ---------------------------------------------------------------------------
printf "\n${BOLD}[5] Source DistrhoPluginInfo.h${RST}\n"

info_h=$(find "$repo_root/plugins/${bare}" "$repo_root/plugins/${bare//_/-}" \
         -name "DistrhoPluginInfo.h" 2>/dev/null | head -1 || true)

if [[ -n "$info_h" ]]; then
  pass "Found: $info_h"

  get_def() { awk "/#define $1[ \t]/{print \$NF}" "$info_h" 2>/dev/null | tr -d '"' | head -1; }

  num_inputs=$(get_def DISTRHO_PLUGIN_NUM_INPUTS)
  num_outputs=$(get_def DISTRHO_PLUGIN_NUM_OUTPUTS)
  has_synth=$(get_def DISTRHO_PLUGIN_IS_SYNTH)
  has_ui=$(get_def DISTRHO_PLUGIN_HAS_UI)
  want_midi_in=$(get_def DISTRHO_PLUGIN_WANT_MIDI_INPUT)
  want_midi_out=$(get_def DISTRHO_PLUGIN_WANT_MIDI_OUTPUT)
  vst3_cat=$(get_def DISTRHO_PLUGIN_VST3_CATEGORIES)
  unique_id=$(get_def DISTRHO_PLUGIN_UNIQUE_ID)
  brand_id=$(get_def DISTRHO_PLUGIN_BRAND_ID)
  plugin_name_def=$(get_def DISTRHO_PLUGIN_NAME)

  info "PLUGIN_NAME:        ${plugin_name_def:-<not set>}"
  info "UNIQUE_ID:          ${unique_id:-<not set>}"
  info "BRAND_ID:           ${brand_id:-<not set>}"
  info "NUM_INPUTS:         ${num_inputs:-<not set>}"
  info "NUM_OUTPUTS:        ${num_outputs:-<not set>}"
  info "IS_SYNTH:           ${has_synth:-<not set>}"
  info "HAS_UI:             ${has_ui:-<not set>}"
  info "WANT_MIDI_INPUT:    ${want_midi_in:-<not set>}"
  info "WANT_MIDI_OUTPUT:   ${want_midi_out:-<not set>}"
  info "VST3_CATEGORIES:    ${vst3_cat:-<not set, defaults to Fx>}"

  # Validation rules
  if [[ "${num_inputs:-1}" == "0" && "${has_synth:-0}" != "1" ]]; then
    fail "NUM_INPUTS=0 but IS_SYNTH not set — Reaper will not register the plugin"
  elif [[ "${num_inputs:-1}" == "0" && "${has_synth:-0}" == "1" ]]; then
    pass "IS_SYNTH=1 (required for zero-input generators)"
  fi

  if [[ -z "${vst3_cat:-}" ]]; then
    warn "VST3_CATEGORIES not set — DPF defaults to \"Fx\" or \"Instrument\" based on IS_SYNTH"
  else
    # Check it's a known valid prefix
    if echo "$vst3_cat" | grep -qE "^(Fx|Instrument|Synth)"; then
      pass "VST3_CATEGORIES starts with a valid prefix"
    else
      fail "VST3_CATEGORIES \"$vst3_cat\" does not start with Fx/Instrument/Synth — may be rejected by hosts"
    fi
  fi

  if [[ -z "${unique_id:-}" ]]; then
    fail "DISTRHO_PLUGIN_UNIQUE_ID not set"
  fi

  # Check getUniqueId() matches the macro
  plugin_cpp=$(find "$repo_root/plugins/${bare}" "$repo_root/plugins/${bare//_/-}" \
               -name "*Plugin.cpp" -path "*/dpf/*" 2>/dev/null | head -1 || true)
  if [[ -n "$plugin_cpp" && -n "${unique_id:-}" ]]; then
    uid_chars=$(echo "$unique_id" | fold -w1 | paste -s -d,)
    if grep -q "d_cconst($(echo "$uid_chars" | sed "s/\(.\)/'\1'/g"))" "$plugin_cpp" 2>/dev/null; then
      pass "getUniqueId() matches DISTRHO_PLUGIN_UNIQUE_ID"
    else
      # Looser check: just make sure the chars appear
      if grep -q "getUniqueId" "$plugin_cpp"; then
        warn "Could not verify getUniqueId() matches UNIQUE_ID macro (check manually)"
      fi
    fi
  fi
else
  warn "DistrhoPluginInfo.h not found in plugins/${bare}/ (skipping source checks)"
fi

# ---------------------------------------------------------------------------
# 6. Metadata embedded in binary
# ---------------------------------------------------------------------------
printf "\n${BOLD}[6] Binary metadata${RST}\n"

bin_strings=$(strings "$so_path" 2>/dev/null)

bin_cat=$(echo "$bin_strings"  | grep -m1 -E "^(Fx|Instrument|Synth)\|" || \
          echo "$bin_strings"  | grep -m1 -E "^(Fx|Instrument|Synth)$"  || true)
bin_ver=$(echo "$bin_strings"  | grep -m1 -E "^v[0-9]+\.[0-9]+\.[0-9]+" || true)
bin_desc=$(echo "$bin_strings" | grep -m1 -E ".{25,}" | \
           grep -iE "(generator|synth|effect|mixer|bass|drum|water|bubble|stream|ocean|instrument)" || true)

[[ -n "$bin_cat"  ]] && info "Category (binary):    $bin_cat"  || warn "No VST3 category string found in binary"
[[ -n "$bin_ver"  ]] && info "Version (binary):     $bin_ver"  || info "Version string:       <not found>"
[[ -n "$bin_desc" ]] && info "Description (binary): $bin_desc"

# Check IS_SYNTH was compiled in (DPF emits "Instrument" in categories when IS_SYNTH=1)
if [[ -n "$bin_cat" ]]; then
  pass "Category string present in binary: $bin_cat"
else
  fail "Category string missing from binary — IS_SYNTH or VST3_CATEGORIES may not have been compiled in"
fi

# ---------------------------------------------------------------------------
# 7. Reaper VST cache
# ---------------------------------------------------------------------------
printf "\n${BOLD}[7] Reaper VST cache${RST}\n"

reaper_ini="${HOME}/.config/REAPER/reaper-vstplugins64.ini"
bundle_basename="$(basename "$bundle")"

if [[ ! -f "$reaper_ini" ]]; then
  warn "Reaper cache file not found: $reaper_ini"
else
  cache_line=$(grep -i "^${bundle_basename}=" "$reaper_ini" 2>/dev/null || true)

  if [[ -z "$cache_line" ]]; then
    warn "Not in Reaper cache — plugin needs a VST rescan to appear"
  elif echo "$cache_line" | grep -qE "=.+,.+,.+"; then
    pass "Reaper cache entry looks valid (plugin was scanned successfully)"
    info "$cache_line"
  else
    fail "Stale/failed Reaper cache entry — plugin was scanned but produced no registered classes"
    info "Entry:  $cache_line"
    info "Cause:  Usually means IS_SYNTH was missing or the plugin crashed on load"
    if [[ $DO_FIX -eq 1 ]]; then
      # Remove the stale entry so Reaper rescans on next startup
      sed -i "/^${bundle_basename}=/d" "$reaper_ini"
      action "Removed stale cache entry from $reaper_ini"
      # Touch the .so so Reaper detects a change even if the hash matches
      touch "$so_path"
      action "Touched $so_path to force rescan detection"
      info "Now rescan VST3 in Reaper: Options → Preferences → Plug-ins → VST → Re-scan"
    else
      info "Run with --fix to remove the stale entry automatically:"
      info "  $(basename "$0") --fix $bare"
    fi
  fi
fi

# ---------------------------------------------------------------------------
# 8. Profile.ttl
# ---------------------------------------------------------------------------
printf "\n${BOLD}[8] profile.ttl${RST}\n"

ttl=$(find "$repo_root/plugins/${bare}" "$repo_root/plugins/${bare//_/-}" \
      -name "profile.ttl" 2>/dev/null | head -1 || true)
if [[ -n "$ttl" ]]; then
  pass "profile.ttl found"
  bundle_name=$(grep -oP '(?<=trn:bundleName\s")[^"]+' "$ttl" || true)
  role=$(grep -oP '(?<=trn:role\s)trn:\S+' "$ttl" | head -1 || true)
  [[ -n "$bundle_name" ]] && info "bundleName: $bundle_name"
  [[ -n "$role"        ]] && info "role:       $role"
  if [[ -n "$bundle_name" && "$bundle_name" != "$bundle_basename" ]]; then
    warn "profile.ttl bundleName ($bundle_name) != installed bundle name ($bundle_basename)"
  fi
else
  warn "profile.ttl not found in plugins/${bare}/"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
printf "\n${BOLD}=== Summary ===${RST}\n"
if [[ $FAILURES -eq 0 ]]; then
  printf "${GRN}${BOLD}All checks passed.${RST}\n\n"
else
  printf "${RED}${BOLD}%d check(s) failed.${RST}\n\n" "$FAILURES"
  exit 1
fi
