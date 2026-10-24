#include "nam_engine.hpp"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/wav.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <stdexcept>

namespace downspout::namfx {

namespace {

constexpr int kModelChunk = 512;       // frames per NAM call; also the maxBufferSize given to Reset()
constexpr int kMaxRatio = 8;           // widest model/host rate ratio that is resampled
constexpr int kFifoSize = 4096;
constexpr int kFifoSlack = 4;          // rounding margin on top of the resamplers' look-ahead
constexpr int kDryDelaySize = 1024;

}  // namespace

// ── ModelRig ────────────────────────────────────────────────────────────────

struct ModelRig::Impl {
    std::array<std::unique_ptr<nam::DSP>, 2> dsp;
    std::array<StreamResampler, 2> up, down;
    std::array<std::vector<float>, 2> fifo;
    std::array<int, 2> fifoRead {};
    std::array<int, 2> fifoCount {};
    int underruns = 0;
    std::vector<float> upBuf, modelBuf, downBuf;
    std::vector<NAM_SAMPLE> inD, outD;

    // `n` host-or-model-rate frames through the network, in chunks it was prepared for.
    void run(const int channel, const float* in, float* out, int n)
    {
        nam::DSP& net = *dsp[static_cast<std::size_t>(channel)];
        while (n > 0) {
            const int c = std::min(n, kModelChunk);
            for (int i = 0; i < c; ++i)
                inD[static_cast<std::size_t>(i)] = in[i];
            NAM_SAMPLE* ip[1] = {inD.data()};
            NAM_SAMPLE* op[1] = {outD.data()};
            net.process(ip, op, c);
            for (int i = 0; i < c; ++i)
                out[i] = static_cast<float>(outD[static_cast<std::size_t>(i)]);
            in += c;
            out += c;
            n -= c;
        }
    }

    void push(const int channel, const float* data, const int n)
    {
        auto& f = fifo[static_cast<std::size_t>(channel)];
        int& read = fifoRead[static_cast<std::size_t>(channel)];
        int& count = fifoCount[static_cast<std::size_t>(channel)];
        for (int i = 0; i < n && count < kFifoSize; ++i) {
            f[static_cast<std::size_t>((read + count) % kFifoSize)] = data[i];
            ++count;
        }
    }

    void pop(const int channel, float* out, const int n)
    {
        auto& f = fifo[static_cast<std::size_t>(channel)];
        int& read = fifoRead[static_cast<std::size_t>(channel)];
        int& count = fifoCount[static_cast<std::size_t>(channel)];
        for (int i = 0; i < n; ++i) {
            if (count > 0) {
                out[i] = f[static_cast<std::size_t>(read)];
                read = (read + 1) % kFifoSize;
                --count;
            } else {
                out[i] = 0.0f;
                ++underruns;
            }
        }
    }
};

ModelRig::ModelRig() : impl_(std::make_unique<Impl>()) {}

int ModelRig::underruns() const { return impl_->underruns; }
ModelRig::~ModelRig() = default;

std::shared_ptr<ModelRig> ModelRig::load(const std::string& path, const double hostRate, std::string& error)
{
    error.clear();
    std::shared_ptr<ModelRig> rig(new ModelRig());
    Impl& m = *rig->impl_;
    try {
        nam::DspLoadOptions options;
        options.prewarm = false;  // Reset() below prewarms once, at the rate the model will run at
        for (auto& net : m.dsp) {
            net = nam::get_dsp(std::filesystem::path(path), options);
            if (!net)
                throw std::runtime_error("the file did not describe a model");
            if (net->NumInputChannels() != 1 || net->NumOutputChannels() != 1)
                throw std::runtime_error("only models with one input and one output channel are supported");
        }
        nam::DSP& first = *m.dsp[0];
        const double expected = first.GetExpectedSampleRate();
        const bool anyRate = first.SupportsArbitrarySampleRate();
        double modelRate = hostRate;
        if (expected > 0.0 && !anyRate && std::fabs(expected - hostRate) > 0.5) {
            const double ratio = expected / hostRate;
            if (ratio <= kMaxRatio && ratio >= 1.0 / kMaxRatio)
                modelRate = expected;
        }
        rig->modelRate_ = modelRate;
        rig->resampling_ = std::fabs(modelRate - hostRate) > 0.5;
        rig->hasLoudness_ = first.HasLoudness();
        if (rig->hasLoudness_)
            rig->loudnessDb_ = first.GetLoudness();

        for (auto& net : m.dsp)
            net->Reset(modelRate, kModelChunk);

        m.inD.assign(kModelChunk, 0.0);
        m.outD.assign(kModelChunk, 0.0);
        for (auto& f : m.fifo)
            f.assign(kFifoSize, 0.0f);
        if (rig->resampling_) {
            for (std::size_t c = 0; c < 2; ++c) {
                m.up[c].configure(hostRate, modelRate);
                m.down[c].configure(modelRate, hostRate);
            }
            m.upBuf.assign(static_cast<std::size_t>(kMaxBlock * kMaxRatio + 16), 0.0f);
            m.modelBuf.assign(m.upBuf.size(), 0.0f);
            m.downBuf.assign(static_cast<std::size_t>(2 * kMaxBlock + 64), 0.0f);
            // Output sample k is the filtered value for input sample k, but it only exists once the
            // two resamplers have seen their look-ahead. Holding back that many zeros means a block
            // never finds the FIFO short, whatever its size; those zeros are the plugin's latency.
            const int prefill = m.up[0].latencyInSamples() + 1
                + static_cast<int>(std::ceil((m.down[0].latencyInSamples() + 1) * hostRate / modelRate)) + kFifoSlack;
            for (std::size_t c = 0; c < 2; ++c) {
                m.fifoCount[c] = prefill;
                m.fifoRead[c] = 0;
            }
            rig->latency_ = prefill;
        }
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    return rig;
}

void ModelRig::process(const int channel, const float* in, float* out, const int n)
{
    Impl& m = *impl_;
    const std::size_t ch = static_cast<std::size_t>(channel & 1);
    if (!resampling_) {
        m.run(static_cast<int>(ch), in, out, n);
        return;
    }
    const int produced = m.up[ch].process(in, n, m.upBuf.data(), static_cast<int>(m.upBuf.size()));
    m.run(static_cast<int>(ch), m.upBuf.data(), m.modelBuf.data(), produced);
    const int back = m.down[ch].process(m.modelBuf.data(), produced, m.downBuf.data(), static_cast<int>(m.downBuf.size()));
    m.push(static_cast<int>(ch), m.downBuf.data(), back);
    m.pop(static_cast<int>(ch), out, n);
}

// ── IrRig ───────────────────────────────────────────────────────────────────

IrRig::IrRig() = default;

std::shared_ptr<IrRig> IrRig::fromSamples(const std::vector<float>& taps, const int channels)
{
    auto rig = std::make_shared<IrRig>();
    const int ch = channels == 2 ? 2 : 1;
    const int length = static_cast<int>(taps.size()) / ch;
    rig->stereo_ = ch == 2;
    rig->conv_[0].setImpulse(taps.data(), length);
    rig->conv_[1].setImpulse(taps.data() + (ch == 2 ? length : 0), length);
    return rig;
}

std::shared_ptr<IrRig> IrRig::load(const std::string& path, const double hostRate, std::string& error)
{
    error.clear();
    try {
        double fileRate = 0.0;
        int channels = 0;
        std::vector<float> taps = nam::detail::load_wav_ir(std::filesystem::path(path), fileRate, channels);
        const int fileLength = static_cast<int>(taps.size()) / channels;

        std::vector<float> out;
        int length = fileLength;
        if (std::fabs(fileRate - hostRate) > 0.5) {
            // The windowed-sinc kernel rings on both sides of every tap. Starting the stream a
            // half-kernel early keeps the part before t=0, so a sharp onset keeps its level;
            // the leading samples are then dropped only as far as they carry no energy.
            const double ratio = hostRate / fileRate;
            StreamResampler probe;
            probe.configure(fileRate, hostRate);
            const int lead = static_cast<int>(std::ceil((probe.latencyInSamples() + 1) * ratio));
            const int body = static_cast<int>(std::ceil(fileLength * ratio));
            const int span = lead + body + lead + 8;  // pre-roll, the IR, the kernel's tail
            const float gain = static_cast<float>(1.0 / ratio);  // taps scale so the response keeps its level
            std::vector<std::vector<float>> resampled(static_cast<std::size_t>(channels));
            int trim = lead;
            for (int c = 0; c < channels; ++c) {
                StreamResampler rs;
                rs.configure(fileRate, hostRate);
                std::vector<float> padded(static_cast<std::size_t>(rs.latencyInSamples() + 1), 0.0f);
                padded.insert(padded.end(), taps.begin() + static_cast<std::ptrdiff_t>(c) * fileLength,
                              taps.begin() + static_cast<std::ptrdiff_t>(c + 1) * fileLength);
                padded.resize(padded.size() + 2 * StreamResampler::kMaxHalf, 0.0f);
                auto& res = resampled[static_cast<std::size_t>(c)];
                res.assign(static_cast<std::size_t>(span) + 2 * StreamResampler::kMaxHalf, 0.0f);
                const int got = rs.process(padded.data(), static_cast<int>(padded.size()), res.data(), static_cast<int>(res.size()));
                res.resize(static_cast<std::size_t>(got));
                for (auto& v : res)
                    v *= gain;
                double total = 0.0;
                for (const float v : res)
                    total += static_cast<double>(v) * v;
                double lead_energy = 0.0;
                int t = 0;
                while (t < trim && t < got && lead_energy + static_cast<double>(res[static_cast<std::size_t>(t)]) * res[static_cast<std::size_t>(t)] <= 1.0e-6 * total) {
                    lead_energy += static_cast<double>(res[static_cast<std::size_t>(t)]) * res[static_cast<std::size_t>(t)];
                    ++t;
                }
                trim = std::min(trim, t);
            }
            length = std::min(span - trim, static_cast<int>(resampled[0].size()) - trim);
            out.assign(static_cast<std::size_t>(length * channels), 0.0f);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < length; ++i)
                    out[static_cast<std::size_t>(c * length + i)] = resampled[static_cast<std::size_t>(c)][static_cast<std::size_t>(trim + i)];
        } else {
            out = std::move(taps);
        }

        const int cap = static_cast<int>(kMaxSeconds * hostRate);
        if (length > cap) {
            std::vector<float> trimmed(static_cast<std::size_t>(cap * channels));
            const int fade = std::min(cap, static_cast<int>(0.010 * hostRate));
            for (int c = 0; c < channels; ++c) {
                for (int i = 0; i < cap; ++i) {
                    float v = out[static_cast<std::size_t>(c * length + i)];
                    if (i >= cap - fade)
                        v *= static_cast<float>(cap - i) / static_cast<float>(fade);
                    trimmed[static_cast<std::size_t>(c * cap + i)] = v;
                }
            }
            out = std::move(trimmed);
            length = cap;
        }
        for (const float v : out) {
            if (!std::isfinite(v))
                throw std::runtime_error("the impulse response contains invalid samples");
        }
        return fromSamples(out, channels);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

// ── Tone ────────────────────────────────────────────────────────────────────

ToneCoeffs designTone(const double sampleRate, const float bassDb, const float midDb, const float trebleDb)
{
    return {designBiquad(FilterType::LowShelf, sampleRate, 120.0, 0.7, bassDb),
            designBiquad(FilterType::Peak, sampleRate, 750.0, 0.8, midDb),
            designBiquad(FilterType::HighShelf, sampleRate, 3500.0, 0.7, trebleDb)};
}

double toneResponseDb(const ToneCoeffs& tone, const double freq, const double sampleRate)
{
    const double mag = biquadMagnitude(tone.bass, freq, sampleRate) * biquadMagnitude(tone.mid, freq, sampleRate)
        * biquadMagnitude(tone.treble, freq, sampleRate);
    return 20.0 * std::log10(std::max(mag, 1.0e-9));
}

// ── Engine ──────────────────────────────────────────────────────────────────

Engine::Engine(const float sampleRate)
{
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        values_[i] = kParameterSpecs[i].defaultValue;
    for (auto& d : dry_)
        d.assign(kDryDelaySize, 0.0f);
    setSampleRate(sampleRate);
}

void Engine::setSampleRate(const float sampleRate)
{
    sampleRate_ = sampleRate > 8000.0f ? sampleRate : 48000.0f;
    smoothCoeff_ = 1.0f - std::exp(-1.0f / (0.010f * sampleRate_));
    gateAttack_ = 1.0f - std::exp(-1.0f / (0.002f * sampleRate_));
    gateRelease_ = 1.0f - std::exp(-1.0f / (0.060f * sampleRate_));
    envAttack_ = 1.0f - std::exp(-1.0f / (0.001f * sampleRate_));
    envRelease_ = 1.0f - std::exp(-1.0f / (0.020f * sampleRate_));
    gateHoldSamples_ = static_cast<int>(0.050f * sampleRate_);
    dirty_ = true;
}

void Engine::setParameter(const std::uint32_t index, const float value)
{
    if (index >= kParameterCount || kParameterSpecs[index].output)
        return;
    const auto& s = kParameterSpecs[index];
    float v = std::clamp(value, s.minimum, s.maximum);
    if (s.integer)
        v = std::round(v);
    values_[index] = v;
    dirty_ = true;
}

void Engine::setModel(std::shared_ptr<ModelRig> model)
{
    model_ = std::move(model);
    dirty_ = true;
}

void Engine::setIr(std::shared_ptr<IrRig> ir)
{
    ir_ = std::move(ir);
    dirty_ = true;
}

void Engine::reset()
{
    for (auto& c : chains_) {
        c.gate = {};
        c.bass.reset();
        c.mid.reset();
        c.treble.reset();
        c.lowCut.reset();
        c.highCut.reset();
    }
    for (auto& d : dry_)
        std::fill(d.begin(), d.end(), 0.0f);
    if (ir_)
        ir_->clear();
    inPeakDb_ = outPeakDb_ = -90.0f;
    inPeakHold_ = outPeakHold_ = 0.0f;
    inGain_.snap();
    outGain_.snap();
    irGain_.snap();
    normGain_.snap();
}

int Engine::latencySamples() const { return modelActive() ? model_->latencySamples() : 0; }

void Engine::updateCoefficients()
{
    dirty_ = false;
    const double sr = sampleRate_;
    const ToneCoeffs tone = designTone(sr, values_[kBass], values_[kMid], values_[kTreble]);
    const bool lowOn = values_[kIrLowCut] > kLowCutOff * 1.001f;
    const bool highOn = values_[kIrHighCut] < kHighCutOff * 0.999f;
    const BiquadCoeffs low = designBiquad(FilterType::HighPass, sr, values_[kIrLowCut], 0.7071, 0.0);
    const BiquadCoeffs high = designBiquad(FilterType::LowPass, sr, values_[kIrHighCut], 0.7071, 0.0);
    for (auto& c : chains_) {
        c.bass.set(tone.bass);
        c.mid.set(tone.mid);
        c.treble.set(tone.treble);
        c.lowCut.set(lowOn ? low : BiquadCoeffs {});
        c.highCut.set(highOn ? high : BiquadCoeffs {});
    }
    inGain_.target = dbToGain(values_[kInputGain]);
    outGain_.target = dbToGain(values_[kOutputGain]);
    irGain_.target = dbToGain(values_[kIrGain]);
    float norm = 1.0f;
    if (modelActive() && values_[kNormalize] >= 0.5f && model_->hasLoudness())
        norm = dbToGain(kNormalizeTargetDb - static_cast<float>(model_->loudnessDb()));
    normGain_.target = norm;

    const int delay = std::min(latencySamples(), kDryDelaySize - 1);
    if (delay != dryDelay_) {
        dryDelay_ = delay;
        for (auto& d : dry_)
            std::fill(d.begin(), d.end(), 0.0f);
    }
}

void Engine::applyGate(Chain& chain, float* x, const int n)
{
    const float threshold = values_[kGate];
    if (threshold <= kGateOff + 0.5f) {
        chain.gate.gain = 1.0f;
        chain.gate.open = true;
        return;
    }
    const float openLevel = dbToGain(threshold);
    const float closeLevel = dbToGain(threshold - 4.0f);
    Gate& g = chain.gate;
    for (int i = 0; i < n; ++i) {
        const float a = std::fabs(x[i]);
        g.envelope += (a > g.envelope ? envAttack_ : envRelease_) * (a - g.envelope);
        if (g.envelope > openLevel) {
            g.open = true;
            g.hold = static_cast<float>(gateHoldSamples_);
        } else if (g.open && g.envelope < closeLevel) {
            if (g.hold > 0.0f)
                g.hold -= 1.0f;
            else
                g.open = false;
        }
        const float target = g.open ? 1.0f : 0.0f;
        g.gain += (target > g.gain ? gateAttack_ : gateRelease_) * (target - g.gain);
        x[i] *= g.gain;
    }
}

void Engine::process(const float* inL, const float* inR, float* outL, float* outR, int frames)
{
    ScopedNoDenormals noDenormals;
    int done = 0;
    while (done < frames) {
        const int n = std::min(kMaxBlock, frames - done);
        processBlock(inL + done, inR + done, outL + done, outR + done, n);
        done += n;
    }
}

void Engine::processBlock(const float* inL, const float* inR, float* outL, float* outR, const int n)
{
    if (dirty_)
        updateCoefficients();

    const bool stereoMode = static_cast<int>(std::lround(values_[kChannels])) == kStereo;
    if (lastChannels_ != static_cast<int>(stereoMode)) {
        lastChannels_ = static_cast<int>(stereoMode);
        if (ir_)
            ir_->clear();
    }
    const int chainCount = stereoMode ? 2 : 1;
    const bool useModel = modelActive();
    const bool useIr = irActive();
    const bool irMono = !ir_ || !ir_->stereo();
    const float mix = values_[kMix];

    // Wet path, one chain per channel in stereo mode, one shared chain otherwise.
    float inPeak = 0.0f;
    Smoothed inEnd = inGain_, normEnd = normGain_;
    for (int c = 0; c < chainCount; ++c) {
        Smoothed ig = inGain_, ng = normGain_;  // every chain ramps over the same samples
        Chain& chain = chains_[static_cast<std::size_t>(c)];
        float* x = wet_[static_cast<std::size_t>(c)].data();
        for (int i = 0; i < n; ++i) {
            const float src = stereoMode ? (c == 0 ? inL[i] : inR[i]) : 0.5f * (inL[i] + inR[i]);
            const float v = src * ig.next(smoothCoeff_);
            x[i] = v;
            inPeak = std::max(inPeak, std::fabs(v));
        }
        applyGate(chain, x, n);
        if (useModel)
            model_->process(c, x, x, n);
        for (int i = 0; i < n; ++i) {
            float v = x[i] * ng.next(smoothCoeff_);
            v = chain.treble.process(chain.mid.process(chain.bass.process(v)));
            x[i] = v;
        }
        inEnd = ig;
        normEnd = ng;
    }
    inGain_ = inEnd;
    normGain_ = normEnd;
    inPeakHold_ = std::max(inPeak, inPeakHold_ * std::exp(-static_cast<float>(n) / (0.5f * sampleRate_)));
    inPeakDb_ = gainToDb(inPeakHold_);

    // Cabinet. A mono IR on a shared chain is convolved once and copied.
    const int irChannels = (stereoMode || !irMono) ? 2 : 1;
    for (int c = 0; c < 2; ++c)
        std::copy(wet_[static_cast<std::size_t>(std::min(c, chainCount - 1))].begin(),
                  wet_[static_cast<std::size_t>(std::min(c, chainCount - 1))].begin() + n,
                  scratch_[static_cast<std::size_t>(c)].begin());
    if (useIr) {
        for (int c = 0; c < irChannels; ++c) {
            float* y = scratch_[static_cast<std::size_t>(c)].data();
            ir_->process(c, y, y, n);
            Chain& chain = chains_[static_cast<std::size_t>(c)];
            for (int i = 0; i < n; ++i)
                y[i] = chain.highCut.process(chain.lowCut.process(y[i]));
        }
        if (irChannels == 1)
            std::copy(scratch_[0].begin(), scratch_[0].begin() + n, scratch_[1].begin());
        for (int c = 0; c < 2; ++c) {
            float* y = scratch_[static_cast<std::size_t>(c)].data();
            Smoothed g = irGain_;
            for (int i = 0; i < n; ++i)
                y[i] *= g.next(smoothCoeff_);
            if (c == 1)
                irGain_ = g;
        }
    }

    // Dry/wet mix, output gain and safety.
    const float* dryIn[2] = {inL, inR};
    float* out[2] = {outL, outR};
    float outPeak = 0.0f;
    for (int c = 0; c < 2; ++c) {
        auto& delayLine = dry_[static_cast<std::size_t>(c)];
        Smoothed g = outGain_;
        for (int i = 0; i < n; ++i) {
            const int wp = (dryPos_ + i) % kDryDelaySize;
            delayLine[static_cast<std::size_t>(wp)] = dryIn[c][i];
            const float dry = delayLine[static_cast<std::size_t>((wp - dryDelay_ + kDryDelaySize) % kDryDelaySize)];
            float v = dry * (1.0f - mix) + scratch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] * g.next(smoothCoeff_) * mix;
            if (!std::isfinite(v))
                v = 0.0f;
            v = std::clamp(v, -8.0f, 8.0f);
            out[c][i] = v;
            outPeak = std::max(outPeak, std::fabs(v));
        }
        if (c == 1)
            outGain_ = g;
    }
    dryPos_ = (dryPos_ + n) % kDryDelaySize;
    outPeakHold_ = std::max(outPeak, outPeakHold_ * std::exp(-static_cast<float>(n) / (0.5f * sampleRate_)));
    outPeakDb_ = gainToDb(outPeakHold_);
}

}  // namespace downspout::namfx
