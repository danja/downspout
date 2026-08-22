#pragma once

#include "syrinx_params.hpp"
#include "modules/BiquadFilter.hpp"

#include <algorithm>
#include <array>
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
        , formant3_(sampleRate, BiquadFilter::Type::Bandpass)
        , formant4_(sampleRate, BiquadFilter::Type::Bandpass)
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
        formant3_.setSampleRate(sr);
        formant4_.setSampleRate(sr);
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

        // Gamma: time-scale so that beta≈1 at this pitch.
        // gammaScale now driven by regime (independent of harmonic band level).
        // regime=0 → gammaScale=1 (tonal); regime=1 → gammaScale=4 (pulse/chaotic).
        regime_      = 1.0f + p.regime * 3.0f;
        gammaScale_  = regime_;
        baseGamma_   = std::clamp(baseF0_ / kPhiAtBetaOne, 1200.0f, 150000.0f);
        gamma_       = baseGamma_ * gammaScale_;

        oversample_  = oversampleForGamma(gamma_);
        dt_          = 1.0f / (sampleRate_ * static_cast<float>(oversample_));

        // Timbre: blends tractQ and timbreGain
        tractQ_      = 9.0f - p.timbre * 7.0f;                     // 9→2
        timbreGain_  = 1.0f + p.timbre * 1.5f;                     // 1→2.5
        betaMin_     = 0.002f;   // Lyrebird value; enables pulse/chaotic regime

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

        // Formant filters: 4-partial tracheal comb (f1, 3f1, 5f1, 7f1) in cascade.
        // Cascade produces antiresonances between peaks, giving hollow/reedy timbres
        // that parallel summing cannot produce. f3/f4 derived from f1.
        formant1Hz_  = std::clamp(p.formant1Hz, 200.0f, 8000.0f);
        formant2Hz_  = std::clamp(p.formant2Hz, 200.0f, 8000.0f);
        formant3Hz_  = std::min(formant1Hz_ * 5.0f, 8000.0f);
        formant4Hz_  = std::min(formant1Hz_ * 7.0f, 8000.0f);
        formantQ_    = std::clamp(p.formantQ, 0.7f, 20.0f);
        coupling_    = std::clamp(p.coupling, 0.0f, 1.0f);
        voiceOffset_ = std::clamp(p.voiceOffset, 0.0f, 1.0f);
        tracheaCm_   = std::clamp(p.tracheaCm, 0.0f, 10.0f);

        formant1_.setParameters(formant1Hz_, formantQ_);  formant1_.reset();
        formant2_.setParameters(formant2Hz_, formantQ_);  formant2_.reset();
        formant3_.setParameters(formant3Hz_, formantQ_);  formant3_.reset();
        formant4_.setParameters(formant4Hz_, formantQ_);  formant4_.reset();

        // Air-sac respiration ODE: reset state
        respirX_ = 0.0f;
        respirP_ = 0.0f;
        psBase_  = 1.0f;
        // Pre-calibrate: run ODE 50 ms with constant forcing to find peak pressure
        if (respiration_ > 0.0f) {
            float rx = 0.0f, rp = 0.0f, peakP = 0.001f;
            const float dt = 1.0f / sampleRate_;
            for (int i = 0; i < static_cast<int>(0.05f * sampleRate_); ++i) {
                const float aP = (rp <= 0.0f) ? kRespAlphaI : kRespAlphaI / kRespAlphaR;
                const float dx = (-(1.0f + rx*rx)*rx - rp + kRespF0) / kRespTauX;
                const float dp = (-(1.0f + rx*rx)*rx - (1.0f + aP)*rp + kRespF0) / kRespTauP;
                rx += dx * dt;
                rp += dp * dt;
                if (rp > peakP) peakP = rp;
            }
            psBase_  = peakP;
            respirX_ = 0.0f;
            respirP_ = 0.0f;
        }

        // Second oscillator: compute gamma and oversample from the offset
        gamma2_      = gamma_ * (1.0f + voiceOffset_) * (1.0f + coupling_ * 0.02f);
        oversample2_ = oversampleForGamma(gamma2_);
        x2_ = kOdeX0 * 1.5f;
        y2_ = 0.0f;

        // Physics tracheal coupling ring buffer
        piRing_.fill(0.0f);
        piRingPos_ = 0;
        // Round-trip delay: 2L/c samples
        piDelaySamples_ = 2.0f * tracheaCm_ * 0.01f / 344.0f * sampleRate_;

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
            amFactor = 1.0f - amDepth_ * (0.5f - 0.5f * std::cos(amPhase_));
        }

        // Pressure drives ODE physics (level_ applied as output gain below)
        const float pressure = std::clamp(env * velocity_ * timbreGain_ * amFactor, 0.0f, 1.4f);

        // Air-sac ODE: modulate syringeal pressure ps multiplicatively.
        // When respiration_=0, ps == pressure (no change to behavior).
        float ps = pressure;
        if (respiration_ > 0.0f && pressure > 0.001f) {
            const float ft = env;
            const float aP = (respirP_ <= 0.0f) ? kRespAlphaI : kRespAlphaI / kRespAlphaR;
            const float dt = 1.0f / sampleRate_;
            const float dx = (-(1.0f + respirX_*respirX_)*respirX_ - respirP_ + kRespF0 * ft) / kRespTauX;
            const float dp = (-(1.0f + respirX_*respirX_)*respirX_ - (1.0f + aP)*respirP_ + kRespF0 * ft) / kRespTauP;
            respirX_ += dx * dt;
            respirP_ += dp * dt;
            if (!(std::abs(respirX_) < 1e6f)) { respirX_ = 0.0f; respirP_ = 0.0f; }
            ps = std::clamp(std::max(0.0f, respirP_) / psBase_ * pressure, 0.0f, 1.4f);
        }

        const float alpha = 0.05f + 0.4f * ps;

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

        // Physics tracheal coupling: compute pi(t) from previous sample's y_ values
        // (weak-coupling / explicit Euler approximation — valid for coupling_ < 0.5)
        float alphaA = alpha;
        float alphaB = alpha;
        if (coupling_ > 0.0f && tracheaCm_ > 0.0f) {
            const float betaInj = coupling_ * 0.5f;
            const float ps_a = ps;
            const float ps_b = ps;

            // Fractional delay read from ring buffer
            const float frac  = piDelaySamples_ - std::floor(piDelaySamples_);
            const int delayI  = static_cast<int>(std::floor(piDelaySamples_));
            const int posA    = (piRingPos_ - delayI      + kPiRingLen) % kPiRingLen;
            const int posB    = (piRingPos_ - delayI - 1  + kPiRingLen) % kPiRingLen;
            const float piDelayed = piRing_[posA] * (1.0f - frac) + piRing_[posB] * frac;

            const float injectA = std::sqrt(std::max(0.0f, ps_a)) * (y_  / gamma_);
            const float injectB = std::sqrt(std::max(0.0f, ps_b)) * (y2_ / gamma2_);
            const float piNow   = betaInj * (injectA + injectB) - kTrachRefl * piDelayed;
            piRing_[piRingPos_] = piNow;
            piRingPos_ = (piRingPos_ + 1) % kPiRingLen;

            alphaA = alpha - kTrachAlphaSlope * piNow;
            alphaB = alpha - kTrachAlphaSlope * piNow;
        }

        // ODE integration (primary syringeal side)
        {
            const float g  = gamma_;
            const float g2 = g * g;
            float xv = x_, yv = y_;
            for (int s = 0; s < oversample_; ++s) {
                const float dx = yv;
                const float dy = -alphaA*g2 - beta*g2*xv - g2*xv*xv*xv
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

        // Apply air-sac pressure scaling to source: √(ps/pressure) per Laje & Mindlin low-freq limit
        if (respiration_ > 0.0f && pressure > 0.001f) {
            source *= std::sqrt(ps / pressure);
        }

        // Turbulence noise injection (at labia, before tract)
        if (noiseRng_.has_value() && noise_ > 0.0f) {
            const float flow = std::max(ps - kNoisePsFloor, 0.0f);
            source += noise_ * kNoiseGain * flow * noiseRng_->next();
        }

        // Second oscillator (simple linear mix when tracheaCm_==0, physics mix otherwise)
        if (coupling_ > 0.0f) {
            const float g2  = gamma2_;
            const float g22 = g2 * g2;
            float xv = x2_, yv = y2_;
            for (int s = 0; s < oversample2_; ++s) {
                const float dx = yv;
                const float dy = -alphaB*g22 - beta*g22*xv - g22*xv*xv*xv
                                 - g2*xv*xv*yv + g22*xv*xv - g2*xv*yv;
                xv += dx * dt_;
                yv += dy * dt_;
            }
            if (!(std::abs(xv) < 1e6f && std::abs(yv) < 1e12f)) { xv = kOdeX0 * 1.5f; yv = 0.0f; }
            x2_ = xv; y2_ = yv;
            const float ampPP2 = 0.5f + 3.4f * alpha + 0.62f * betaClean;
            const float source2 = (y2_ / g2) * kSourceGain / ampPP2;
            source = source * (1.0f - coupling_ * 0.4f) + source2 * coupling_ * 0.4f;

            // Write piNow after both y_ values are updated (physics coupling only)
            // (already written above before integration if tracheaCm_>0; nothing extra needed)
        }

        // 180 Hz highpass on source (mirrors syrinx-processor §8.1)
        const float hp = source - srcHpX_ + dcCoeff_ * srcHpY_;
        srcHpX_ = source;
        srcHpY_ = hp;

        // Tract filter + harmonic band (harmonic_ now solely controls band level)
        float filtered = tract_.process(hp);
        if (harmonic_ > 0.0f)
            filtered += harmonic_ * 0.9f * tractHarm_.process(hp);

        // Tracheal comb: 4-partial cascade (f1, 3f1, 5f1, 7f1).
        // Cascade two-at-a-time produces antiresonances between peaks — the zeros that give
        // hollow, reedy, and nasal bird timbres. Parallel summing cannot produce spectral zeros.
        filtered += kFormantGain * formant4_.process(
                        formant3_.process(
                            formant2_.process(
                                formant1_.process(hp))));

        // DC block then tracheal LP
        const float dcOut = filtered - dcX_ + dcCoeff_ * dcY_;
        dcX_ = filtered; dcY_ = dcOut;
        lpY_ += lpCoeff_ * (dcOut - lpY_);

        return lpY_ * env * level_;
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
    static constexpr float kFormantGain   = 0.25f;  // fixed formant mix level

    // Air-sac ODE constants (Fainstein, Goller & Mindlin 2025, Table S2)
    static constexpr float kRespTauX   = 0.25f;
    static constexpr float kRespTauP   = 0.20f;
    static constexpr float kRespAlphaI = 0.05f;
    static constexpr float kRespAlphaR = 0.125f;  // alpha_i / alpha_o
    static constexpr float kRespF0     = 35.0f;

    // Tracheal physics coupling constants
    static constexpr int   kPiRingLen       = 64;   // safe at 96 kHz with 10 cm tube
    static constexpr float kTrachRefl       = 0.9f;
    static constexpr float kTrachAlphaSlope = 0.40f;

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
    BiquadFilter tract_, tractHarm_, formant1_, formant2_, formant3_, formant4_;
    float srcHpX_ = 0, srcHpY_ = 0;
    float dcX_ = 0, dcY_ = 0, lpY_ = 0;
    float dcCoeff_, lpCoeff_;

    // ODE state (primary + coupling)
    float x_ = kOdeX0, y_ = 0.0f;
    float x2_ = kOdeX0, y2_ = 0.0f;
    float gamma_, gamma2_, baseGamma_, gammaScale_, regime_;
    float dt_;
    int   oversample_;
    int   oversample2_ = 16;

    // Air-sac respiration ODE state
    float respirX_ = 0.0f, respirP_ = 0.0f, psBase_ = 1.0f;

    // Physics tracheal coupling
    std::array<float, kPiRingLen> piRing_{};
    int   piRingPos_       = 0;
    float piDelaySamples_  = 0.0f;

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
    float durationSec_, respiration_;
    float formant1Hz_, formant2Hz_, formant3Hz_, formant4Hz_, formantQ_;
    float coupling_, voiceOffset_, tracheaCm_;

    std::uint8_t midiNote_ = 0;
    int sampleCount_   = 0;
    int filterCounter_ = 0;
    bool active_       = false;
};

} // namespace downspout::syrinx
