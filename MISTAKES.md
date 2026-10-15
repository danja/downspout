# Mistakes log

Open or new entries only. Resolved lessons are promoted to "Lessons from past mistakes"
in `CLAUDE.md` and removed from here. Format: what happened, root cause, prevention.

## Open

- **Screenshot script captures the real desktop.** `scripts/capture-plugin-screenshots.sh`
  only starts Xvfb when `DISPLAY` is empty, so from a session with `DISPLAY` set it
  screenshotted the user's browser (twice, 2026-10-07 and 2026-10-08, overwriting
  `pratt.png`). Prevention is in CLAUDE.md (`env -u DISPLAY`). Remove this entry once the
  script always forces Xvfb (shared glue, needs approval; tracked in TODO.md under Pratt).

## Plank: repeated note fix killed the string (2026-10-15)
- **What happened:** "Second note doesn't play" was fixed by retriggering on a repeated note-on, but the host's overlapping order (on, on, off, off) meant the first note-off released the freshly retriggered string, so output still died.
- **Root cause:** Assumed the repeat was a missing note-off; never modelled the host's on-before-off ordering. Core tests only covered on/off in separate blocks.
- **Prevention:** For note handling, test the same-tick orderings a host can send (off-then-on, on-then-off) and count overlapping note-ons per voice.
