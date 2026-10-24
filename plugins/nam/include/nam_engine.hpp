#pragma once

// Portable core of the NAM plugin: loads Neural Amp Modeler (.nam) models and impulse responses
// (.wav), and runs input gain -> noise gate -> model -> tone -> cabinet IR -> output. No DPF.
//
// Loading allocates and may take a while (a model is prewarmed), so it belongs on a non-audio
// thread. A loaded rig is handed to Engine::setModel/setIr, after which only the audio thread
// touches it; the loading thread must keep its own reference until the audio thread has let go.

#include "nam_dsp.hpp"
#include "nam_params.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace downspout::namfx {

// Longest block handed to a rig or the chain in one call; Engine::process splits longer ones.
inline constexpr int kMaxBlock = 256;

class ModelRig {
public:
    ~ModelRig();
    ModelRig(const ModelRig&) = delete;
    ModelRig& operator=(const ModelRig&) = delete;

    // Returns null and sets `error` when the file cannot be used.
    [[nodiscard]] static std::shared_ptr<ModelRig> load(const std::string& path, double hostRate, std::string& error);

    // Processes one channel (0 or 1), `n` <= kMaxBlock samples. `in` and `out` may be the same.
    void process(int channel, const float* in, float* out, int n);

    [[nodiscard]] double modelRate() const { return modelRate_; }
    [[nodiscard]] bool resampling() const { return resampling_; }
    // Host-rate samples the rig delays the signal by (0 unless resampling).
    [[nodiscard]] int latencySamples() const { return latency_; }
    // Samples the resampler failed to deliver on time since loading; always 0 unless something is wrong.
    [[nodiscard]] int underruns() const;
    [[nodiscard]] bool hasLoudness() const { return hasLoudness_; }
    [[nodiscard]] double loudnessDb() const { return loudnessDb_; }

private:
    ModelRig();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    double modelRate_ = 48000.0;
    bool resampling_ = false;
    int latency_ = 0;
    bool hasLoudness_ = false;
    double loudnessDb_ = 0.0;
};

class IrRig {
public:
    IrRig();
    // Reads a mono or stereo WAV, resamples it to `hostRate` and trims it to kMaxSeconds.
    [[nodiscard]] static std::shared_ptr<IrRig> load(const std::string& path, double hostRate, std::string& error);
    // Builds a rig from raw taps (channel-major, `channels` of 1 or 2), already at the host rate.
    [[nodiscard]] static std::shared_ptr<IrRig> fromSamples(const std::vector<float>& taps, int channels);

    static constexpr double kMaxSeconds = 2.0;

    void process(int channel, const float* in, float* out, int n) { conv_[static_cast<std::size_t>(channel & 1)].process(in, out, n); }
    void clear()
    {
        conv_[0].clear();
        conv_[1].clear();
    }
    [[nodiscard]] bool stereo() const { return stereo_; }
    [[nodiscard]] int lengthSamples() const { return conv_[0].length(); }

private:
    std::array<PartitionedConvolver, 2> conv_;
    bool stereo_ = false;
};

// The three-band tone control as biquads, shared by the engine and the panel's response plot.
struct ToneCoeffs {
    BiquadCoeffs bass, mid, treble;
};
[[nodiscard]] ToneCoeffs designTone(double sampleRate, float bassDb, float midDb, float trebleDb);
[[nodiscard]] double toneResponseDb(const ToneCoeffs& tone, double freq, double sampleRate);

class Engine {
public:
    explicit Engine(float sampleRate = 48000.0f);

    void setSampleRate(float sampleRate);
    void setParameter(std::uint32_t index, float value);
    [[nodiscard]] float parameter(std::uint32_t index) const { return index < kParameterCount ? values_[index] : 0.0f; }

    void setModel(std::shared_ptr<ModelRig> model);
    void setIr(std::shared_ptr<IrRig> ir);

    // Clears filters, gate, meters and convolver history. Keeps the loaded model, IR and parameters.
    void reset();

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames);

    // Host-rate samples of delay (the model's resampler); the dry path is delayed to match.
    [[nodiscard]] int latencySamples() const;
    [[nodiscard]] float inputPeakDb() const { return inPeakDb_; }
    [[nodiscard]] float outputPeakDb() const { return outPeakDb_; }
    [[nodiscard]] bool modelActive() const { return model_ != nullptr && values_[kModelEnable] >= 0.5f; }
    [[nodiscard]] bool irActive() const { return ir_ != nullptr && values_[kIrEnable] >= 0.5f; }

private:
    struct Smoothed {
        float current = 1.0f;
        float target = 1.0f;
        void snap() { current = target; }
        float next(const float coeff)
        {
            current += coeff * (target - current);
            return current;
        }
    };
    struct Gate {
        float envelope = 0.0f;
        float gain = 1.0f;
        float hold = 0.0f;
        bool open = true;
    };
    struct Chain {
        Gate gate;
        Biquad bass, mid, treble, lowCut, highCut;
    };

    void updateCoefficients();
    void processBlock(const float* inL, const float* inR, float* outL, float* outR, int n);
    void applyGate(Chain& chain, float* x, int n);

    float sampleRate_ = 48000.0f;
    std::array<float, kParameterCount> values_ {};
    bool dirty_ = true;
    float smoothCoeff_ = 0.01f;
    float gateAttack_ = 0.01f, gateRelease_ = 0.001f, envAttack_ = 0.1f, envRelease_ = 0.001f;
    int gateHoldSamples_ = 2400;

    std::shared_ptr<ModelRig> model_;
    std::shared_ptr<IrRig> ir_;
    std::array<Chain, 2> chains_;
    Smoothed inGain_, outGain_, irGain_, normGain_;

    std::array<std::vector<float>, 2> dry_;  // delay line for the dry path
    int dryPos_ = 0;
    int dryDelay_ = 0;

    std::array<std::array<float, kMaxBlock>, 2> wet_ {};
    std::array<std::array<float, kMaxBlock>, 2> scratch_ {};
    float inPeakDb_ = -90.0f, outPeakDb_ = -90.0f;
    float inPeakHold_ = 0.0f, outPeakHold_ = 0.0f;
    int lastChannels_ = -1;
};

}  // namespace downspout::namfx
