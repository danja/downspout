#include "treatment_core_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>

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

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
