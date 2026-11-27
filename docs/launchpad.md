# Launchpad integration notes

These notes capture the current Downspout assumptions for Novation Launchpad
Mini MK3 / X style programmer-mode hardware.

## Programmer mode layout

The `flues/lv2/padseq` mapping is the reference.

- Enter programmer mode with SysEx:
  `F0 00 20 29 02 0D 0E 01 F7`
- The 8x8 grid uses note-on/note-off messages.
- Grid row 0 is the bottom hardware row.
- Grid row 7 is the top hardware row.
- Grid notes:
  - row 0: `11..18`
  - row 1: `21..28`
  - row 2: `31..38`
  - row 3: `41..48`
  - row 4: `51..58`
  - row 5: `61..68`
  - row 6: `71..78`
  - row 7: `81..88`
- Right side buttons are CCs `19, 29, 39, 49, 59, 69, 79, 89`.
- Top buttons are CCs `91..98`; `99` is the logo button on Mini MK3 style
  mappings.

UI grids should normally draw row 0 at the bottom and row 7 at the top, matching
`padseq`.

## LED messages

In programmer mode, the Launchpad treats normal MIDI messages as LED commands:

- note-on channel 1 (`0x90`) sets static grid LED colour;
- note-on channel 2 (`0x91`) flashes grid LED colour;
- note-on channel 3 (`0x92`) pulses grid LED colour;
- CC channel 1 (`0xB0`) sets static side/top button LED colour;
- velocity/value `0` turns an LED off.

Controller release messages can therefore look like LED-off commands if echoed
back to the device. Launchpad pad/control input should be consumed, not echoed,
unless a plugin intentionally wants to use it as LED output.

Plugins that use note/CC LED messages still need generated musical MIDI to avoid
channels 1-3. PadSeq follows Plank: LED Feedback defaults on and **LED Out**
defaults to `Note+CC`, with generated notes on channel 4. Its `SysEx` mode paints
the same colours in one bulk message, which is the workaround when a synth shares
the MIDI output and must not hear channel-1 LED notes/CCs. Lifeform also defaults
musical output to channel 4 for the legacy note/CC case.

## Clearing and panic

In `Note+CC` mode, `padseq` clears the surface two ways: it sends the bulk
LED-lighting SysEx below, then mirrors the reset with channel-1 note-on/CC LED-off
messages so the blackout survives hosts that filter SysEx:

- header: `F0 00 20 29 02 0D`
- command: `03`
- repeated triplets: lighting type `00`, LED id, colour `00`
- terminator: `F7`

For the Mini MK3 programmer layout, a full clear should cover all 64 grid notes,
the 8 side-button CC ids, and the 9 top/logo CC ids. PadSeq also suppresses LED
feedback after a clear until the user edits or runs the pattern again, so a
periodic refresh cannot re-light stale buttons.

In `SysEx` mode both refresh and clear use only the bulk message below, so no
channel-1 note/CC LED traffic reaches a synth sharing the output. With the
default `Note+CC` mode, that synth must listen on the generated-note channel
(default 4) or be fed through a filter that drops channels 1-3. The vendored DPF
VST3 bridge has been approved for local SysEx forwarding so mode and clear SysEx
can reach the controller.

## DPF/VST3 note

DPF's VST3 bridge converts outgoing DPF MIDI events to VST3 note/CC-style
events. In this repository the vendored DPF bridge has been explicitly approved
for a local SysEx forwarding patch so programmer-mode and bulk LED SysEx can be
sent as VST3 data events. Future shared/framework changes still require explicit
approval before implementation.

## Routing

For VST3 plugins with one MIDI output carrying both musical MIDI and LED
feedback, the host must route that output both to the target instrument and to
the Launchpad hardware. Plank and PadSeq keep generated notes on channel 4 (or
the configured channel), leaving channels 1-3 for LEDs. Set the instrument to
listen on that channel, filter the LED channels before it, or set PadSeq's LED
Out to `SysEx` so routine lighting and clear never emit channel-1 notes/CCs.
