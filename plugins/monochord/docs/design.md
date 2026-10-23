# Monochord: design notes

Source read: `~/github/oxide-studio/crates/oxide-dsp/src/physical/tension.rs` (MIT/Apache-2.0). The idea (a delay-line string with a loss filter,
dispersion all-pass, pluck, bow and hammer exciters and a small body) is kept; the engine was redesigned.

## What the source does (from reading the code; not run)

- **The loop is not tuned for its own filters.** The delay is `sr / f0`, but the loop also contains a one-pole low-pass and a dispersion
  all-pass whose delay is never subtracted, so the string plays flat. Estimated from the filters' phase delays at the default settings
  (damping 0.3, stiffness 0.2): about 1.5 samples extra, 0.8 % (14 cents) at middle C and about 3 % (50 cents) at C6; more with more damping.
- **The loss is a fixed per-pass factor** (`string_decay_loss`, 0.8-0.999) plus the low-pass, so the decay time is tied to the pitch and cannot be
  set; a higher note simply dies sooner.
- **The hammer pulse is not zero-mean** (a positive half-sine), so a struck string carries a DC component that decays only through the loop.
- **The bow** uses a friction curve `2 v / (1 + (3 v)^2)` on the difference between a bow speed (the note velocity, 0 to 1) and the string value. In
  Monochord's own first version, which used the same curve, a bow speed above about 0.1 let the string settle to a constant offset rather than stick and slip (and then
  it is silent, because the DC is blocked);
  the source's speed range is 0-1, so it very likely does the same at most velocities.
- The dispersion is one all-pass section, whose delay changes far too little between the fundamental and the partials to move them audibly.
- The amplitude envelope is fixed (1 ms attack, 2 s decay to 0.7, 200 ms release); only the first 1024 frames of a buffer are processed.

## What Monochord does

- **Tuning:** `tuneLoop()` computes the damping low-pass pole and the stiffness all-pass, evaluates their phase delays at the note's frequency, and
  takes the remainder of one period as a whole number of samples plus a fraction. The fractional-delay all-pass coefficient is found by bisection so
  that its phase delay *at that frequency* (not at DC) equals the fraction. The result: the loop's total phase delay at f0 equals `sr / f0` to 1e-12
  samples for every note from 24 to 108, at 44.1, 48, 96 and 192 kHz, whatever the damping and stiffness. It is recomputed every 32 samples, so pitch
  bend retunes the loop.
- **Decay:** the loop gain is `10^(-3 / (decay x f0))` divided by the low-pass's gain at f0, so the *fundamental* falls 60 dB in Decay seconds when Damping is 0;
  damping then makes the high partials fall faster (the rate of partial 8 over the fundamental's: 1.00 at Damping 0, 2.16 at 0.7). Loss is per pass, so heavy damping
  on a high note leaves a string that is gone in milliseconds (as with a real string, and the loop gain cannot be raised above 1 to compensate: the low-pass has unity
  gain at DC).
- **Stiffness:** a cascade of four identical first-order all-passes whose low-frequency delay together is `stiffness^2 x a quarter of the period`. The delay is a fixed
  fraction of the cycle, so the inharmonicity is similar at any pitch, and the tuning compensation keeps the fundamental exact.
- **Exciters:** a pluck is a triangle (apex at Position), rounded off by up to six 3-point smoothing passes as Hardness falls; a hammer is a half-sine pulse of width
  2-30 % of the string, centred at Position. Both are written into the loop with the mean removed and the peak scaled to the velocity. A bow adds the friction force each
  sample while the key is held, at a slow speed (0.03 + 0.12 x velocity) that always sticks and slips; its oscillation size is set by the friction curve, so its level follows
  the velocity at the output.
- **Pickup:** the string is read at two points a fraction of its length apart and subtracted: a comb that nulls every n-th harmonic when the pickup is at 1/n.
- **Body:** two band-pass resonators (120 and 280 Hz times Body size, Q 4 and 5) on the output. **Damper:** after note-off the output fades with the Release time;
  the sustain pedal holds the voice. A voice whose string has died (peak below 1e-6) is freed even if the key is held.
- **Velocity** mixes between every note at full level (0) and the key velocity (1), and scales the excitation once (not again at the output).

## Measured (the tests, 134 checks; also under ASan/UBSan)

- Loop phase delay error at most 4.6e-13 samples; every plucked or struck note from 28 to 100 is within 0.4 cents with damping 0 or 0.8 and stiffness 0 or 0.6
  (damping 0.8 above note 81 is skipped: the string is gone before a pitch can be measured), at 44.1, 96 kHz and under a full pitch bend (within 10 cents).
- Decay: the fundamental falls 60 dB in 1, 3 and 8 s within 12 % at notes 48 and 72.
- Stiffness 0: partial 6 is +0.01 cents; 0.5: +2.9; 0.6: +8.2; 0.7: +19.5; 0.8: +40; 0.9: +62 cents. The fundamental stays within 1 cent.
- Pluck at 0.2: the fifth harmonic is -38.8 dB (the fourth -24.9 dB, theory -24.1 dB); a middle pluck has no second harmonic; a pickup at 1/4 removes the fourth
  harmonic by over 25 dB; a hard hammer's ninth harmonic is over 6 dB above a soft one's.
- Bow: sustained (0.12 rms at 0.5-1 s and 0.19 at 2-3 s with a 0.3 s decay), in tune within 15 cents (+6.3), silent after release, bounded, no DC, sounding at every
  pressure and velocity, level follows velocity.
- Voices, stealing, sustain pedal, CC 120 and 123, velocity, sample-accurate events, block-size independence, every one of the 14 controls having an audible effect;
  400 random patches with random notes (down to 20 Hz) at 44.1, 96 and 192 kHz: no NaN or inf, output below 40.
- CPU on a busy machine: 0.8 % of a core for one note, 6 % for eight (plucked, bowed, or with pickup, maximum stiffness and body).

## Not done

- One string per voice: no sympathetic strings, no strumming of several strings, no string-to-string coupling.
- The bow's amplitude is nearly independent of speed (the friction curve saturates), so Velocity scales the output instead of the bow speed; no bow noise.
- The loop is damped per pass, so the same Damping is stronger on high notes; there is no frequency-dependent T60 curve.
- Stiffness is a fixed four-section cascade; the delay at the lowest notes is capped by the delay-line size (16384 samples: 20 Hz at 192 kHz is the limit).
- Not listened to; no host validation (REAPER).
