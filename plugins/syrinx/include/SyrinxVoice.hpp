#pragma once

#include "syrinx_params.hpp"
#include "modules/BiquadFilter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace downspout::syrinx {

// ---- xorshift32, mirrors syrinx-processor.js Xorshift ----
struct Xorshift32 {
    std::uint32_t state;

    explicit Xorshift32(std::uint32_t stream)
    {
        // mirrors JS: state = (stream * 2654435761 + 1) >>> 0; if 0 use constant
        std::uint32_t s = stream * 2654435761u + 1u;
        if (s == 0u) s = 0x9e3779b9u;
        state = s;
        for (int i = 0; i < 8; ++i) next();
    }

    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<std::int32_t>(state) * (1.0f / 2147483648.0f);
    }
};

// ---- Mindlin-Laje syrinx oscillator voice ----
class SyrinxVoice {
public:
    explicit SyrinxVoice(float sampleRate = 48000.0f)
        : sampleRate_(sampleRate)
        , tract_(sampleRate, BiquadFilter::Type::Bandpass)
        , tractHarm_(sampleRate, BiquadFilter::Type::Bandpass)
        , formant1_(sampleRate, BiquadFilter::Type::Bandpass)
        , formant2_(sampleRate, BiquadFilter::Type::Bandpass)
    {
        dcCoeff_ = std::exp(-2.0f * kPi * 180.0f / sampleRate);
        lpCoeff_ = 1.0f - std::exp(-2.0f * kPi * 12000.0f / sampleRate);
    }

    void setSampleRate(float sr)
    {
        sampleRate_ = sr;
        dcCoeff_ = std::exp(-2.0f * kPi * 180.0f / sr);
        lpCoeff_ = 1.0f - std::exp(-2.0f * kPi * 12000.0f / sr);
        tract_.setSampleRate(sr);
        tractHarm_.setSampleRate(sr);
        formant1_.setSampleRate(sr);
        formant2_.setSampleRate(sr);
    }

    // Trigger a new note. stream = unique noise stream id for xorshift.
    void trigger(std::uint8_t midiNote, float velocity, const PresetParams& p, std::uint32_t stream)
    {
        // base pitch from MIDI + preset pitch offset
        baseF0_  = 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
        baseF0_ *= std::pow(2.0f, p.pitchSemitones / 12.0f);
        midiNote_= midiNote;
        velocity_= velocity;

        // ODE initial state
        x_ = kOdeX0;
        y_ = 0.0f;

        // Gamma: time-scale so that beta≈1 at this pitch; gammaScale shifts ODE regime
        gammaScale_  = 1.0f + p.harmonic;          // range 1–2
        baseGamma_   = std::clamp(baseF0_ / kPhiAtBetaOne, 1200.0f, 150000.0f);
        gamma_       = baseGamma_ * gammaScale_;

        oversample_  = oversampleForGamma(gamma_);
        dt_          = 1.0f / (sampleRate_ * static_cast<float>(oversample_));

        // Timbre: blends tractQ and timbreGain
        tractQ_      = 9.0f - p.timbre * 7.0f;                     // 9→2
        timbreGain_  = 1.0f + p.timbre * 1.5f;                     // 1→2.5
        betaMin_     = 0.15f;

        // Store preset params
        noise_       = std::clamp(p.noise, 0.0f, 1.0f);
        roughness_   = std::clamp(p.roughness, 0.0f, 1.0f);
        vibratoRateHz_    = p.vibratoRateHz;
        vibratoDepthCents_= p.vibratoDepthCents;
        bend_        = std::clamp(p.bend, -1.0f, 1.0f);
        harmonic_    = std::clamp(p.harmonic, 0.0f, 1.0f);
        amRateHz_    = p.amRateHz;
        amDepth_     = std::clamp(p.amDepth, 0.0f, 1.0f);
        level_       = std::clamp(p.level, 0.0f, 1.4f);
        durationSec_ = std::max(0.05f, p.durationSec);
        respiration_ = std::clamp(p.respiration, 0.0f, 1.0f);
        breathPhase_ = 0.0f;

        // Formant filters (fixed-frequency resonances)
        formant1Hz_  = std::clamp(p.formant1Hz, 200.0f, 8000.0f);
        formant2Hz_  = std::clamp(p.formant2Hz, 200.0f, 8000.0f);
        formantQ_    = std::clamp(p.formantQ, 0.7f, 20.0f);
        coupling_    = std::clamp(p.coupling, 0.0f, 1.0f);
        voiceOffset_ = std::clamp(p.voiceOffset, 0.0f, 1.0f);

        formant1_.setParameters(formant1Hz_, formantQ_);
        formant1_.reset();
        formant2_.setParameters(formant2Hz_, formantQ_);
        formant2_.reset();

        // Second oscillator: compute its own oversample from the offset gamma
        {
            const float g2base = gamma_ * (1.0f + voiceOffset_);
            oversample2_ = oversampleForGamma(g2base);
        }

        // Coupling: second oscillator state (slightly offset initial conditions)
        x2_ = kOdeX0 * 1.5f;
        y2_ = 0.0f;

        // Roughness LP
        roughCoeff_ = std::exp(-2.0f * kPi * 140.0f / sampleRate_);
        const float c = roughCoeff_;
        roughScale_ = std::sqrt(3.0f) * std::sqrt((1.0f + c) / (1.0f - c));
        roughLp_    = 0.0f;

        // Noise PRNG
        noiseRng_.reset();
        roughRng_.reset();
        if (noise_ > 0.0f)    noiseRng_.emplace(stream * 4u + 1u);
        if (roughness_ > 0.0f) roughRng_.emplace(stream * 4u + 2u);

        // Source HP & DC states
        srcHpX_ = srcHpY_ = 0.0f;
        dcX_ = dcY_ = lpY_ = 0.0f;

        // Filters
        tract_.reset();
        tractHarm_.reset();

        // Envelope
        envState_   = EnvAttack;
        envValue_   = 0.0f;
        attackSamples_ = static_cast<int>(std::max(1.0f, kDefaultAttack * sampleRate_));
        releaseTau_ = kReleaseTau * sampleRate_;   // samples

        // Vibrato phase
        vibratoPhase_ = 0.0f;
        amPhase_      = 0.0f;

        sampleCount_  = 0;
        active_       = true;
        filterCounter_= 0;
    }

    void release()
    {
        if (active_ && envState_ != EnvRelease)
            envState_ = EnvRelease;
    }

    void kill()
    {
        active_   = false;
        envState_ = EnvIdle;
    }

    [[nodiscard]] bool isActive() const { return active_; }
    [[nodiscard]] bool isReleasing() const { return envState_ == EnvRelease; }
    [[nodiscard]] std::uint8_t midiNote() const { return midiNote_; }

    float process()
    {
        if (!active_) return 0.0f;

        ++sampleCount_;
        const float t = static_cast<float>(sampleCount_) / sampleRate_;

        // Auto-release after syllable duration
        if (envState_ == EnvSustain && t >= durationSec_)
            envState_ = EnvRelease;

        // Envelope
        const float env = updateEnvelope();
        if (!active_) return 0.0f;

        // Vibrato
        const float vibDepth = vibratoDepthCents_ / 1200.0f; // cents→semitones→ratio factor
        vibratoPhase_ += 2.0f * kPi * vibratoRateHz_ / sampleRate_;
        if (vibratoPhase_ > 2.0f * kPi) vibratoPhase_ -= 2.0f * kPi;
        const float vibFactor = 1.0f + vibDepth * std::sin(vibratoPhase_);

        // Pitch contour: linear log sweep over the first kBendDuration seconds
        const float bendU = std::min(1.0f, t / kBendDuration);
        const float f0 = baseF0_ * vibFactor * std::exp(bend_ * 1.5f * kLn2 * (bendU - 0.5f));

        // AM modulation of pressure
        float amFactor = 1.0f;
        if (amRateHz_ > 0.0f && amDepth_ > 0.0f) {
            amPhase_ += 2.0f * kPi * amRateHz_ / sampleRate_;
            if (amPhase_ > 2.0f * kPi) amPhase_ -= 2.0f * kPi;
            // amDepth_=0: no modulation, amDepth_=1: full trill 0→1
            amFactor = 1.0f - amDepth_ * (0.5f - 0.5f * std::cos(amPhase_));
        }

        // Pressure drives ODE physics (level_ applied as output gain below)
        const float pressure = std::clamp(env * velocity_ * timbreGain_ * amFactor, 0.0f, 1.4f);
        const float alpha    = 0.05f + 0.4f * pressure;

        // Beta (with gammaScale: gammaScale>1 → lower beta → richer harmonics)
        const float betaClean = std::clamp(1.0f / (gammaScale_ * gammaScale_), betaMin_, 16.0f);
        const float beta      = std::clamp(betaClean * roughFactor(), betaMin_, 16.0f);

        // Update tract filter every 32 samples and when pitch changes significantly
        ++filterCounter_;
        if ((filterCounter_ & 31) == 0) {
            tract_.setParameters(f0, tractQ_);
            if (harmonic_ > 0.0f)
                tractHarm_.setParameters(f0 * 2.0f, tractQ_);
        }

        // ODE integration (single syringeal side)
        {
            const float g  = gamma_;
            const float g2 = g * g;
            float xv = x_, yv = y_;
            for (int s = 0; s < oversample_; ++s) {
                const float dx = yv;
                const float dy = -alpha*g2 - beta*g2*xv - g2*xv*xv*xv
                                 - g*xv*xv*yv + g2*xv*xv - g*xv*yv;
                xv += dx * dt_;
                yv += dy * dt_;
            }
            if (!(std::abs(xv) < 1e6f && std::abs(yv) < 1e12f)) { xv = kOdeX0; yv = 0.0f; }
            x_ = xv; y_ = yv;
        }

        // Source signal
        const float ampPP  = 0.5f + 3.4f * alpha + 0.62f * betaClean;
        float source = (y_ / gamma_) * kSourceGain / ampPP;

        // Turbulence noise injection (at labia, before tract)
        if (noiseRng_.has_value() && noise_ > 0.0f) {
            const float flow = std::max(pressure - kNoisePsFloor, 0.0f);
            source += noise_ * kNoiseGain * flow * noiseRng_->next();
        }

        // Coupling: second ODE at voice-offset frequency + tiny coupling detune → beating/chorus
        if (coupling_ > 0.0f) {
            const float g2  = gamma_ * (1.0f + voiceOffset_) * (1.0f + coupling_ * 0.02f);
            const float g22 = g2 * g2;
            float xv = x2_, yv = y2_;
            for (int s = 0; s < oversample2_; ++s) {
                const float dx = yv;
                const float dy = -alpha*g22 - beta*g22*xv - g22*xv*xv*xv
                                 - g2*xv*xv*yv + g22*xv*xv - g2*xv*yv;
                xv += dx * dt_;
                yv += dy * dt_;
            }
            if (!(std::abs(xv) < 1e6f && std::abs(yv) < 1e12f)) { xv = kOdeX0 * 1.5f; yv = 0.0f; }
            x2_ = xv; y2_ = yv;
            const float ampPP2 = 0.5f + 3.4f * alpha + 0.62f * betaClean;
            const float source2 = (y2_ / g2) * kSourceGain / ampPP2;
            source = source * (1.0f - coupling_ * 0.4f) + source2 * coupling_ * 0.4f;
        }

        // 180 Hz highpass on source (mirrors syrinx-processor §8.1)
        const float hp = source - srcHpX_ + dcCoeff_ * srcHpY_;
        srcHpX_ = source;
        srcHpY_ = hp;

        // Tract filter + harmonic
        float filtered = tract_.process(hp);
        if (harmonic_ > 0.0f)
            filtered += harmonic_ * 0.9f * tractHarm_.process(hp);

        // Fixed-frequency formant resonances (supplementary vocal-tract coloring)
        filtered += kFormantGain * formant1_.process(hp);
        filtered += kFormantGain * formant2_.process(hp);

        // DC block then tracheal LP
        const float dcOut = filtered - dcX_ + dcCoeff_ * dcY_;
        dcX_ = filtered; dcY_ = dcOut;
        lpY_ += lpCoeff_ * (dcOut - lpY_);

        // Breathing modulation
        float breathFactor = 1.0f;
        if (respiration_ > 0.0f) {
            breathPhase_ += 2.0f * kPi * kBreathRateHz / sampleRate_;
            if (breathPhase_ > 2.0f * kPi) breathPhase_ -= 2.0f * kPi;
            breathFactor = 1.0f - respiration_ * 0.5f * (1.0f - std::cos(breathPhase_));
        }

        return lpY_ * env * level_ * breathFactor;
    }

private:
    static constexpr float kPi            = 3.14159265359f;
    static constexpr float kLn2           = 0.693147180559f;
    static constexpr float kOdeX0         = 1.0e-4f;
    static constexpr float kPhiAtBetaOne  = 0.17061f;
    static constexpr float kSourceGain    = 5.0f;   // boosted from Lyrebird's 0.35 (no compressor here)
    static constexpr float kNoiseGain     = 6.0f;
    static constexpr float kNoisePsFloor  = 0.05f;
    static constexpr float kDefaultAttack = 0.008f;
    static constexpr float kReleaseTau    = 0.06f;  // 60 ms release
    static constexpr float kBendDuration  = 0.5f;   // pitch sweep over first 0.5 s
    static constexpr float kBreathRateHz  = 0.8f;   // breathing cycle rate
    static constexpr float kFormantGain   = 0.25f;  // fixed formant mix level

    enum EnvState { EnvIdle, EnvAttack, EnvSustain, EnvRelease };

    int oversampleForGamma(float gamma) const
    {
        return std::min(64, std::max(16, static_cast<int>(std::ceil(gamma / (0.06f * sampleRate_)))));
    }

    float roughFactor()
    {
        if (!roughRng_.has_value() || roughness_ <= 0.0f) return 1.0f;
        roughLp_ = (1.0f - roughCoeff_) * roughRng_->next() + roughCoeff_ * roughLp_;
        const float f = 1.0f + 0.30f * roughness_ * roughLp_ * roughScale_;
        return std::clamp(f, 0.1f, 4.0f);
    }

    float updateEnvelope()
    {
        switch (envState_) {
        case EnvAttack:
            envValue_ += 1.0f / static_cast<float>(attackSamples_);
            if (envValue_ >= 1.0f) {
                envValue_ = 1.0f;
                envState_ = EnvSustain;
            }
            return envValue_;
        case EnvSustain:
            return 1.0f;
        case EnvRelease: {
            envValue_ -= envValue_ / releaseTau_;
            if (envValue_ < 1e-5f) {
                envValue_ = 0.0f;
                envState_ = EnvIdle;
                active_   = false;
            }
            return envValue_;
        }
        case EnvIdle:
        default:
            active_ = false;
            return 0.0f;
        }
    }

    // ---- DSP state ----
    float sampleRate_;
    BiquadFilter tract_, tractHarm_, formant1_, formant2_;
    float srcHpX_ = 0, srcHpY_ = 0;
    float dcX_ = 0, dcY_ = 0, lpY_ = 0;
    float dcCoeff_, lpCoeff_;

    // ODE state (primary + coupling)
    float x_ = kOdeX0, y_ = 0.0f;
    float x2_ = kOdeX0, y2_ = 0.0f;
    float gamma_, baseGamma_, gammaScale_;
    float dt_;
    int   oversample_;
    int   oversample2_ = 16;

    // Envelope
    EnvState envState_ = EnvIdle;
    float    envValue_ = 0.0f;
    int      attackSamples_;
    float    releaseTau_;

    // Vibrato & AM
    float vibratoPhase_ = 0.0f;
    float amPhase_      = 0.0f;

    // Roughness LP
    float roughLp_ = 0.0f, roughCoeff_, roughScale_;

    // Noise PRNGs (optional)
    std::optional<Xorshift32> noiseRng_;
    std::optional<Xorshift32> roughRng_;

    // Preset params (captured at trigger)
    float baseF0_, velocity_;
    float noise_, roughness_, timbre_, timbreGain_, tractQ_, betaMin_;
    float vibratoRateHz_, vibratoDepthCents_;
    float bend_, harmonic_, amRateHz_, amDepth_, level_;
    float durationSec_, respiration_, breathPhase_ = 0.0f;
    float formant1Hz_, formant2Hz_, formantQ_, coupling_, voiceOffset_;

    std::uint8_t midiNote_ = 0;
    int sampleCount_   = 0;
    int filterCounter_ = 0;
    bool active_       = false;
};

} // namespace downspout::syrinx
