# LYNX plugin survey (2026-11-17)

Farelo task `t20259e38295b` asked for a survey of <https://github.com/Ka1-S/LYNX>
for potential Downspout plugins and an overlap report. The repository now serves
the **LYNX SHARE** documentation; this survey used the current
[`docs/PLUGINS.md`](https://github.com/Ka1-S/LYNX/blob/main/docs/PLUGINS.md)
(raw `main`) and the repository README/STATUS pages. It is a read-only study:
no upstream code, presets, graphics or audio were copied.

## Summary

LYNX ships 32 generator engines and 57 effects, mostly clean-room FL Studio
natives plus clean-room/fitted engines inspired by FabFilter, Valhalla, Waves,
Soundtoys and SSL. Most of its distinctive mixing tools already have a
Downspout equivalent in a different form. The useful gaps are a few focused
audio processors, not a reason to clone its FL compatibility engines.

## Candidates worth considering

1. **Level-independent transient designer** (LYNX *Strike Shaper*, after SPL
   Transient Designer). Attack ±15 dB and Sustain ±24 dB from two envelopes
   with no threshold. Downspout's `spliff` finds and cuts/boosts transients in
   three bands, and `helterskelter` is an envelope wah, but neither offers a
   threshold-free attack/sustain pair. Best as a small new effect or a mode in
   `spliff`; keep the level-independent behaviour, which is the point.
2. **One-knob multiband denoiser with Listen** (LYNX *Quell Denoiser*, after
   Waves NS1). Six phase-coherent bands, per-band downward expanders, fast
   open/slower close, linked stereo, and a difference output. This overlaps the
   existing noise-reduction TODO in `TODO.md`; that plan is more ambitious
   (STFT, 512 bands, musical-noise suppression). Quell suggests a cheaper first
   milestone: a fixed-band, learnable noise floor with Listen.
3. **Stereo matrix / image shaper** (LYNX *Stereo Shaper* / *Stereo Enhancer*).
   Per-side routing into L/R, inter-channel delay and phase, and mono-to-wide
   width. Downspout has `bassops` mid/side width, `chorale` Width, `spense`
   drift and `octet` M/S bands, but no dedicated, legible stereo matrix. This
   is also the existing "stereo enhancer" TODO; the LYNX controls are a good
   UI reference, not an implementation to copy.
4. **Multitap modulated delay** (LYNX *Lattice Mod*, after Valhalla UberMod):
   1-16 taps with spread/slope/skew, per-tap detune and vibrato, rotating stereo
   feedback and diffusion. `prism` delays bands, `rift` chops and `loopdelay`
   captures, but none exposes a tap garden. A bounded 8-tap version would fit
   the Downspout panel style.
5. **Frequency-shifting echo** (LYNX *Spiral Echo*): the shift accumulates on
   each repeat, with opposite shifts in the stereo sides. `fathom` already puts
   a frequency shifter in a delay feedback loop, but as a drone instrument; an
   audio insert would be a distinct, compact effect.
6. **Optical leveler** (LYNX *Glow Leveler*): an electro-optical cell model with
   ~10 ms attack and a two-stage release. `silken` has a conventional
   compressor/leveller, `glue` a bus compressor and `guardian` a limiter; the
   LA-2A-style program-dependent release remains uncovered.
7. **Vocal doubler** (LYNX *Gemini Doubler*): two pitch-shifting grain voices
   with independent delay, detune, drift, pan and feedback. This is one of the
   vocal extras already listed as left out of `silken`; `chorale` covers the
   chorus part but not independent double-tracking.
8. **SF2/SF3 SoundFont player and slice-based sampler** (LYNX *Soundfont*,
   *Slicex*, *FPC*, *DirectWave*). Downspout has no sample-playback instrument,
   so this is a genuine category gap rather than an overlap. It is also a much
   larger build than the effects above and would need a file-state and
   sample-loading design before UI work.

## Strong overlap with existing Downspout plugins

| LYNX engine / role | Downspout equivalent | Decision |
|---|---|---|
| Apex Limiter, Emphasis, L1-L4 | `guardian` | Covered. |
| Crest Compressor, Fruity Compressor, SSL bus comp | `glue`, `trio`, `silken` | Covered. |
| Prism Multiband, Fruity Multiband, Maximus | `trio`, `octet` partially | Covered; dynamic EQ per band is a possible `octet` feature, not a new plugin. |
| Sentry Gate, Fruity Limiter's gate | `gater` and the `gate` TODO | Covered or already planned. |
| Facet EQ, Parametric EQ, 7-band EQ | `octet` | Covered; Facet's draggable analyser matches `octet`'s panel. |
| Hush De-esser, Waves DeEsser | `silken` de-esser | Covered. |
| Ember Saturator, Fast Dist, WaveShaper, Blood Overdrive | `damiano`, `skream`, `nam` | Covered. |
| Epoch Verb, Atrium, Foil Plate, Nebula, Halo, Horizon | `heavyverb`, `lightverb`, `ambo` | Covered; Shimmer is a `heavyverb` mode. |
| Echoform, Delay 2/3/Bank, Fruity Delay | `loopdelay`, `prism`, `rift`, `spindown` | Covered, except the dedicated controls in candidates 4 and 5. |
| Orbit, Lattice Mod, chorus/flanger family | `chorale`, `spense`, `fathom` | Partly covered; see candidate 4. |
| Ridge Strip, Glow Leveler, Aether | `silken` (vocal strip), `glue` | Vocal chain covered; opto and console strip remain partial. |
| LYNX Tune, Pitcher, Newtone | `qune`, `sinevox` | Covered for single-pitch inserts; Newtone's offline note editing is a different product. |
| Vocoder, Vocodex | `sinevox` | Covered. |
| Gross Beat, Transporter, Fruity Slicer | `rift`, `interleaver`, `fresco` | Partly covered: slicing and stutter exist, but not LYNX's exact time/volume grid. |
| Effector, Spreader, PanOMatic, auto-pan | `infra`, `spense`, `p-mix`, `e-mix` | Covered by Downspout's modulation/gate tools. |
| 3x Osc, MiniSynth, Sawer, Sytrus, Harmor, FLEX | `strata`, `quartet`, `basilico`, `monochord` | Covered as broad timbral categories. |
| Fruit Kick, Drumaxx, FPC, DrumKit natives | `drumkit`, `thumper`, `snatch`, `tops`, `clatter` | Covered. |
| 3x Osc/FX presets and `.flp` compatibility | none | Not a goal: Downspout is VST3-native and generative, not an FL import host. |

## Recommendation

Do not chase LYNX's FL compatibility engines. Of the gaps above, the
transient designer, stereo matrix and band-limited denoiser are the smallest
well-defined additions and fit the existing `spliff`/new-plugin/`TODO.md`
paths. The sampler/SoundFont player is the largest and should wait for an
explicit scope decision. This survey only records candidates; it does not
authorise vendoring or porting upstream material.
