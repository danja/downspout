# Mistakes log

Open or new entries only. Resolved lessons are promoted to "Lessons from past mistakes"
in `CLAUDE.md` and removed from here. Format: what happened, root cause, prevention.

## Open

- **Screenshot script captures the real desktop.** `scripts/capture-plugin-screenshots.sh`
  only starts Xvfb when `DISPLAY` is empty, so from a session with `DISPLAY` set it
  screenshotted the user's browser (twice, 2026-10-07 and 2026-10-08, overwriting
  `pratt.png`). Prevention is in CLAUDE.md (`env -u DISPLAY`). Remove this entry once the
  script always forces Xvfb (shared glue, needs approval; tracked in TODO.md under Pratt).
