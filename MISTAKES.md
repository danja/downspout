# Mistakes log

- 2026-09-09: `pkill -f "<pattern>"` hung the persistent shell session because
  the pattern also matched the shell's own command line. Prefer `kill <pid>`
  after resolving PIDs with `ps`, or use patterns that cannot match the
  invoking command.
- 2026-09-09: `scripts/capture-plugin-screenshots.sh moka` captured the
  terminal instead of the plugin window because `xdotool search --name moka`
  matched the terminal title. When the operator terminal contains the plugin
  name, capture by explicit window id (`xwininfo -root -tree` filtered by
  PID, then `import -window <id>`).
