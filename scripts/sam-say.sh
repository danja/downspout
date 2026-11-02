#!/usr/bin/env bash
# Talk to a running Sam plugin over its embedded MCP server (loopback, ports 7230-7239).
#
#   sam-say.sh hello world            speak it now (the default; says nothing back on success)
#   echo "hello world" | sam-say.sh   the same, text from stdin
#   sam-say.sh -p "/HEH3LOW2 WER4LD"  read the text as SAM phonemes (-e forces English)
#
# Everything else is a subcommand as the first word (use `--` to speak a text that starts with one):
#   set TEXT...      load the text without speaking it (for Keys/Transport mode to play)
#   text             show the loaded text and how it splits into words
#   status           mode, text, and the word being spoken now
#   check TEXT...    vet words/phonemes: what SAM reads and whether it accepts them (changes nothing)
#   param NAME VALUE set a parameter, e.g. `param speed 90`, `param mode Transport`, `param loop off`
#   params           list every parameter with value, range and options
#   voice NAME       SAM | Elf | Robot | Stuffy | "Old Lady" | Alien
#
# SAM_PORT=7231 pins the port; otherwise the first live Sam from 7230 up is used.
# Needs: curl, jq.

set -euo pipefail

usage() { sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//' >&2; }
die() { echo "sam-say: $*" >&2; exit 1; }

command -v curl >/dev/null || die "curl is required"
command -v jq >/dev/null || die "jq is required"

find_port() {
  if [[ -n "${SAM_PORT:-}" ]]; then echo "$SAM_PORT"; return; fi
  local p
  for p in $(seq 7230 7239); do
    if curl -s --max-time 0.5 -X POST "http://127.0.0.1:$p/mcp" -H 'Content-Type: application/json' \
         -d '{"jsonrpc":"2.0","id":0,"method":"ping"}' 2>/dev/null | grep -q '"result"'; then
      echo "$p"; return
    fi
  done
  die "no Sam answering on ports 7230-7239 (is the plugin loaded and active in a host? DOWNSPOUT_SAM_MCP=0 turns the server off)"
}

# call TOOL ARGS_JSON [quiet]: prints the tool's reply unless quiet; non-zero exit on any error.
call() {
  local tool=$1 args=$2 quiet=${3:-} port body reply
  port=$(find_port)
  body=$(jq -cn --arg name "$tool" --argjson args "$args" \
    '{jsonrpc:"2.0",id:1,method:"tools/call",params:{name:$name,arguments:$args}}')
  reply=$(curl -s --max-time 10 -X POST "http://127.0.0.1:$port/mcp" -H 'Content-Type: application/json' -d "$body") \
    || die "request to port $port failed"
  if [[ $(jq -r '.error != null' <<<"$reply") == true ]]; then
    die "$(jq -r '.error.message' <<<"$reply")"
  fi
  if [[ $(jq -r '.result.isError // false' <<<"$reply") == true ]]; then
    die "$(jq -r '.result.content[0].text' <<<"$reply")"
  fi
  [[ -n $quiet ]] || jq -r '.result.content[0].text' <<<"$reply"
}

phonetic=""   # "", true or false: left alone unless -p / -e is given

# text_args TOOL [quiet]: text from the remaining words, or stdin when there are none.
text_call() {
  local tool=$1 quiet=${2:-}; shift 2 || true
  local text
  if [[ $# -gt 0 ]]; then text="$*"; elif [[ ! -t 0 ]]; then text=$(cat); else usage; exit 2; fi
  local args
  args=$(jq -cn --arg t "$text" --arg p "$phonetic" \
    '{text:$t} + (if $p == "" then {} else {phonetic: ($p == "true")} end)')
  call "$tool" "$args" "$quiet"
}

while [[ ${1:-} == -* && ${1:-} != -- ]]; do
  case $1 in
    -p) phonetic=true ;;
    -e) phonetic=false ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option $1 (try --help)" ;;
  esac
  shift
done

cmd=${1:-}
case $cmd in
  set)    shift; text_call set_text quiet "$@" ;;
  check)  shift; text_call check_text "" "$@" ;;
  text)   call get_text '{}' ;;
  status) call get_status '{}' ;;
  params) call get_parameters '{}' ;;
  param)
    [[ $# -eq 3 ]] || die "usage: param NAME VALUE"
    # A number, true/false, or an option label such as Transport or 1/8.
    val=$(jq -cn --arg v "$3" '$v | (try fromjson catch $v)')
    call set_parameter "$(jq -cn --arg n "$2" --argjson v "$val" '{name:$n,value:$v}')" |
      jq -r '"\(.symbol) = \(.label // .value)"' ;;
  voice)
    [[ $# -ge 2 ]] || die "usage: voice NAME"
    shift; call set_voice "$(jq -cn --arg p "$*" '{preset:$p}')" quiet ;;
  --)     shift; text_call say quiet "$@" ;;
  *)      text_call say quiet "$@" ;;
esac
