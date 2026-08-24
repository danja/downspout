# Sample Profiling System — Architecture Overview

**Status:** design, pre-implementation
**Audience:** development agent team
**Companion documents:** `01-descriptor-spec.md` (normative), `02-sample-profile-ontology.ttl` (normative)

---

## 1. Purpose

Given an audio sample — typically a single percussion hit, but the design does not
assume this — produce a compact, deterministic, machine-readable *profile*: a fixed
set of numeric descriptors serialised as RDF (Turtle).

Two consumers are in scope:

1. **Campione** (Downspout VST3 multi-zone sampler). When a set of drum samples is
   loaded, assign each to an appropriate General MIDI percussion note without user
   intervention. Constrained environment: portable C++, no heavyweight dependencies,
   runs at load time inside a plugin.
2. **A standalone library tool** (later). Profile large sample collections, store the
   RDF in a triplestore, query and cluster it, and eventually train a learned
   classifier.

---

## 2. Core architectural decision: descriptor layer / decision layer

The two use cases differ only in *how a profile is turned into a label*, not in what a
profile is. Therefore:

```
                 ┌──────────────────────────────────────────┐
   audio  ────►  │  DESCRIPTOR LAYER                        │  ────►  profile
                 │  deterministic, portable C++, no deps    │         (RDF / vector)
                 │  spec: 01-descriptor-spec.md             │
                 └──────────────────────────────────────────┘
                                                                          │
                              ┌───────────────────────────────────────────┤
                              ▼                       ▼                   ▼
                  ┌───────────────────┐  ┌────────────────────┐  ┌────────────────┐
                  │ DECISION LAYER A  │  │ DECISION LAYER B   │  │ DECISION LAYER │
                  │ rules + filename  │  │ LLM via MCP        │  │ C: trained     │
                  │ prior + Hungarian │  │ (oracle / dev aid) │  │ model          │
                  │ → Campione        │  │                    │  │ → standalone   │
                  └───────────────────┘  └────────────────────┘  └────────────────┘
```

**The descriptor layer is written once and shared verbatim.** It is a static library
with a C API and no dependency on the plugin framework, the RDF serialiser, or any
classifier. The standalone tool links the same object code.

### Non-negotiable consequences

- **Versioned extractor.** Every profile records the extractor version and the analysis
  parameters used, via PROV-O. Descriptors computed by different versions are not
  comparable. Without this, the training corpus silently rots as the extractor evolves.
- **Determinism.** Same input bytes + same extractor version ⇒ byte-identical Turtle
  output. No qualification: profiles use skolemised IRIs rather than blank nodes
  precisely so that this holds. This is a test requirement, not an aspiration.
- **No classification logic in the descriptor layer.** No thresholds, no instrument
  names, no MIDI. If a classifier needs a new number, it is added to the spec and the
  version bumped.

---

## 3. Components

### 3.1 `libsampleprofile` — descriptor extraction

Portable C++17, header + single translation unit where practical. Dependencies: an FFT
only (pffft or kissfft; Campione may already have suitable code behind its auto-pitch
detection — check before adding one). No RDF dependency at this layer; serd belongs to
§3.2 and the DSP core must build and test without it.

```c
sp_profile_t sp_analyse(const float* const* channels,
                        int   channel_count,
                        size_t frame_count,
                        double sample_rate,
                        const sp_params_t* params);   /* NULL = defaults */
```

Output is a plain struct of doubles — see §5 of the descriptor spec for the canonical
65-element flat vector. Runtime target: under 20 ms for a 2-second 44.1 kHz stereo
sample on modest hardware. Not real-time-safe; called from the loader thread.

### 3.2 `libsampleprofile-rdf` — Turtle reading and writing

Separate library so the DSP core stays dependency-free. Emits and consumes the shapes
defined in `02-sample-profile-ontology.ttl`.

**Use [serd](https://drobilla.net/software/serd).** ISC-licensed, no dependencies
beyond the C standard library, a few thousand lines, well under 100 KiB compiled,
tested on Linux/macOS/Windows with GCC, Clang and MSVC, and explicitly designed for use
inside plugins. Do not use Redland/librdf here: it drags in raptor2 and rasqal, which
can want libxml2, libcurl and PCRE, and its LGPL option makes static linking into a
VST3 binary a licensing conversation with no upside.

The decisive argument is the **reader**, not the writer. The `gm:alias` table (§3.3) is
loadable Turtle data, so the plugin must parse Turtle — prefixed names, escapes,
`@base` resolution, numeric literal forms. Hand-writing that is the worst option
available. serd passes the full Turtle and TriG test suites and is fuzz-tested.

Scope it tightly:

- **serd only, no sord.** No in-memory model is needed. Stream the alias file through
  the reader callback into an `unordered_map`; stream profiles out through the writer.
  No SPARQL in the plugin, so rasqal is irrelevant.
- **Number formatting is ours, not serd's.** The spec mandates 6-significant-figure
  output for cross-platform byte-identity. Format doubles in the extractor and pass
  them to serd as pre-serialised literal strings with explicit `^^xsd:double`. Do not
  hand serd a numeric value and accept whatever it prints.
- **Skolemised IRIs, no blank nodes.** Profile, band-energy and envelope-segment nodes
  get real IRIs derived from the sample IRI — `<urn:sha256:3f7a1c…#profile>`,
  `#band-0`, `#env-3`. Output becomes byte-deterministic and diffable in git, Fuseki
  ingest stops having blank-node identity problems on reload, and SPARQL over the
  corpus gets substantially easier. Cost: nothing.

If content-addressed profiles are wanted later, RDFC-1.0 canonicalisation exists, but
it is overkill for this shape of data.

On the standalone/corpus side, serd is not the natural choice — Fuseki is already in
the picture and the ML work will be Python, so rdflib or Jena for query-heavy tooling,
with the extractor's Turtle as the interchange format. Python bindings for serd exist
if identical parsing on both sides matters more than convenience.

### 3.3 Filename tokeniser

Independent of the DSP. Takes a filename, returns zero or more scored
`(gm:Instrument, confidence)` candidates.

In practice, filenames are the single strongest signal available: real-world packs are
`Kick_01.wav`, `CH.wav`, `OHH_soft.wav`, `SD_rim.wav`. A tokeniser plus alias table
gets a long way before any DSP runs. Treat the result as a **prior**, not an answer —
acoustic evidence confirms or overrides it, and the RDF records which evidence drove
the final call so that a wrong assignment is diagnosable.

Design notes:

- Split on `_`, `-`, space, camel-case boundaries, and digit runs.
- Case-fold; strip index numbers and velocity-layer suffixes (`v1`, `vel3`, `_127`).
- Alias table is **data, not code** — ship it as a Turtle file using `gm:alias`, so it
  can be extended without recompiling. Seed it from a survey of real sample packs.
- Beware ambiguity: `rim` (37 side stick vs. 38 snare rim), `perc`, `hat` without
  open/closed qualifier, `tom` without pitch. Emit multiple candidates with split
  confidence rather than guessing.

### 3.4 Rule classifier (Campione)

Maps a profile to a scored candidate list over the GM percussion set. Rules operate on
the descriptor vector; suggested first-cut discriminators, in rough order of value:

| Discriminator | Separates |
|---|---|
| `temporalCentroidNormalised`, `effectiveDuration` | short/decaying (kick, closed hat) vs. long ringing (crash, open hat, ride) |
| `spectralFlatness.mean` | noise-like (snare, hats, cymbals) vs. pitched (kick, toms, cowbell, congas) |
| `bandEnergy[0..1]` (20–120 Hz share) | kick vs. everything else |
| `dominantPartial.frequency` + `salience` | kick (40–90 Hz) vs. floor/mid/high tom (80–250 Hz) |
| `attackTailCentroidDelta` | pitch-swept membranes vs. static metal |
| `bandEnergy[6..7]` (2.5 kHz+ share) | hats and cymbals vs. drums |
| `envelopeSegment[*].rmsDb` shape | closed vs. open hat; gated vs. natural decay |
| `decayFitR2` | single-exponential (damped drum) vs. multi-modal (cymbal) |

Do not hand-tune thresholds against intuition. Tune them against the labelled eval set
(§4, step 5) and record the achieved accuracy per class.

### 3.5 Kit-level assignment

**Classify the kit, not the sample.** When *n* samples are loaded together,
independent per-sample classification will cheerfully put three files on note 36. Build
a cost matrix (samples × candidate GM slots, cost = negative log score) and solve the
assignment with the Hungarian algorithm. *n* is small; cost is negligible. This single
change removes the most visible failure mode.

Rules:
- Allow a sample to remain unassigned if all costs exceed a rejection threshold.
- Permit deliberate duplicates only where GM itself pairs them (35/36, 38/40, 49/57,
  51/59) and only when the scores are close.
- Single-sample loads bypass the solver and take the argmax.

### 3.6 MCP integration

Campione already embeds an MCP HTTP server. Add two tools:

- `profile_zone(zone_index) → { turtle, vector }`
- `classify_zone(zone_index, gm_note)` — apply/override an assignment

This gives, at near-zero cost, an LLM-as-classifier path: hand a client the descriptors
plus the filename and let it choose the note. Useful in its own right, and more useful
still as a cheap oracle for evaluating the rule classifier during development.

### 3.7 Corpus and learned classifier (later)

The deterministic extractor generates the corpus. Every time a user drags a zone to a
different note, that correction is a ground-truth label — emit it as
`sp:Classification` with `sp:evidence sp:UserAssignment` and push to Fuseki. After a few
thousand samples there is a training set with a schema already attached, queryable by
SPARQL for stratification and nearest-neighbour inspection.

The standalone application is then primarily a *graph* application — library-scale
query, dedupe, clustering, similarity search — with the trained model as one more
decision-layer plugin behind the same interface.

---

## 4. Build order

Each step is independently testable; do not start *n+1* before *n* passes.

1. **Freeze the descriptor spec.** Field names, units, FFT parameters, edge cases.
   Everything downstream depends on it. Reviewed and merged before code is written.
2. **`libsampleprofile`.** With a reference test corpus: synthetic signals (sine bursts,
   white noise bursts, exponentially decaying sines, silence, DC, clipped material) with
   analytically known descriptor values. Assert to 1e-9.
3. **Ontology file + serd reader/writer.** Validate output with `serdi` and
   `riot --validate`, plus a SHACL shapes file. Round-trip through Fuseki. Test the
   reader against a deliberately awkward alias file: non-ASCII tokens, escapes,
   `@base`, comments.
4. **Filename tokeniser + alias table**, with its own unit tests over a list of real
   pack filenames.
5. **Hand-label 100–200 samples** spanning at least four differently-produced packs
   (acoustic, 808/909, lo-fi, orchestral percussion). This is the eval set. It is the
   step that gets skipped and then regretted; it gates everything after it.
6. **Rule classifier + Hungarian assignment**, measured against the eval set. Report
   per-class precision/recall, not just overall accuracy — the overall figure is
   dominated by kick and snare.
7. **MCP tools**, Fuseki ingest, then the learned model.

---

## 5. Interfaces between teams

| Boundary | Contract |
|---|---|
| DSP ↔ everything | `sp_profile_t` struct and the 65-element vector ordering in spec §5 |
| Serialiser ↔ consumers | The ontology file; SHACL shapes derived from it; skolemised IRI scheme |
| Tokeniser ↔ classifier | `(IRI, confidence)` candidate list |
| Classifier ↔ Campione | GM note number per zone + confidence + evidence set |
| Campione ↔ store | Turtle over HTTP to Fuseki, graph per sample-source directory |

---

## 6. Open questions for the team

1. **FFT library.** Does Campione's existing pitch detector expose a reusable FFT? If so,
   what size/precision constraints does it impose?
2. **Existing Turtle code.** Campione already saves and loads patch files as Turtle.
   *What does that use?* If it is hand-rolled string emission, this project should not
   add a second, parallel mechanism — migrate the patch I/O onto serd at the same time
   and have one RDF path in the codebase. Check this before anyone writes serialiser
   code; it may change the size and shape of the task considerably.
3. **Sample identity.** Profiles need stable IRIs. Content hash (SHA-256 of the audio
   data, excluding metadata chunks) is proposed — it survives renaming and moving, which
   file IRIs do not. Confirm the cost is acceptable at load time.
4. **Non-percussive input.** The descriptor set is percussion-oriented but not
   percussion-only. Should the profiler flag "probably not a one-shot" (long duration,
   sustained envelope, multiple onsets) rather than emit misleading temporal features?
   Recommendation: yes — a boolean `sp:multipleOnsets` and `sp:sustained`, cheap to
   compute, prevents downstream nonsense.
5. **Namespace hosting.** `http://purl.org/stuff/sampleprofile/` and
   `http://purl.org/stuff/gm/` need PURL registration and content negotiation before
   first public release.
6. **Alignment verification.** The ontology asserts a small number of mappings to the
   Audio Features Ontology, Music Ontology and EBUCore. These are marked in the file and
   must be checked against the live vocabularies before release; do not assume the term
   names are correct as written.
