# m-mix

`m-mix` is a transport-aware MIDI gate that combines the probabilistic bar
transition idea from `p-mix` with the Euclidean block pattern from `e-mix`.

The plugin processes incoming MIDI rather than audio. Note-on events pass only
when both gates are open. Note-on velocity is scaled by the combined fade gain
when velocity fades are enabled. Note-offs are tracked against passed note-ons
so notes that were blocked do not release downstream notes, and held notes are
released when the gate closes.

Stopped transport passes MIDI through, except when mute is enabled. When a host
does not provide BBT but does provide rolling frame position and tempo, the VST3
wrapper derives bar position from those values so the gate still runs.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
