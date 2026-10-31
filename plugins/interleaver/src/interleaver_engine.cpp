#include "interleaver_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::interleaver {

namespace {
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] float clampf(const float v, const float lo, const float hi)
{
    return std::min(std::max(v, lo), hi);
}
}  // namespace

Parameters clampParameters(Parameters p)
{
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto& s = kParameterSpecs[i];
        float v = p.values[i];
        if (!(v == v))
            v = s.defaultValue;
        v = clampf(v, s.minimum, s.maximum);
        if (s.integer)
            v = std::round(v);
        p.values[i] = v;
    }
    return p;
}

// ---- Pattern functions ----------------------------------------------------------------------------------

float edgeStep(const float x, const Shape shape)
{
    if (shape == kShapeHard)
        return x < 0.5f ? 0.0f : 1.0f;
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;
    return shape == kShapeLinear ? x : 0.5f - 0.5f * std::cos(kPi * x);
}

float edgeWidth(const float balance, const float crossfadePercent, const Shape shape)
{
    if (shape == kShapeHard)
        return 0.0f;
    return clampf(crossfadePercent * 0.01f, 0.0f, 1.0f) * std::min(balance, 1.0f - balance);
}

float timeMask(const float phase, const float balance, const float width, const Shape shape)
{
    if (balance <= 0.0f)
        return 1.0f;
    if (balance >= 1.0f)
        return 0.0f;
    if (width <= 1.0e-6f || shape == kShapeHard)
        return phase < balance ? 0.0f : 1.0f;
    // Up edge at `balance`, and the B->A edge centred on the cycle boundary, seen from both sides of it.
    // The three terms are disjoint because width <= min(balance, 1 - balance).
    const float up = edgeStep((phase - balance) / width + 0.5f, shape);
    const float downEnd = edgeStep((phase - 1.0f) / width + 0.5f, shape);
    const float downStart = 1.0f - edgeStep(phase / width + 0.5f, shape);
    return clampf(up - downEnd + downStart, 0.0f, 1.0f);
}

bool spectralPicksB(const int k, const float offset, const int binWidth, const float balance)
{
    const float period = 2.0f * static_cast<float>(std::max(binWidth, 1));
    const float pos = (static_cast<float>(k) + offset) / period;
    const float q = pos - std::floor(pos);
    return q >= balance;
}

// ---- PitchTracker ---------------------------------------------------------------------------------------

void PitchTracker::prepare(const float sampleRate)
{
    decimation_ = std::max(1, static_cast<int>(std::lround(sampleRate / 12000.0f)));
    decimatedRate_ = sampleRate / static_cast<float>(decimation_);
    minLag_ = std::max(2, static_cast<int>(std::floor(decimatedRate_ / 1000.0f)));
    maxLag_ = std::min(kRing - kWindow - 8, static_cast<int>(std::ceil(decimatedRate_ / 50.0f)));
    reset();
}

void PitchTracker::reset()
{
    ring_.fill(0.0f);
    writeIndex_ = 0;
    accum_ = 0.0f;
    accumCount_ = 0;
    sinceAnalysis_ = 0;
    voiced_ = false;
    periodSamples_ = 0.0f;
    confidence_ = 0.0f;
    unvoicedRun_ = 0;
}

void PitchTracker::push(const float mono)
{
    accum_ += mono;
    if (++accumCount_ < decimation_)
        return;
    ring_[static_cast<size_t>(writeIndex_)] = accum_ / static_cast<float>(decimation_);
    writeIndex_ = (writeIndex_ + 1) % kRing;
    accum_ = 0.0f;
    accumCount_ = 0;
    if (++sinceAnalysis_ >= kHopDecimated) {
        sinceAnalysis_ = 0;
        analyse();
    }
}

void PitchTracker::analyse()
{
    const int length = kWindow + maxLag_ + 1;
    float x[kRing];
    for (int i = 0; i < length; ++i)
        x[i] = ring_[static_cast<size_t>((writeIndex_ - length + i + 2 * kRing) % kRing)];

    const float* ref = x + (length - kWindow);
    float e0 = 0.0f;
    for (int n = 0; n < kWindow; ++n)
        e0 += ref[n] * ref[n];
    if (e0 < 1.0e-7f * static_cast<float>(kWindow)) {
        confidence_ = 0.0f;
        if (++unvoicedRun_ > 4)
            voiced_ = false;
        return;
    }

    float r[kRing] = {};
    float best = 0.0f;
    for (int lag = minLag_ - 1; lag <= maxLag_ + 1 && lag + kWindow <= length; ++lag) {
        const float* lagged = x + (length - kWindow - lag);
        float dot = 0.0f, e = 0.0f;
        for (int n = 0; n < kWindow; ++n) {
            dot += ref[n] * lagged[n];
            e += lagged[n] * lagged[n];
        }
        r[lag] = dot / std::sqrt(e0 * e + 1.0e-20f);
        if (lag >= minLag_ && lag <= maxLag_)
            best = std::max(best, r[lag]);
    }
    confidence_ = best;
    if (best < 0.6f) {
        if (++unvoicedRun_ > 4)
            voiced_ = false;
        return;
    }
    int chosen = -1;
    for (int lag = minLag_; lag <= maxLag_; ++lag) {
        if (r[lag] >= 0.9f * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
            chosen = lag;
            break;
        }
    }
    if (chosen < 0)
        return;
    const float a = r[chosen - 1], b = r[chosen], c = r[chosen + 1];
    const float denominator = a - 2.0f * b + c;
    const float shift = std::fabs(denominator) > 1.0e-9f ? clampf(0.5f * (a - c) / denominator, -0.5f, 0.5f) : 0.0f;
    periodSamples_ = (static_cast<float>(chosen) + shift) * static_cast<float>(decimation_);
    voiced_ = true;
    unvoicedRun_ = 0;
}

// ---- Engine ---------------------------------------------------------------------------------------------

Engine::Engine()
{
    window_.resize(kFftSize);
    for (int n = 0; n < kFftSize; ++n)
        window_[static_cast<size_t>(n)] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(n) / static_cast<float>(kFftSize));
    twiddle_.resize(kFftSize / 2);
    for (int k = 0; k < kFftSize / 2; ++k) {
        const float a = -2.0f * kPi * static_cast<float>(k) / static_cast<float>(kFftSize);
        twiddle_[static_cast<size_t>(k)] = Cx(std::cos(a), std::sin(a));
    }
    bitReverse_.resize(kFftSize);
    int bits = 0;
    while ((1 << bits) < kFftSize)
        ++bits;
    for (int i = 0; i < kFftSize; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitReverse_[static_cast<size_t>(i)] = r;
    }
    zA_.resize(kFftSize);
    zB_.resize(kFftSize);
    zOut_.resize(kFftSize);
    for (auto& d : delay_)
        d.assign(kLatencySamples, 0.0f);
    for (auto& source : inRing_)
        for (auto& ring : source)
            ring.assign(kFftSize, 0.0f);
    for (auto& o : ola_)
        o.assign(kFftSize, 0.0f);
    prepare(48000.0f);
}

void Engine::prepare(const float sampleRate)
{
    sampleRate_ = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    tracker_.prepare(sampleRate_);
    reset();
}

void Engine::reset()
{
    phase_ = 0.0f;
    cycleSamples_ = 0.0f;  // snaps to the target on the next block
    jitterFactor_ = 1.0f;
    tracker_.reset();
    for (auto& d : delay_)
        std::fill(d.begin(), d.end(), 0.0f);
    delayIndex_ = 0;
    for (auto& source : inRing_)
        for (auto& ring : source)
            std::fill(ring.begin(), ring.end(), 0.0f);
    ringIndex_ = 0;
    hopCounter_ = 0;
    for (auto& o : ola_)
        std::fill(o.begin(), o.end(), 0.0f);
    for (auto& b : block_)
        b.fill(0.0f);
    blockIndex_ = kHop;
    driftBins_ = 0.0;
    spectralWasActive_ = false;
    modeMix_ = 0.0f;
    gain_ = 1.0f;
    status_ = {};
}

float Engine::nextRandom()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f;  // [-1, 1)
}

void Engine::fft(Cx* data, const bool inverse) const
{
    for (int i = 0; i < kFftSize; ++i) {
        const int j = bitReverse_[static_cast<size_t>(i)];
        if (j > i)
            std::swap(data[i], data[j]);
    }
    for (int size = 2; size <= kFftSize; size <<= 1) {
        const int half = size / 2;
        const int stride = kFftSize / size;
        for (int start = 0; start < kFftSize; start += size) {
            for (int k = 0; k < half; ++k) {
                Cx w = twiddle_[static_cast<size_t>(k * stride)];
                if (inverse)
                    w = std::conj(w);
                const Cx t = w * data[start + k + half];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
        }
    }
}

void Engine::processFrame(const Parameters& p)
{
    const int scheme = static_cast<int>(std::lround(p.values[kScheme]));
    const int width = static_cast<int>(std::lround(p.values[kBinWidth]));
    const float balance = p.values[kBalance] * 0.01f;
    const float split = p.values[kSplit] * 0.01f;
    const float period = 2.0f * static_cast<float>(width);
    const float offsetL = static_cast<float>(std::fmod(driftBins_, 1.0e6));
    const float offsetR = offsetL + split * 0.5f * period;
    const float biasA = std::pow(10.0f, (balance - 0.5f) * 48.0f / 20.0f);

    // Two real signals per FFT: left in the real part, right in the imaginary part.
    for (int n = 0; n < kFftSize; ++n) {
        const size_t idx = static_cast<size_t>((ringIndex_ + n) % kFftSize);
        const float w = window_[static_cast<size_t>(n)];
        zA_[static_cast<size_t>(n)] = Cx(inRing_[0][0][idx] * w, inRing_[0][1][idx] * w);
        zB_[static_cast<size_t>(n)] = Cx(inRing_[1][0][idx] * w, inRing_[1][1][idx] * w);
    }
    fft(zA_.data(), false);
    fft(zB_.data(), false);

    const Cx minusHalfI(0.0f, -0.5f);
    for (int k = 0; k <= kFftSize / 2; ++k) {
        const size_t i = static_cast<size_t>(k);
        const size_t m = static_cast<size_t>((kFftSize - k) & (kFftSize - 1));
        const Cx a[2] = {(zA_[i] + std::conj(zA_[m])) * 0.5f, (zA_[i] - std::conj(zA_[m])) * minusHalfI};
        const Cx b[2] = {(zB_[i] + std::conj(zB_[m])) * 0.5f, (zB_[i] - std::conj(zB_[m])) * minusHalfI};
        Cx y[2];
        for (int c = 0; c < 2; ++c) {
            const float offset = c == 0 ? offsetL : offsetR;
            if (scheme == kSchemeLoudest) {
                y[c] = std::abs(a[c]) * biasA >= std::abs(b[c]) ? a[c] : b[c];
            } else {
                const bool useB = spectralPicksB(k, offset, width, balance);
                if (scheme == kSchemeBins) {
                    y[c] = useB ? b[c] : a[c];
                } else {
                    // Magnitude from one source, phase from the other; the roles swap on every segment.
                    const Cx& magSource = useB ? b[c] : a[c];
                    const Cx& phaseSource = useB ? a[c] : b[c];
                    const float pm = std::abs(phaseSource);
                    y[c] = pm > 1.0e-12f ? phaseSource * (std::abs(magSource) / pm) : Cx(std::abs(magSource), 0.0f);
                }
            }
        }
        if (k == 0 || k == kFftSize / 2) {
            zOut_[i] = Cx(y[0].real(), y[1].real());  // DC and Nyquist carry no imaginary part
        } else {
            zOut_[i] = y[0] + Cx(0.0f, 1.0f) * y[1];
            zOut_[m] = std::conj(y[0]) + Cx(0.0f, 1.0f) * std::conj(y[1]);
        }
    }
    fft(zOut_.data(), true);

    // Hann analysis and synthesis at 4x overlap sum to 1.5; the inverse FFT is unnormalised.
    const float scale = 1.0f / (static_cast<float>(kFftSize) * 1.5f);
    for (int n = 0; n < kFftSize; ++n) {
        const float w = window_[static_cast<size_t>(n)] * scale;
        ola_[0][static_cast<size_t>(n)] += zOut_[static_cast<size_t>(n)].real() * w;
        ola_[1][static_cast<size_t>(n)] += zOut_[static_cast<size_t>(n)].imag() * w;
    }
    for (int c = 0; c < 2; ++c) {
        auto& o = ola_[static_cast<size_t>(c)];
        std::copy(o.begin(), o.begin() + kHop, block_[static_cast<size_t>(c)].begin());
        std::copy(o.begin() + kHop, o.end(), o.begin());
        std::fill(o.end() - kHop, o.end(), 0.0f);
    }
}

void Engine::process(const Parameters& raw, const std::uint32_t frames, const float* const* inputs, float* const* outputs)
{
    const Parameters p = clampParameters(raw);
    const int mode = static_cast<int>(std::lround(p.values[kMode]));
    const int sync = static_cast<int>(std::lround(p.values[kSync]));
    const Shape shape = static_cast<Shape>(static_cast<int>(std::lround(p.values[kShape])));
    const float balance = p.values[kBalance] * 0.01f;
    const float split = p.values[kSplit] * 0.01f;
    const float width = edgeWidth(balance, p.values[kCrossfade], shape);
    const float jitter = p.values[kJitter] * 0.01f;
    const float freeCycle = 2.0f * p.values[kSlice] * 0.001f * sampleRate_;
    const float targetGain = std::pow(10.0f, p.values[kOutput] / 20.0f);
    const float modeStep = 1.0f / (0.02f * sampleRate_);
    const float hopDrift = p.values[kDrift] * static_cast<float>(kHop) / sampleRate_;

    for (std::uint32_t i = 0; i < frames; ++i) {
        const float a[2] = {inputs[0][i], inputs[1][i]};
        const float b[2] = {inputs[2][i], inputs[3][i]};

        // ---- Time path
        if (sync != kSyncFree)
            tracker_.push(sync == kSyncTrackA ? a[0] + a[1] : b[0] + b[1]);
        float target = freeCycle;
        if (sync != kSyncFree && tracker_.voiced())
            target = 2.0f * p.values[kPeriods] * tracker_.periodSamples();
        target = clampf(target, 2.0f, 0.9f * sampleRate_);
        if (cycleSamples_ <= 0.0f)
            cycleSamples_ = target;
        else
            cycleSamples_ += (target - cycleSamples_) * 0.002f;

        phase_ += 1.0f / std::max(2.0f, cycleSamples_ * jitterFactor_);
        if (phase_ >= 1.0f) {
            phase_ -= std::floor(phase_);
            jitterFactor_ = jitter > 0.0f ? std::max(0.25f, 1.0f + jitter * 0.5f * nextRandom()) : 1.0f;
        }
        float timeOut[2];
        for (int c = 0; c < 2; ++c) {
            float ph = c == 0 ? phase_ : phase_ + split * 0.5f;
            ph -= std::floor(ph);
            const float m = timeMask(ph, balance, width, shape);
            timeOut[c] = a[c] * std::cos(m * 0.5f * kPi) + b[c] * std::sin(m * 0.5f * kPi);
        }
        float delayed[2];
        for (int c = 0; c < 2; ++c) {
            auto& d = delay_[static_cast<size_t>(c)];
            delayed[c] = d[static_cast<size_t>(delayIndex_)];
            d[static_cast<size_t>(delayIndex_)] = timeOut[c];
        }
        delayIndex_ = (delayIndex_ + 1) % kLatencySamples;

        // ---- Spectral path: the output block was produced by the previous hop
        float spectral[2] = {0.0f, 0.0f};
        if (blockIndex_ < kHop) {
            spectral[0] = block_[0][static_cast<size_t>(blockIndex_)];
            spectral[1] = block_[1][static_cast<size_t>(blockIndex_)];
            ++blockIndex_;
        }
        const size_t w = static_cast<size_t>(ringIndex_);
        inRing_[0][0][w] = a[0];
        inRing_[0][1][w] = a[1];
        inRing_[1][0][w] = b[0];
        inRing_[1][1][w] = b[1];
        ringIndex_ = (ringIndex_ + 1) % kFftSize;
        if (++hopCounter_ >= kHop) {
            hopCounter_ = 0;
            const bool active = mode == kModeSpectral || modeMix_ > 0.0f;
            if (active) {
                if (!spectralWasActive_)
                    for (auto& o : ola_)
                        std::fill(o.begin(), o.end(), 0.0f);
                processFrame(p);
                driftBins_ += static_cast<double>(hopDrift);
            } else {
                for (auto& blk : block_)
                    blk.fill(0.0f);
            }
            spectralWasActive_ = active;
            blockIndex_ = 0;
        }

        const float want = mode == kModeSpectral ? 1.0f : 0.0f;
        modeMix_ += clampf(want - modeMix_, -modeStep, modeStep);
        gain_ += (targetGain - gain_) * 0.002f;
        for (int c = 0; c < 2; ++c)
            outputs[c][i] = ((1.0f - modeMix_) * delayed[c] + modeMix_ * spectral[c]) * gain_;
    }

    status_.detectedHz = (sync != kSyncFree && tracker_.voiced() && tracker_.periodSamples() > 0.0f)
                             ? sampleRate_ / tracker_.periodSamples() : 0.0f;
    status_.cycleMs = mode == kModeTime ? cycleSamples_ / sampleRate_ * 1000.0f : 0.0f;
}

}  // namespace downspout::interleaver
