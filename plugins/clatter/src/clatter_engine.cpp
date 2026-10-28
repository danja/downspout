#include "clatter_core_types.hpp"

#include <algorithm>
#include <cmath>

namespace clatter {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kPi = 3.14159265f;
constexpr float kLn1000 = 6.9078f;  // a decay time is the time to fall 60 dB

// Inharmonic partial sets for the metal family (agogo bell, triangle, jingle bell, bell tree).
constexpr float kRatios[4][6] = {
    {1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f},
    {1.0f, 2.5f, 4.2f, 6.1f, 8.3f, 10.9f},
    {1.0f, 1.47f, 2.09f, 2.56f, 3.40f, 4.17f},
    {1.0f, 2.4f, 3.7f, 5.2f, 6.8f, 8.9f},
};
constexpr float kWeights[4][6] = {
    {1.0f, 0.7f, 0.5f, 0.3f, 0.2f, 0.1f},
    {1.0f, 0.8f, 0.6f, 0.45f, 0.3f, 0.2f},
    {1.0f, 0.9f, 0.8f, 0.6f, 0.5f, 0.4f},
    {1.0f, 0.6f, 0.45f, 0.3f, 0.2f, 0.1f},
};
// Circular-membrane mode ratios (fundamental plus three overtones).
constexpr float kMembraneRatios[4] = {1.0f, 1.59f, 2.14f, 3.41f};
constexpr float kBellTreeStep = 1.12f;  // each strike of the bell tree is a bell higher

float clampf(const float x, const float lo, const float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Per-sample multiplier that takes an envelope down by e every `seconds`.
float expCoef(const float seconds, const float sr) { return std::exp(-1.0f / (std::max(1e-5f, seconds) * sr)); }

downspout::dsp::SvfCoeff bandCoeff(const float fc, const float q, const float sr)
{
    downspout::dsp::SvfCoeff c;
    c.g = std::tan(kPi * clampf(fc, 100.0f, sr * 0.45f) / sr);
    c.k = 1.0 / std::max(0.3f, q);
    c.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    c.a2 = c.g * c.a1;
    c.a3 = c.g * c.a2;
    return c;
}

}  // namespace

int ClatterEngine::soundForNote(const int note)
{
    for (int i = 0; i < kSoundCount; ++i)
        if (kPatches[i].note == note)
            return i;
    return -1;
}

void ClatterEngine::trigger(const int sound, const float velocity)
{
    if (sound < 0 || sound >= kSoundCount)
        return;
    const Patch& P = kPatches[sound];
    const FamilyTrim& T = params_.family[static_cast<int>(P.family)];

    if (P.group > 0)
        for (Voice& o : voices_)
            if (o.active && kPatches[o.sound].group == P.group)
                o.choking = true;

    Voice* v = nullptr;
    Voice* oldest = &voices_[0];
    for (Voice& o : voices_) {
        if (!o.active) { v = &o; break; }
        if (o.n > oldest->n) oldest = &o;
    }
    if (v == nullptr)
        v = oldest;
    *v = Voice();

    v->active = true;
    v->sound = sound;
    v->velocity = clampf(velocity, 0.0f, 1.0f);
    const float pan = clampf(P.pan * params_.width / 0.6f, -1.0f, 1.0f);
    v->gainL = std::cos((pan + 1.0f) * kPi * 0.25f);
    v->gainR = std::sin((pan + 1.0f) * kPi * 0.25f);
    v->freqMul = std::pow(2.0f, clampf(T.tune, -12.0f, 12.0f) / 12.0f);
    const float decay = P.decay * clampf(T.decay, 0.25f, 4.0f);
    v->tau = decay / kLn1000;
    v->toneGain = 2.0f * std::max(0.02f, clampf(T.tone, 0.0f, 1.0f));
    v->coef = expCoef(v->tau, sr_);
    const float sr = sr_;
    float extra = 0.0f;  // seconds added by restrikes

    switch (P.family) {
    case Family::Membrane:
        v->dropCoef = expCoef(0.045f, sr);
        v->clickCoef = expCoef(0.003f, sr);
        for (int i = 0; i < 4; ++i)
            v->inc[i] = P.freq * v->freqMul * kMembraneRatios[i] / sr;
        break;
    case Family::Metal: {
        const int set = static_cast<int>(P.p1);
        for (int i = 0; i < 6; ++i) {
            v->inc[i] = P.freq * v->freqMul * kRatios[set][i] / sr;
            v->amp[i] = kWeights[set][i] * std::pow(v->toneGain, static_cast<float>(i) * 0.35f);
            v->pcoef[i] = expCoef(v->tau / (1.0f + 0.5f * static_cast<float>(i)), sr);
            v->pe[i] = 1.0f;
        }
        v->strikesLeft = std::max(1, static_cast<int>(P.p2)) - 1;
        v->strikeEvery = static_cast<long>(P.p3 * 0.001f * sr);
        extra = static_cast<float>(v->strikesLeft) * P.p3 * 0.001f;
        break;
    }
    case Family::Wood:
        v->clickCoef = expCoef(0.0015f, sr);
        v->inc[0] = P.freq * v->freqMul / sr;
        v->inc[1] = P.freq * v->freqMul * 2.67f / sr;
        v->amp[1] = 0.35f * v->toneGain;
        v->strikesLeft = std::max(1, static_cast<int>(P.p2)) - 1;
        v->strikeEvery = static_cast<long>(P.p3 * 0.001f * sr);
        extra = static_cast<float>(v->strikesLeft) * P.p3 * 0.001f;
        break;
    case Family::Shaker:
        v->coeff = bandCoeff(P.freq * v->freqMul * std::pow(2.0f, (clampf(T.tone, 0.0f, 1.0f) - 0.5f) * 2.0f), P.p3, sr);
        v->attack = std::max(1L, static_cast<long>(P.p1 * 0.001f * sr));
        extra = P.p1 * 0.001f;
        break;
    case Family::Clap:
        v->coeff = bandCoeff(P.freq * v->freqMul * std::pow(2.0f, (clampf(T.tone, 0.0f, 1.0f) - 0.5f) * 2.0f), P.p3, sr);
        if (P.p1 > 0.0f) {  // clap: a few short bursts, then the tail
            v->strikesLeft = static_cast<int>(P.p1) - 1;
            v->strikeEvery = static_cast<long>(P.p2 * 0.001f * sr);
            v->pcoef[0] = expCoef(v->tau, sr);
            v->pcoef[1] = expCoef(0.0025f, sr);
            v->coef = v->strikesLeft > 0 ? v->pcoef[1] : v->pcoef[0];
            extra = static_cast<float>(v->strikesLeft) * P.p2 * 0.001f;
        }
        break;
    }
    // A scrape's "decay" is its length; everything else rings for a little over its 60 dB time.
    const bool scrape = P.family == Family::Clap && P.p1 <= 0.0f;
    v->dur = static_cast<long>((scrape ? decay : (decay + extra) * 1.15f) * sr);
}

float ClatterEngine::renderVoice(Voice& v)
{
    if (v.n >= v.dur) {
        v.active = false;
        return 0.0f;
    }
    ++v.n;
    const Patch& P = kPatches[v.sound];
    const float dt = 1.0f / sr_;
    float s = 0.0f;

    switch (P.family) {
    case Family::Membrane: {
        const float stretch = 1.0f + P.p1 * v.dropEnv;
        const float e1 = v.env, e2 = e1 * e1, e3 = e2 * e1;
        const float a = P.p2 * v.toneGain;
        s = std::sin(kTwoPi * v.phase[0]) * e1 + a * std::sin(kTwoPi * v.phase[1]) * e2 +
            0.5f * a * std::sin(kTwoPi * v.phase[2]) * e3 + P.p3 * std::sin(kTwoPi * v.phase[3]) * e2;
        for (int i = 0; i < 4; ++i) {
            v.phase[i] += v.inc[i] * stretch;
            if (v.phase[i] >= 1.0f) v.phase[i] -= 1.0f;
        }
        v.lp += 0.35f * (noise() - v.lp);
        s += v.lp * v.clickEnv * 0.5f * v.toneGain;
        v.env *= v.coef;
        v.dropEnv *= v.dropCoef;
        v.clickEnv *= v.clickCoef;
        s *= 0.6f;
        break;
    }
    case Family::Metal: {
        if (v.strikesLeft > 0 && ++v.age >= v.strikeEvery) {
            v.age = 0;
            --v.strikesLeft;
            for (int i = 0; i < 6; ++i) v.pe[i] = 1.0f;
            if (static_cast<int>(P.p1) == 3) v.stepMul *= kBellTreeStep;
        }
        for (int i = 0; i < 6; ++i) {
            const float step = v.inc[i] * v.stepMul;
            if (step >= 0.45f) continue;  // above Nyquist-ish: leave it out rather than alias
            s += v.amp[i] * std::sin(kTwoPi * v.phase[i]) * v.pe[i];
            v.phase[i] += step;
            if (v.phase[i] >= 1.0f) v.phase[i] -= 1.0f;
            v.pe[i] *= v.pcoef[i];
        }
        s *= 0.25f;
        break;
    }
    case Family::Wood: {
        if (v.strikesLeft > 0 && ++v.age >= v.strikeEvery) {
            v.age = 0;
            --v.strikesLeft;
            v.env = 1.0f;
            v.clickEnv = 1.0f;
        }
        s = std::sin(kTwoPi * v.phase[0]) * v.env + v.amp[1] * std::sin(kTwoPi * v.phase[1]) * v.env * v.env;
        for (int i = 0; i < 2; ++i) {
            v.phase[i] += v.inc[i];
            if (v.phase[i] >= 1.0f) v.phase[i] -= 1.0f;
        }
        v.lp += 0.5f * (noise() - v.lp);
        s += v.lp * v.clickEnv * P.p1 * v.toneGain;
        v.env *= v.coef;
        v.clickEnv *= v.clickCoef;
        s *= 0.7f;
        break;
    }
    case Family::Shaker: {
        const auto o = downspout::dsp::runSvf(v.svf, v.coeff, static_cast<double>(noise()));
        float e;
        if (v.n <= v.attack) {
            e = static_cast<float>(v.n) / static_cast<float>(v.attack);
        } else {
            e = v.env;
            v.env *= v.coef;
        }
        if (P.p2 > 0.0f) {  // rattle: amplitude modulation
            v.rattlePhase += P.p2 * dt;
            const float am = 0.5f + 0.5f * std::sin(kTwoPi * v.rattlePhase);
            e *= am * am;
        }
        s = static_cast<float>(v.coeff.k * o.bp) * e * 2.5f;
        break;
    }
    case Family::Clap: {
        const auto o = downspout::dsp::runSvf(v.svf, v.coeff, static_cast<double>(noise()));
        const float band = static_cast<float>(v.coeff.k * o.bp);
        if (P.p1 > 0.0f) {
            if (v.strikesLeft > 0 && ++v.age >= v.strikeEvery) {
                v.age = 0;
                --v.strikesLeft;
                v.env = 1.0f;
                v.coef = v.strikesLeft > 0 ? v.pcoef[1] : v.pcoef[0];
            }
            s = band * v.env * 2.0f;
            v.env *= v.coef;
        } else {  // guiro: a scraper dragged over teeth
            v.scrapePhase += P.p2 * dt;
            const float tooth = std::exp(-5.0f * (v.scrapePhase - std::floor(v.scrapePhase)));
            const float fall = 1.0f - static_cast<float>(v.n) / static_cast<float>(std::max(1L, v.dur));
            s = band * tooth * fall * 2.0f;
        }
        break;
    }
    }

    if (v.choking) {
        v.chokeGain -= dt / 0.003f;
        if (v.chokeGain <= 0.0f) {
            v.active = false;
            return 0.0f;
        }
        s *= v.chokeGain;
    }
    return s;
}

void ClatterEngine::render(float* outL, float* outR, const int n)
{
    const float level = params_.level;
    for (Voice& v : voices_) {
        if (!v.active)
            continue;
        const float gain = v.velocity * kPatches[v.sound].gain * level;
        for (int i = 0; i < n && v.active; ++i) {
            const float s = renderVoice(v) * gain;
            outL[i] += s * v.gainL;
            outR[i] += s * v.gainR;
        }
    }
}

bool ClatterEngine::active() const
{
    for (const Voice& v : voices_)
        if (v.active)
            return true;
    return false;
}

}  // namespace clatter
