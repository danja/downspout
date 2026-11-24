# MIDI note-on velocity support

Scope: plugins that receive incoming note-on messages. This document makes explicit
what `data2` actually does. It is intentionally source-linked: if behaviour changes,
update the table and the cited handler in the same commit.

## Terms

- **Full** — incoming velocity changes the produced sound or emitted note.
- **Partial** — only some voices/note paths use velocity.
- **Modifier** — velocity changes a parameter or generative weight rather than note level.
- **Gate only** — velocity is read only to distinguish note-on from note-off.
- **Generated** — the plugin uses its own Velocity/Energy parameter for emitted notes;
  incoming velocity is not used.
- **Preserved** — the byte is passed through or recorded unchanged, but not interpreted.
- **N/A** — no incoming note-on action.

Velocity 0 is treated as note-off by most instruments. Floozy is the deliberate
exception: a 0-velocity note-on starts a silent note.

## Instruments

| Plugin | Support | What velocity does | Evidence |
|---|---|---|---|
| basilico | Full | Accent (`punch_`) and voice level | `plugins/basilico/src/basilico_engine.cpp:398-438,464-488,573` |
| bore | Full | Scales mouth pressure and voice level through the **Velocity** sensitivity control (default 65 %) | `plugins/bore/src/bore_engine.cpp:106-116,279,366`; direct test `plugins/bore/tests/bore_core_tests.cpp:146-150` |
| bubbles | Modifier | New-bubble envelope amplitude; note number shifts size | `plugins/bubbles/src/bubbles_engine.cpp:201,221,339-357` |
| campione | Full | Sample playback gain | `plugins/campione/src/campione_engine.cpp:156-184,314` |
| canticle | Full | Voice amplitude and phase seed | `plugins/canticle/src/canticle_engine.cpp:332,430,569-590,625-631` |
| clatter | Full | Linear velocity-to-voice gain | `plugins/clatter/src/dpf/ClatterPlugin.cpp:142-154`, `plugins/clatter/src/clatter_engine.cpp:49-57,81,267` |
| drumkit | Full | All eleven voices use `vel/127`; clap, crash and hats now scale with the other hits | `plugins/drumkit/src/drumkit_engine.cpp:112-178`, `plugins/drumkit/include/voices/ClapVoice.hpp:92,135`, `plugins/drumkit/include/voices/CrashVoice.hpp:88,119`, `plugins/drumkit/include/voices/HiHatVoice.hpp:95,137`; test `plugins/drumkit/tests/drumkit_core_tests.cpp:111` |
| floozy | Full | Attack transient and excitation level; not a note-off at 0 | `plugins/floozy/src/floozy_engine.cpp:163-168,207,581-595,855-857`; test `plugins/floozy/tests/floozy_core_tests.cpp:155-168` |
| fontana | Full | SoundFont velocity-layer selection and output gain | `plugins/fontana/src/fontana_engine.cpp:438,481-510,556-574,690` |
| fathom | Full | Gain `0.6 + 0.4 * vel/127` | `plugins/fathom/src/fathom_engine.cpp:105-137,207-238,406` |
| gremlin | Full | Transient and output gain | `plugins/gremlin/src/gremlin_processor.cpp:503-524`, `plugins/gremlin/include/GremlinEngine.hpp:156-167,301` |
| magneto | Modifier | Velocity sets **Throttle**, changing RPM/engine noise rather than note amplitude | `plugins/magneto/src/dpf/MagnetoPlugin.cpp:296-315`, `plugins/magneto/include/magneto_params.hpp:151-155` |
| monochord | Full | Blended into string level by **Velocity** (default 70 %) | `plugins/monochord/src/monochord_core.cpp:77-98,127-134,339` |
| mosaic | Full | Slice gain `0.25 + 0.75 * vel/127`; autonomous triggers use a fixed 96 | `plugins/mosaic/src/mosaic_core.cpp:48,58,62`; test `plugins/mosaic/tests/mosaic_core_tests.cpp:205` |
| moka | Full | Partial amplitude `0.25 + 0.75 * vel/127` and mallet transient; velocity also seeds the burst | `plugins/moka/src/moka_engine.cpp:78-81,175-182,221,237,466-493`; test `plugins/moka/tests/moka_core_tests.cpp:185-196` |
| paunchlad | Full | Launchpad press strength scales generated audio level and sends | `plugins/paunchlad/src/paunchlad_processor.cpp:235-244,284,321-460` |
| plank | Full | Pluck gain `0.25 + 0.75 * vel/127`; MIDI-thru note-ons preserve the incoming velocity | `plugins/plank/src/plank_core.cpp:381,405,455,817`; test `plugins/plank/tests/plank_core_tests.cpp:296` |
| pratt | Full | Gain `pow(vel/127, 1.35)` and brightness bucket | `plugins/pratt/src/pratt_engine.cpp:285-341,418-428` |
| quartet | Full | Voice gain through the **Velocity** amount (can be disabled) | `plugins/quartet/src/quartet_core.cpp:191,588-589` |
| resonance-garden | Full | Wet resonator gain `0.25 + 0.75 * vel/127`; internal fallback voices stay at unit gain | `plugins/resonance-garden/src/resonance_garden_core.cpp:18-28`; test `plugins/resonance-garden/tests/resonance_garden_core_tests.cpp:137` |
| sam | Full | Gain `0.25 + 0.75 * vel/127` | `plugins/sam/src/sam_engine.cpp:202-211,318-343` |
| snatch | Full | Output level | `plugins/snatch/src/dpf/SnatchPlugin.cpp:51-54`, `plugins/snatch/src/snatch_engine.cpp:26,52` |
| strata | Full | Blended into wavetable level by **Velocity** (default 70 %) | `plugins/strata/src/strata_core.cpp:127,252-253,540` |
| syrinx | Full | Voice pressure and per-syllable velocity | `plugins/syrinx/src/syrinx_engine.cpp:128,190-193`, `plugins/syrinx/include/SyrinxVoice.hpp:355` |
| thumper | Full | Hit level | `plugins/thumper/src/thumper_engine.cpp:34,105` |
| trajectory | Full | Velocity gain scaled by the **Velocity** sensitivity control | `plugins/trajectory/src/trajectory_engine.cpp:94-108,176-192` |
| wheelhouse | Partial | Key click and percussion scale with velocity; drawbar sustain stays velocity-independent, as on an organ | `plugins/wheelhouse/src/wheelhouse_core.cpp:18-20,212-224,268`; test `plugins/wheelhouse/tests/wheelhouse_core_tests.cpp:374` |

The six plugins that previously ignored or partly used velocity (mosaic,
resonance-garden, drumkit, plank, wheelhouse and bassgen) now have direct core
tests. No host parameters, CC mappings or audio/MIDI routing changed, so their
`profile.ttl` files are unchanged.

`tuney-vst` is listed as an instrument in the capability reference, but its notes come
from the editor/parameters, not host MIDI input, so incoming velocity is N/A.

## Note-on effects, routers and grids

These plugins detect note-on but deliberately use it as a gate, pitch source or grid
press. Velocity is not a dynamic control unless stated.

| Plugin | Support | Note-on means | Evidence |
|---|---|---|---|
| gater | Gate only | Note number selects one of two audio outputs; velocity is never read | `plugins/gater/src/dpf/GaterPlugin.cpp:26-30` |
| harmonic-atlas | Generated | Follows root pitch only; emitted velocity is tension-derived | `plugins/harmonic-atlas/src/harmonic_atlas_core.cpp:256-257,318-320` |
| lifeform | Generated | Launchpad cell toggle; emitted velocity comes from the Velocity parameter | `plugins/lifeform/src/lifeform_processor.cpp:373-376,845-850` |
| luma | Generated | Launchpad cell toggle; emitted velocity comes from Energy/cell state | `plugins/luma/src/luma_processor.cpp:274-277,654-657` |
| markov | Generated | Pitch-class learning only; emitted velocity comes from the Velocity parameter | `plugins/markov/src/markov_core.cpp:428-439,529-531` |
| melgen | Generated | Follow pitch only; emitted velocity comes from the pattern | `plugins/melgen/src/melgen_engine.cpp:70-71,106-129,149` |
| oracle | Gate only | Note count/pitch feed analysis; response velocity comes from audio level | `plugins/oracle/src/oracle_core.cpp:12-19` |
| orchid | Gate only | Note pitch changes playback rate | `plugins/orchid/src/orchid_engine.cpp:776-784` |
| padseq | Generated | Launchpad cell toggle; emitted velocity comes from the Velocity parameter | `plugins/padseq/src/padseq_core.cpp:436-440` |
| sieve | Gate only | Held note selects the spectral mask | `plugins/sieve/src/sieve_engine.cpp:122-127` |
| sinevox | Gate only | Held-note stack transposes formants; emitted formant velocity is amplitude-derived | `plugins/sinevox/src/sinevox_engine.cpp:484-503` |
| spindown | Gate only | Any held note stops the tape | `plugins/spindown/src/spindown_core.cpp:143-148` |
| sprout | Generated | Held/latched pitch only; emitted velocity comes from sequence state | `plugins/sprout/src/sprout_core.cpp:436-437,566-568` |
| zmorph | Gate only | Note number maps to cutoff | `plugins/zmorph/src/dpf/ZmorphPlugin.cpp:105-110` |

## MIDI processors and generators

| Plugin | Support | What velocity does | Evidence |
|---|---|---|---|
| arpgen | Modifier | Averaged held velocity blends toward 96 by `velocityFollow` (default 0.8) | `plugins/arpgen/src/arpgen_core.cpp:225-226,264-279,420-421` |
| bassgen | Partial | Blends into follow-injected notes and matched pattern-note boundaries when Dodge responds; unmatched notes keep pattern velocity | `plugins/bassgen/src/bassgen_engine.cpp:323-325,365-371`; test `plugins/bassgen/tests/bassgen_core_tests.cpp:1141` |
| cadence | Modifier | Onset capture weighting `0.35 + 0.65 * vel/127` | `plugins/cadence/src/cadence_engine.cpp:400,746-785` |
| counterpointer | Full | Weighted capture flows into generated velocity via `velocity_follow` | `plugins/counterpointer/src/counterpointer_engine.cpp:448-453,480-484,669-674,1228-1237` |
| m-mix | Full | Outgoing velocity is input velocity scaled by the Euclidean gate when Velocity Fades is on | `plugins/m-mix/src/m_mix_engine.cpp:186-214` |
| midiscribe | Preserved | Recorded to SMF and re-emitted unchanged | `plugins/midiscribe/src/dpf/MidiscribePlugin.cpp:286-298,308-311`, `plugins/midiscribe/src/SmfWriter.hpp:122-131` |
| mnemosyne | Full | Captured velocity is stored with the phrase and replayed | `plugins/mnemosyne/src/mnemosyne_core.cpp:34-37,46` |
| qune | Generated | Held notes only; emitted velocity comes from **Out Velocity** (default 100) | `plugins/qune/src/qune_engine.cpp:82,295` |
| retune | Preserved | Passed through to the retuned note-on, clamped to 1..127 | `plugins/retune/src/retune_engine.cpp:46-68` |
| sidecar | Modifier | Mixed into the generation seed, not into output note velocity | `plugins/sidecar/src/dpf/SidecarPlugin.cpp:804-813` |

## Pass-through only

Incoming velocity survives unchanged when pass-through is enabled, but the plugin does
not interpret it: `flues-synth-driver`, `ghost`, `gremlin-driver`, `loopdelay` and
`lightverb`. Gremlin-driver's own generated note-ons are hard-coded to 127.

## Not applicable

CC-only or clock-only plugins ignore note-on entirely. Output-only generators and
plugins with no MIDI input also have no incoming velocity to handle. See
`docs/pages/plugins-summary.md` for the complete port and MIDI-role list.
