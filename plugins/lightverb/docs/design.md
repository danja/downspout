# Lightverb design

Lightverb is an original stereo effect built around one four-line Hadamard
feedback-delay network. Each sample performs four delay reads and writes plus
four one-pole damping updates. Space changes tap lengths inside preallocated
buffers; Decay derives stable per-line feedback from the requested RT60. The
algorithm does not attempt convolution-quality early reflections or room
simulation.

The effect has stereo input/output, zero reported latency, an exact dry path at
0% mix, and an effect-only path at 100% for auxiliary sends. Delay and pre-delay
storage is allocated in `prepare()` and never resized during processing. The
feedback network and final output are bounded against overload and non-finite
input.

CC 32 controls Wet mix and CC 33 controls Space on every MIDI channel. These
assignments follow T-Mix CC 20–27 and Loopdelay CC 30–31 without collision.
MIDI takeover and the reverb tail are transient; the versioned state contains
only the saved panel parameters.
CC 19 provides acquire/release lifecycle, with an optional required gate and
0/Omni or 1–16 control-channel filtering. The DPF wrapper forwards all incoming
MIDI events unchanged to the next processor.
