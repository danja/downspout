# Box design notes

## Scope

Box is an original stereo audio effect, not an LV2 port. It models a wooden
instrument structure (plate, soundhole and cavity) and deliberately excludes
strings, air radiation from a particular room, and any host transport
behaviour. The portable core has no DPF, VST3 or UI dependencies.

## Assumptions mapped into DPF/VST3

- **Audio routing.** Two audio inputs and two audio outputs, grouped as one
  stereo pair. Both channels share the same plate geometry; Stereo Width spreads
  and slightly detunes the two microphone positions. No sidechain is declared.
- **MIDI.** One MIDI input. CC 1-4 are fixed to Body Size, Damping, Soundhole and
  Drive on the selectable CC Channel (1-16). CCs are decoded at block start:
  events carry a frame offset, but the modal bank is a block-level control-grid
  snapshot, so splitting a block for sub-block parameter changes would add
  cost without a audible benefit. Other CCs, notes, bend and aftertouch are
  ignored.
- **State.** Every editable setting is a normal automatable parameter. Output
  parameters (Randomise Count, Body Mode, Air Mode, Decay, Mode Q) are read-only
  and are recomputed from the patch. Randomise follows the HeavyVerb pattern:
  the button computes a new body and commits each physical value through the
  normal parameter path, so the processor, host and panel agree. DPF's VST3
  bridge deliberately suppresses `kParameterIsTrigger` writes, so a trigger
  parameter cannot do this from a UI. The wrapper therefore declares no extra
  state (`WANT_STATE` is absent), which avoids saving default snapshots into
  every project.
- **Transport.** None. The model has no tempo, bar or song-position dependency.
- **Lifecycle.** `activate()` and a sample-rate change clear biquad histories,
  the drive envelope and the control-grid counter only. They never call `init()`
  or overwrite the patch. A fresh engine starts its wet mix at the current Mix
  so the first sample is already processed when Mix is 100 %.
- **UI.** The panel derives its response plot from the same `analyseBody()`
  function as the DSP, using 48 kHz as a fixed plotting rate. The peak is
  normalised to 0 dB so the shape stays visible across the deliberately wide
  parameter ranges; the plot is labelled "relative". The theme switch is a
  UI-local preference and is not saved.

## Testing decisions

- Audibility is asserted from a rendered difference signal, not by inspecting
  intermediate coefficient values. The noise probe covers the whole modal bank.
- A Size sweep from 1x to 4x and back is rendered explicitly: the old code
  interpolated biquad coefficients and reached ~1e38 even though every static
  Size value was finite. The fix glides physical values and installs each
  freshly designed filter directly.
- Physical-law tests use values inside the published ranges; earlier drafts
  used out-of-range values and measured clamping rather than the law.
- Non-finite automation falls back to the published default, while finite
  out-of-range values clamp to the endpoint. Both paths are tested.
- The impulse test checks decay in two late windows, and the extreme test
  checks finiteness and a loose peak bound rather than a misleading output-level
  ceiling.
