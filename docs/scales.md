# Downspout Scale Reference

This is the canonical reference for scales used across Downspout plugins. **All scale-related decisions — adding new scales, mapping UI labels, writing tests — must be consistent with this document.** See the append-only rule below before touching any scale enum.

## Append-only rule

Scale enums are serialised as integers in host project state. Inserting a scale anywhere except the end silently reinterprets every saved project. The stability tests in bassgen and counterpointer enforce this: they pin the integer values of specific scales and will fail if anything is inserted before them.

**Never insert a scale in the middle of an existing enum. Always append at the end (before `count`/`SCALE_COUNT`).**

## Canonical list

The table below is the authoritative superset. Ordinal 0 starts at `chromatic` (present in counterpointer, lifeform, and bassgen subsets that begin there). Not every plugin exposes every row — see the per-plugin table.

| # | Canonical name | camelCase symbol | SCALE_ constant | Display name |
|---|---|---|---|---|
| 0 | chromatic | `chromatic` | `SCALE_CHROMATIC` | Chromatic |
| 1 | major | `major` | `SCALE_MAJOR` | Major |
| 2 | ionian | `ionian` | `SCALE_IONIAN` | Ionian |
| 3 | minor (natural) | `minor` | `SCALE_NAT_MINOR` | Minor |
| 4 | harmonicMinor | `harmonicMinor` | `SCALE_HARM_MINOR` | Harmonic Minor |
| 5 | melodicMinor | `melodicMinor` | `SCALE_MELODIC_MINOR` | Melodic Minor |
| 6 | dorian | `dorian` | `SCALE_DORIAN` | Dorian |
| 7 | phrygian | `phrygian` | `SCALE_PHRYGIAN` | Phrygian |
| 8 | lydian | `lydian` | `SCALE_LYDIAN` | Lydian |
| 9 | mixolydian | `mixolydian` | `SCALE_MIXOLYDIAN` | Mixolydian |
| 10 | locrian | `locrian` | `SCALE_LOCRIAN` | Locrian |
| 11 | phrygianDominant | `phrygianDominant` | `SCALE_PHRYGIAN_DOMINANT` | Phrygian Dominant |
| 12 | neapolitanMajor | `neapolitanMajor` | `SCALE_NEO_MAJOR` | Neapolitan Major |
| 13 | neapolitanMinor | `neapolitanMinor` | `SCALE_NEO_MINOR` | Neapolitan Minor |
| 14 | pentMajor | `pentMajor` | `SCALE_PENT_MAJOR` | Pentatonic Major |
| 15 | pentMinor | `pentMinor` | `SCALE_PENT_MINOR` | Pentatonic Minor |
| 16 | pentatonic | `pentatonic` | — | Pentatonic |
| 17 | blues | `blues` | `SCALE_BLUES` | Blues |
| 18 | wholeTone | `wholeTone` | `SCALE_WHOLE_TONE` | Whole Tone |
| 19 | altered | `altered` | `SCALE_ALTERED` | Altered |
| 20 | halfWholeDiminished | `halfWholeDiminished` | `SCALE_HALF_WHOLE_DIMINISHED` | Half-Whole Diminished |
| 21 | wholeHalfDiminished | `wholeHalfDiminished` | `SCALE_WHOLE_HALF_DIMINISHED` | Whole-Half Diminished |
| 22 | bebopDominant | `bebopDominant` | `SCALE_BEBOP_DOMINANT` | Bebop Dominant |
| 23 | bebopMajor | `bebopMajor` | `SCALE_BEBOP_MAJOR` | Bebop Major |
| 24 | bebopMinor | `bebopMinor` | `SCALE_BEBOP_MINOR` | Bebop Minor |

Row 16 (`pentatonic`) is a generic single-scale entry used by lifeform and luma in place of the separate pentMajor/pentMinor pair. New plugins should prefer the explicit pair (14/15).

## Per-plugin scale coverage

`✓` = included. Ordinals shown are the plugin's own local integer value (which may differ from the canonical table above if the plugin's enum starts at a different offset or omits earlier rows).

| Canonical name | bassgen | melgen | ground | counterpointer | cadence | arpgen | lifeform | luma |
|---|---|---|---|---|---|---|---|---|
| chromatic | — | — | — | 0 | 0 | — | 0 | — |
| major | 0 | 0 | 0 | 1 | 1 | 0 | 1 | 0 |
| ionian | 1 | 1 | 1 | 2 | 2 | 1 | 2 | 1 |
| minor | 2 | 2 | 2 | 3 | 3 | 2 | 3 | 2 |
| harmonicMinor | 3 | 3 | 3 | 4 | 4 | 3 | — | — |
| melodicMinor | 4 | 4 | 4 | 5 | 5 | — | — | — |
| dorian | 5 | 5 | 5 | 6 | 6 | 4 | 4 | 3 |
| phrygian | 6 | 6 | 6 | 7 | 7 | — | — | — |
| lydian | 7 | 7 | 7 | 8 | 8 | 6 | — | — |
| mixolydian | 8 | 8 | 8 | 9 | 9 | 5 | 5 | 4 |
| locrian | 9 | 9 | 9 | 10 | 10 | — | — | — |
| phrygianDominant | 10 | 10 | 10 | 11 | 11 | 7 | — | 7 |
| neapolitanMajor | 11 | 11 | 11 | 12 | 12 | 8 | 8 | 8 |
| neapolitanMinor | 12 | 12 | 12 | 13 | 13 | 9 | 9 | 9 |
| pentMajor | 13 | 13 | 13 | 14 | 14 | 10 | — | — |
| pentMinor | 14 | 14 | 14 | 15 | 15 | 11 | — | — |
| pentatonic | — | — | — | — | — | — | 6 | 5 |
| blues | 15 | 15 | 15 | 16 | 16 | 12 | 7 | 6 |
| wholeTone | 16 | 16 | 16 | 17 | 17 | — | — | — |
| altered | 17 | 17 | 17 | 18 | 18 | — | 10 | — |
| halfWholeDiminished | 18 | 18 | 18 | 19 | 19 | — | 11 | — |
| wholeHalfDiminished | 19 | 19 | 19 | 20 | 20 | — | 12 | — |
| bebopDominant | 20 | 20 | 20 | 21 | 21 | — | 13 | — |
| bebopMajor | 21 | 21 | 21 | 22 | 22 | — | 14 | — |
| bebopMinor | 22 | 22 | 22 | 23 | 23 | — | 15 | 15 |
| **count** | **23** | **23** | **23** | **24** | **24** | **13** | **16** | **16** |

## Naming conventions

Two conventions coexist in the codebase:

- **camelCase enum class** (`ScaleId::bebopMinor`): bassgen, melgen, ground, lifeform, luma. Preferred for new C++ plugins.
- **SCALE_ constants** (`SCALE_BEBOP_MINOR`): counterpointer, cadence, arpgen. Legacy style; do not use for new plugins.

When adding a scale selector parameter to a plugin, use the camelCase `enum class ScaleId` style and copy the subset from the nearest plugin with a similar feature set.

## Stability test pins

These assertions exist to catch mid-enum insertions. They must remain passing:

- `bassgen`: `ScaleId::minor == 2`, `ScaleId::bebopMinor == 22`, `ScaleId::count == 23`
- `counterpointer`: `SCALE_BEBOP_MINOR == 23`, `SCALE_COUNT == 24`

If a test pin fails after a scale change, the change was an insertion rather than an append — revert and append instead.
