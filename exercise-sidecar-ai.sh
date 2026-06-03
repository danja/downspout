#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${DOWNSPOUT_BUILD_DIR:-$repo_root/build}"
state_file="${DOWNSPOUT_AI_STATE:-$repo_root/tools/ai-coordinator/examples/state.json}"
response_file="${DOWNSPOUT_AI_RESPONSE:-$repo_root/tools/ai-coordinator/examples/response.json}"
out_dir="${DOWNSPOUT_AI_OUT_DIR:-/tmp/downspout-sidecar-exercise}"
build_type="${CMAKE_BUILD_TYPE:-Release}"

coordinator="$build_dir/tools/ai-coordinator/downspout-ai-coordinator"
solo_midi="$out_dir/sidecar-solo.mid"
phrase_txt="$out_dir/sidecar-phrase.txt"
derived_midi="$out_dir/sidecar-derived-from-midi.mid"
derived_phrase_txt="$out_dir/sidecar-derived-from-midi-phrase.txt"
analyzed_state="$out_dir/sidecar-analyzed-state.json"
request_json="$out_dir/sidecar-request.json"
midi_request_json="$out_dir/sidecar-midi-request.json"
response_midi="$out_dir/sidecar-response-render.mid"
response_phrase_txt="$out_dir/sidecar-response-render-phrase.txt"
openai_midi="$out_dir/sidecar-openai.mid"
openai_phrase_txt="$out_dir/sidecar-openai-phrase.txt"
openai_raw_json="$out_dir/sidecar-openai-raw.json"
openai_from_midi_midi="$out_dir/sidecar-openai-from-midi.mid"
openai_from_midi_phrase_txt="$out_dir/sidecar-openai-from-midi-phrase.txt"
openai_from_midi_raw_json="$out_dir/sidecar-openai-from-midi-raw.json"

if [[ ! -f "$state_file" ]]; then
  echo "Missing state file: $state_file" >&2
  exit 1
fi

if [[ ! -f "$response_file" ]]; then
  echo "Missing response file: $response_file" >&2
  exit 1
fi

mkdir -p "$out_dir"

echo "Configuring downspout"
cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type"

echo "Building Sidecar AI coordinator"
cmake --build "$build_dir" --target downspout-ai-coordinator downspout_ai_coordinator_tests

if cmake --build "$build_dir" --target help | grep -q "sidecar-vst3"; then
  echo "Building Sidecar VST3"
  cmake --build "$build_dir" --target sidecar-vst3
fi

echo "Running Sidecar/coordinator tests"
ctest --test-dir "$build_dir" -R "downspout_(ai_coordinator|sidecar)_.*tests" --output-on-failure

echo "Checking coordinator startup"
if ! "$coordinator" health; then
  echo "OpenAI key is not configured; live /openai and DOWNSPOUT_RUN_OPENAI=1 will fail until .env or OPENAI_API_KEY is set."
fi

echo "Generating sample solo MIDI"
"$coordinator" generate "$state_file" --out "$solo_midi" --phrase "$phrase_txt"

echo "Generating derived solo from sample MIDI"
"$coordinator" generate-from-midi "$solo_midi" --out "$derived_midi" --phrase "$derived_phrase_txt"

echo "Analyzing sample MIDI to state JSON"
"$coordinator" analyze-midi "$solo_midi" --out "$analyzed_state"

echo "Building request JSON from state"
"$coordinator" build-request "$state_file" --out "$request_json"

echo "Building request JSON from sample MIDI"
"$coordinator" build-request-from-midi "$solo_midi" --out "$midi_request_json"

echo "Rendering validated response JSON"
"$coordinator" render-response "$response_file" --out "$response_midi" --phrase "$response_phrase_txt"

if [[ "${DOWNSPOUT_RUN_OPENAI:-0}" != "0" ]]; then
  echo "Calling OpenAI for a validated phrase"
  "$coordinator" openai "$state_file" --out "$openai_midi" --phrase "$openai_phrase_txt" --raw "$openai_raw_json"

  echo "Calling OpenAI from analyzed MIDI context"
  "$coordinator" openai-from-midi "$solo_midi" --out "$openai_from_midi_midi" --phrase "$openai_from_midi_phrase_txt" --raw "$openai_from_midi_raw_json"
fi

if [[ ! -s "$solo_midi" ]]; then
  echo "Expected non-empty MIDI output: $solo_midi" >&2
  exit 1
fi

if [[ ! -s "$phrase_txt" ]]; then
  echo "Expected non-empty phrase output: $phrase_txt" >&2
  exit 1
fi

if [[ ! -s "$derived_midi" ]]; then
  echo "Expected non-empty MIDI-derived output: $derived_midi" >&2
  exit 1
fi

if [[ ! -s "$analyzed_state" ]]; then
  echo "Expected non-empty analyzed state output: $analyzed_state" >&2
  exit 1
fi

if [[ ! -s "$request_json" ]]; then
  echo "Expected non-empty request output: $request_json" >&2
  exit 1
fi

if [[ ! -s "$midi_request_json" ]]; then
  echo "Expected non-empty MIDI request output: $midi_request_json" >&2
  exit 1
fi

if [[ ! -s "$response_midi" ]]; then
  echo "Expected non-empty response-rendered MIDI output: $response_midi" >&2
  exit 1
fi

if [[ "${DOWNSPOUT_RUN_OPENAI:-0}" != "0" && ! -s "$openai_midi" ]]; then
  echo "Expected non-empty OpenAI MIDI output: $openai_midi" >&2
  exit 1
fi

if [[ "${DOWNSPOUT_RUN_OPENAI:-0}" != "0" && ! -s "$openai_from_midi_midi" ]]; then
  echo "Expected non-empty OpenAI-from-MIDI output: $openai_from_midi_midi" >&2
  exit 1
fi

echo
echo "Generated:"
echo "  MIDI:   $solo_midi"
echo "  Phrase: $phrase_txt"
echo "  MIDI-derived solo:   $derived_midi"
echo "  MIDI-derived phrase: $derived_phrase_txt"
echo "  Analyzed state:      $analyzed_state"
echo "  Request JSON:        $request_json"
echo "  MIDI request JSON:   $midi_request_json"
echo "  Response MIDI:       $response_midi"
echo "  Response phrase:     $response_phrase_txt"
if [[ "${DOWNSPOUT_RUN_OPENAI:-0}" != "0" ]]; then
  echo "  OpenAI MIDI:         $openai_midi"
  echo "  OpenAI phrase:       $openai_phrase_txt"
  echo "  OpenAI raw response: $openai_raw_json"
  echo "  OpenAI from MIDI:    $openai_from_midi_midi"
  echo "  OpenAI MIDI phrase:  $openai_from_midi_phrase_txt"
  echo "  OpenAI MIDI raw:     $openai_from_midi_raw_json"
fi

echo
echo "Phrase preview:"
sed -n '1,12p' "$phrase_txt"

echo
echo "Import the MIDI file into the DAW to audition the generated Sidecar phrase."
echo "Set DOWNSPOUT_RUN_OPENAI=1 to include the API-backed coordinator path."
