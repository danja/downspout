# Fresco design notes

Original design: a short-time Fourier transform with the magnitudes replaced, from the standard definitions. Decoders are written from the PNG (RFC 2083 / 1950 / 1951),
Netpbm and BMP specifications; no third-party code is vendored and no library is linked.

## Signal flow
Per channel: input -> 2048-sample frame, periodic Hann, every 512 samples (75 % overlap) -> FFT -> magnitudes replaced -> inverse FFT -> Hann -> overlap-add -> 512-sample
output queue. The queue plays one hop behind the frame, so the wet path is exactly **2048 samples** late (measured, `kLatency`); the dry path for Mix is delayed by the same amount.

## The picture to magnitudes
For each frame the playhead (0..1) picks a column (linear interpolation between columns); for each bin `k` at `f = k sr / 2048` between Bottom and Top the row is
`ln(Top/f)/ln(Top/Bottom)` (Log) or `(Top-f)/(Top-Bottom)` (Linear) of the height, interpolated between rows, inverted if Pixels is Dark = loud, raised to
Contrast, and mapped to `10^(-Range (1-g) / 20)`; a pixel that is exactly black stays silent. A bin that falls between two pixel rows takes their interpolation, not an average over
the rows it spans, so a very fine picture is aliased at the bottom of a log axis (many rows share a bin).

## Phase and level
- **Tone**: bin `k` is a sinusoid with a random start phase (per channel, fixed seed) that advances `k` quarter turns per frame (hop = N/4), so consecutive frames are coherent: a lit
  bin is a steady tone at its own bin-centre frequency. A white pixel is `0.02` amplitude (`kBinAmplitude`); the overlap-add gain is `1/2` for the synthesis window alone.
- **Input**: the bin keeps the input's phase (`X/|X|`), so the timing and pitch character of the input are kept; the overlap-add gain is `1/1.5` (analysis and synthesis
  windows). The result is a coloured version of the input, not a sum of steady tones; where the input has no energy in a bin (below 1e-6) the Tone phase is used.
- **Follow input level**: the frame gain is `(1 - f) + f min(1, 8 rms)` with `rms` the frame's input RMS, so silence in is exact silence out at 1 and the picture can drone at 0.

## Playhead
Free loop: `timeS` advances one hop per frame, `playhead = frac(timeS / Length)`. Follow host: `position = beatPosition * 60 / bpm + offsetInBlock / sr` at the
frame, modulo Length; used only when the host reports a valid tempo, otherwise the free-running clock is used. The transport's playing flag is not consulted (a stopped
transport holds a column, the position being frozen).

## Picture loading
`setState("picture", path)` runs on the host's main thread, decodes the file (`loadImageFile`), and hands the audio thread an immutable `shared_ptr<const Image>`. An empty or
unreadable path falls back to the built-in picture so the plugin does not go silent for want of a file. Pictures are converted to 8-bit brightness (Rec. 709 weights on the stored
values; alpha treated as black) and reduced to at most 2048 pixels on a side. Limits: 8192 x 8192 and 16 Mpixel before reduction, 64 MB file; the inflate stops at the size the
header promises, so a damaged file cannot allocate more. PNG: all colour types and bit depths, not interlaced; PNM: P2/P3/P5/P6; BMP: 8, 24 and 32 bit uncompressed.

## Not done
JPEG, GIF and video (video needs a decoder dependency such as FFmpeg, which needs approval); interlaced PNG; a pitch-tracking or transient-preserving mode; oversampled
resynthesis for pictures with very fine detail; panel preview of the live column levels.
