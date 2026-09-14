# Mistakes log

- 2026-09-09: `pkill -f "<pattern>"` hung the persistent shell session because
  the pattern also matched the shell's own command line. Prefer `kill <pid>`
  after resolving PIDs with `ps`, or use patterns that cannot match the
  invoking command.
- 2026-09-09: `pgrep -f "<pattern>"` in `$(...)` has the same self-match trap:
  the pattern string appears in the invoking command line, so pgrep can
  return the caller itself and a following `kill` destroys the session. Use
  the bracket trick (`pgrep -f "foo[.]bar"`) or resolve PIDs via `ps`.
- 2026-09-09: `scripts/capture-plugin-screenshots.sh moka` captured the
  terminal instead of the plugin window because `xdotool search --name moka`
  matched the terminal title. When the operator terminal contains the plugin
  name, capture by explicit window id (`xwininfo -root -tree` filtered by
  PID, then `import -window <id>`).
- 2026-09-14: Commit c48069e ("minor fixes & tweaks") zeroed the gremlin
  defaults for `chaos` (0.38), `crunch` (0.18), and `stutter` (0.20) without
  re-running the plugin core tests. Those three parameters are what drive the
  per-mode timbre differences, so `downspout_gremlin_core_tests` began failing
  its mode-separation assertion (Servo vs Ring measured 0.0285 against a 0.035
  threshold) and stayed broken on `main` until CI surfaced it. Root cause was
  twofold: a parameter-default change was not treated as a behavioural change
  worth testing, and the test itself compared modes with a hand-weighted scalar
  metric (rms/zero-crossings/side/peak/roughness) whose values for every mode
  pair sat in a 0.03-0.04 band -- the threshold was inside the metric's own
  scatter, so any default tweak could flip it. Prevention: run the affected
  plugin's core tests after changing any parameter default, and prefer
  discriminators with real margin. The mode/scene checks now compare coarse
  half-octave log-magnitude spectra with a mean-removed cosine distance, which
  separates every mode pair by ~2x the threshold and is stable across -O0..-O3
  and -ffast-math.
