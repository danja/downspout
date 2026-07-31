-- Build a standalone REAPER exercise project for Downspout's generative suite.
-- Run in a fresh REAPER instance; this script intentionally does not reuse the
-- caller's current project tab.

local PROJECT_PATH = "/home/danny/github/downspout/artifacts/reaper/generative-workstation-lab.RPP"
local TEMPO = 96.0
local PROJECT_END = 40.0 -- sixteen 4/4 bars at 96 BPM

local missing_fx = {}

local function db_to_amp(db)
  return 10.0 ^ (db / 20.0)
end

local function native_color(r, g, b)
  return reaper.ColorToNative(r, g, b) | 0x1000000
end

local function add_track(name, db, pan, r, g, b)
  local index = reaper.CountTracks(0)
  reaper.InsertTrackAtIndex(index, true)
  local track = reaper.GetTrack(0, index)
  reaper.GetSetMediaTrackInfo_String(track, "P_NAME", name, true)
  reaper.SetMediaTrackInfo_Value(track, "D_VOL", db_to_amp(db))
  reaper.SetMediaTrackInfo_Value(track, "D_PAN", pan)
  reaper.SetTrackColor(track, native_color(r, g, b))
  return track
end

local function add_fx(track, name, fallback)
  local index = reaper.TrackFX_AddByName(track, name, false, -1)
  if index < 0 and fallback then
    index = reaper.TrackFX_AddByName(track, fallback, false, -1)
  end
  if index < 0 then
    missing_fx[#missing_fx + 1] = name
  end
  return index
end

local function add_note(take, start_time, duration, channel, pitch, velocity)
  local start_ppq = reaper.MIDI_GetPPQPosFromProjTime(take, start_time)
  local end_ppq = reaper.MIDI_GetPPQPosFromProjTime(take, start_time + duration)
  reaper.MIDI_InsertNote(take, false, false, start_ppq, end_ppq,
                         channel, pitch, velocity, true)
end

local function create_phrase_item(track)
  local item = reaper.CreateNewMIDIItemInProj(track, 0.0, PROJECT_END, false)
  local take = reaper.GetActiveTake(item)
  local motif = {60, 63, 67, 70, 67, 65, 63, 58}
  local beat = 60.0 / TEMPO
  for phrase = 0, 7 do
    local phrase_start = phrase * 8.0 * beat
    for step = 0, 7 do
      local pitch = motif[step + 1] + ((phrase % 4 == 3) and 5 or 0)
      local velocity = (step == 0 or step == 4) and 104 or 82
      add_note(take, phrase_start + step * beat, beat * 0.72,
               0, pitch, velocity)
    end
  end
  reaper.MIDI_Sort(take)
end

local function create_pad_item(track)
  local item = reaper.CreateNewMIDIItemInProj(track, 0.0, PROJECT_END, false)
  local take = reaper.GetActiveTake(item)
  local beat = 60.0 / TEMPO
  local chords = {
    {48, 55, 60}, {46, 53, 58}, {51, 58, 63}, {43, 50, 55}
  }
  for section = 0, 3 do
    local start_time = section * 16.0 * beat
    for _, pitch in ipairs(chords[section + 1]) do
      add_note(take, start_time, 16.0 * beat - 0.08, 0, pitch, 66)
    end
  end
  reaper.MIDI_Sort(take)
end

local function create_mosaic_trigger_item(track)
  local item = reaper.CreateNewMIDIItemInProj(track, 0.0, PROJECT_END, false)
  local take = reaper.GetActiveTake(item)
  local beat = 60.0 / TEMPO
  local pitches = {48, 55, 60, 67, 53, 58, 65, 70}
  for step = 0, 63 do
    if step % 4 ~= 3 then
      add_note(take, step * beat, beat * 0.18, 0,
               pitches[(step % #pitches) + 1], 72 + (step % 4) * 8)
    end
  end
  reaper.MIDI_Sort(take)
end

reaper.Undo_BeginBlock()
reaper.PreventUIRefresh(1)

-- The command-line launcher gives this script a fresh project. Remove any
-- accidental template tracks defensively, then save the empty checkpoint.
for index = reaper.CountTracks(0) - 1, 0, -1 do
  reaper.DeleteTrack(reaper.GetTrack(0, index))
end
reaper.SetTempoTimeSigMarker(0, -1, 0.0, -1, -1.0, TEMPO, 4, 4, false)
reaper.Main_SaveProjectEx(0, PROJECT_PATH, 0)

local form = add_track("01 FORM VOICE - Conductor to Canticle", -17.0, -0.08, 220, 154, 78)
add_fx(form, "VST3: Conductor (danja)", "Conductor (danja)")
add_fx(form, "VST3i: Canticle (danja)", "Canticle (danja)")

local harmony = add_track("02 HARMONIC FIELD - Atlas, Canticle, Garden, Orbit", -16.0, -0.16, 104, 188, 206)
add_fx(harmony, "VST3: Harmonic Atlas (danja)", "Harmonic Atlas (danja)")
add_fx(harmony, "VST3i: Canticle (danja)", "Canticle (danja)")
add_fx(harmony, "VST3: Resonance Garden (danja)", "Resonance Garden (danja)")
add_fx(harmony, "VST3: Orbit (danja)", "Orbit (danja)")

local rhythm = add_track("03 POLYRHYTHM LISTENER - Polymeter, DrumKit, Oracle", -13.0, -0.04, 226, 118, 92)
add_fx(rhythm, "VST3: Polymeter (danja)", "Polymeter (danja)")
add_fx(rhythm, "VST3i: DrumKit (danja)", "DrumKit (danja)")
add_fx(rhythm, "VST3: Oracle (danja)", "Oracle (danja)")

local response = add_track("04 ORACLE RESPONSE - Canticle and Orbit", -19.0, 0.24, 90, 182, 214)
add_fx(response, "VST3i: Canticle (danja)", "Canticle (danja)")
add_fx(response, "VST3: Orbit (danja)", "Orbit (danja)")
local oracle_send = reaper.CreateTrackSend(rhythm, response)
if oracle_send >= 0 then
  reaper.SetTrackSendInfo_Value(rhythm, 0, oracle_send, "D_VOL", db_to_amp(-6.0))
  reaper.SetTrackSendInfo_Value(rhythm, 0, oracle_send, "I_MIDIFLAGS", 0)
end

local memory = add_track("05 MOTIF MEMORY - Mnemosyne to Canticle", -17.0, 0.12, 176, 132, 202)
add_fx(memory, "VST3: Mnemosyne (danja)", "Mnemosyne (danja)")
add_fx(memory, "VST3i: Canticle (danja)", "Canticle (danja)")
add_fx(memory, "VST3: Orbit (danja)", "Orbit (danja)")
create_phrase_item(memory)

local drift = add_track("06 DRIFT PAD - Drift to Canticle", -21.0, -0.26, 117, 190, 134)
add_fx(drift, "VST3: Drift (danja)", "Drift (danja)")
add_fx(drift, "VST3i: Canticle (danja)", "Canticle (danja)")
add_fx(drift, "VST3: Orbit (danja)", "Orbit (danja)")
create_pad_item(drift)

local mosaic = add_track("07 MOSAIC TEXTURE - load WAV slots, then Garden and Orbit", -19.0, 0.28, 220, 166, 84)
add_fx(mosaic, "VST3i: Mosaic (danja)", "Mosaic (danja)")
add_fx(mosaic, "VST3: Resonance Garden (danja)", "Resonance Garden (danja)")
add_fx(mosaic, "VST3: Orbit (danja)", "Orbit (danja)")
create_mosaic_trigger_item(mosaic)

local master = reaper.GetMasterTrack(0)
reaper.SetMediaTrackInfo_Value(master, "D_VOL", db_to_amp(-6.0))
add_fx(master, "VST3: Guardian (danja)", "Guardian (danja)")

-- A muted item keeps the complete generative form visible even if all authored
-- MIDI is later removed.
local anchor = reaper.CreateNewMIDIItemInProj(form, 0.0, PROJECT_END, false)
reaper.SetMediaItemInfo_Value(anchor, "B_MUTE", 1)

local region_colors = {
  native_color(63, 87, 105), native_color(92, 106, 75),
  native_color(112, 79, 116), native_color(128, 72, 62)
}
local region_names = {
  "01 Establish", "02 Interact", "03 Recombine", "04 Safety check"
}
for i = 0, 3 do
  reaper.AddProjectMarker2(0, true, i * 10.0, (i + 1) * 10.0,
                           region_names[i + 1], -1, region_colors[i + 1])
end

local warning = ""
if #missing_fx > 0 then
  warning = "\n\nWARNING - FX NOT FOUND:\n- " .. table.concat(missing_fx, "\n- ")
end
local notes = [[
DOWNSPOUT GENERATIVE WORKSTATION LAB

Tempo: 96 BPM, 4/4, 16 bars.

Routing:
- Conductor drives a sparse Canticle form voice.
- Harmonic Atlas drives Canticle, then Resonance Garden and Orbit.
- Polymeter drives DrumKit; Oracle listens after the instrument.
- The Polymeter/Oracle track sends post-FX audio and response MIDI to a second Canticle voice.
- An authored two-bar motif feeds Mnemosyne before Canticle and Orbit.
- Drift shares a track with a held Canticle pad so its CC lanes can be monitored.
- Mosaic receives an authored trigger stream, then feeds Resonance Garden and Orbit.
- Guardian is last on the master and the master starts at -6 dB.

Before first Mosaic playback, open Mosaic and load one to four WAV files. The
sample paths will be stored in the project. Start playback from bar 1 so all
transport-aware generators establish deterministic state.
]] .. warning
reaper.GetSetProjectNotes(0, true, notes)

reaper.GetSet_LoopTimeRange(true, true, 0.0, PROJECT_END, false)
reaper.GetSetRepeat(1)
reaper.SetEditCurPos(0.0, false, false)
reaper.Main_SaveProjectEx(0, PROJECT_PATH, 0)

reaper.PreventUIRefresh(-1)
reaper.UpdateArrange()
reaper.Undo_EndBlock("Create Downspout generative workstation lab", -1)

if #missing_fx > 0 then
  reaper.ShowConsoleMsg("Generative workstation project saved with missing FX:\n- " ..
                        table.concat(missing_fx, "\n- ") .. "\n")
end
