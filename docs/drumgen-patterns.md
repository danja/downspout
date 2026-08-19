# DrumGen Pattern Templates

## Context

DrumGen is a MIDI drum generator plugin whose patterns emerge from Genre + Style selectors
combined with probabilistic parameters (density, variation, amounts per lane). The request
is to add a **template layer**: named rhythm skeletons that act as structural starting
points, leaving the slider parameters to shape velocity and density on top of the skeleton.

The immediate source of templates is `docs/reference/Doumbek_Rhythm_Cheat_Sheet.pdf`,
which provides ~25 named Middle Eastern and world-music rhythms in time signatures from
2/4 to 10/8. These patterns currently have no Genre equivalent in the generator and would
otherwise require the user to manually tune knobs toward a specific rhythm.

---

## Template Format: `.dg-pattern`

Templates use a plain-text format that extends the existing `serializePatternState()`
output with a metadata header block. No new parser is needed — the header is stripped and
the body is passed directly to the existing `deserializePatternState()`.

```
# DrumGen Pattern Template v1
name=Maqsum
source=Doumbek Rhythm Cheat Sheet
genre_tags=world,afro,middle-eastern
time_sig=4/4
bars=1
description=Egyptian 4/4 foundation rhythm; backbone of popular Arabic music
# ---
version=2
bars=1
meter_n=4
meter_d=4
stepsPerBeat=4
stepsPerBar=16
totalSteps=16
generationSerial=0
lane=0,36,100:0,0:0,0:0,0:0,100:0,0:0,0:0,0:0,0:0,...
lane=1,39,...
...
```

Lines beginning with `#` are comments. The `# ---` sentinel separates the metadata block
from the body. Everything after `# ---` is the raw PatternState string.

**Why not MIDI (.mid)?**
MIDI files are universally editable and a natural interchange format, but they do not carry
drumgen lane semantics (which MIDI note maps to which generator lane depends on the active
Kit Map) and would require a binary MIDI parser with no existing infrastructure. The text
format reuses code already present in `drumgen_serialization.cpp` and keeps templates
human-readable and diff-friendly. MIDI export of the current pattern remains a useful
stretch goal for DAW interop.

---

## Lane Mapping for Doumbek Patterns

The doumbek has three fundamental tones. The mapping to drumgen lanes is:

| Doumbek symbol | Meaning                     | DrumGen lane  | MIDI note (Flues kit) |
|----------------|-----------------------------|---------------|-----------------------|
| D              | Doum — low resonant stroke  | Kick (0)      | 36                    |
| T              | Tek — right-hand high tone  | ClosedHat (4) | 42                    |
| K              | Ka — left-hand high tone    | Clap (1)      | 39                    |
| d              | soft Doum                   | Kick (0)      | 36, velocity –30      |
| t              | soft Tek                    | ClosedHat (4) | 42, velocity –30      |
| k              | soft Ka                     | Clap (1)      | 39, velocity –30      |
| TK / tk        | simultaneous Tek + Ka       | Hat + Clap    | same step, both lanes |
| kT / Dk etc.   | rapid consecutive pair      | both lanes    | same 1/16 step        |
| – (dash)       | rest                        | —             | no hit                |

Base velocities:
- Strong (D, T, K uppercase): 100
- Soft (d, t, k lowercase): 70
- Simultaneous pairs: each at its normal velocity

All patterns are encoded at 1/16 resolution (`stepsPerBeat=4`) regardless of their native
subdivision, placing hits on the nearest 1/16 grid position.

---

## Patterns to Encode

### 4/4 (16 steps at 1/16)

| Name     | Pattern (basic)            | Notes                                      |
|----------|----------------------------|--------------------------------------------|
| Maqsum   | D T K T D K T TK           | Foundation of Egyptian pop                 |
| Baladi   | D D T D T K                | 6-stroke, slightly asymmetric              |
| Saidi    | D T D D T                  | Upper-Egypt feel; also encode filled var.  |
| Nawari   | T D T D T                  | Gypsy/Roma variant                         |
| Bolero   | D T T D                    | Sparse, dramatic                           |
| Bambi    | D KT -K T KT -K D D        | Complex hand-crossing pattern              |
| Wahda    | D TK TK T TK TK T TK       | Dense 4/4, "one" feel                      |
| Sombati  | D kT -k T D kk T kk        | Softer variant of Wahda family             |
| Zaffa    | D tt t t D t t             | Wedding procession feel                    |

### 2/4 (8 steps at 1/16)

| Name     | Pattern (basic)            | Notes                                      |
|----------|----------------------------|--------------------------------------------|
| Malfuf   | D -T T                     | Fast-spinning; also encode running style   |
| Khaligi  | D -D T                     | Malfuf with 2 Doums; Gulf region           |
| Ayub     | D -k D T                   | Sufi / Zar ceremony                        |
| Karachi  | T -k T D                   | Inverted Ayub                              |

### 3/4 (12 steps at 1/16)

| Name | Pattern      | Notes                       |
|------|--------------|-----------------------------|
| Vals | D T T        | Turkish waltz (Vals = Waltz)|

### 5/8 (10 steps at 1/16 — approximate; use stepsPerBeat=2 for 1/8 grid)

| Name      | Pattern         | Count     |
|-----------|-----------------|-----------|
| Turkish 5 | D k T k k       | 12 123    |
| Shoush    | D tk tk D T     | 123 12    |

### 6/4 (24 steps)

| Name   | Pattern (basic)       | Notes       |
|--------|-----------------------|-------------|
| Sudasi | D D D D D T           | Six-beat    |

### 6/8 (12 steps at 1/8 grid)

| Name       | Pattern                 | Notes             |
|------------|-------------------------|-------------------|
| Moroccan 6 | D k k D k k             | 123 123           |
| Reng       | D KT D TK TK            | Persian Shish Hasht|

### 7/8 (14 steps)

| Name        | Pattern             | Count           |
|-------------|---------------------|-----------------|
| Laz         | D k T k D k k       | 12 12 123       |
| Kalamatiano | D k k D k T k       | 123 12 12       |

### 8/4 (32 steps)

| Name       | Pattern (2 lines)              | Notes              |
|------------|--------------------------------|--------------------|
| Çiftetelli | D K T K T / D D T              | Belly dance staple |
| Masmoudi   | D D TK TK T / D TK TK T TK TK | Also "3 Doum" var. |

### 9/8 (18 steps)

| Name       | Pattern (basic)               | Count       |
|------------|-------------------------------|-------------|
| Karşılama  | D T D T T                     | 12 12 12 123|
| Gypsy 9    | D D tk T tk T T tk            | Romani 9/8  |
| Syncopated | D D tk Tk kT kk T             | Advanced    |

### 10/8 (20 steps)

| Name   | Pattern (basic)    | Count          |
|--------|--------------------|----------------|
| Curcuna| D T D T            | 123 12 12 123  |
| Samai  | D T D D T          | 12 12 12 123   |

---

## Architecture

### New files

```
plugins/drumgen/
  patterns/
    world/
      maqsum.dg-pattern
      baladi.dg-pattern
      saidi.dg-pattern
      nawari.dg-pattern
      bolero.dg-pattern
      bambi.dg-pattern
      wahda.dg-pattern
      sombati.dg-pattern
      zaffa.dg-pattern
      malfuf.dg-pattern
      khaligi.dg-pattern
      ayub.dg-pattern
      karachi.dg-pattern
      vals.dg-pattern
      turkish5.dg-pattern
      shoush.dg-pattern
      sudasi.dg-pattern
      moroccan6.dg-pattern
      reng.dg-pattern
      laz.dg-pattern
      kalamatiano.dg-pattern
      ciftetelli.dg-pattern
      masmoudi.dg-pattern
      karsılama.dg-pattern
      gypsy9.dg-pattern
      syncopated9.dg-pattern
      curcuna.dg-pattern
      samai.dg-pattern
  include/
    drumgen_template.hpp       # PatternTemplate struct + TemplateLibrary class
  src/
    drumgen_template.cpp       # TemplateLibrary implementation (scan, load, parse)
  src/dpf/
    DrumgenPlugin.cpp          # extend setState/getState for template loading
    DrumgenUI.cpp              # template dropdown, Load/Preview buttons, step grid
```

### `PatternTemplate` struct (`drumgen_template.hpp`)

```cpp
struct PatternTemplate {
    std::string name;
    std::string source;
    std::string timeSig;
    std::string description;
    std::vector<std::string> genreTags;
    PatternState pattern;     // decoded, ready to use
};
```

### `TemplateLibrary` class

```cpp
class TemplateLibrary {
public:
    // Scan directory recursively for *.dg-pattern files; call once at startup.
    void scan(const std::string& directory);

    int count() const;
    const PatternTemplate* get(int index) const;       // nullptr if out of range
    const PatternTemplate* findByName(const std::string& name) const;

    // Parse a single file; returns nullopt on failure.
    static std::optional<PatternTemplate> load(const std::string& path);
};
```

`load()` strips the metadata header (reads until `# ---`), parses key=value pairs into
`PatternTemplate` fields, then passes the remainder to the existing
`deserializePatternState()`. No new parser logic required.

### Plugin integration

Loading a template is a **UI-only action**, not an automatable parameter:
- It mutates `controls.pattern` (the existing `pattern` state key) directly.
- The existing `setState("pattern", ...)` path already handles this.
- Host can save/restore the resulting pattern in its normal preset mechanism.

A new state key `template_name` (string) is added so hosts can record which template was
last loaded — this is informational only and does not drive regeneration on load.

No new integer parameter is needed. The template acts like pressing "New" with a
predetermined outcome.

### UI changes (`DrumgenUI.cpp`)

Extend the existing left Pattern Panel. Below the current seven dropdowns:

```
┌─────────────────────────────────┐
│ Template  [Maqsum          ▼]   │
│           [Load] [Preview]      │
└─────────────────────────────────┘
```

- **Template dropdown**: lists all `TemplateLibrary` entries by name; first entry is
  `"— none —"`.
- **Load button**: calls `TemplateLibrary::get(selectedIndex)`, serialises its
  `PatternState`, and calls `setState("pattern", serialized)` on the plugin. Also
  sets `controls.bars` and `resolution` from the template's metadata if they differ.
- **Preview button**: opens an inline step-grid overlay (see below). Does not alter
  the live pattern.

### Step-grid preview overlay

A modal-style panel rendered over the mix section showing the template before committing:

```
 PREVIEW: Maqsum (4/4)
 ┌──────────────────────────────────────────────────┐
 │ Kick  ■ · · · · · · · ■ · · · · · · ·            │
 │ Clap  · · · ■ · · · · · · · ■ · · · ·            │
 │ Hat   · · ■ · · · ■ · · · ■ · · · ■ ■            │
 └──────────────────────────────────────────────────┘
       [Load]          [Close]
```

- Rows = the 11 lanes (only rows with any hits shown).
- Columns = steps (up to 32, scrollable if bars > 2).
- Cell shade encodes velocity (darker = louder).
- "Load" inside the preview commits the pattern.
- "Close" dismisses without change.

This is a pure visual preview. Audio preview requires the host to be playing; once "Load"
is pressed the pattern is live and the host transport will play it.

---

## Implementation Steps

1. **Create `docs/drumgen-patterns.md`** — move this document there.

2. **Write template format parser** (`drumgen_template.cpp` / `.hpp`):
   - `TemplateLibrary::load()` — strip header, parse metadata, call
     `deserializePatternState()`.
   - `TemplateLibrary::scan()` — walk `patterns/` directory, call `load()` per file.
   - Unit tests in `tests/test_drumgen_template.cpp`.

3. **Encode all Doumbek patterns** as `.dg-pattern` files in `plugins/drumgen/patterns/world/`:
   - Use the lane mapping table above (D→Kick, T→Hat, K→Clap).
   - Encode at 1/16 resolution where possible; use `stepsPerBeat=2` for 5/8 and other
     odd meters that don't subdivide cleanly to 1/16.
   - For patterns longer than 1 bar at 4/4 (e.g., Çiftetelli, Masmoudi), set `bars=2`.

4. **Plugin instantiation** (`DrumgenPlugin.cpp`):
   - Construct `TemplateLibrary` at plugin init; scan `patterns/` directory relative to
     plugin bundle path (use DPF's `getBundlePath()` or equivalent).
   - Add state key `template_name` (string, empty default).
   - `setState("pattern", …)` path already exists; no changes needed for load.

5. **UI: template dropdown and Load/Preview buttons** (`DrumgenUI.cpp`):
   - Add `templateDropdown_` selector widget below existing dropdowns.
   - Add `loadTemplate()` method calling `setState("pattern", ...)`.
   - Add `previewTemplate()` method setting a local preview flag.

6. **UI: step-grid overlay** (`DrumgenUI.cpp`):
   - New `drawStepGrid(const PatternState&)` helper using existing NanoVG calls.
   - Rendered conditionally when `showPreview_` flag is set.
   - "Load" and "Close" buttons within the overlay.

7. **CMake** (`plugins/drumgen/CMakeLists.txt`):
   - Add `drumgen_template.cpp` to the build.
   - Install `patterns/` directory alongside the VST3 bundle:
     ```cmake
     install(DIRECTORY patterns/ DESTINATION ...)
     ```

8. **New plugin completion checklist items**:
   - Screenshot must show the template dropdown and preview overlay.
   - `profile.ttl` update: add `template_name` state key.

---

## Verification

- `tests/test_drumgen_template.cpp`:
  - `TemplateLibrary::load()` correctly parses all bundled `.dg-pattern` files.
  - Loaded `PatternState` round-trips through `serializePatternState()` unchanged.
  - `scan()` finds all files in `patterns/world/`.
- Manual: load Maqsum template, verify Kick hits on expected 1/16 steps in REAPER piano roll.
- Manual: open preview overlay for each pattern, confirm correct lane layout.
- Manual: load template, adjust Density slider, confirm hits are attenuated/dropped.
- Build: `cmake --build` produces VST3 with `patterns/` alongside bundle with no warnings.

---

## Resolved Decisions

- **User templates directory**: `~/.vst3/drumgen-data/patterns/` (default on all platforms).
  Bundled templates live in `plugins/drumgen/patterns/`; user templates live here. The
  `TemplateLibrary::scan()` merges both directories, with user templates listed after bundled.
- **Odd-meter templates**: patterns loop at their own length regardless of host bar lines.
  A 7/8 template in a 4/4 project creates a natural polyrhythm. No padding applied.
  All meters from the Doumbek sheet (2/4 through 10/8) are encoded in the first pass.
- **Kit remapping**: templates store lane indices (0–10), not MIDI note numbers. The engine
  resolves note numbers at play time based on the active Kit Map. Template files must not
  contain hard-coded MIDI note values in the lane encoding.
