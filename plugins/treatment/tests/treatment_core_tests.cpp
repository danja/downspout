#include "treatment_core_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

static int gPassed = 0;
static int gFailed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        ++gPassed;
    } else {
        std::printf("FAIL: %s\n", name);
        ++gFailed;
    }
}

static bool nearlyEqual(const float a, const float b, const float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

using namespace downspout::treatment;

static constexpr std::uint32_t kFrames = 512;
static constexpr double kSr = 48000.0;

struct Block {
    std::array<float, kFrames> inL {}, inR {}, outL {}, outR {};
};

static void run(EngineState& s, const Parameters& p, Block& b)
{
    const float* ins[] = { b.inL.data(), b.inR.data() };
    float* outs[] = { b.outL.data(), b.outR.data() };
    processBlock(s, p, kFrames, kSr, ins, outs);
}

// RMS of a window, for level comparisons.
static float rms(const float* data, const std::uint32_t from, const std::uint32_t to)
{
    double sum = 0.0;
    for (std::uint32_t i = from; i < to; ++i)
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return static_cast<float>(std::sqrt(sum / std::max(1u, to - from)));
}

// Peak magnitude of the two-section cascade, in dB, from the coefficients the
// DSP settled on. 0 dB is unity.
static float worstCascadeDb(const Parameters& p)
{
    EngineState s;
    Block warm;
    // A couple of blocks so the coefficients stop moving and equal the target.
    run(s, p, warm);
    run(s, p, warm);

    const BiquadCoeffs& n = s.notch[0].coeffs;
    const BiquadCoeffs& d = s.diffusion[0].coeffs;

    float peakDb = -100.0f;
    for (int hz = 20; hz <= 24000; hz += 10) {
        const double w = 2.0 * M_PI * hz / kSr;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto response = [&z1, &z2](const BiquadCoeffs& c) {
            const double b0 = c.b0, b1 = c.b1, b2 = c.b2, a1 = c.a1, a2 = c.a2;
            return std::abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
        };
        const double magnitude = response(n) * response(d);
        if (magnitude > 0.0)
            peakDb = std::max(peakDb, static_cast<float>(20.0 * std::log10(magnitude)));
    }
    return peakDb;
}

// ── The analytic model ──────────────────────────────────────────────────────

// The mass-air-mass resonance must follow the textbook formula
// w0 = c * sqrt(rho0 / (m * D)) with D = cavity + gap.
static void testResonanceFormula()
{
    Parameters p;
    p.cavity = 100.0f;
    p.gap = 50.0f;
    p.mass = 0.8f;
    const float depth = (p.cavity + p.gap) * 0.001f;
    const float w0 = kSpeedOfSound * std::sqrt(kAirDensity / (p.mass * depth));
    const float expectedHz = w0 / (2.0f * 3.14159265f);

    const PanelState panel = analysePanel(p);
    check("resonance matches mass-air-mass formula", nearlyEqual(panel.resonanceHz, expectedHz, 0.05f));
    // With these dimensions the panel should sit in the bass, which is the
    // whole point of a treatment panel.
    check("default panel resonates in the bass", panel.resonanceHz > 120.0f && panel.resonanceHz < 260.0f);
}

// Heavier facing mass and deeper cavities both lower the resonance.
static void testResonanceTracksGeometry()
{
    const PanelState base = analysePanel(Parameters {});

    Parameters heavier = {};
    heavier.mass = 2.0f;
    check("heavier facing lowers resonance", analysePanel(heavier).resonanceHz < base.resonanceHz);

    Parameters deeper = {};
    deeper.cavity = 250.0f;
    deeper.gap = 120.0f;
    check("deeper cavity lowers resonance", analysePanel(deeper).resonanceHz < base.resonanceHz);

    Parameters wideGap = {};
    wideGap.gap = 200.0f;
    check("larger air gap lowers resonance", analysePanel(wideGap).resonanceHz < base.resonanceHz);

    // Half the depth should be about 1.41x the frequency: f ~ 1/sqrt(D).
    Parameters half = {};
    half.cavity = 50.0f;
    half.gap = 25.0f;
    const float ratio = analysePanel(half).resonanceHz / base.resonanceHz;
    check("f scales as 1/sqrt(depth)", std::fabs(ratio - std::sqrt(2.0f)) < 0.05f);
}

// Peak absorption is maximal when the specific flow resistance matches the
// characteristic impedance of air, and falls off either side.
static void testImpedanceOptimum()
{
    // r = rho0*c. Solve for the resistivity that achieves it at 100 mm.
    const float cavityMetres = 0.100f;
    const float matchedResist = kCharacteristicImpedance / cavityMetres;

    Parameters matched {};
    matched.resist = matchedResist;
    const PanelState optimum = analysePanel(matched);
    check("matched impedance absorbs near unity", optimum.peakAbsorb > 0.98f);

    // Both too open and too closed absorb less than the match.
    Parameters tooOpen {};
    tooOpen.resist = matchedResist * 0.05f;
    check("too-open fill absorbs less", analysePanel(tooOpen).peakAbsorb < optimum.peakAbsorb);

    Parameters tooClosed {};
    tooClosed.resist = matchedResist * 20.0f;
    check("over-dense fill absorbs less", analysePanel(tooClosed).peakAbsorb < optimum.peakAbsorb);

    // Symmetry: equal ratios either side of the match give equal absorption.
    Parameters lowSide {};
    lowSide.resist = matchedResist * 0.25f;
    Parameters highSide {};
    highSide.resist = matchedResist * 4.0f;
    check("absorption is symmetric about the match",
          nearlyEqual(analysePanel(lowSide).peakAbsorb, analysePanel(highSide).peakAbsorb, 0.01f));
}

// The fill's diffusion corner rises with resistivity and falls with depth.
static void testDiffusionCorner()
{
    const PanelState base = analysePanel(Parameters {});

    Parameters denser = {};
    denser.resist = 20000.0f;
    check("denser fill raises diffusion corner", analysePanel(denser).diffusionHz > base.diffusionHz);

    Parameters thicker = {};
    thicker.cavity = 200.0f;
    check("thicker fill lowers diffusion corner", analysePanel(thicker).diffusionHz < base.diffusionHz);
}

// Q stays inside the clamped range for the whole parameter space.
static void testQStaysClamped()
{
    float lo = 1e9f, hi = -1e9f;
    for (float cavity = 20.0f; cavity <= 400.0f; cavity += 20.0f) {
        for (float mass = 0.2f; mass <= 8.0f; mass += 0.4f) {
            Parameters p;
            p.cavity = cavity;
            p.mass = mass;
            const float q = analysePanel(p).q;
            lo = std::min(lo, q);
            hi = std::max(hi, q);
        }
    }
    check("q never below the floor", lo >= kMinQ - 1e-4f);
    check("q never above the ceiling", hi <= kMaxQ + 1e-4f);
}

// ── DSP behaviour ───────────────────────────────────────────────────────────

// The panel draws its curve from panelCoeffs/transmissionAt, so those must
// agree with what the DSP actually runs — otherwise the picture lies.
static void testResponseHelpersMatchDsp()
{
    const Parameters p;
    const PanelResponse response = panelCoeffs(p, kSr);
    check("panelCoeffs is valid at the default panel", response.valid);

    // Compare against coefficients the running engine settled on.
    EngineState s;
    Block warm;
    run(s, p, warm);
    run(s, p, warm);
    bool same = true;
    for (int c = 0; c < 2; ++c) {
        same = same
            && nearlyEqual(s.notch[c].coeffs.b0, response.notch.b0, 1e-4f)
            && nearlyEqual(s.notch[c].coeffs.b1, response.notch.b1, 1e-4f)
            && nearlyEqual(s.notch[c].coeffs.b2, response.notch.b2, 1e-4f)
            && nearlyEqual(s.notch[c].coeffs.a1, response.notch.a1, 1e-4f)
            && nearlyEqual(s.notch[c].coeffs.a2, response.notch.a2, 1e-4f)
            && nearlyEqual(s.diffusion[c].coeffs.b0, response.diffusion.b0, 1e-4f)
            && nearlyEqual(s.diffusion[c].coeffs.a1, response.diffusion.a1, 1e-4f);
    }
    check("panelCoeffs matches the running DSP", same);

    // transmissionAt must reproduce the same magnitude the tests already trust.
    const BiquadCoeffs& n = s.notch[0].coeffs;
    const BiquadCoeffs& d = s.diffusion[0].coeffs;
    auto reference = [&](const double hz) {
        const double w = 2.0 * M_PI * hz / kSr;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto mag = [&z1, &z2](const BiquadCoeffs& c) {
            const double b0 = c.b0, b1 = c.b1, b2 = c.b2, a1 = c.a1, a2 = c.a2;
            return std::abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
        };
        return mag(n) * mag(d);
    };

    bool agrees = true;
    for (int hz = 20; hz <= 20000; hz += 37) {
        const float got = transmissionAt(response, hz, kSr);
        agrees = agrees && nearlyEqual(got, static_cast<float>(reference(hz)), 1e-4f);
    }
    check("transmissionAt matches the coefficient response", agrees);

    // Transparent configurations report unity, not garbage.
    Parameters off;
    off.amount = 0.0f;
    check("transmissionAt is unity at amount 0",
          nearlyEqual(transmissionAt(panelCoeffs(off, kSr), 500.0, kSr), 1.0f, 1e-3f));

    // And it is never above unity anywhere, for any panel.
    Parameters probe;
    float worst = 1.0f;
    for (float cavity = 20.0f; cavity <= 400.0f; cavity += 60.0f) {
        for (float gap = 0.0f; gap <= 400.0f; gap += 100.0f) {
            for (float mass = 0.2f; mass <= 8.0f; mass += 2.0f) {
                for (float resist = 1000.0f; resist <= 60000.0f; resist += 12000.0f) {
                    probe.cavity = cavity; probe.gap = gap;
                    probe.mass = mass; probe.resist = resist;
                    const PanelResponse r = panelCoeffs(probe, kSr);
                    for (int hz = 20; hz <= 20000; hz += 53)
                        worst = std::max(worst, transmissionAt(r, hz, kSr));
                }
            }
        }
    }
    check("transmissionAt never exceeds unity", worst <= 1.0f);

    // An invalid response degrades to transparent rather than to nonsense.
    PanelResponse invalid;
    check("invalid response reads transparent",
          transmissionAt(invalid, 500.0, kSr) == 1.0f);
}

static void testSilence()
{
    Block b;
    EngineState s;
    run(s, Parameters {}, b);
    bool silent = true;
    for (std::uint32_t i = 0; i < kFrames && silent; ++i)
        silent = b.outL[i] == 0.0f && b.outR[i] == 0.0f;
    check("silence in gives silence out", silent);
}

// Amount 0 asks for no absorption, so both sections get 0 dB of gain and the
// coefficients collapse to b == a. The recursion is then unity in exact
// arithmetic, but float rounding in the feedback path leaves a residual of a
// few 1e-6 on a 0.5-peak signal, so the tolerance has to allow for that.
static void testAmountZeroIsTransparent()
{
    Block b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        b.inL[i] = b.inR[i] = 0.5f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 16.0f);
    EngineState s;
    Parameters p;
    p.amount = 0.0f;
    // Warm up so the filter states are settled and only the steady-state
    // rounding is being measured.
    run(s, p, b);
    run(s, p, b);
    float worst = 0.0f;
    for (std::uint32_t i = kFrames / 2; i < kFrames; ++i)
        worst = std::max(worst, std::fabs(b.outL[i] - b.inL[i]));
    check("amount 0 passes dry unchanged", worst < 1e-4f);
}

static void testBypassIsDry()
{
    Block b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        b.inL[i] = b.inR[i] = 0.5f * std::sin(2.0f * 3.14159f * static_cast<float>(i) / 16.0f);
    EngineState s;
    Parameters p;
    p.bypass = 1.0f;
    run(s, p, b);
    run(s, p, b);
    float worst = 0.0f;
    for (std::uint32_t i = kFrames / 2; i < kFrames; ++i)
        worst = std::max(worst, std::fabs(b.outL[i] - b.inL[i]));
    check("bypass passes dry unchanged", worst < 1e-4f);
}

// The panel must actually attenuate. Drive it at its own resonance and at the
// diffusion corner, where the model says it absorbs most.
static void testPanelAbsorbs()
{
    const PanelState panel = analysePanel(Parameters {});

    auto levelAt = [&](const float hz, const float amount) {
        Block b;
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const float ph = 2.0f * 3.14159f * hz * static_cast<float>(i) / static_cast<float>(kSr);
            b.inL[i] = b.inR[i] = 0.5f * std::sin(ph);
        }
        EngineState s;
        Parameters p;
        p.amount = amount;
        run(s, p, b);
        return rms(b.outL.data(), kFrames / 2, kFrames);
    };

    const float atResonance = levelAt(panel.resonanceHz, 100.0f);
    const float offResonance = levelAt(panel.resonanceHz * 12.0f, 100.0f);
    check("panel attenuates at its resonance", atResonance < offResonance * 0.5f);

    // Amount must be monotonic: more treatment, less output.
    const float half = levelAt(panel.resonanceHz, 50.0f);
    const float full = levelAt(panel.resonanceHz, 100.0f);
    check("amount 50 sits between dry and full", full < half && half < 0.55f);

    // The diffusion shelf should hold the top end below the dry reference.
    // Probe well above the corner: a Butterworth shelf is at half its gain
    // exactly AT the corner, so 0.9x would read as barely any effect.
    const float topDry = levelAt(panel.diffusionHz * 1.8f, 0.0f);
    const float topWet = levelAt(panel.diffusionHz * 1.8f, 100.0f);
    check("fill absorbs above its diffusion corner", topWet < topDry * 0.8f);

    // And the panel must not boost anywhere: the whole point is absorption.
    // Measured on the magnitude response rather than by comparing samples,
    // because a sub-unity filter still produces large sample ratios wherever
    // the input crosses zero.
    check("panel never boosts the signal", worstCascadeDb(Parameters {}) <= 0.01f);
}

// Resonance depth should track the impedance match: a well-matched fill digs a
// deeper dip than an over-damped one.
static void testMatchControlsDepth()
{
    const float cavityMetres = 0.100f;
    const float matchedResist = kCharacteristicImpedance / cavityMetres;

    auto depthAt = [&](const float resist) {
        const float hz = analysePanel(Parameters { 100.0f, 50.0f, 0.8f, resist, 100.0f, 0.0f, 1.0f })
                             .resonanceHz;
        Block b;
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const float ph = 2.0f * 3.14159f * hz * static_cast<float>(i) / static_cast<float>(kSr);
            b.inL[i] = b.inR[i] = 0.5f * std::sin(ph);
        }
        EngineState s;
        Parameters p;
        p.resist = resist;
        run(s, p, b);
        return rms(b.outL.data(), kFrames / 2, kFrames);
    };

    const float matched = depthAt(matchedResist);
    const float overDamped = depthAt(matchedResist * 30.0f);
    check("matched fill digs deeper than over-damped", matched < overDamped);
}

static void testDeterminism()
{
    auto runOnce = []() {
        Block b;
        for (std::uint32_t i = 0; i < kFrames; ++i)
            b.inL[i] = b.inR[i] = 0.4f * std::sin(2.0f * 3.14159f * 880.0f * i / 48000.0f);
        EngineState s;
        run(s, Parameters {}, b);
        return b;
    };
    const Block a = runOnce();
    const Block b = runOnce();
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(a.outL[i], b.outL[i], 1e-6f) && nearlyEqual(a.outR[i], b.outR[i], 1e-6f);
    check("identical input gives identical output", same);
}

static void testBlockSplitDeterminism()
{
    Block a, b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        a.inL[i] = a.inR[i] = b.inL[i] = b.inR[i]
            = 0.4f * std::sin(2.0f * 3.14159f * 330.0f * i / 48000.0f);
    const Parameters p;
    {
        EngineState s;
        run(s, p, a);
    }
    {
        EngineState s;
        constexpr std::uint32_t kHalf = kFrames / 2;
        const float* ins[] = { b.inL.data(), b.inR.data() };
        float* outs[] = { b.outL.data(), b.outR.data() };
        processBlock(s, p, kHalf, kSr, ins, outs);
        const float* ins2[] = { b.inL.data() + kHalf, b.inR.data() + kHalf };
        float* outs2[] = { b.outL.data() + kHalf, b.outR.data() + kHalf };
        processBlock(s, p, kFrames - kHalf, kSr, ins2, outs2);
    }
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(a.outL[i], b.outL[i], 1e-6f);
    check("split blocks match single block", same);
}

// Non-finite input must not poison the output bus.
static void testNonFiniteInputContained()
{
    Block b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        b.inL[i] = b.inR[i] = 0.4f * std::sin(2.0f * 3.14159f * 200.0f * i / 48000.0f);
    b.inL[10] = std::nanf("");
    b.inR[20] = 1.0e30f;
    EngineState s;
    run(s, Parameters {}, b);
    bool finite = true;
    for (std::uint32_t i = 0; i < kFrames && finite; ++i)
        finite = std::isfinite(b.outL[i]) && std::isfinite(b.outR[i])
            && std::fabs(b.outL[i]) < 2.0f;
    check("non-finite input stays contained", finite);
}

// ── Randomise ───────────────────────────────────────────────────────────────

static void testRandomiseIsSeededAndInRange()
{
    Parameters p;
    p.seed = 42.0f;

    const Parameters a = randomiseParameters(p);
    const Parameters b = randomiseParameters(p);
    check("randomise is deterministic from the seed",
          a.cavity == b.cavity && a.gap == b.gap && a.mass == b.mass
              && a.resist == b.resist && a.seed == b.seed);

    Parameters other = p;
    other.seed = 43.0f;
    const Parameters c = randomiseParameters(other);
    check("a different seed gives a different panel",
          a.cavity != c.cavity || a.gap != c.gap || a.mass != c.mass || a.resist != c.resist);

    // Every drawn value must land in range and be a round, plausible number.
    bool inRange = true;
    for (int i = 0; i < 200; ++i) {
        Parameters q = p;
        q.seed = static_cast<float>(i + 1);
        const Parameters r = randomiseParameters(q);
        inRange = inRange
            && r.cavity >= kParameterSpecs[index(ParamId::cavity)].minimum
            && r.cavity <= kParameterSpecs[index(ParamId::cavity)].maximum
            && r.gap >= kParameterSpecs[index(ParamId::gap)].minimum
            && r.gap <= kParameterSpecs[index(ParamId::gap)].maximum
            && r.mass >= kParameterSpecs[index(ParamId::mass)].minimum
            && r.mass <= kParameterSpecs[index(ParamId::mass)].maximum
            && r.resist >= kParameterSpecs[index(ParamId::resist)].minimum
            && r.resist <= kParameterSpecs[index(ParamId::resist)].maximum
            && r.seed >= 1.0f && r.seed <= 9999.0f;
    }
    check("randomised panels stay in range", inRange);

    // Amount and Bypass are deliberately preserved so a sweep keeps its depth.
    Parameters preserve;
    preserve.amount = 42.0f;
    preserve.bypass = 1.0f;
    const Parameters kept = randomiseParameters(preserve);
    check("randomise preserves amount and bypass",
          kept.amount == 42.0f && kept.bypass == 1.0f);

    // Repeating from the advanced seed must move again.
    check("a second press differs", randomiseParameters(a).cavity != a.cavity
              || randomiseParameters(a).resist != a.resist);

    // Every panel Randomise can reach must stay a pure absorber.
    float worstBoost = -100.0f;
    for (int i = 1; i <= 60; ++i) {
        Parameters q = p;
        q.seed = static_cast<float>(i);
        worstBoost = std::max(worstBoost, worstCascadeDb(randomiseParameters(q)));
    }
    check("randomised panels never boost", worstBoost <= 0.01f);
}

// ── Clamp and serialization ─────────────────────────────────────────────────

static void testClamp()
{
    Parameters extreme;
    extreme.cavity = -50.0f;
    extreme.gap = 1.0e6f;
    extreme.mass = 0.0f;
    extreme.resist = std::nanf("");
    extreme.amount = 500.0f;
    extreme.bypass = 7.0f;
    extreme.seed = -3.0f;

    const Parameters c = clampParameters(extreme);
    check("clamp cavity", c.cavity >= kParameterSpecs[index(ParamId::cavity)].minimum);
    check("clamp gap", c.gap <= kParameterSpecs[index(ParamId::gap)].maximum);
    check("clamp mass", c.mass >= kParameterSpecs[index(ParamId::mass)].minimum);
    check("clamp resist is finite", std::isfinite(c.resist));
    check("clamp amount", c.amount <= 100.0f);
    check("clamp bypass", c.bypass <= 1.0f);
    check("clamp seed", c.seed >= 1.0f);

    // NaN parameters must not produce NaN audio.
    Parameters broken;
    broken.mass = std::nanf("");
    const PanelState panel = analysePanel(broken);
    check("broken parameters give a finite panel", std::isfinite(panel.resonanceHz)
              && std::isfinite(panel.q) && std::isfinite(panel.peakAbsorb));
}

static void testSerialization()
{
    Parameters p;
    p.cavity = 155.0f;
    p.gap = 42.0f;
    p.mass = 1.75f;
    p.resist = 12345.0f;
    p.amount = 65.0f;
    p.bypass = 1.0f;
    p.seed = 777.0f;

    const std::string text = serializeParameters(p);
    const auto restored = deserializeParameters(text);
    check("serialization round-trip valid", restored.has_value());
    if (restored.has_value()) {
        const float eps = 1e-3f;
        check("rt cavity", std::fabs(restored->cavity - p.cavity) < eps);
        check("rt gap", std::fabs(restored->gap - p.gap) < eps);
        check("rt mass", std::fabs(restored->mass - p.mass) < eps);
        check("rt resist", std::fabs(restored->resist - p.resist) < eps);
        check("rt amount", std::fabs(restored->amount - p.amount) < eps);
        check("rt bypass", restored->bypass == p.bypass);
        check("rt seed", std::fabs(restored->seed - p.seed) < eps);
    }
    check("garbage deserialises to nullopt", !deserializeParameters("nonsense\n").has_value());
}

// ── Panel bank and special effect ───────────────────────────────────────────

static Parameters bank(const int panels)
{
    Parameters p;
    p.panels = static_cast<float>(panels);
    return p;
}

static Block tone(const float hz)
{
    Block b;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        b.inL[i] = b.inR[i] = 0.5f * std::sin(2.0f * 3.14159265f * hz * static_cast<float>(i)
                                              / static_cast<float>(kSr));
    return b;
}

// Steady-state level of a sine through a parameter set (second half of 4 blocks).
static float levelOf(const Parameters& p, const float hz)
{
    EngineState s;
    float level = 0.0f;
    std::uint32_t n = 0;
    for (int blk = 0; blk < 4; ++blk) {
        Block b;
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const double t = static_cast<double>(blk * kFrames + i) / kSr;
            b.inL[i] = b.inR[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * t));
        }
        run(s, p, b);
        if (blk == 3) {
            level = rms(b.outL.data(), 0, kFrames);
            n = kFrames;
        }
    }
    return n ? level : 0.0f;
}

static void testBankDefaultsAreInert()
{
    const Parameters d;
    check("default is one panel", d.panels == 1.0f);
    check("default mode is Absorb", d.mode == 0.0f);
    check("default sweep depth is off", d.sweepDepth == 0.0f);
    check("default stereo offset is 0", d.stereo == 0.0f);
    check("spec table defaults match the core",
          kParameterSpecs[index(ParamId::panels)].defaultValue == d.panels
              && kParameterSpecs[index(ParamId::sweepDepth)].defaultValue == d.sweepDepth
              && kParameterSpecs[index(ParamId::mode)].defaultValue == d.mode
              && kParameterSpecs[index(ParamId::sweepRate)].defaultValue == d.sweepRate);

    bool extrasMatch = true;
    for (int k = 1; k < kMaxPanels; ++k) {
        const PanelGeometry& g = d.extra[static_cast<std::size_t>(k - 1)];
        extrasMatch = extrasMatch
            && kParameterSpecs[panelFieldIndex(k, 0)].defaultValue == g.cavity
            && kParameterSpecs[panelFieldIndex(k, 1)].defaultValue == g.gap
            && kParameterSpecs[panelFieldIndex(k, 2)].defaultValue == g.mass
            && kParameterSpecs[panelFieldIndex(k, 3)].defaultValue == g.resist;
    }
    check("extra panel spec defaults match the core", extrasMatch);
    check("panel 2 cavity symbol", std::string(kParameterSpecs[panelFieldIndex(1, 0)].symbol) == "p2_cavity");
    check("panel 4 resist symbol", std::string(kParameterSpecs[panelFieldIndex(3, 3)].symbol) == "p4_resist");
    check("original indices are stable", index(ParamId::randomise) == 13 && index(ParamId::outDiffusion) == 16
              && index(ParamId::panels) == 17);
}

static void testPanelView()
{
    Parameters p = bank(3);
    const Parameters v = panelView(p, 2);
    check("panelView 2 is the third panel", v.cavity == p.extra[1].cavity && v.mass == p.extra[1].mass
              && v.resist == p.extra[1].resist && v.panels == 1.0f);
    check("panelView 0 is panel 1", panelView(p, 0).cavity == p.cavity);
}

// Panels beyond the count must not leak into the audio.
static void testInactivePanelsAreSilent()
{
    Parameters a = bank(1);
    Parameters b = bank(1);
    b.extra[0].cavity = 350.0f;
    b.extra[1].mass = 5.0f;
    Block x = tone(220.0f), y = tone(220.0f);
    EngineState sa, sb;
    run(sa, a, x);
    run(sb, b, y);
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = x.outL[i] == y.outL[i];
    check("panels beyond the count do not touch the audio", same);
}

// Every added panel must change the sound, and dig at its own resonance.
static void testEachPanelIsAudible()
{
    bool allAudible = true;
    for (int n = 2; n <= kMaxPanels; ++n) {
        const Parameters with = bank(n);
        const Parameters without = bank(n - 1);
        const float hz = analysePanel(panelView(with, n - 1)).resonanceHz;
        const float a = levelOf(with, hz);
        const float b = levelOf(without, hz);
        allAudible = allAudible && a < b * 0.9f;
    }
    check("each added panel absorbs at its own resonance", allAudible);

    // Fields of panels 2-4 each move the resonance or the depth.
    Parameters base = bank(4);
    bool fieldsAudible = true;
    for (int k = 1; k < kMaxPanels; ++k) {
        for (int f = 0; f < 4; ++f) {
            Parameters q = base;
            PanelGeometry& g = q.extra[static_cast<std::size_t>(k - 1)];
            float* field = f == 0 ? &g.cavity : f == 1 ? &g.gap : f == 2 ? &g.mass : &g.resist;
            *field = f == 3 ? *field * 0.3f : f == 2 ? *field * 2.0f : *field * 0.5f + 20.0f;
            const PanelState before = analysePanel(panelView(base, k));
            const PanelState after = analysePanel(panelView(q, k));
            const bool moved = std::fabs(before.resonanceHz - after.resonanceHz) > 0.5f
                || std::fabs(before.peakAbsorb - after.peakAbsorb) > 0.01f
                || std::fabs(before.diffusionHz - after.diffusionHz) > 1.0f;
            fieldsAudible = fieldsAudible && moved;
        }
    }
    check("every field of panels 2-4 changes the panel", fieldsAudible);
}

static void testAbsorbBankNeverBoosts()
{
    float worst = 0.0f;
    for (int n = 1; n <= kMaxPanels; ++n) {
        Parameters p = bank(n);
        for (int i = 1; i <= 40; ++i) {
            p.seed = static_cast<float>(i);
            p = randomiseParameters(p);
            float bankPeak = 0.0f;
            for (int hz = 20; hz <= 20000; hz += 47) {
                float mag = 1.0f;
                for (int k = 0; k < n; ++k)
                    mag *= transmissionAt(panelCoeffs(panelView(p, k), kSr), hz, kSr);
                bankPeak = std::max(bankPeak, mag);
            }
            worst = std::max(worst, bankPeak);
        }
    }
    check("an absorbing bank never exceeds unity", worst <= 1.0001f);
}

static void testRingMode()
{
    Parameters absorb;
    Parameters ring;
    ring.mode = 1.0f;
    const float hz = analysePanel(Parameters {}).resonanceHz;
    const float cut = transmissionAt(panelCoeffs(absorb, kSr), hz, kSr);
    const float boost = transmissionAt(panelCoeffs(ring, kSr), hz, kSr);
    check("absorb mode cuts at the resonance", cut < 0.9f);
    check("ring mode boosts at the resonance", boost > 1.2f);

    // Bounded: each panel caps its notch and shelf boost, so even four panels
    // stacked on one frequency stay inside 4 * 2 * kMaxRingBoostDb.
    float worst = 0.0f;
    for (int i = 1; i <= 30; ++i) {
        Parameters q = bank(4);
        q.mode = 1.0f;
        q.seed = static_cast<float>(i);
        q = randomiseParameters(q);
        for (int hz2 = 20; hz2 <= 20000; hz2 += 31) {
            float mag = 1.0f;
            for (int k = 0; k < 4; ++k)
                mag *= transmissionAt(panelCoeffs(panelView(q, k), kSr), hz2, kSr);
            worst = std::max(worst, mag);
        }
    }
    check("a ring bank stays under the documented cap",
          worst <= std::pow(10.0f, 4.0f * 2.0f * kMaxRingBoostDb / 20.0f));

    // Stability: an impulse through four ring panels, swept, must decay.
    Parameters q = bank(4);
    q.mode = 1.0f;
    q.sweepDepth = 100.0f;
    q.sweepRate = 2.0f;
    EngineState s;
    Block b;
    b.inL[0] = b.inR[0] = 1.0f;
    float tail = 0.0f;
    float peak = 0.0f;
    for (int blk = 0; blk < 400; ++blk) {
        run(s, q, b);
        b.inL.fill(0.0f);
        b.inR.fill(0.0f);
        for (std::uint32_t i = 0; i < kFrames; ++i)
            peak = std::max(peak, std::fabs(b.outL[i]));
        if (blk == 399)
            tail = rms(b.outL.data(), 0, kFrames);
    }
    check("a swept ring bank is finite and its impulse decays", std::isfinite(peak) && peak < 50.0f && tail < 1e-3f);
}

static void testSweep()
{
    // Depth 0 is the static panel; the rate is then inert.
    {
        Parameters a, b;
        b.sweepRate = 5.0f;
        Block x = tone(180.0f), y = tone(180.0f);
        EngineState sa, sb;
        run(sa, a, x);
        run(sb, b, y);
        bool same = true;
        for (std::uint32_t i = 0; i < kFrames && same; ++i)
            same = x.outL[i] == y.outL[i];
        check("sweep rate is inert at depth 0", same);
    }

    // The resonance actually moves: level at the static resonance changes over time.
    {
        Parameters p;
        p.sweepDepth = 100.0f;
        p.sweepRate = 1.0f;
        const float hz = analysePanel(p).resonanceHz;
        EngineState s;
        float lo = 1e9f, hi = 0.0f;
        for (int blk = 0; blk < 200; ++blk) {
            Block b;
            for (std::uint32_t i = 0; i < kFrames; ++i) {
                const double t = static_cast<double>(blk * kFrames + i) / kSr;
                b.inL[i] = b.inR[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * t));
            }
            run(s, p, b);
            if (blk > 20) {
                const float level = rms(b.outL.data(), 0, kFrames);
                lo = std::min(lo, level);
                hi = std::max(hi, level);
            }
        }
        check("sweep moves the resonance over time", hi > lo * 1.5f);
    }

    // Block size must not change a swept render.
    {
        Parameters p = bank(3);
        p.sweepDepth = 70.0f;
        p.sweepRate = 3.0f;
        p.stereo = 40.0f;
        Block a = tone(150.0f), b = tone(150.0f);
        { EngineState s; run(s, p, a); }
        {
            EngineState s;
            const std::uint32_t cuts[] = { 37, 100, 1, 200, 174 };
            std::uint32_t at = 0;
            for (const std::uint32_t len : cuts) {
                const float* ins[] = { b.inL.data() + at, b.inR.data() + at };
                float* outs[] = { b.outL.data() + at, b.outR.data() + at };
                processBlock(s, p, len, kSr, ins, outs);
                at += len;
            }
        }
        bool same = true;
        for (std::uint32_t i = 0; i < kFrames && same; ++i)
            same = nearlyEqual(a.outL[i], b.outL[i], 1e-6f) && nearlyEqual(a.outR[i], b.outR[i], 1e-6f);
        check("swept render is independent of block size", same);
    }

    // Stereo offset decorrelates the channels, and is inert without a sweep.
    {
        Parameters p;
        p.sweepDepth = 100.0f;
        p.sweepRate = 2.0f;
        const float hz = analysePanel(p).resonanceHz;
        auto channelGap = [&](const Parameters& q) {
            EngineState s;
            float gap = 0.0f;
            for (int blk = 0; blk < 120; ++blk) {
                Block b;
                for (std::uint32_t i = 0; i < kFrames; ++i) {
                    const double t = static_cast<double>(blk * kFrames + i) / kSr;
                    b.inL[i] = b.inR[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * t));
                }
                run(s, q, b);
                for (std::uint32_t i = 0; i < kFrames; ++i)
                    gap = std::max(gap, std::fabs(b.outL[i] - b.outR[i]));
            }
            return gap;
        };
        Parameters wide = p;
        wide.stereo = 100.0f;
        check("stereo 0 keeps the channels identical", channelGap(p) == 0.0f);
        check("stereo offset separates the channels", channelGap(wide) > 0.05f);

        Parameters still;
        still.stereo = 100.0f;
        check("stereo offset is inert without a sweep", channelGap(still) == 0.0f);
    }

    // Amount 0 stays transparent through the whole effect.
    {
        Parameters p = bank(4);
        p.amount = 0.0f;
        p.sweepDepth = 100.0f;
        p.sweepRate = 4.0f;
        p.stereo = 100.0f;
        Block b = tone(300.0f);
        EngineState s;
        run(s, p, b);
        run(s, p, b);
        float worst = 0.0f;
        for (std::uint32_t i = kFrames / 2; i < kFrames; ++i)
            worst = std::max(worst, std::fabs(b.outL[i] - b.inL[i]));
        check("amount 0 is transparent with a swept four-panel bank", worst < 1e-3f);
    }
}

static void testSyncedSweep()
{
    check("sync is off by default", Parameters {}.sweepSync == 0.0f);

    Parameters p;
    p.sweepDepth = 100.0f;
    p.sweepSync = 1.0f;
    p.sweepBeats = 4.0f;  // one 4/4 bar

    // Playing: the phase is the song position, so two engines that start at the
    // same beat render the same audio no matter how the host chops the blocks.
    auto render = [&](const std::uint32_t chunk) {
        EngineState s;
        s.hostBpm = 120.0;
        s.hostPlaying = true;
        Block b = tone(150.0f);
        for (std::uint32_t at = 0; at < kFrames; at += chunk) {
            const std::uint32_t len = std::min(chunk, kFrames - at);
            s.hostBeat = 3.0 + static_cast<double>(at) * 120.0 / (60.0 * kSr);
            const float* ins[] = { b.inL.data() + at, b.inR.data() + at };
            float* outs[] = { b.outL.data() + at, b.outR.data() + at };
            processBlock(s, p, len, kSr, ins, outs);
        }
        return b;
    };
    const Block whole = render(kFrames);
    const Block cut = render(64);
    bool same = true;
    for (std::uint32_t i = 0; i < kFrames && same; ++i)
        same = nearlyEqual(whole.outL[i], cut.outL[i], 1e-5f);
    check("a playing synced sweep follows the song position", same);

    // The same beat one bar later gives the same sweep position (period = 4 beats).
    auto at = [&](const double beat) {
        EngineState s;
        s.hostBpm = 120.0;
        s.hostPlaying = true;
        s.hostBeat = beat;
        Block b = tone(150.0f);
        run(s, p, b);
        return b;
    };
    const Block a0 = at(1.0), a1 = at(5.0), a2 = at(2.0);
    bool periodic = true, differs = false;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        periodic = periodic && nearlyEqual(a0.outL[i], a1.outL[i], 1e-5f);
        differs = differs || std::fabs(a0.outL[i] - a2.outL[i]) > 1e-3f;
    }
    check("synced sweep repeats every division", periodic);
    check("a different beat gives a different sweep position", differs);

    // The division changes the sweep, and tempo scales the free-running rate.
    Parameters q = p;
    q.sweepBeats = 1.0f;
    EngineState s;
    s.hostBpm = 120.0;
    s.hostPlaying = true;
    s.hostBeat = 1.0;
    Block b = tone(150.0f);
    run(s, q, b);
    bool divisionMatters = false;
    for (std::uint32_t i = 0; i < kFrames; ++i)
        divisionMatters = divisionMatters || std::fabs(b.outL[i] - a0.outL[i]) > 1e-3f;
    check("sweep division is audible", divisionMatters);

    // Sync with no host tempo falls back to the Rate control.
    EngineState noTempo, freeRun;
    Parameters f = p;
    f.sweepSync = 0.0f;
    Block x = tone(150.0f), y = tone(150.0f);
    run(noTempo, p, x);
    run(freeRun, f, y);
    bool fallback = true;
    for (std::uint32_t i = 0; i < kFrames && fallback; ++i)
        fallback = x.outL[i] == y.outL[i];
    check("sync without a host tempo falls back to Rate", fallback);

    const auto r = deserializeParameters(serializeParameters(p));
    check("sync state round-trips", r && r->sweepSync == 1.0f && r->sweepBeats == 4.0f);
}

static void testBankRandomise()
{
    Parameters one = bank(1);
    one.seed = 11.0f;
    const Parameters r1 = randomiseParameters(one);
    bool untouched = true;
    for (std::size_t k = 0; k < 3; ++k)
        untouched = untouched && r1.extra[k].cavity == one.extra[k].cavity
            && r1.extra[k].mass == one.extra[k].mass;
    check("single-panel randomise leaves the other panels alone", untouched);

    Parameters four = bank(4);
    four.seed = 11.0f;
    const Parameters r4 = randomiseParameters(four);
    check("four-panel randomise keeps panel 1 identical to the single-panel draw",
          r4.cavity == r1.cavity && r4.gap == r1.gap && r4.mass == r1.mass && r4.resist == r1.resist);
    bool moved = true;
    bool inRange = true;
    for (std::size_t k = 0; k < 3; ++k) {
        moved = moved && r4.extra[k].cavity != four.extra[k].cavity;
        inRange = inRange && r4.extra[k].cavity >= 20.0f && r4.extra[k].cavity <= 400.0f
            && r4.extra[k].mass >= 0.2f && r4.extra[k].mass <= 8.0f
            && r4.extra[k].resist >= 1000.0f && r4.extra[k].resist <= 60000.0f
            && r4.extra[k].gap >= 0.0f && r4.extra[k].gap <= 400.0f;
    }
    check("randomise redraws every active panel", moved);
    check("randomised bank stays in range", inRange);
    check("randomise keeps panels, mode and sweep",
          r4.panels == 4.0f && r4.mode == four.mode && r4.sweepDepth == four.sweepDepth);
}

static void testBankSerialization()
{
    Parameters p = bank(3);
    p.extra[0].cavity = 222.0f;
    p.extra[1].resist = 31000.0f;
    p.extra[2].mass = 2.5f;
    p.mode = 1.0f;
    p.sweepRate = 1.25f;
    p.sweepDepth = 64.0f;
    p.stereo = 30.0f;
    const auto r = deserializeParameters(serializeParameters(p));
    check("bank round-trips", r.has_value());
    if (r) {
        check("rt panels", r->panels == 3.0f);
        check("rt extra fields", std::fabs(r->extra[0].cavity - 222.0f) < 1e-3f
                  && std::fabs(r->extra[1].resist - 31000.0f) < 1e-1f
                  && std::fabs(r->extra[2].mass - 2.5f) < 1e-3f);
        check("rt effect", r->mode == 1.0f && std::fabs(r->sweepRate - 1.25f) < 1e-3f
                  && std::fabs(r->sweepDepth - 64.0f) < 1e-3f && std::fabs(r->stereo - 30.0f) < 1e-3f);
    }

    // A version-1 project, written before the bank existed, must still load and
    // leave the new controls at their inert defaults.
    const auto old = deserializeParameters(
        "version=1\ncavity=120.0\ngap=40.0\nmass=1.1\nresist=9000.0\namount=80.0\nbypass=0.0\nseed=5.0\n");
    check("a version-1 state still loads", old.has_value());
    if (old) {
        check("old state keeps its panel", old->cavity == 120.0f && old->amount == 80.0f);
        check("old state gets one panel, Absorb, no sweep",
              old->panels == 1.0f && old->mode == 0.0f && old->sweepDepth == 0.0f);
    }
    check("unknown bank key is rejected", !deserializeParameters("version=2\np5_cavity=1\n").has_value());
    check("unknown bank field is rejected", !deserializeParameters("version=2\np2_colour=1\n").has_value());

    Parameters broken = bank(9);
    broken.extra[1].mass = std::nanf("");
    broken.sweepDepth = 900.0f;
    broken.mode = 5.0f;
    const Parameters c = clampParameters(broken);
    check("bank clamp", c.panels == 4.0f && std::isfinite(c.extra[1].mass) && c.sweepDepth == 100.0f
              && c.mode == 1.0f);
}

int main()
{
    testResonanceFormula();
    testResonanceTracksGeometry();
    testImpedanceOptimum();
    testDiffusionCorner();
    testQStaysClamped();

    testResponseHelpersMatchDsp();
    testSilence();
    testAmountZeroIsTransparent();
    testBypassIsDry();
    testPanelAbsorbs();
    testMatchControlsDepth();
    testDeterminism();
    testBlockSplitDeterminism();
    testNonFiniteInputContained();

    testRandomiseIsSeededAndInRange();
    testClamp();
    testSerialization();

    testBankDefaultsAreInert();
    testPanelView();
    testInactivePanelsAreSilent();
    testEachPanelIsAudible();
    testAbsorbBankNeverBoosts();
    testRingMode();
    testSweep();
    testSyncedSweep();
    testBankRandomise();
    testBankSerialization();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
