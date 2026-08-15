# Campione — Design & Implementation Plan

## Overview

Campione is a new Downspout VST3 sampler plugin designed for ease of use in both
per-note drum-kit and full-keyboard melodic contexts. It is intended for an
experimental environment, so the design is expected to iterate.

Core features:
- Multi-zone sample loading (WAV files) with per-note MIDI mapping and channel select
- Looping with crossfading and zero-crossing snap for click-free indefinite loops
- Direct recording into the plugin; recordings saved as WAV files beside the DAW project
- Auto-map recorded notes to keyboard by detected pitch
- Gap-filling: pitch-shift nearest loaded zone to cover unmapped notes
- Full preset serialization (all parameters and zone state saved between sessions)

Design decisions:
- 8-voice polyphony
- Linear interpolation for pitch-shifted playback
- Recordings saved as WAV to `campione_recordings/` beside the DAW project; paths stored in state

Follows the established Downspout pattern: portable core → thin DPF wrapper → NanoVG UI.
Reference shapes: Rift (WAV loading, atomic thread safety), Orchid (MIDI→playback rate,
zero-crossing), DrumKit (MIDI dispatch), p-mix (serialization format).

---

## Repository Layout

```
plugins/campione/
├── include/
│   ├── campione_params.hpp          # ParameterIndex, StateIndex, state key constants
│   ├── campione_core_types.hpp      # SampleZone, Voice, Parameters, EngineState
│   ├── campione_engine.hpp          # processBlock(), activate(), clampParameters()
│   ├── campione_serialization.hpp   # serialize/deserialize Parameters + zone list
│   ├── campione_sample_loader.hpp   # loadWavZone(), saveWavZone()
│   └── campione_pitch_utils.hpp     # snapToZeroCrossing(), findLoopStart(), computePlaybackRate()
├── src/
│   ├── campione_engine.cpp
│   ├── campione_serialization.cpp
│   ├── campione_sample_loader.cpp
│   ├── campione_pitch_utils.cpp
│   └── dpf/
│       ├── CampionePlugin.cpp
│       └── CampioneUI.cpp
├── tests/
│   └── campione_core_tests.cpp
├── docs/
│   └── porting-notes.md
├── CMakeLists.txt
└── profile.ttl
```

---

## Core Types (`campione_core_types.hpp`)

```cpp
namespace downspout::campione {

inline constexpr int kMaxVoices = 8;
inline constexpr int kMaxZones  = 128;

struct SampleZone {
    int rootNote = 60;          // MIDI note at which sample plays at original speed
    int rangeLow = 0;           // lowest MIDI note this zone answers (inclusive)
    int rangeHigh = 127;        // highest MIDI note (inclusive)
    int midiChannel = 0;        // 0 = all channels, 1–16 = specific channel
    std::vector<float> data;    // interleaved float PCM (mono or stereo)
    int channelCount = 1;
    double sampleRate = 44100.0;
    bool loopEnabled = false;
    uint32_t loopStart = 0;     // frame index (after zero-crossing snap)
    uint32_t loopEnd = 0;       // frame index (after zero-crossing snap)
    uint32_t crossfadeFrames = 0; // pre-computed from crossfadeDurationMs
    std::string sourcePath;     // file path (loaded) or recorded WAV path
};

struct Voice {
    bool active = false;
    int zoneIndex = -1;
    int midiNote = -1;
    int midiChannel = 0;
    float velocity = 1.0f;
    double position = 0.0;        // playback position in source frames (fractional)
    bool inCrossfade = false;
    double crossfadePosition = 0.0; // position in loopStart region for xfade blend
};

struct Parameters {
    float masterVolume = 0.8f;
    float midiChannel = 0.0f;       // 0 = all, 1–16 = specific
    float crossfadeDurationMs = 20.0f;
    float pitchBendRange = 2.0f;    // semitones
};

struct EngineState {
    std::array<Voice, kMaxVoices> voices {};
    bool recording = false;
    std::vector<float> recordBuffer;
    double recordSampleRate = 44100.0;
    int recordChannelCount = 1;
};

struct MidiInputEvent {
    uint32_t frame = 0;
    uint8_t size = 0;
    uint8_t data[4] {};
};

struct AudioBlock {
    const float* inputs[2] {};
    float* outputs[2] {};
    int channelCount = 2;
};

} // namespace downspout::campione
```

---

## Parameters (`campione_params.hpp`)

```cpp
enum ParameterIndex : uint32_t {
    kParamMasterVolume = 0,
    kParamMidiChannel,
    kParamCrossfadeDuration,
    kParamPitchBendRange,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateZones,
    kStateCount
};

inline constexpr const char* kStateKeyParameters = "parameters";
inline constexpr const char* kStateKeyZones      = "zones";
```

---

## Engine (`campione_engine.hpp / .cpp`)

### Zone lookup

```
findZone(zones, midiNote, midiChannel) → nearest matching zone index
  1. Prefer exact range match on [rangeLow, rangeHigh] AND channel match
  2. If none, find zone with nearest rootNote (gap-filling): ignore range, pick closest pitch
  3. Return -1 if zones is empty
```

### Pitch-shifted playback rate

```
computePlaybackRate(zone, midiNote, hostSampleRate) → double
  semitones = midiNote - zone.rootNote
  rate = pow(2.0, semitones / 12.0) * (zone.sampleRate / hostSampleRate)
```

Pattern from `plugins/orchid/src/orchid_engine.cpp:computePlaybackRate()`.

### Voice allocation

```
allocateVoice(voices, midiNote, midiChannel) → voice index
  Steal oldest active voice on same note+channel, else steal oldest overall
```

### processBlock()

```cpp
void processBlock(EngineState& state, const Parameters& params,
                  const std::vector<SampleZone>& zones,
                  const MidiInputEvent* midiEvents, uint32_t midiCount,
                  uint32_t frames, double hostSampleRate, AudioBlock audio);
```

Per-frame loop:
1. Dispatch MIDI events at correct frame offsets
2. note-on: `findZone()`, `allocateVoice()`, position=0, compute rate
3. note-off: deactivate matching voice
4. Each active voice: linear-interpolate zone.data at fractional position, accumulate
   to output, advance position by playbackRate
5. Loop: when position ≥ loopEnd wrap to loopStart; apply crossfade blend when
   position ≥ (loopEnd - crossfadeFrames)
6. One-shot (loopEnabled=false): deactivate when position ≥ zone.data.size()
7. Apply masterVolume × velocity

---

## Zero-Crossing Snap (`campione_pitch_utils.hpp / .cpp`)

Based on the stencil algorithm in `docs/zero-crossing-algorithm.md`.
Implemented in Orchid (`orchid_engine.cpp:loopJoinCost`) and Mosaic
(`mosaic_core.cpp:snapToZeroCrossing`).

```cpp
// Snap frame to nearest zero-crossing within windowFrames
uint32_t snapToZeroCrossing(const std::vector<float>& data, int channelCount,
                             uint32_t frame, uint32_t windowFrames, bool preferRising);

// Stencil algorithm: find phase-coherent loop start for given loop end
uint32_t findLoopStart(const std::vector<float>& data, int channelCount,
                       uint32_t loopEnd, uint32_t searchWindowFrames);

// Pitch utilities
double freqToMidi(double freqHz);          // copy from orchid_engine.cpp
double computePlaybackRate(const SampleZone& zone, int midiNote, double hostSampleRate);

// Simple autocorrelation pitch detector (for auto-map after recording)
double detectFundamentalHz(const std::vector<float>& data, double sampleRate);
```

Called at load time: snap loopEnd, then find phase-coherent loopStart.
Pre-compute crossfadeFrames = (crossfadeDurationMs / 1000.0) × zone.sampleRate.

---

## Sample Loader (`campione_sample_loader.hpp / .cpp`)

Ported from `plugins/rift/src/rift_sample_loader.cpp`.
Supports PCM 8/16/24/32-bit and IEEE float32 WAV.

```cpp
struct ZoneLoadResult {
    SampleZone zone;
    std::string error;
};

ZoneLoadResult loadWavZone(const std::string& path);
std::string    saveWavZone(const SampleZone& zone, const std::string& path);
```

---

## Serialization (`campione_serialization.hpp / .cpp`)

### `kStateKeyParameters` — key=value text

```
version=1
master_volume=0.8
midi_channel=0
crossfade_duration_ms=20.0
pitch_bend_range=2.0
```

### `kStateKeyZones` — multi-block text

```
version=1
zone_count=2
---
root_note=60
range_low=48
range_high=71
midi_channel=0
loop_enabled=1
loop_start=4410
loop_end=44100
source_path=/home/danny/samples/piano_C4.wav
---
root_note=72
range_low=72
range_high=95
midi_channel=0
loop_enabled=0
loop_start=0
loop_end=0
source_path=/path/to/campione_recordings/rec_1723680000.wav
```

Recorded zones point to auto-saved WAV in `campione_recordings/` beside the DAW project.

---

## DPF Wrapper (`CampionePlugin.cpp`)

```cpp
class CampionePlugin : public Plugin {
    // Plugin(kParameterCount, 0, kStateCount), USE_FILE_BROWSER (same as Rift)

    Parameters parameters_;
    std::atomic<std::shared_ptr<const std::vector<SampleZone>>> zones_; // thread-safe
    EngineState engineState_;
    bool recording_ = false;
    std::string recordingOutputDir_;

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override;
    String getState(const char* key) const override;
    void setState(const char* key, const char* value) override;
    void initState(uint32_t index, State& state) override;
    void uiFileBrowserSelected(const char* filename) override;
};
```

Thread safety: atomic shared_ptr swap for zones, same pattern as Rift's `fileSampleSource_`.

**Recording flow:**
1. UI sends record_start trigger (via state key)
2. Plugin sets `recording_ = true`, clears `recordBuffer_`
3. Each `run()` while recording: append input frames to `recordBuffer_`
4. UI sends record_stop trigger
5. Pitch detection → set rootNote; save WAV to `recordingOutputDir_/campione_recordings/`
6. Construct SampleZone, atomic-swap into zones_

---

## UI Layout (`CampioneUI.cpp`)

600×400, NanoVG, following p-mix/e-mix/rift pattern.

```
┌─────────────────────────────────────────────────┐
│  CAMPIONE                      [Vol] [MIDI Ch]  │
├─────────────────────────────────────────────────┤
│  Zone list (scrollable):                        │
│  [#] [Root] [Range]    [Loop] [Path]    [Del]   │
│   1   C4    C3–B4       ON   piano.wav    ×     │
│   2   A3    C3–G4       OFF  strings.wav  ×     │
├─────────────────────────────────────────────────┤
│  [Load WAV]  [● Record]  [Xfade: 20ms]          │
│  Waveform display + loop point markers          │
│  [Loop ▶] [Loop ◀]  (drag to adjust loop pts)  │
│  [● ● ● ● ● ● ● ●]  voice activity LEDs        │
└─────────────────────────────────────────────────┘
```

---

## CMakeLists.txt

```cmake
add_library(downspout_campione_core STATIC
    src/campione_engine.cpp
    src/campione_serialization.cpp
    src/campione_sample_loader.cpp
    src/campione_pitch_utils.cpp
)
target_link_libraries(downspout_campione_core PUBLIC downspout-project-options)
target_include_directories(downspout_campione_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include"
)

if(DOWNSPOUT_ENABLE_DPF AND DOWNSPOUT_DPF_DIR)
    dpf_add_plugin(campione
        TARGETS ${DOWNSPOUT_DPF_PLUGIN_TARGETS}
        ${DOWNSPOUT_DPF_PLUGIN_ARGS}
        USE_FILE_BROWSER
        FILES_DSP src/dpf/CampionePlugin.cpp
        FILES_UI  src/dpf/CampioneUI.cpp
    )
    target_link_libraries(campione-dsp PUBLIC downspout_campione_core)
    install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/campione.vst3" DESTINATION "." OPTIONAL)
endif()

if(BUILD_TESTING)
    add_executable(downspout_campione_core_tests tests/campione_core_tests.cpp)
    target_link_libraries(downspout_campione_core_tests PRIVATE downspout_campione_core)
    add_test(NAME downspout_campione_core_tests COMMAND downspout_campione_core_tests)
endif()
```

Root `CMakeLists.txt` additions:
```cmake
option(DOWNSPOUT_BUILD_CAMPIONE "Configure the campione sampler target" ON)
if(DOWNSPOUT_BUILD_CAMPIONE)
    add_subdirectory(plugins/campione)
endif()
```

---

## Phased Implementation

### Phase 1 — Core sampler (file loading + playback)
1. `campione_params.hpp` — enums and state key constants
2. `campione_core_types.hpp` — SampleZone, Voice, Parameters, EngineState
3. `campione_sample_loader.cpp` — port Rift WAV loader
4. `campione_pitch_utils.cpp` — snapToZeroCrossing, findLoopStart, computePlaybackRate
5. `campione_engine.cpp` — findZone, allocateVoice, processBlock
6. `campione_serialization.cpp`
7. `CampionePlugin.cpp` — MIDI in, file browser, state, audio
8. `CampioneUI.cpp` — minimal: load button, zone list, volume knob
9. `campione_core_tests.cpp` — zone lookup, voice allocation, loop wrap, xfade blend
10. `CMakeLists.txt` + root entry + `profile.ttl`
11. Build; verify VST3 loads and plays a WAV file

### Phase 2 — Recording + auto-map
12. Recording state machine in plugin wrapper
13. `saveWavZone()` in sample loader
14. `detectFundamentalHz()` (autocorrelation) in pitch utils
15. Record button in UI with elapsed-time display
16. Test: record a note → auto-mapped → plays back correctly

### Phase 3 — Full UI + polish
17. Waveform display with loop-point drag handles
18. MIDI channel dropdown
19. Zone range editor
20. Gap-filling regression tests
21. Screenshot capture and first-time-user review

### Completion checklist
- [ ] `CMakeLists.txt` and root build option
- [ ] `install.sh`, `scripts/package-release.sh`, `.github/workflows/release.yml`
- [ ] `README.md`, `docs/install.md`, `docs/release.md`, `docs/architecture.md`, `docs/plan.md`
- [ ] `docs/pages/_products/campione.md` + `scripts/capture-plugin-screenshots.sh` entry
- [ ] Screenshot captured and reviewed as first-time user
- [ ] `profile.ttl` current with all parameters

---

## Key Reference Files

| Purpose | File |
|---------|------|
| WAV loader pattern | `plugins/rift/src/rift_sample_loader.cpp` |
| Thread-safe sample swap | `plugins/rift/src/dpf/RiftPlugin.cpp` (`fileSampleSource_`) |
| MIDI → playback rate | `plugins/orchid/src/orchid_engine.cpp` (`computePlaybackRate`) |
| MIDI event dispatch | `plugins/orchid/src/dpf/OrchidPlugin.cpp` (`run()`) |
| Zero-crossing implementations | `plugins/orchid/src/orchid_engine.cpp` (`loopJoinCost`) |
| Zero-crossing algorithm spec | `docs/zero-crossing-algorithm.md` |
| DPF file browser usage | `plugins/rift/src/dpf/RiftPlugin.cpp` (`uiFileBrowserSelected`) |
| Serialization text format | `plugins/p-mix/src/p_mix_serialization.cpp` |
| CMake plugin pattern | `plugins/rift/CMakeLists.txt` |
| Shared types | `include/downspout/meter.hpp` |

---

## Verification

1. **Core tests**: zone lookup hits correct zone; +12 semitones → rate ≈ 2.0×; loop
   wraps correctly; crossfade blend sums to 1.0 at midpoint.
2. **Build**: `cmake --build` produces `campione.vst3` without errors.
3. **File load**: load a WAV via file browser; play MIDI note 60; verify audio output.
4. **Gap fill**: play notes outside loaded zone range; verify pitch-shifted output.
5. **Loop**: enable loop; hold note indefinitely; verify no click at loop point.
6. **State round-trip**: save project, reload; verify zones and parameters restored.
7. **Recording**: record 2 s of audio; verify WAV saved; auto-mapped root note correct.
