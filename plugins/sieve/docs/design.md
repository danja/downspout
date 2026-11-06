# Sieve design notes

STFT mask (sqrt-Hann, 1024-pt, hop 256). Per bin, compute nearest scale note (or held MIDI note) in cents; gain = gaussian over width, floored at floor dB, with odd/even tilt. Overlap-add resynthesis. Latency ~1024 samples.

Controls as in README. Output level applied.