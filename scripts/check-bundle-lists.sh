#!/usr/bin/env bash
#
# check-bundle-lists.sh -- verify every hand-maintained bundle list agrees.
#
# Downspout repeats the set of plugin bundles in four places, and until this
# script existed nothing checked that they still matched:
#
#   1. plugins/*/CMakeLists.txt        dpf_add_plugin() + install(DIRECTORY ...)
#   2. scripts/package-release.sh      required_bundles  (hard gate, Linux)
#   3. scripts/package-built-bundles.sh required_bundles (advisory, mac/win)
#   4. .github/workflows/release.yml   the release-notes bundle list
#
# plus a fifth list that is documentation rather than behaviour:
#
#   5. install.sh                      -DDOWNSPOUT_BUILD_* cmake_args
#
# The source of truth is (1). A plugin that exists and installs but is absent
# from (2) ships in a source tarball and is missing from the release zip, and
# the failure mode is a release build that aborts late rather than a test that
# fails early. AGENTS.md has asked for this check as a manual checklist step on
# every new plugin; this script makes it automatic.
#
# Usage:
#   scripts/check-bundle-lists.sh            # check, exit non-zero on drift
#   scripts/check-bundle-lists.sh --list     # print the authoritative set
#
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

failures=0

fail() {
  echo "FAIL  $1" >&2
  failures=$((failures + 1))
}

ok() {
  echo "ok    $1"
}

# ── 1. Authoritative set, from the plugin CMakeLists ────────────────────────
#
# A plugin counts as shipping when its CMakeLists declares an install rule for
# a .vst3 bundle. dpf_add_plugin() gives the DPF target name, which must be the
# same token, otherwise DPF would build one bundle and install() another.

declare -A installed=()
declare -A dpf_target=()
declare -A install_rule=()

for cm in plugins/*/CMakeLists.txt; do
  slug="$(basename "$(dirname "$cm")")"
  target="$(grep -oP 'dpf_add_plugin\(\K[A-Za-z0-9_]+' "$cm" | head -1 || true)"
  rule="$(grep -oP 'install\(DIRECTORY[^)]*?bin/\K[A-Za-z0-9_]+(?=\.vst3)' "$cm" | head -1 || true)"

  [[ -z "$target" && -z "$rule" ]] && continue

  dpf_target["$slug"]="$target"
  install_rule["$slug"]="$rule"

  if [[ -n "$rule" ]]; then
    installed["$rule.vst3"]=1
  fi
done

# dpf_add_plugin name vs install rule
for slug in "${!dpf_target[@]}"; do
  target="${dpf_target[$slug]}"
  rule="${install_rule[$slug]:-}"
  if [[ -n "$rule" && "$target" != "$rule" ]]; then
    fail "$slug: dpf_add_plugin($target) does not match its install rule (${rule}.vst3)"
  fi
done
[[ $failures -eq 0 ]] && ok "every dpf_add_plugin name matches its install rule"

authoritative="$(printf '%s\n' "${!installed[@]}" | sort)"

if [[ "${1:-}" == "--list" ]]; then
  echo "$authoritative"
  exit 0
fi

echo "Authoritative set: $(echo "$authoritative" | wc -l | tr -d ' ') bundles from $(echo "${!installed[@]}" >/dev/null; ls -d plugins/*/CMakeLists.txt | wc -l | tr -d ' ') plugin CMakeLists files"
echo

# ── Helpers to read a bash array literal out of a script ───────────────────
#
# These lists are conditional: package-release.sh has a base required_bundles
# and a second one for sidecar_build=ON. Both are real gates on different
# configurations, so they are checked as separate variants rather than merged.
# Merging them would hide a bundle dropped from the base list but still present
# in the sidecar one.

# array_variants <file> <array-name> -> one variant per line, prefixed "V<n>: "
array_variants() {
  python3 - "$1" "$2" <<'PY'
import re, sys
path, name = sys.argv[1], sys.argv[2]
text = open(path).read()
for i, block in enumerate(re.findall(rf'{name}=\((.*?)\n?\)', text, re.S)):
    entries, seen = [], set()
    for e in re.findall(r'[A-Za-z0-9_]+\.vst3', block):
        if e not in seen:
            seen.add(e)
            entries.append(e)
    print(f"V{i}:" + ",".join(sorted(entries)))
PY
}

strip_variant() { echo "${1#*:}" | tr ',' '\n' | grep -v '^$'; }

# compare_variant <label> <variant-line>
compare_variant() {
  local label="$1" variant="$2"
  local listed missing stale
  listed="$(strip_variant "$variant")"

  # comm -23: in the authoritative set but not in this list -> the list is behind.
  # comm -13: in this list but not in the authoritative set -> the list is stale.
  missing="$(comm -23 <(echo "$authoritative") <(echo "$listed" | sort) | tr '\n' ' ')"
  stale="$(comm -13 <(echo "$authoritative") <(echo "$listed" | sort) | tr '\n' ' ')"
  missing="${missing% }"
  stale="${stale% }"

  if [[ -n "$missing" ]]; then
    fail "$label: missing bundles that plugins install: $missing"
  fi
  if [[ -n "$stale" ]]; then
    fail "$label: lists bundles no plugin installs: $stale"
  fi
  [[ -z "$missing" && -z "$stale" ]] && \
    ok "$label in step ($(echo "$listed" | wc -l | tr -d ' ') bundles)"
}

# Bundles that are legitimately absent from the base list and only required when
# their own build flag is on. sidecar builds only under sidecar_build=ON, so
# demanding it unconditionally would fail every default packaging run.
#
# This is the one place the authoritative CMake-derived set has to be adjusted
# by hand, because the conditionality lives in the packaging scripts and not in
# any CMakeLists: sidecar's install rule is guarded by the same
# DOWNSPOUT_ENABLE_DPF block as every other plugin. The union check below keeps
# this list honest -- if a name here is not installed by any plugin, or is
# missing from every variant, the check fails.
CONDITIONAL_FROM_BASE=(sidecar.vst3)

check_variants() {
  local label="$1" file="$2" name="$3"
  local variants
  variants="$(array_variants "$file" "$name")"
  if [[ -z "$variants" ]]; then
    fail "$label: no '$name=(...)' array found in $file"
    return
  fi

  local allowed
  allowed="$(printf '%s\n' "${CONDITIONAL_FROM_BASE[@]}" | sort)"

  # Every conditional bundle must appear in at least one variant, otherwise it
  # is named nowhere and the exception list has gone stale.
  local union
  union="$(printf '%s\n' "${variants[@]}" | grep '^V' | sed 's/^V[0-9]*://' | tr ',' '\n' | sort -u)"
  local never
  never="$(comm -23 <(echo "$allowed") <(echo "$union") | tr '\n' ' ')"
  never="${never% }"
  if [[ -n "$never" ]]; then
    fail "$label: CONDITIONAL_FROM_BASE names bundles that appear in no variant: $never"
  fi
  local not_conditional
  not_conditional="$(comm -23 <(echo "$allowed") <(echo "$authoritative") | tr '\n' ' ')"
  not_conditional="${not_conditional% }"
  if [[ -n "$not_conditional" ]]; then
    fail "$label: CONDITIONAL_FROM_BASE names bundles no plugin installs: $not_conditional"
  fi

  local n=0
  while read -r line; do
    [[ -z "$line" ]] && continue
    local listed missing stale unexpected
    listed="$(strip_variant "$line" | sort)"
    missing="$(comm -23 <(echo "$authoritative") <(echo "$listed") | tr '\n' ' ')"
    stale="$(comm -13 <(echo "$authoritative") <(echo "$listed") | tr '\n' ' ')"
    missing="${missing% }"
    stale="${stale% }"

    # Only conditionally-present bundles may be absent from any one variant.
    unexpected="$(printf '%s\n' $missing | sort | comm -23 - <(echo "$allowed") | tr '\n' ' ')"
    unexpected="${unexpected% }"

    if [[ -n "$unexpected" ]]; then
      fail "$label [variant $n]: missing bundles that plugins install: $unexpected"
    fi
    if [[ -n "$stale" ]]; then
      fail "$label [variant $n]: lists bundles no plugin installs: $stale"
    fi
    if [[ -z "$unexpected" && -z "$stale" ]]; then
      ok "$label [variant $n] in step ($(echo "$listed" | wc -l | tr -d ' ') bundles)"
    fi
    n=$((n + 1))
  done <<< "$variants"
}

# ── 2. scripts/package-release.sh (hard gate) ──────────────────────────────
check_variants "scripts/package-release.sh" scripts/package-release.sh required_bundles

# ── 3. scripts/package-built-bundles.sh (advisory) ─────────────────────────
check_variants "scripts/package-built-bundles.sh" scripts/package-built-bundles.sh required_bundles

# ── 4. .github/workflows/release.yml notes list ────────────────────────────
notes="$(grep -oP 'echo "- \K[A-Za-z0-9_]+(?=\.vst3")' .github/workflows/release.yml | sed 's/$/.vst3/' | sort)"
compare_variant ".github/workflows/release.yml bundle list" "V0:$(echo "$notes" | tr '\n' ',' | sed 's/,$//')"

# ── 5. install.sh build options ────────────────────────────────────────────
#
# Every option defaults to ON, so a missing entry is documentation drift rather
# than a behaviour change. Duplicates are still worth reporting.

cmake_options="$(grep -oP 'option\(\KDOWNSPOUT_BUILD_[A-Z0-9_]+' CMakeLists.txt | sort -u)"
# DOWNSPOUT_BUILD_SCREENSHOT_APPS is deliberately absent: it turns on the jack
# standalone targets for documentation captures, which install.sh must not build.
cmake_options="$(echo "$cmake_options" | grep -v '^DOWNSPOUT_BUILD_SCREENSHOT_APPS$' || true)"
install_opts="$(grep -oP '\-D\KDOWNSPOUT_BUILD_[A-Z0-9_]+(?==)' install.sh | sort -u)"

missing_opts="$(comm -23 <(echo "$cmake_options") <(echo "$install_opts") | tr '\n' ' ')"
missing_opts="${missing_opts% }"
if [[ -n "$missing_opts" ]]; then
  fail "install.sh cmake_args: missing build options: $missing_opts"
else
  ok "install.sh cmake_args lists every build option"
fi

dupes="$(grep -oP '\-D\KDOWNSPOUT_BUILD_[A-Z0-9_]+(?==)' install.sh | sort | uniq -d | tr '\n' ' ')"
dupes="${dupes% }"
if [[ -n "$dupes" ]]; then
  fail "install.sh cmake_args: duplicated build options: $dupes"
else
  ok "install.sh cmake_args has no duplicated build options"
fi

# ── Result ─────────────────────────────────────────────────────────────────

echo
if [[ $failures -gt 0 ]]; then
  echo "$failures check(s) failed." >&2
  exit 1
fi
echo "All bundle lists are in step."
