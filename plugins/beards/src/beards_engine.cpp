// Beards DSP: a portable port of the Mutable Instruments Beads granular
// processor (Copyright 2017 Emilie Gillet, MIT licence). The grain engine,
// trigger generator, envelopes, voice stealing, delay line, feedback path and
// dry/wet law follow the original; the reverb is a compact Griesinger/Dattorro
// tank written for this plugin. See docs/design.md.

#include "beards_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace downspout::beards {

namespace {

constexpr int kNumGrains = 32;
constexpr int kInterpolationTail = 4;
constexpr int kFreezeTailSize = 256;
constexpr int kSafetyZone = 256;  // DelayGrain::head_crossing_safety_zone
constexpr std::uint32_t kChunk = 256;
constexpr float kPi = 3.14159265358979323846f;

struct Frame {
    float l = 0.0f;
    float r = 0.0f;
};

inline Frame operator+(Frame a, Frame b) { return {a.l + b.l, a.r + b.r}; }
inline Frame operator-(Frame a, Frame b) { return {a.l - b.l, a.r - b.r}; }
inline Frame operator*(Frame a, float k) { return {a.l * k, a.r * k}; }
inline Frame operator*(float k, Frame a) { return {a.l * k, a.r * k}; }

inline float semitonesToRatio(const float s) { return std::exp2(s / 12.0f); }
inline float smoothStep(const float t) { return t * t * (3.0f - 2.0f * t); }

inline float softClip(const float x)
{
    if (x < -3.0f)
        return -1.0f;
    if (x > 3.0f)
        return 1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float sanitize(const float x) { return std::isfinite(x) ? x : 0.0f; }

struct Rng {
    std::uint32_t state = 2463534242u;
    float uniform()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
};

// ── Storage quality ─────────────────────────────────────────────────────────
// Beads stores 16, 12 or 8 (mu-law) bit samples; we keep floats and quantise on
// write so the noise floor and tonal character carry over.

float quantise(const int quality, float x)
{
    x = std::clamp(x, -1.0f, 1.0f);
    if (quality == 1)
        return std::round(x * 2048.0f) / 2048.0f;
    if (quality == 2)
    {
        const float k = std::log(256.0f);
        const float sign = x < 0.0f ? -1.0f : 1.0f;
        const float companded = std::log1p(255.0f * std::fabs(x)) / k;
        const float q = std::round(companded * 127.0f) / 127.0f;
        return sign * (std::exp(q * k) - 1.0f) / 255.0f;
    }
    return x;
}

// ── Limiter ─────────────────────────────────────────────────────────────────

class Limiter {
public:
    void init(const float decay, const float maxPeak)
    {
        peak_ = 0.5f;
        peakLp_ = 0.5f;
        decay_ = decay;
        maxPeak_ = maxPeak;
    }

    void process(Frame& f)
    {
        const float peak = std::min(std::max(std::fabs(f.l), std::fabs(f.r)), 40.0f);
        peak_ += (peak - peak_) * (peak > peak_ ? 0.1f : decay_);
        peakLp_ += (peak_ - peakLp_) * (peak_ > peakLp_ ? 0.5f : decay_);
        const float gain = peakLp_ <= maxPeak_ ? 1.0f : maxPeak_ / peakLp_;
        f.l *= gain;
        f.r *= gain;
    }

private:
    float peak_ = 0.5f, peakLp_ = 0.5f, decay_ = 0.0f, maxPeak_ = 1.0f;
};

// 40 Hz, Q 0.5 high-pass (Beads' DC blocker), TPT state-variable form.
class HighPass {
public:
    void init(const float cutoffOverSr, const float q)
    {
        g_ = std::tan(kPi * cutoffOverSr);
        k_ = 1.0f / q;
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
        ic1_ = ic2_ = 0.0f;
    }

    float process(const float x)
    {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        return x - k_ * v1 - v2;
    }

private:
    float g_ = 0.0f, k_ = 2.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f, ic1_ = 0.0f, ic2_ = 0.0f;
};

// ── Grain envelopes ─────────────────────────────────────────────────────────

class GrainEnvelope {
public:
    void init(const float sampleRate)
    {
        stage_ = kDone;
        phase_ = 0.0f;
        inc_ = {0.5f, 0.5f, 0.5f, 0.0f};
        smoothness_ = 0.0f;
        minAttack_ = 0.0005f * sampleRate;
        minRelease_ = 0.010f * sampleRate;
    }

    void start(const float duration, float shape, const float fractionalSample)
    {
        stage_ = kAttack;
        phase_ = 0.0f;

        const float sustain = std::max(1.0f - 3.0f * shape, 0.0f);
        shape = (shape - 0.33333333f) * 1.5f;
        if (shape < 0.0f)
            shape = 0.0f;
        else if (shape < 0.5f)
            shape = shape * shape * shape * 4.0f;
        const float attack = shape;
        const float release = (1.0f - attack) * (1.0f - sustain);

        const float attackDuration = std::max(duration * attack, minAttack_ * (1.0f + 3.0f * sustain));
        const float sustainDuration = std::max(duration * sustain, 2.0f);
        const float releaseDuration = std::max(duration * release, minRelease_);

        inc_[kAttack] = 1.0f / attackDuration;
        inc_[kSustain] = 1.0f / sustainDuration;
        inc_[kRelease] = 1.0f / releaseDuration;
        smoothness_ = attack < 0.5f ? 2.0f * attack : 2.0f - 2.0f * attack;

        phase_ += inc_[stage_] * fractionalSample;
        wrapPhase();
    }

    void stop() { stage_ = kDone; }
    bool done() const { return stage_ >= kDone; }

    float render()
    {
        if (stage_ == kDone)
            return 0.0f;

        float value;
        if (stage_ == kSustain)
        {
            value = 1.0f;
        }
        else
        {
            const bool attack = stage_ == kAttack;
            const float t = attack ? phase_ : 1.0f - phase_;
            const float base = attack ? t : (t * t) * (t * t);
            value = base + (smoothStep(t) - base) * smoothness_;
        }
        phase_ += inc_[stage_];
        wrapPhase();
        return value;
    }

private:
    enum Stage { kAttack, kSustain, kRelease, kDone };

    void wrapPhase()
    {
        if (phase_ >= 1.0f)
        {
            const float t = (phase_ - 1.0f) / inc_[stage_];
            stage_ = static_cast<Stage>(static_cast<int>(stage_) + 1);
            phase_ = t * inc_[stage_];
        }
    }

    Stage stage_ = kDone;
    float phase_ = 0.0f;
    std::array<float, 4> inc_ {};
    float smoothness_ = 0.0f;
    float minAttack_ = 0.0f, minRelease_ = 0.0f;
};

class VoiceStealingEnvelope {
public:
    void resetAndPause()
    {
        value_ = 1.0f;
        decrement_ = 0.0f;
    }
    int start(const float duration)
    {
        decrement_ = 1.0f / duration;
        return static_cast<int>(value_ * duration);
    }
    float render()
    {
        value_ -= decrement_;
        return std::max(value_, 0.0f);
    }
    bool done() const { return value_ <= 0.0f; }

private:
    float value_ = 1.0f, decrement_ = 0.0f;
};

// ── Delay line (stereo float, newest sample at the lowest index) ────────────

class DelayLine {
public:
    void init(const int maxDelay)
    {
        maxDelay_ = maxDelay;
        buffer_.assign(static_cast<std::size_t>(maxDelay + kInterpolationTail), Frame {});
        reset();
    }

    void reset()
    {
        std::fill(buffer_.begin(), buffer_.end(), Frame {});
        std::fill(freezeTail_.begin(), freezeTail_.end(), Frame {});
        writePtr_ = 0;
        tailWritePtr_ = 0;
        tailXfade_ = 0;
        frozen_ = false;
        duration_ = maxDelay_;
    }

    void write(Frame in, const bool frozen)
    {
        if (frozen)
        {
            if (!frozen_)
                tailWritePtr_ = 0;
            if (tailWritePtr_ < kFreezeTailSize)
                freezeTail_[static_cast<std::size_t>(tailWritePtr_++)] = in;
        }
        else
        {
            if (frozen_ && tailWritePtr_)
                tailXfade_ = tailWritePtr_;
            if (tailWritePtr_)
            {
                const int position = tailXfade_ - tailWritePtr_;
                const float gain = smoothStep(static_cast<float>(tailWritePtr_) / static_cast<float>(tailXfade_));
                const Frame tail = freezeTail_[static_cast<std::size_t>(position)];
                in.l += (tail.l - in.l) * gain;
                in.r += (tail.r - in.r) * gain;
                --tailWritePtr_;
            }
            store(in);
        }
        frozen_ = frozen;
    }

    // Hermite read, delay split into integer and fractional parts.
    Frame read(int delayIntegral, const float t) const
    {
        delayIntegral += maxDelay_ - 1;  // fix latency
        const int i = (writePtr_ + delayIntegral) % maxDelay_;
        const Frame x0 = buffer_[static_cast<std::size_t>(i)];
        const Frame x1 = buffer_[static_cast<std::size_t>(i + 1)];
        const Frame x2 = buffer_[static_cast<std::size_t>(i + 2)];
        const Frame x3 = buffer_[static_cast<std::size_t>(i + 3)];

        const Frame c = (x2 - x0) * 0.5f;
        const Frame v = x1 - x2;
        const Frame w = c + v;
        const Frame a = w + v + (x3 - x1) * 0.5f;
        const Frame bNeg = w + a;
        return (((a * t) - bNeg) * t + c) * t + x1;
    }

    bool frozen() const { return frozen_; }
    int duration() const { return duration_; }
    int maxDelay() const { return maxDelay_; }

private:
    void store(const Frame in)
    {
        buffer_[static_cast<std::size_t>(writePtr_)] = in;
        if (writePtr_ < kInterpolationTail)
            buffer_[static_cast<std::size_t>(writePtr_ + maxDelay_)] = in;
        writePtr_ = writePtr_ == 0 ? maxDelay_ - 1 : writePtr_ - 1;
        duration_ = std::min(duration_ + 1, maxDelay_);
    }

    std::vector<Frame> buffer_;
    std::array<Frame, kFreezeTailSize> freezeTail_ {};
    int maxDelay_ = 1;
    int writePtr_ = 0;
    int tailWritePtr_ = 0;
    int tailXfade_ = 0;
    int duration_ = 1;
    bool frozen_ = false;
};

// ── Randomised parameter (Beads, no-CV branch with a positive attenuverter) ─

struct Randomised {
    float base = 0.0f;
    float amount = 0.0f;  // 0..1

    float value(Rng& rng, const float range, const bool clampResult) const
    {
        float result = base;
        const float randomValue = clampResult ? rng.uniform() * range - result
                                              : (2.0f * rng.uniform() - 1.0f) * range;
        result += randomValue * amount;
        return clampResult ? std::clamp(result, 0.0f, 1.0f) : result;
    }
};

struct GrainParams {
    Randomised time, size, shape, pitch;
};

struct Trigger {
    bool trigger = false;
    float fractionalSample = 0.0f;
    float regularity = 1.0f;
};

// ── Grain ───────────────────────────────────────────────────────────────────

class Grain {
public:
    void init(const float sampleRate)
    {
        initialDelay_ = delay_ = 0;
        speed_ = 1024;
        gain_ = {{{0.5f, 0.5f}, {0.5f, 0.5f}}};
        envelope_.init(sampleRate);
        stealing_.resetAndPause();
        remaining_ = 0;
    }

    bool done() const { return envelope_.done() || stealing_.done(); }
    int remaining() const { return remaining_; }

    void fadeOut(const int duration)
    {
        if (remaining_ > duration)
            remaining_ = stealing_.start(static_cast<float>(duration));
    }

    static bool compare(Grain* const& lhs, Grain* const& rhs) { return lhs->remaining_ < rhs->remaining_; }

    void start(const DelayLine& line, float delay, const float speed, float duration, const float shape,
               const float gain, float pan, const float fractionalSample)
    {
        const float consumptionSpeed = speed - (line.frozen() ? 0.0f : 1.0f);
        delay -= fractionalSample * consumptionSpeed;

        const float maxDelay = static_cast<float>(line.maxDelay());
        if (consumptionSpeed != 0.0f)
            duration = std::min(duration, maxDelay / std::fabs(consumptionSpeed));

        if (consumptionSpeed > 0.0f)
            delay = std::max(delay, duration * consumptionSpeed);
        else if (consumptionSpeed < 0.0f)
            delay = std::min(delay, maxDelay + duration * consumptionSpeed);

        initialDelay_ = delay_ = static_cast<std::int32_t>(delay * 1024.0f);
        speed_ = static_cast<std::int32_t>(speed * 1024.0f);

        // Roughly equal power. Uses a first-order approximation of the square root.
        if (pan < 0.5f)
        {
            pan *= 2.0f;
            gain_[0][0] = gain;
            gain_[1][0] = 0.0f;
            gain_[1][1] = pan * (2.0f - pan) * gain;
            gain_[0][1] = (1.0f - pan) * (1.0f + pan) * gain;
        }
        else
        {
            pan = 1.0f - pan;
            pan *= 2.0f;
            gain_[1][1] = gain;
            gain_[0][1] = 0.0f;
            gain_[0][0] = pan * (2.0f - pan) * gain;
            gain_[1][0] = (1.0f - pan) * (1.0f + pan) * gain;
        }

        envelope_.start(duration, shape, fractionalSample);
        remaining_ = static_cast<int>(duration);
        stealing_.resetAndPause();
    }

    void render(const DelayLine& line, Frame& out)
    {
        if (done())
            return;

        delay_ -= speed_;
        if (!line.frozen())
            delay_ += 1024;

        const std::int32_t tail = line.maxDelay() << 10;
        const std::int32_t fadeZone = kSafetyZone << 10;

        // Head-crossing policy STOP: the grain ends when it reaches either end.
        if (delay_ < 0 || delay_ >= tail)
        {
            delay_ = 0;
            envelope_.stop();
        }

        const std::int32_t d = std::abs(delay_ >= tail / 2 ? delay_ - tail : delay_);
        const float fadeRamp = d < fadeZone ? static_cast<float>(d) / static_cast<float>(fadeZone) : 1.0f;

        const std::int32_t integral = delay_ >> 10;
        const float fractional = static_cast<float>(delay_ & 1023) / 1024.0f;
        Frame sample = line.read(integral, fractional);
        sample = sample * smoothStep(fadeRamp);
        sample = sample * (envelope_.render() * stealing_.render());

        out.l += sample.l * gain_[0][0] + sample.r * gain_[0][1];
        out.r += sample.l * gain_[1][0] + sample.r * gain_[1][1];

        --remaining_;
        if (done())
            remaining_ = 0;
    }

private:
    std::array<std::array<float, 2>, 2> gain_ {};
    GrainEnvelope envelope_;
    VoiceStealingEnvelope stealing_;
    int remaining_ = 0;
    std::int32_t delay_ = 0;
    std::int32_t speed_ = 1024;
    std::int32_t initialDelay_ = 0;
};

constexpr float kInstantCrossfadeTime = 0.005f;
constexpr float kSimpleSchedulerThreshold = 0.025f;
constexpr float kPreemptiveCrossfadeTime = 0.1f;

class GranularPlayer {
public:
    void init(const float sampleRate)
    {
        instantCrossfade_ = static_cast<int>(sampleRate * kInstantCrossfadeTime);
        preemptiveCrossfade_ = static_cast<int>(sampleRate * kPreemptiveCrossfadeTime);
        simpleSchedulerThreshold_ = static_cast<int>(sampleRate * kSimpleSchedulerThreshold);
        maxGrainDuration_ = sampleRate * 4.0f;
        grainInterval_ = 1;
        for (int i = 0; i < kNumGrains; ++i)
        {
            grain_[static_cast<std::size_t>(i)].init(sampleRate);
            priority_[static_cast<std::size_t>(i)] = &grain_[static_cast<std::size_t>(i)];
        }
    }

    int countActive() const
    {
        int n = 0;
        for (const Grain& g : grain_)
            n += g.done() ? 0 : 1;
        return n;
    }

    void render(const DelayLine& line, const Trigger& trigger, const GrainParams& p, Rng& rng, Frame& out)
    {
        if (trigger.trigger)
        {
            const float duration = p.size.value(rng, 1.0f, true);
            const bool reverse = duration < 0.3333333f;

            const float numActive = static_cast<float>(countActive() + 1);
            const float amplitude = 1.0f / std::sqrt(numActive);
            const float spread =
                std::min(p.time.amount + p.size.amount + p.shape.amount + p.pitch.amount + (1.0f - trigger.regularity),
                         1.0f);
            const float panSpread = 0.5f * numActive * spread;
            const float u = rng.uniform();
            const float v = rng.uniform();
            const float pan = 0.5f + (u - 0.5f) * std::min(panSpread * v, 1.0f);

            const int safety = kSafetyZone + 1;
            const int maxDelay = line.duration() - 2 * safety;

            const float pitchSemitones = p.pitch.value(rng, 24.0f, false);
            const float ratio = semitonesToRatio(pitchSemitones);

            priority_[0]->start(line, p.time.value(rng, 1.0f, true) * static_cast<float>(std::max(maxDelay, 0)) +
                                          static_cast<float>(safety),
                                (reverse ? -1.0f : 1.0f) * ratio, computeDuration(duration),
                                p.shape.value(rng, 1.0f, true), amplitude, pan, trigger.fractionalSample);
            sortList();
        }

        for (Grain& g : grain_)
            g.render(line, out);

        ++grainInterval_;
    }

private:
    float computeDuration(const float duration) const
    {
        const bool reverse = duration < 0.3333333f;
        const float shortness = reverse ? duration * 3.0f * (2.0f - duration * 3.0f)
                                        : 1.0f - 1.5f * (duration - 0.3333333f);
        const float clamped = std::min(1.0f, 1.025f * shortness);
        return semitonesToRatio(clamped * -108.0f) * maxGrainDuration_;
    }

    void sortList()
    {
        const int numSafety = 4;
        std::array<int, 4> fade {};

        if (grainInterval_ < simpleSchedulerThreshold_)
        {
            // Grains arrive fast: steal the oldest and move the new one to the back.
            std::rotate(priority_.begin(), priority_.begin() + 1, priority_.end());
            fade = {grainInterval_ + 1, instantCrossfade_, instantCrossfade_, preemptiveCrossfade_};
            if (grainInterval_ > instantCrossfade_)
                std::swap(fade[0], fade[2]);
        }
        else
        {
            std::partial_sort(priority_.begin(), priority_.begin() + numSafety, priority_.end(), Grain::compare);
            fade = {instantCrossfade_, instantCrossfade_, grainInterval_, preemptiveCrossfade_};
            if (grainInterval_ > preemptiveCrossfade_)
                std::swap(fade[2], fade[3]);
        }

        for (int i = 0; i < numSafety; ++i)
            priority_[static_cast<std::size_t>(i)]->fadeOut(fade[static_cast<std::size_t>(i)]);
        grainInterval_ = 0;
    }

    std::array<Grain, kNumGrains> grain_ {};
    std::array<Grain*, kNumGrains> priority_ {};
    int instantCrossfade_ = 1, preemptiveCrossfade_ = 1, simpleSchedulerThreshold_ = 1;
    float maxGrainDuration_ = 1.0f;
    int grainInterval_ = 1;
};

// ── Trigger generator (internal clock only; Beards has no gate input) ───────

class TriggerGenerator {
public:
    void init(const float sampleRate)
    {
        maxPulseFrequency_ = 130.81278f / sampleRate;
        phase_ = 0.0f;
        ratio_ = 1.0f;
        started_ = false;
    }

    Trigger tick(const float density, Rng& rng)
    {
        Trigger out;
        out.regularity = 1.0f;
        float frequency;
        bool randomise;
        if (density < 0.48f)
        {
            frequency = maxPulseFrequency_ * semitonesToRatio(-density * 192.0f);
            randomise = false;
        }
        else if (density > 0.52f)
        {
            frequency = 0.5f * maxPulseFrequency_ * semitonesToRatio(-(1.0f - density) * 192.0f);
            randomise = true;
        }
        else
        {
            out.trigger = false;
            out.regularity = 0.0f;
            return out;
        }

        if (!randomise)
            ratio_ = 1.0f;

        const float f = std::clamp(frequency * ratio_, 0.000001f, 0.25f);
        const bool rising = !started_;
        started_ = true;

        phase_ += f;
        float t = 0.0f;
        bool reset = false;
        if (rising)
        {
            phase_ = 0.0f;
            reset = true;
        }
        else if (phase_ >= 1.0f)
        {
            phase_ -= 1.0f;
            t = phase_ / f;
            reset = true;
        }

        if (reset && randomise)
        {
            const float x = 0.1f + 0.9f * rng.uniform();
            const float logOneMinusX = -x * (1.0f + x * (0.5f + x * (0.3333f + x * 0.25f)));
            ratio_ = -1.0f / logOneMinusX;
        }

        out.trigger = reset;
        out.fractionalSample = t;
        out.regularity = randomise ? 0.0f : 1.0f;
        return out;
    }

private:
    float maxPulseFrequency_ = 0.0f, phase_ = 0.0f, ratio_ = 1.0f;
    bool started_ = false;
};

// ── Reverb (Griesinger topology: 4 input allpasses, two-tank loop) ──────────

class Ring {
public:
    void init(const int size)
    {
        buffer_.assign(static_cast<std::size_t>(size + 2), 0.0f);
        write_ = 0;
    }
    int size() const { return static_cast<int>(buffer_.size()); }
    void push(const float x)
    {
        buffer_[static_cast<std::size_t>(write_)] = x;
        write_ = (write_ + 1) % size();
    }
    float tap(const float delay) const
    {
        float pos = static_cast<float>(write_) - 1.0f - delay;
        while (pos < 0.0f)
            pos += static_cast<float>(size());
        const int i0 = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(i0);
        const float a = buffer_[static_cast<std::size_t>(i0 % size())];
        const float b = buffer_[static_cast<std::size_t>((i0 + 1) % size())];
        return a + (b - a) * frac;
    }

private:
    std::vector<float> buffer_;
    int write_ = 0;
};

class Allpass {
public:
    void init(const int delay)
    {
        delay_ = delay;
        ring_.init(delay + 1);
    }
    float process(const float x, const float g)
    {
        const float z = ring_.tap(static_cast<float>(delay_ - 1));
        const float w = x + g * z;
        ring_.push(w);
        return z - g * w;
    }

private:
    Ring ring_;
    int delay_ = 1;
};

class Reverb {
public:
    void init(const float sampleRate)
    {
        const float k = sampleRate / 32000.0f;
        auto scaled = [k](const int n) { return std::max(4, static_cast<int>(static_cast<float>(n) * k)); };
        ap_[0].init(scaled(150));
        ap_[1].init(scaled(214));
        ap_[2].init(scaled(319));
        ap_[3].init(scaled(527));
        dap1a_.init(scaled(2182));
        dap1b_.init(scaled(2690));
        dap2a_.init(scaled(2525));
        dap2b_.init(scaled(2197));
        del1Len_ = static_cast<float>(scaled(4460));
        del2Len_ = static_cast<float>(scaled(6261));
        del1_.init(scaled(4501) + static_cast<int>(60.0f * k));
        del2_.init(scaled(6312) + static_cast<int>(60.0f * k));
        lfoStep1_ = 0.5f / sampleRate;
        lfoStep2_ = 0.3f / sampleRate;
        lfoDepth1_ = 40.0f * k;
        lfoDepth2_ = 50.0f * k;
        lfo1_ = lfo2_ = 0.0f;
        lp1_ = lp2_ = 0.0f;
        sendSmooth_ = drySmooth_ = 0.0f;
        mix_ = 0.0f;
    }

    void set(const float mix, const float time, const float lp)
    {
        mix_ = mix;
        time_ = time;
        lp_ = lp;
    }

    void process(Frame& io)
    {
        const float kap = 0.625f;
        const float inputGain = 0.2f * (1.0f - 0.333f * time_);
        const float outputGain = 0.4f / inputGain;
        const float sendSqrt = mix_ * (2.0f - mix_);
        const float send = sendSqrt * (2.0f - sendSqrt) * inputGain;
        const float dry = (1.0f - mix_) * (1.0f + mix_);

        sendSmooth_ += (send - sendSmooth_) * 0.002f;
        drySmooth_ += (dry - drySmooth_) * 0.002f;

        float x = (io.l + io.r) * sendSmooth_;
        for (Allpass& a : ap_)
            x = a.process(x, kap);

        lfo1_ += lfoStep1_;
        lfo1_ -= std::floor(lfo1_);
        lfo2_ += lfoStep2_;
        lfo2_ -= std::floor(lfo2_);
        const float mod1 = std::sin(2.0f * kPi * lfo1_) * lfoDepth1_;
        const float mod2 = std::sin(2.0f * kPi * lfo2_) * lfoDepth2_;

        // Tank 1 is fed by tank 2's delay and vice versa.
        float t1 = x + del2_.tap(del2Len_ + mod2) * time_;
        lp1_ += (t1 - lp1_) * (1.0f - lp_);
        t1 = lp1_;
        t1 = dap1a_.process(t1, -kap);
        t1 = dap1b_.process(t1, kap);
        const float wetL = t1;

        float t2 = x + del1_.tap(del1Len_ + mod1) * time_;
        lp2_ += (t2 - lp2_) * (1.0f - lp_);
        t2 = lp2_;
        t2 = dap2a_.process(t2, kap);
        t2 = dap2b_.process(t2, -kap);
        const float wetR = t2;

        del1_.push(wetL);
        del2_.push(wetR);

        io.l = io.l * drySmooth_ + wetL * outputGain;
        io.r = io.r * drySmooth_ + wetR * outputGain;
    }

private:
    std::array<Allpass, 4> ap_ {};
    Allpass dap1a_, dap1b_, dap2a_, dap2b_;
    Ring del1_, del2_;
    float del1Len_ = 1.0f, del2Len_ = 1.0f;
    float lfoStep1_ = 0.0f, lfoStep2_ = 0.0f, lfoDepth1_ = 0.0f, lfoDepth2_ = 0.0f, lfo1_ = 0.0f, lfo2_ = 0.0f;
    float lp1_ = 0.0f, lp2_ = 0.0f, mix_ = 0.0f, time_ = 0.5f, lp_ = 0.6f;
    float sendSmooth_ = 0.0f, drySmooth_ = 0.0f;
};

}  // namespace

// ── Engine ──────────────────────────────────────────────────────────────────

struct EngineImpl {
    float sampleRate = 48000.0f;
    Rng rng;
    DelayLine delay;
    GranularPlayer player;
    TriggerGenerator triggers;
    Reverb reverb;
    std::array<HighPass, 2> dcBlocker {};
    Limiter inputLimiter, preReverbLimiter, outputLimiter;

    Frame previous {};
    float feedback = 0.0f;
    float dry = 0.0f;
    float wet = 0.0f;
    bool pendingGrain = false;
    bool grainParamWas = false;
    int quality = 0;

    // Transport sync: free-runs from the host position at block start, and
    // resyncs when the transport starts or jumps.
    Transport transport;
    bool transportWasPlaying = false;
    bool syncResync = true;
    double syncPos = 0.0;
    double syncIndex = 0.0;
    double syncStepUsed = 0.0;
};

EngineState::EngineState() : impl(std::make_unique<EngineImpl>()) {}
EngineState::~EngineState() = default;

void activate(EngineState& state, const double sampleRate)
{
    EngineImpl& e = *state.impl;
    e.sampleRate = static_cast<float>(std::clamp(sampleRate, 8000.0, 384000.0));
    e.rng = Rng {};
    e.delay.init(static_cast<int>(e.sampleRate * 4.0f));
    e.player.init(e.sampleRate);
    e.triggers.init(e.sampleRate);
    e.reverb.init(e.sampleRate);
    for (HighPass& h : e.dcBlocker)
        h.init(40.0f / e.sampleRate, 0.5f);
    e.inputLimiter.init(1.0f / e.sampleRate, 0.95f);
    e.preReverbLimiter.init(1.0f / (e.sampleRate * 4.0f), 0.9f);
    e.outputLimiter.init(1.0f / (e.sampleRate * 6.0f), 0.9f);
    e.previous = {};
    e.feedback = 0.0f;
    e.dry = 0.0f;
    e.wet = 0.0f;
    e.pendingGrain = false;
    e.grainParamWas = false;
    e.transport = {};
    e.transportWasPlaying = false;
    e.syncResync = true;
}

void setTransport(EngineState& state, const Transport& transport)
{
    EngineImpl& e = *state.impl;
    const bool jumped = e.transportWasPlaying && std::fabs(transport.quarters - e.syncPos) > 0.05;
    if (transport.playing && (!e.transportWasPlaying || jumped))
        e.syncResync = true;
    e.transportWasPlaying = transport.playing;
    e.transport = transport;
    if (e.syncResync)
        e.syncPos = transport.quarters;
}

int activeGrains(const EngineState& state) { return state.impl->player.countActive(); }

void processBlock(EngineState& state, const Parameters& params, const std::uint32_t frames, const float* inL,
                  const float* inR, float* outL, float* outR)
{
    EngineImpl& e = *state.impl;

    const bool grainParam = params[ParamId::grain] >= 0.5f;
    if (grainParam && !e.grainParamWas)
        e.pendingGrain = true;
    e.grainParamWas = grainParam;

    const int quality = std::clamp(static_cast<int>(std::lround(params[ParamId::quality])), 0, 2);
    e.quality = quality;
    const bool frozen = params[ParamId::freeze] >= 0.5f;
    const float density = params[ParamId::density];

    const int syncMode = std::clamp(static_cast<int>(std::lround(params[ParamId::sync])), 0, 6);
    const double syncStep = kSyncQuarters[static_cast<std::size_t>(syncMode)];
    const bool syncActive = syncStep > 0.0 && e.transport.playing;
    const double quartersPerSample = e.transport.bpm / 60.0 / static_cast<double>(e.sampleRate);
    if (e.syncResync || syncStep != e.syncStepUsed)
    {
        // A grid line exactly at the start position counts as upcoming.
        e.syncIndex = std::ceil(e.syncPos / std::max(syncStep, 1.0e-6) - 1.0e-6) - 1.0;
        e.syncStepUsed = syncStep;
        e.syncResync = false;
    }

    GrainParams gp;
    gp.time = {params[ParamId::position], params[ParamId::timeRand]};
    gp.size = {params[ParamId::size], params[ParamId::sizeRand]};
    gp.shape = {params[ParamId::shape], params[ParamId::shapeRand]};
    gp.pitch = {params[ParamId::pitch], params[ParamId::pitchRand]};

    // Beads' per-resolution feedback coefficients (8, 12, 16 bit).
    static constexpr std::array<float, 3> kLowPass = {0.999f, 0.9f, 0.75f};   // indexed by our quality
    static constexpr std::array<float, 3> kSoftLimit = {0.0f, 1.3f, 1.75f};
    const float lpCoefficient = kLowPass[static_cast<std::size_t>(quality)];
    const float softLimitGain = kSoftLimit[static_cast<std::size_t>(quality)];
    constexpr float kMaxFeedbackGain = 3.0f;

    const float feedbackTarget = params[ParamId::feedback];

    const float dryWet = params[ParamId::dryWet];
    const float dryTarget = (1.0f - dryWet) * (1.0f + dryWet) * 0.75f;
    const float wetTarget = dryWet * (2.0f - dryWet) * 1.5f;

    const float reverbAmount = params[ParamId::reverb];
    const bool caveman = quality != 0;
    e.reverb.set(reverbAmount * reverbAmount * 0.9f,
                 caveman ? 0.45f + 0.5f * reverbAmount * reverbAmount * reverbAmount
                         : 0.3f + 0.6f * reverbAmount * reverbAmount * reverbAmount,
                 caveman ? (quality == 1 ? 0.75f : 0.4f) : 0.6f);

    std::uint32_t done = 0;
    while (done < frames)
    {
        const std::uint32_t n = std::min(kChunk, frames - done);
        const float invN = 1.0f / static_cast<float>(n);
        const float feedbackStep = (feedbackTarget - e.feedback) * invN;
        const float dryStep = (dryTarget - e.dry) * invN;
        const float wetStep = (wetTarget - e.wet) * invN;

        for (std::uint32_t k = 0; k < n; ++k)
        {
            const std::uint32_t idx = done + k;
            e.feedback += feedbackStep;
            e.dry += dryStep;
            e.wet += wetStep;

            Frame input {sanitize(inL[idx]), sanitize(inR[idx])};

            Trigger trigger = e.triggers.tick(density, e.rng);
            bool syncFire = false;
            if (syncActive)
            {
                // Transport owns the clock while it plays; the random clock is muted.
                trigger = Trigger {};
                e.syncPos += quartersPerSample;
                const double index = std::floor(e.syncPos / syncStep + 1.0e-9);
                if (index > e.syncIndex)
                {
                    e.syncIndex = index;
                    syncFire = true;
                }
            }
            else if (e.transport.playing)
                e.syncPos += quartersPerSample;
            if (e.pendingGrain || syncFire)
            {
                trigger.trigger = true;
                trigger.fractionalSample = 0.0f;
                trigger.regularity = 1.0f;
                e.pendingGrain = false;
            }

            // Feedback path with input limiting and soft clipping.
            Frame s = input;
            if (softLimitGain > 0.0f)
                s = s + e.previous * (e.feedback * (kMaxFeedbackGain / softLimitGain));
            else
                s = s + e.previous * (e.feedback * e.feedback * kMaxFeedbackGain);
            e.inputLimiter.process(s);
            if (softLimitGain > 0.0f)
            {
                s = s * softLimitGain;
                s.l = softClip(s.l);
                s.r = softClip(s.r);
                s = input + (s - input) * e.feedback;
            }

            s.l = quantise(quality, s.l);
            s.r = quantise(quality, s.r);
            e.delay.write(s, frozen);

            Frame wetOut {};
            e.player.render(e.delay, trigger, gp, e.rng, wetOut);

            e.previous.l += (e.dcBlocker[0].process(wetOut.l) - e.previous.l) * lpCoefficient;
            e.previous.r += (e.dcBlocker[1].process(wetOut.r) - e.previous.r) * lpCoefficient;

            Frame dryOut = quality == 0 ? input : Frame {quantise(quality, input.l), quantise(quality, input.r)};
            Frame mixed = dryOut * e.dry + wetOut * e.wet;

            e.preReverbLimiter.process(mixed);
            e.reverb.process(mixed);
            e.outputLimiter.process(mixed);

            outL[idx] = sanitize(mixed.l);
            outR[idx] = sanitize(mixed.r);
        }
        done += n;
    }
}

bool applyMidi(Parameters& params, EngineState& state, const std::uint8_t* data, const std::size_t size)
{
    if (data == nullptr || size < 3 || (data[0] & 0xF0) != 0xB0)
        return false;

    const int channel = static_cast<int>(data[0] & 0x0F) + 1;
    const int controller = data[1];
    const int value = data[2];
    bool changed = false;

    const int midiCh = static_cast<int>(std::lround(params[ParamId::midiCh]));
    if (midiCh != 0 && (midiCh == 17 || midiCh == channel))
    {
        for (const CcTarget& target : kControllerMap)
        {
            if (target.controller == controller)
            {
                setParameter(params, target.param, controllerToParameter(target.param, value));
                changed = true;
            }
        }
        if (controller == kFreezeCc)
        {
            setParameter(params, ParamId::freeze, value >= 64 ? 1.0f : 0.0f);
            changed = true;
        }
    }

    const int conductorCh = static_cast<int>(std::lround(params[ParamId::conductorCh]));
    if (conductorCh != 0 && conductorCh == channel)
    {
        const float unit = static_cast<float>(std::clamp(value, 0, 127)) / 127.0f;
        switch (controller)
        {
        case kConductorDensityCc:
            setParameter(params, ParamId::density, unit);
            changed = true;
            break;
        case kConductorEnergyCc:
            setParameter(params, ParamId::dryWet, unit);
            changed = true;
            break;
        case kConductorMutationCc:
            setParameter(params, ParamId::timeRand, unit);
            setParameter(params, ParamId::pitchRand, 0.5f * unit);
            changed = true;
            break;
        case kConductorResetCc:
            if (value == 127)
            {
                state.impl->pendingGrain = true;
                changed = true;
            }
            break;
        default:
            break;
        }
    }
    return changed;
}

}  // namespace downspout::beards
