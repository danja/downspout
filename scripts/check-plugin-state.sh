#!/usr/bin/env bash
#
# check-plugin-state.sh -- audit session-state support across all plugins.
#
# Three things have to line up for a plugin's settings to survive a host session:
#
#   1. DISTRHO_PLUGIN_WANT_STATE       -- enables initState() and setState()
#   2. DISTRHO_PLUGIN_WANT_FULL_STATE  -- enables getState()
#   3. all three callbacks implemented in the DPF wrapper
#
# Getting (1) without (2) is the dangerous case and it fails silently. DPF's VST3
# wrapper seeds its state map from initState's defaultValue
# (DistrhoPluginVST3.cpp:662) and only refreshes it from the plugin inside
# `#if DISTRHO_PLUGIN_WANT_FULL_STATE` (DistrhoPluginVST3.cpp:1185). A plugin in
# that state looks like it saves -- it has states, and the host writes a state
# block -- but every save records the plugin's *defaults*, so reopening a project
# silently discards the user's settings.
#
# Nothing in the build or the core test suites can detect this: the suites test
# the portable core, never the wrapper, and the plugin compiles cleanly.
#
# "Absent" plugins are not necessarily losing anything: DPF's VST3 getState saves
# every non-output, non-trigger parameter by symbol (DistrhoPluginVST3.cpp:1169), so a
# plugin whose settings are all parameters persists them with no WANT_STATE.
#
# Usage:
#   scripts/check-plugin-state.sh            # report, exit 1 if any plugin is broken
#   scripts/check-plugin-state.sh --report   # report, exit 0 (informational)
#   scripts/check-plugin-state.sh --list-missing
#
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

mode="${1:-check}"

ok_count=0
broken_count=0
absent_count=0
broken=()
absent=()
inert=()

for info in plugins/*/src/dpf/DistrhoPluginInfo.h; do
  plugin="$(basename "$(dirname "$(dirname "$(dirname "$info")")")")"
  header="$(cat "$info")"
  wrapper="$(ls plugins/"$plugin"/src/dpf/*Plugin.cpp 2>/dev/null | head -1 || true)"

  has_state=$(grep -qP 'DISTRHO_PLUGIN_WANT_STATE\s+1' <<<"$header" && echo yes || echo no)
  has_full=$(grep -qP 'DISTRHO_PLUGIN_WANT_FULL_STATE\s+1' <<<"$header" && echo yes || echo no)

  impl_init=no; impl_get=no; impl_set=no; state_count=-1
  if [[ -n "$wrapper" ]]; then
    grep -q 'initState' "$wrapper" && impl_init=yes
    grep -q 'getState'  "$wrapper" && impl_get=yes
    grep -q 'setState'  "$wrapper" && impl_set=yes
    # Plugin(parameterCount, programCount, stateCount) -- third argument.
    # Anchor on the initialiser-list form so a stray "Plugin()" elsewhere in
    # the file cannot be mistaken for the constructor.
    #
    # Known limitation: a symbolic stateCount (plank uses kStateCount) contains
    # no digits, so state_count stays -1 and the stateCount==0 branch is skipped.
    # That errs towards reporting a problem, never towards hiding one.
    count="$(grep -oP ':\s*Plugin\([^)]*\)' "$wrapper" | head -1 \
             | awk -F',' '{gsub(/[^0-9]/,"",$3); print $3}')"
    [[ -n "$count" ]] && state_count="$count"
  fi

  # With stateCount 0 the WANT_STATE macros are inert: there are no states, so
  # nothing is written and nothing is lost. Untidy, not broken.
  if [[ "$state_count" == "0" ]]; then
    absent_count=$((absent_count + 1))
    absent+=("$plugin")
    if [[ "$has_state" == yes || "$has_full" == yes ]]; then
      inert+=("$plugin (declares the state macros but stateCount is 0)")
    fi
    continue
  fi

  if [[ "$has_state" == no && "$has_full" == no ]]; then
    absent_count=$((absent_count + 1))
    absent+=("$plugin")
    continue
  fi

  problem=""
  [[ "$has_state" == yes && "$has_full" == no ]] && problem="WANT_STATE without WANT_FULL_STATE (writes defaults)"
  [[ "$impl_init" == no && "$has_state" == yes ]] && problem="WANT_STATE set but initState() not implemented"
  [[ "$impl_get"  == no && "$has_full"  == yes ]] && problem="WANT_FULL_STATE set but getState() not implemented"
  [[ "$impl_set"  == no && "$has_state" == yes ]] && problem="WANT_STATE set but setState() not implemented"

  if [[ -n "$problem" ]]; then
    broken_count=$((broken_count + 1))
    broken+=("$plugin: $problem")
  else
    ok_count=$((ok_count + 1))
  fi
done

total=$((ok_count + broken_count + absent_count))
printf "Session state audit over %d plugins\n" "$total"
printf "  correct (saves and restores) : %d\n" "$ok_count"
printf "  BROKEN (silently writes defaults) : %d\n" "$broken_count"
printf "  parameters only (no extra state)  : %d\n\n" "$absent_count"

if [[ ${#broken[@]} -gt 0 ]]; then
  echo "BROKEN -- these write their defaults into every host project:"
  printf '  %s\n' "${broken[@]}"
  echo
fi

if [[ ${#absent[@]} -gt 0 ]]; then
  echo "PARAMETERS ONLY -- no WANT_STATE. Not data loss: DPF saves every non-output,"
  echo "non-trigger parameter itself. Only data held outside parameters (patterns, paths,"
  echo "text) would need real state. Confirm in a host that these reopen intact:"
  printf '  %s\n' "${absent[@]}" | fold -s -w 78 | sed 's/^/  /'
  echo
fi

if [[ ${#inert[@]} -gt 0 ]]; then
  echo "INERT MACROS (harmless, but misleading -- tidy these up):"
  printf '  %s\n' "${inert[@]}"
  echo
fi

if [[ "$mode" == "--list-missing" ]]; then
  printf '%s\n' "${absent[@]}"
  exit 0
fi

if [[ $broken_count -gt 0 && "$mode" != "--report" ]]; then
  echo "$broken_count plugin(s) have broken session state." >&2
  exit 1
fi

echo "No plugin has broken session state."
