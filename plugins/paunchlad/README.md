# PaunchLad

PaunchLad is a Launchpad-controlled dub performance effect. Put it on a snare,
drum bus, send return, or live input channel, then use the Launchpad for echo
throws, sirens, car alarms, thunder hits, monster growls, rewind zaps, spring
splashes, dropouts, and tape-style chops.

It is both practical and playable:

- stereo audio input/output passes the source through a dub delay path;
- Launchpad MIDI input triggers momentary performance actions;
- generated alarms, sirens, snare hits, crashes, subs, lasers, horns, bubbles,
  and monster effects are mixed into the audio output and delay;
- MIDI output sends Launchpad Mini Mk3 Programmer Mode LED feedback by default.

LED feedback follows the same approach as Luma: it is on by default so the
Launchpad enters programmer mode and the grid uses the reliable `11..88` pad
map. Leave it on when the plugin's MIDI output is routed directly back to the
Launchpad hardware. Turn it off when the plugin's MIDI output is routed onward
to instruments, because Launchpad LED updates are ordinary MIDI note/CC
messages and can otherwise play notes.

If the Launchpad and on-screen grid do not line up, use the `Map` button to
cycle the pad orientation. The same map is applied to input presses and LED
feedback.

PaunchLad accepts the Launchpad programmer grid notes (`11..88`) while LED
feedback is enabled. With LEDs disabled it falls back to a linear chromatic grid
(`36..99`) because that is the common simple MIDI-source layout.

## Grid layout

Visible top to bottom:

- row 1: panic, freeze, and reset gestures;
- row 2: car alarms, robot warnings, horns, and monster alarms, for example pad `(2,7)`;
- row 3: sirens, lasers, risers, and electrical zaps;
- row 4: echoing snare throws, cannon claps, thunder tails, for example pad `(4,3)`;
- row 5: spring splashes, bubbles, boings, and tone jumps;
- row 6: dry dropouts, sub drops, thunder hits, and crashes;
- row 7: rhythmic chops, stutters, rewind squeals, and zaps;
- row 8: dub echo throws, horn stabs, bubbles, and runaway repeats.

The top Launchpad buttons trigger common gestures and safety actions. The right
side buttons set siren/alarm level and fire matching alarm accents. The on-screen
`Map` button rotates the hardware mapping when the Launchpad orientation does
not match the UI.

## Reaper routing

1. Put `paunchlad.vst3` on an audio track or return track.
2. Send snare, percussion, vocal, or full drum bus audio into that track.
3. Enable the Launchpad MIDI input for that track, but avoid also routing the
   same Launchpad input directly to a synth track unless you want those notes.
4. Set the track's `MIDI Hardware Output` to the Launchpad output port for LED
   feedback. This is the recommended setup and matches Luma.
5. If you route PaunchLad's MIDI output onward to instruments instead, turn
   `LED Feedback` off.

For classic snare echo, put PaunchLad on an aux/return and send only the snare
or rimshot to it. For live dub mixing, put it on a drum bus and use the dry,
wet, feedback, and output sliders as the safety controls.

## Build

PaunchLad is enabled by default with `DOWNSPOUT_BUILD_PAUNCHLAD=ON`.

```bash
cmake -S ../.. -B ../../build -DDOWNSPOUT_BUILD_PAUNCHLAD=ON
cmake --build ../../build --target paunchlad-vst3 downspout_paunchlad_core_tests
ctest --test-dir ../../build --output-on-failure -R paunchlad
```

## Saving and restoring

The nine settings (dry, echo wet, feedback, tape tone, siren, spring, output, LED feedback
and pad map) are stored as one versioned text state, so a project reopens with the same mix.
Pads and panic are momentary and are never stored, and a state naming one is refused. A
corrupt or future-version state is rejected whole and leaves the current settings untouched.
Starting playback no longer resets the settings, and a sample-rate change resizes the echo
without touching them.
