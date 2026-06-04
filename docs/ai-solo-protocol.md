# Downspout AI Solo Protocol v1

Protocol id: `downspout.ai_solo.v1`

This protocol is the contract between `downspout-ai-coordinator` and a remote
LLM used to generate Sidecar solo phrases.

## Role

Generate a short monophonic MIDI phrase for Sidecar. The phrase should sound
like a solo line, fill, or answering phrase over the supplied tune state.

The model must return only one JSON object. No Markdown, no explanation, no
code fence, no comments.

## Input

The coordinator sends:

- `protocol`: must be `downspout.ai_solo.v1`.
- `task`: `generate_midi_phrase`.
- `instructions`: hard behavior rules.
- `tune_state`: compact musical state.
- `response_schema`: required JSON shape.

### Tune State

```json
{
  "key": 0,
  "scale": "major",
  "genre": "jazz",
  "tempo": 120,
  "bars": 4,
  "beats_per_bar": 4,
  "channel": 1,
  "register_low": 60,
  "register_high": 84,
  "density": 0.55,
  "risk": 0.35,
  "seed": 1,
  "midi_context": true,
  "guide_pitch_classes": [0, 4, 7]
}
```

`key` and `guide_pitch_classes` are pitch classes, where C is `0`, C# is `1`,
and B is `11`.

## Output

Return exactly:

```json
{
  "version": 1,
  "bars": 4,
  "beats_per_bar": 4,
  "events": [
    {"beat": 0.0, "duration": 0.5, "note": 64, "velocity": 88}
  ]
}
```

## Hard Rules

- Return only JSON matching the response schema.
- Use beat positions relative to the phrase start.
- Keep events monophonic, sorted by `beat`, and non-overlapping.
- Keep every note between `tune_state.register_low` and
  `tune_state.register_high`.
- Keep every event inside `bars * beats_per_bar`.
- Prefer guide pitch classes on strong beats when supplied.
- Use at least four distinct MIDI notes when the register span allows it.
- Do not repeat the same MIDI note more than twice in a row.
- Do not return a phrase consisting of one repeated note.

## Musical Guidance

- Make a contour: rise, fall, answer, or arc.
- Use rests; do not fill every possible subdivision unless density is high.
- Match density roughly to `tune_state.density`.
- Higher `risk` may use wider leaps and chromatic neighbor tones.
- Lower `risk` should stay closer to guide pitch classes and scale tones.
- Jazz output should prefer chord/guide tones on strong beats and passing tones
  on weak beats.

## Coordinator Validation

The coordinator treats model output as untrusted. It parses the first JSON
object, validates bounds/timing/monophony, constrains notes into the requested
register, and repairs low-variety pitch output before returning the phrase to
Sidecar or writing MIDI.
