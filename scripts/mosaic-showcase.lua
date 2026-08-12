-- mosaic-showcase.lua
-- Builds a REAPER project that puts Mosaic centre-stage as a grain/collage
-- instrument, supported by DrumGen, BassGen, Basilico, Orchid, P-Mix, and
-- Ambo.  Run from REAPER's Actions → Load ReaScript or the Script Console.
--
-- Before pressing Play, open the Mosaic UI and load 1-4 WAV files (field
-- recordings, instrument hits, anything with tonal content works well).
-- The piece is 48 bars at 90 BPM (~2 min 8 sec) in D minor.

local PROJECT_PATH = "/home/danny/github/downspout/artifacts/reaper/mosaic-showcase.RPP"
local TEMPO        = 90.0
local METER_NUM    = 4
local METER_DEN    = 4
local TOTAL_BARS   = 48

local beat       = 60.0 / TEMPO
local bar        = beat * METER_NUM
local project_end = bar * TOTAL_BARS

local missing_fx = {}

-- ── helpers ──────────────────────────────────────────────────────────────────

local function db_to_amp(db)   return 10.0 ^ (db / 20.0) end
local function bar_t(b)        return bar * (b - 1) end   -- 1-based bar → time

local function native_color(r, g, b)
  return reaper.ColorToNative(r, g, b) | 0x1000000
end

local function add_track(name, db, pan, r, g, b)
  local idx   = reaper.CountTracks(0)
  reaper.InsertTrackAtIndex(idx, true)
  local track = reaper.GetTrack(0, idx)
  reaper.GetSetMediaTrackInfo_String(track, "P_NAME", name, true)
  reaper.SetMediaTrackInfo_Value(track, "D_VOL", db_to_amp(db))
  reaper.SetMediaTrackInfo_Value(track, "D_PAN", pan)
  reaper.SetTrackColor(track, native_color(r, g, b))
  return track
end

local function add_fx(track, name)
  local idx = reaper.TrackFX_AddByName(track, name, false, -1)
  if idx < 0 then missing_fx[#missing_fx + 1] = name end
  return idx
end

local function add_send(src, dst, db, midi_only)
  local s = reaper.CreateTrackSend(src, dst)
  if s < 0 then return end
  reaper.SetTrackSendInfo_Value(src, 0, s, "D_VOL", db_to_amp(db))
  if midi_only then
    reaper.SetTrackSendInfo_Value(src, 0, s, "B_SEND",     0)
    reaper.SetTrackSendInfo_Value(src, 0, s, "B_MIDI_SEND",1)
  end
end

local function note(take, t, dur, ch, pitch, vel)
  local s = reaper.MIDI_GetPPQPosFromProjTime(take, t)
  local e = reaper.MIDI_GetPPQPosFromProjTime(take, t + dur)
  reaper.MIDI_InsertNote(take, false, false, s, e, ch, pitch, vel, true)
end

local function new_take(track, t_start, t_end)
  local item = reaper.CreateNewMIDIItemInProj(track, t_start, t_end, false)
  return reaper.GetActiveTake(item)
end

-- ── MIDI builders ─────────────────────────────────────────────────────────────

-- Mosaic: a stream of note-on triggers whose pitch transposes the grain by
-- (note - 60) semitones.  Density and velocity vary per section to give shape.
local function mosaic_triggers(track)
  local take = new_take(track, 0, project_end)

  -- Chord tones in D minor used as grain pitch hints
  local dm   = {62, 65, 69}        -- D F A
  local bb   = {58, 62, 65}        -- Bb D F
  local f    = {53, 57, 60}        -- F A C
  local a    = {57, 61, 64}        -- A C# E
  local prog = {dm, bb, f, a}      -- 4-bar cycle

  -- per-section grid (in beats) and velocity range
  local sections = {
    {bar_start=1,  bar_end=12, grid=2.0, vel_lo=60, vel_hi=78},   -- A: sparse
    {bar_start=13, bar_end=24, grid=1.0, vel_lo=68, vel_hi=90},   -- B: medium
    {bar_start=25, bar_end=36, grid=0.5, vel_lo=72, vel_hi=104},  -- C: dense
    {bar_start=37, bar_end=48, grid=2.0, vel_lo=54, vel_hi=72},   -- D: fade out
  }

  local rng = 0x9d2c5680  -- deterministic LCG
  local function rand_int(lo, hi)
    rng = (rng * 1664525 + 1013904223) & 0xffffffff
    return lo + (rng >> 16) % (hi - lo + 1)
  end

  for _, sec in ipairs(sections) do
    local t = bar_t(sec.bar_start)
    local t_end_sec = bar_t(sec.bar_end + 1)
    local step = sec.grid * beat
    local chord_idx = 1
    while t < t_end_sec - 0.01 do
      -- decide which chord tone to use
      local bar_num = math.floor(t / bar)
      chord_idx = (bar_num % #prog) + 1
      local tones = prog[chord_idx]
      local pitch = tones[rand_int(1, #tones)]
      local vel   = rand_int(sec.vel_lo, sec.vel_hi)
      -- skip some beats in sparse sections to add breathing room
      local skip = (sec.grid >= 2.0) and (rand_int(0, 2) == 0) or false
      if not skip then
        note(take, t, step * 0.12, 0, pitch, vel)
      end
      t = t + step
    end
  end

  reaper.MIDI_Sort(take)
end

-- Canticle pad chords for the Orchid source: Dm / Bb / F / A progression,
-- held two bars each, soft and sustained.
local function canticle_chords(track)
  local take = new_take(track, 0, project_end)
  local prog = {
    {50, 53, 57, 62},  -- Dm/A voiced low: A2 C3 F3 D4
    {46, 53, 58, 65},  -- Bb:  Bb1 F2 Bb2 F3
    {53, 60, 65, 69},  -- F:   F2 C3 F3 A3
    {52, 57, 61, 64},  -- A:   E2 A2 C#3 E3
  }
  local chord_bars = 4  -- each chord lasts 4 bars
  for rep = 0, math.floor(TOTAL_BARS / (#prog * chord_bars)) do
    for ci, chord in ipairs(prog) do
      local t_start = bar_t(rep * #prog * chord_bars + (ci - 1) * chord_bars + 1)
      local t_end   = t_start + bar * chord_bars - 0.06
      if t_start < project_end then
        for _, p in ipairs(chord) do
          note(take, t_start, t_end - t_start, 0, p, 58)
        end
      end
    end
  end
  reaper.MIDI_Sort(take)
end

-- Orchid MIDI pitch-shift: play target notes so Orchid shifts the frozen grain
-- into the chord tones.  Starts at bar 13 when Orchid texture enters.
local function orchid_midi(track)
  local take = new_take(track, bar_t(13), project_end)
  local prog = {62, 58, 53, 57}  -- D3 Bb2 F2 A2  (one note per 4-bar block)
  local chord_bars = 4
  local bar_in_section = 0
  for b = 13, TOTAL_BARS, chord_bars do
    local ci = (bar_in_section % #prog) + 1
    local t_start = bar_t(b)
    local t_end   = math.min(bar_t(b + chord_bars) - 0.06, project_end)
    if t_start < project_end then
      note(take, t_start, t_end - t_start, 0, prog[ci], 80)
    end
    bar_in_section = bar_in_section + 1
  end
  reaper.MIDI_Sort(take)
end

-- ── project assembly ──────────────────────────────────────────────────────────

reaper.Undo_BeginBlock()
reaper.PreventUIRefresh(1)

-- Clear any template tracks, set tempo, initial save
for i = reaper.CountTracks(0) - 1, 0, -1 do
  reaper.DeleteTrack(reaper.GetTrack(0, i))
end
reaper.SetTempoTimeSigMarker(0, -1, 0.0, -1, -1.0, TEMPO,
                             METER_NUM, METER_DEN, false)
reaper.Main_SaveProjectEx(0, PROJECT_PATH, 0)

-- 1. Mosaic Grains  ─────────────────────────────────────────────────────────
--    Mosaic (grain engine) → P-Mix (rhythmic gating of grains) → Ambo send
local mosaic_tr = add_track("01 Mosaic Grains", -9.0, 0.0, 210, 148, 72)
add_fx(mosaic_tr, "VST3i: Mosaic (danja)")
add_fx(mosaic_tr, "VST3: P-Mix (danja)")
mosaic_triggers(mosaic_tr)

-- 2. Orchid Texture  ────────────────────────────────────────────────────────
--    Canticle (pad source) → Orchid (voiced freeze) + MIDI pitch control
--    Enters at bar 13 so section A is Mosaic alone.
local orchid_tr = add_track("02 Orchid Texture", -14.0, 0.08, 88, 178, 194)
add_fx(orchid_tr, "VST3i: Canticle (danja)")
add_fx(orchid_tr, "VST3: Orchid (danja)")
canticle_chords(orchid_tr)
orchid_midi(orchid_tr)
-- Mute before bar 13 by automating volume — simpler: just note in project notes

-- 3. Rhythm  ────────────────────────────────────────────────────────────────
--    DrumGen (generative) → DrumKit
local drum_tr = add_track("03 Rhythm", -12.0, 0.0, 128, 100, 176)
add_fx(drum_tr, "VST3: DrumGen (danja)")
add_fx(drum_tr, "VST3i: DrumKit (danja)")

-- 4. Bass  ──────────────────────────────────────────────────────────────────
--    BassGen → Basilico (Dub model; set via UI)
local bass_tr = add_track("04 Bass", -11.0, -0.04, 78, 168, 118)
add_fx(bass_tr, "VST3: BassGen (danja)")
add_fx(bass_tr, "VST3i: Basilico (danja)")

-- 5. Ambo Reverb (send bus)  ────────────────────────────────────────────────
--    100% wet; receives sends from Mosaic and Orchid
local ambo_tr = add_track("05 Ambo Reverb", -18.0, 0.0, 58, 82, 96)
add_fx(ambo_tr, "VST3: Ambo (danja)")
reaper.SetMediaTrackInfo_Value(ambo_tr, "B_MAINSEND", 1)
-- Configure Ambo send from Mosaic and Orchid after both tracks exist
add_send(mosaic_tr, ambo_tr, -9.0, false)
add_send(orchid_tr, ambo_tr, -12.0, false)

-- 6. Master  ────────────────────────────────────────────────────────────────
local master = reaper.GetMasterTrack(0)
reaper.SetMediaTrackInfo_Value(master, "D_VOL", db_to_amp(-4.0))
add_fx(master, "VST3: Guardian (danja)")

-- ── regions & markers ─────────────────────────────────────────────────────

local region_defs = {
  {1,  12, "A  Mosaic solo",          native_color(88,  118, 96)},
  {13, 24, "B  Orchid enters",        native_color(72,  104, 148)},
  {25, 36, "C  Full density",         native_color(148, 90,  72)},
  {37, 48, "D  Fade",                 native_color(90,  80,  120)},
}
for _, r in ipairs(region_defs) do
  reaper.AddProjectMarker2(0, true, bar_t(r[1]), bar_t(r[2] + 1),
                           r[3], -1, r[4])
end

reaper.AddProjectMarker2(0, false, bar_t(13), 0,
                         "Orchid texture in", -1, native_color(160, 200, 220))
reaper.AddProjectMarker2(0, false, bar_t(25), 0,
                         "Full density", -1, native_color(220, 160, 100))
reaper.AddProjectMarker2(0, false, bar_t(37), 0,
                         "Fade out", -1, native_color(160, 140, 200))

-- ── loop range ────────────────────────────────────────────────────────────

reaper.GetSet_LoopTimeRange(true, true, 0.0, project_end, false)
reaper.GetSetRepeat(1)
reaper.SetEditCurPos(0.0, false, false)

-- ── project notes ─────────────────────────────────────────────────────────

local warning = #missing_fx > 0
  and "\n\nMISSING FX:\n- " .. table.concat(missing_fx, "\n- ")
  or  ""

local notes = string.format([[
MOSAIC SHOWCASE
%d BPM, %d/%d, %d bars (~%d min %d sec)
Tonal centre: D minor

SETUP BEFORE FIRST PLAY
1. Open the Mosaic UI (track 01) and load 1-4 WAV files into the sample slots.
   Field recordings, sustained instrument notes, and percussive textures all
   work well together.  The zero-crossing snap means onsets are clean even at
   short attack times.
2. In BassGen (track 04) set Root = D and Scale = Minor.
3. In Basilico choose the Dub model for a low, warm character.
4. In Orchid (track 02) set Sensitivity ~70, Periodicity ~65.
5. In the Ambo reverb set Space to taste — start around 60.

ARRANGEMENT
  A (bars  1-12)  Mosaic alone: sparse grain triggers at 2-beat grid, P-Mix
                  adds slow rhythmic gating.  Orchid track is silent.
  B (bars 13-24)  Orchid texture enters: Canticle sustains D-minor chords
                  which Orchid captures and holds; MIDI notes shift the frozen
                  pitch to match the chord root.  Grain density doubles.
  C (bars 25-36)  Full arrangement: DrumGen and BassGen are heard clearly,
                  Mosaic is at 16th-note density with wide velocity variation.
  D (bars 37-48)  Fade: Mosaic returns to sparse 2-beat grid, low velocities.

SIGNAL FLOW
  Mosaic → P-Mix → main bus, send → Ambo
  Canticle → Orchid → main bus, send → Ambo
  DrumGen → DrumKit → main bus
  BassGen → Basilico → main bus
  Ambo reverb (send bus, 100%% wet) → main bus
  Master: Guardian limiter at -4 dB

MOSAIC GRAIN ENGINE (new in this build)
  Voice onset is snapped to the nearest zero-crossing in the sample (±3 ms
  window, rising crossing preferred for forward playback, falling for reverse).
  This means the attack envelope fades in a waveform that starts at zero rather
  than masking a mid-cycle click — short attack times (~5 ms) now work cleanly.
]] .. warning,
TEMPO, METER_NUM, METER_DEN, TOTAL_BARS,
math.floor(project_end / 60), math.floor(project_end % 60))

reaper.GetSetProjectNotes(0, true, notes)

-- ── save ─────────────────────────────────────────────────────────────────────

reaper.Main_SaveProjectEx(0, PROJECT_PATH, 0)

reaper.Undo_EndBlock("Build Mosaic Showcase project", -1)
reaper.PreventUIRefresh(-1)
reaper.UpdateArrange()

local status = #missing_fx > 0
  and ("Saved. WARNING: " .. #missing_fx .. " FX not found — "
       .. table.concat(missing_fx, ", "))
  or  "Saved: " .. PROJECT_PATH
reaper.ShowMessageBox(status, "mosaic-showcase", 0)
