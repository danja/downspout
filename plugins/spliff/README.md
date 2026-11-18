# Spliff

`spliff.vst3` — adaptive transient processor, a downspout-style approximation of
Oeksound Spiff with a third level-independent Attack/Sustain Designer mode after
LYNX's *Strike Shaper* / the SPL Transient Designer.

Cut/Boost work in the Spiff manner: transients are detected per frequency band
and gain is applied only where transient energy exists. **Designer** takes the
opposite, classic transient-designer route: two envelopes (0.2/30 ms fast and
20/250 ms slow) are compared as a ratio in dB, with no threshold and no absolute
level in the decision. Scaling the input therefore leaves the gain unchanged.

## Using it

**Pick a mode first.**

- **Cut** tames clicks, mouth noise and harsh sticks — late in the chain.
- **Boost** brings drums and piano attacks forward — early, before compression.
- **Designer** uses Attack (±15 dB) and Sustain (±24 dB) instead of the Spiff
  controls. Attack sets gain while the fast envelope leads (onsets); Sustain
  sets it while the slow envelope leads (body/tails). Both controls default to
  0 dB and are inert, so old projects are unchanged.

**Cut/Boost workflow** (from the manual): Delta on, raise Depth, tune
Sensitivity and Decay, sculpt the band splits, optionally Sharpness and Decay
LF/HF, then Delta off, re-tune Depth, Bypass to compare, Mix to taste, Trim to
compensate. In Designer, start with Attack/Sustain at 0 dB and move one at a
time; Delta is the clearest way to hear what each envelope is doing.

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Mode | Cut, Boost, Designer | Cut | Designer swaps the rows below for Attack/Sustain |
| Depth | 0–100 % | 50 % | Amount of cut/boost; CC 1; Cut/Boost only |
| Sensitivity | 0–100 % | 50 % | Higher catches more transients; CC 2; Cut/Boost only |
| Sharpness | 0–100 % | 30 % | Per-band independence; low = natural on drums |
| Decay | 0–100 % | 25 % | Recovery time; 0 = onset only; CC 3; Cut/Boost only |
| Decay LF/HF | LF … equal … HF | equal | Which end of the spectrum decays longer |
| Split Low / High | 20 Hz–12 kHz | 250 Hz / 4 kHz | Detector band crossovers (sidechain weighting, not EQ) |
| Mix | 0–100 % | 100 % | Dry/wet blend; CC 4 |
| Trim | −12 to +12 dB | 0 dB | Wet-only makeup gain |
| Bypass | active/bypassed | active | Soft in intent; DSP keeps running |
| Delta | off/on | off | Monitor wet−dry: what is removed or added |
| Attack | −15 to +15 dB | 0 dB | Designer only; fast envelope (onsets) |
| Sustain | −24 to +24 dB | 0 dB | Designer only; slow envelope (body/tails) |
| CC Depth/Sens/Decay/Mix | 0–127 | 1/2/3/4 | 0 = off; Drift lane defaults |
| CC Channel | 1–16 | 1 | MIDI channel for the four CCs |

Route Drift's MIDI output to Spliff's MIDI input: CC 1 → Depth, CC 2 →
Sensitivity, CC 3 → Decay, CC 4 → Mix. In Designer, only Mix (CC 4) has an
effect; Attack and Sustain are panel state parameters.

**Stereo.** All three modes use a shared mono detector (fixed full link — gain
changes never shift the image); both channels keep their own audio content. M/S
operation from the manual is not implemented.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_SPLIFF=ON
cmake --build build --target downspout_spliff_core_tests spliff-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_spliff_core_tests --output-on-failure
```

Core tests cover silence, dry passthrough at Mix 0, bypass, Depth-0
transparency, cut reduction and boost lift on an onset burst, the sensitivity
gate, decay-tail behaviour and tilt direction, Delta polarity, Trim gain,
block-split determinism, clamping, and state round trip. Designer tests assert
0 dB transparency, Attack/Sustain lift and cut inside their stated bounds,
level independence over a 200 dB range, a response to a small non-zero envelope
ratio (no threshold), and block-split determinism.

## Status

Core DSP (both Spiff modes and Designer), deterministic tests, the VST3 target
and the panel are complete. The catalogue screenshot predates the three-way
selector; fresh Cut and Designer captures were taken in
`/tmp/opencode/spliff-ui-screenshot/` but visual acceptance is pending. Host
validation in REAPER remains. Deliberately deferred to follow-ups: M/S stereo
modes, parametric sensitivity-curve bands, resolution / oversample / window /
phase controls, and live gain-reduction metering in the panel.
