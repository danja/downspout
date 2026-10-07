#include "pratt_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::pratt {

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr int kChannels = 16;
constexpr int kDrumChannel = 9;
constexpr double kBaseRoomTailSeconds = 1.701;

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float processCascade(const Cascade& c, std::array<BiquadState, kMaxSections>& st, float x) {
    for (int i = 0; i < c.count; ++i) x = processSection(c.sections[i], st[i], x);
    return x;
}

// Peak of a simulated hit (noise -> cascade -> band split -> decay), the
// per-hit normalisation the Python renderer applies with `wave /= max(abs(wave))`.
double drumPeak(const Cascade& cascade, int pitch, double fs) {
    const bool hat = pitch == 42 || pitch == 44 || pitch == 46;
    const bool longHit = pitch == 46 || pitch == 49 || pitch == 51 || pitch == 57;
    const double lpCoef = 1.0 - std::exp(-kTwoPi * (hat ? 4500.0 : 250.0) / fs);
    const double decay = std::exp(-1.0 / ((pitch == 46 ? 0.24 : 0.075) * fs));
    const long length = static_cast<long>((longHit ? 1.2 : 0.45) * fs);
    std::array<BiquadState, kMaxSections> st{};
    std::uint32_t rng = static_cast<std::uint32_t>(pitch * 131 + 100) | 1u;
    double lp = 0.0, env = 1.0, peak = 1e-9;
    for (long i = 0; i < length; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float noise = static_cast<float>(rng) / 4294967295.0f * 2.0f - 1.0f;
        const float y = processCascade(cascade, st, noise);
        lp += lpCoef * (y - lp);
        peak = std::max(peak, std::abs(((y - lp) + 0.08 * lp) * env));
        env *= decay;
    }
    return peak;
}

}  // namespace

Cascade buildCascade(int n, double cutoffHz, double sampleRate) {
    Cascade c;
    n = clampi(n, 1, kMaxIndex);
    cutoffHz = clampd(cutoffHz, 10.0, 0.1 * sampleRate);
    const std::vector<Biquad> v = makeSections(n, kTwoPi * cutoffHz, sampleRate);
    c.count = static_cast<int>(std::min<std::size_t>(v.size(), kMaxSections));
    for (int i = 0; i < c.count; ++i) c.sections[i] = v[i];
    return c;
}

struct Engine::Voice {
    bool active = false, released = false, sustained = false, drum = false, kick = false;
    int channel = 0, pitch = 0, velocity = 0;
    std::uint64_t order = 0;
    std::shared_ptr<const Wavetable> bright, dark;
    double phase = 0.0, f0 = 0.0;
    long n = 0;
    double attackSamples = 1.0;
    double decayEnv = 1.0, decayMul = 1.0, brightEnv = 1.0, brightMul = 1.0;
    double relEnv = 1.0, relMul = 1.0;
    double gain = 0.0;
    double vibDepth = 0.0, vibPhase = 0.0, vibInc = 0.0;
    // Drums.
    Cascade cascade;
    std::array<BiquadState, kMaxSections> state{};
    double lp = 0.0, lpCoef = 0.0;
    std::uint32_t rng = 1;
    long length = 0;
    double env1 = 1.0, mul1 = 1.0, env2 = 1.0, mul2 = 1.0;
    double kickPhase = 0.0, kickFreqEnv = 1.0, kickFreqMul = 1.0;
};

struct Engine::Channel {
    int program = 0;
    double volume = 100.0 / 127.0, expression = 1.0, pan = 64.0 / 127.0, bend = 0.0;
    double volumeSm = 100.0 / 127.0, expressionSm = 1.0, panSm = 64.0 / 127.0, bendSm = 0.0;
    bool sustain = false;
    int voiceCount = 0;
    // Per-sample derived values.
    double gain = 1.0, left = 0.7071, right = 0.7071, bendRatio = 1.0;
};

struct Engine::FilterBank {
    Cascade cur, old;
    std::array<BiquadState, kMaxSections> curL{}, curR{}, oldL{}, oldR{};
    int curN = -1;
    double curCutoff = -1.0;
    int fadePos = 0, fadeLen = 1;
    bool fading = false;

    void retarget(int n, double cutoff, double fs) {
        if (n == curN && cutoff == curCutoff) return;
        if (curN >= 0) {
            old = cur;
            oldL = curL;
            oldR = curR;
            fading = true;
            fadePos = 0;
            fadeLen = std::max(1, static_cast<int>(0.02 * fs));
        }
        cur = buildCascade(n, cutoff, fs);
        curL = {};
        curR = {};
        curN = n;
        curCutoff = cutoff;
    }

    void process(float& l, float& r) {
        float yl = processCascade(cur, curL, l);
        float yr = processCascade(cur, curR, r);
        if (fading) {
            const float a = static_cast<float>(fadePos) / static_cast<float>(fadeLen);
            const float ol = processCascade(old, oldL, l);
            const float orr = processCascade(old, oldR, r);
            yl = ol + a * (yl - ol);
            yr = orr + a * (yr - orr);
            if (++fadePos >= fadeLen) fading = false;
        }
        l = yl;
        r = yr;
    }
};

Engine::Engine(double sampleRate)
    : fs_(sampleRate), voices_(kMaxVoices), channels_(kChannels), filter_(new FilterBank()) {
    const std::size_t len = static_cast<std::size_t>(kBaseRoomTailSeconds * fs_) + 2;
    roomBuffer_.assign(len, 0.0f);
    roomLpCoef_ = 1.0 - std::exp(-kTwoPi * 3800.0 / fs_);
    hpCoef_ = 1.0 / (1.0 + kTwoPi * 22.0 / fs_);
    // (seconds, level, side) from the Python reference.
    const double taps[][3] = {{0.053, .115, 0}, {0.071, .112, 1}, {0.113, .080, 1}, {0.149, .076, 0},
                              {0.199, .052, 0}, {0.251, .052, 1}, {0.337, .037, 1}, {0.421, .034, 0},
                              {0.557, .024, 0}, {0.683, .022, 1}, {0.883, .016, 0}, {1.091, .013, 1},
                              {1.433, .008, 0}, {1.701, .007, 1}};
    for (const auto& t : taps)
        roomTaps_.push_back({static_cast<int>(t[0] * fs_), static_cast<int>(t[1] * 1e6), static_cast<int>(t[2])});
}

Engine::~Engine() = default;

void Engine::warmCaches() { warmRoots(1, kMaxIndex); }

void Engine::warmRoots(int first, int last) {
    for (int n = clampi(first, 1, kMaxIndex); n <= clampi(last, 1, kMaxIndex); ++n) roots(n);
}

void Engine::setParams(const EngineParams& p) {
    const Mode previous = params_.mode;
    params_ = p;
    if (params_.mode == Mode::Filter && previous != Mode::Filter) allSoundOff();
    params_.filterIndexA = clampi(p.filterIndexA, 1, kMaxIndex);
    params_.filterIndexB = clampi(p.filterIndexB, 1, kMaxIndex);
    params_.filterMix = clampd(p.filterMix, 0.0, 1.0);
    params_.room = clampd(p.room, 0.0, 1.0);
    params_.bendRange = clampd(p.bendRange, 0.0, 24.0);
    if (params_.mode != Mode::Synth) {
        const long long product = static_cast<long long>(params_.filterIndexA) * params_.filterIndexB;
        const int n = static_cast<int>(std::min<long long>(product, kMaxIndex));
        filter_->retarget(n, clampd(p.filterCutoffHz, 10.0, 0.1 * fs_), fs_);
    }
}

namespace {

struct Shaping {
    int base, xiQ, rollQ;
};

Shaping shapingFor(int preset, const VoiceTuning& tuning) {
    const Preset& pr = kPresets[static_cast<std::size_t>(preset)];
    return {tuning.base > 0 ? clampi(tuning.base, 1, 64) : pr.base,
            static_cast<int>(std::lround(pr.xi * clampd(tuning.xiScale, 0.1, 4.0) * 1000.0)),
            static_cast<int>(std::lround((pr.roll + clampd(tuning.rollOffset, -1.0, 2.0)) * 1000.0))};
}

// Upper bound on cached tables (about 16 KB each). Past it, new tables are still
// built for the note that needs them but are not cached, so the audio thread never
// frees a large cache. Normal use stays far below this; see trimTables().
constexpr std::size_t kMaxCachedTables = 4096;

}  // namespace

std::shared_ptr<const Wavetable> Engine::table(int pitch, int preset, int bucket, bool dark,
                                               const VoiceTuning& tuning) {
    const auto [base, xiQ, rollQ] = shapingFor(preset, tuning);
    const Preset& pr = kPresets[static_cast<std::size_t>(preset)];
    const TableKey key{pitch, preset, bucket, dark, base, xiQ, rollQ};
    {
        std::lock_guard<std::mutex> lock(tablesMutex_);
        auto it = tables_.find(key);
        if (it != tables_.end()) return it->second;
    }
    TableParams tp;
    tp.pitch = pitch;
    tp.base = base;
    tp.roll = rollQ / 1000.0;
    tp.xi = xiQ / 1000.0;
    tp.velocity = (bucket + 0.5) / 8.0;
    tp.extraRoll = dark ? 0.75 : 0.0;
    tp.evenGain = pr.evenGain;
    tp.upperStart = pr.upperStart;
    tp.upperGain = pr.upperGain;
    tp.sampleRate = fs_;
    auto built = std::make_shared<const Wavetable>(buildWavetable(tp));  // outside the lock
    std::lock_guard<std::mutex> lock(tablesMutex_);
    if (tables_.size() >= kMaxCachedTables) return built;  // uncached; the voice still holds it
    return tables_.emplace(key, built).first->second;
}

void Engine::trimTables(const VoiceTuning& keep) {
    std::vector<std::shared_ptr<const Wavetable>> doomed;
    {
        std::lock_guard<std::mutex> lock(tablesMutex_);
        for (auto it = tables_.begin(); it != tables_.end();) {
            const int preset = std::get<1>(it->first);
            const Shaping s = shapingFor(preset, keep);
            if (std::get<4>(it->first) != s.base || std::get<5>(it->first) != s.xiQ ||
                std::get<6>(it->first) != s.rollQ) {
                doomed.push_back(std::move(it->second));
                it = tables_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // `doomed` is released here, outside the lock; voices still playing a table keep it alive.
}

Cascade Engine::drumCascade(int pitch) {
    {
        std::lock_guard<std::mutex> lock(drumMutex_);
        auto it = drumCascades_.find(pitch);
        if (it != drumCascades_.end()) return it->second;
    }
    Cascade built = buildCascade(pitch + 1, 6000.0, fs_);
    built.noiseNorm = 1.0 / drumPeak(built, pitch, fs_);
    std::lock_guard<std::mutex> lock(drumMutex_);
    return drumCascades_.emplace(pitch, built).first->second;
}

void Engine::preload(int preset, const VoiceTuning& tuning, int lowPitch, int highPitch) {
    preset = clampi(preset, 0, static_cast<int>(kPresetCount) - 1);
    const bool wantDark = kPresets[static_cast<std::size_t>(preset)].brightDecay > 0.0;
    for (int pitch = clampi(lowPitch, 0, 127); pitch <= clampi(highPitch, 0, 127); ++pitch)
        for (int b = 0; b < 8; ++b) {
            table(pitch, preset, b, false, tuning);
            if (wantDark) table(pitch, preset, b, true, tuning);
        }
}

void Engine::preloadDrums() {
    for (int pitch = 27; pitch <= 87; ++pitch) preloadDrum(pitch);
}

void Engine::preloadDrum(int pitch) {
    if (pitch >= 0 && pitch <= 127 && pitch != 35 && pitch != 36) drumCascade(pitch);
}

Engine::Voice* Engine::allocateVoice() {
    Voice* victim = nullptr;
    for (Voice& v : voices_)
        if (!v.active) return &v;
    // Steal: quietest releasing voice, else the oldest.
    for (Voice& v : voices_)
        if (v.released && (!victim || v.relEnv < victim->relEnv)) victim = &v;
    if (!victim) {
        victim = &voices_[0];
        for (Voice& v : voices_)
            if (v.order < victim->order) victim = &v;
    }
    --channels_[static_cast<std::size_t>(victim->channel)].voiceCount;
    *victim = Voice();
    return victim;
}

void Engine::noteOn(int channel, int pitch, int velocity) {
    if (channel < 0 || channel >= kChannels || pitch < 0 || pitch > 127) return;
    if (velocity <= 0) {
        noteOff(channel, pitch);
        return;
    }
    if (params_.mode == Mode::Filter) return;  // voices are not rendered in Filter mode
    velocity = std::min(velocity, 127);
    Voice* vp = allocateVoice();
    Voice& v = *vp;
    v = Voice();
    v.active = true;
    v.channel = channel;
    v.pitch = pitch;
    v.velocity = velocity;
    v.order = ++noteCounter_;
    ++channels_[static_cast<std::size_t>(channel)].voiceCount;

    if (channel == kDrumChannel && params_.percussion) {
        v.drum = true;
        v.rng = static_cast<std::uint32_t>(pitch * 131 + velocity) | 1u;
        const bool long_ = pitch == 46 || pitch == 49 || pitch == 51 || pitch == 57;
        v.length = static_cast<long>((long_ ? 1.2 : 0.45) * fs_);
        v.gain = 0.72 * std::pow(velocity / 127.0, 1.25);
        if (pitch == 35 || pitch == 36) {
            v.kick = true;
            v.mul1 = std::exp(-1.0 / (0.10 * fs_));
            v.mul2 = std::exp(-1.0 / (0.008 * fs_));
            v.kickFreqMul = std::exp(-20.0 / fs_);
        } else {
            v.cascade = drumCascade(pitch);
            const bool hat = pitch == 42 || pitch == 44 || pitch == 46;
            v.lpCoef = 1.0 - std::exp(-kTwoPi * (hat ? 4500.0 : 250.0) / fs_);
            v.mul1 = std::exp(-1.0 / ((pitch == 46 ? 0.24 : 0.075) * fs_));
        }
        return;
    }

    const int preset = params_.presetOverride >= 0
                           ? clampi(params_.presetOverride, 0, static_cast<int>(kPresetCount) - 1)
                           : presetForProgram(channels_[static_cast<std::size_t>(channel)].program);
    const Preset& pr = kPresets[static_cast<std::size_t>(preset)];
    const int bucket = std::min(7, velocity / 16);
    const VoiceTuning tuning{params_.baseOverride, params_.xiScale, params_.rollOffset};
    v.bright = table(pitch, preset, bucket, false, tuning);
    if (pr.brightDecay > 0.0) {
        v.dark = table(pitch, preset, bucket, true, tuning);
        v.brightMul = std::exp(-1.0 / (pr.brightDecay * fs_));
    }
    if (pr.decay > 0.0) v.decayMul = std::exp(-1.0 / (pr.decay * fs_));
    v.relMul = std::exp(-1.0 / (pr.release * fs_));
    v.attackSamples = std::max(1.0, pr.attack * fs_);
    v.f0 = pitchToHz(pitch);
    v.phase = std::fmod(channel * 0.071, 1.0);
    v.vibDepth = pr.vibrato;
    v.vibInc = kTwoPi * (5.1 + 0.05 * channel) / fs_;
    v.gain = std::pow(velocity / 127.0, 1.35) * pr.level;
}

namespace {
void releaseVoice(bool& released, bool& sustained) {
    released = true;
    sustained = false;
}
}  // namespace

void Engine::noteOff(int channel, int pitch) {
    if (channel < 0 || channel >= kChannels) return;
    Voice* target = nullptr;
    for (Voice& v : voices_)
        if (v.active && !v.drum && !v.released && !v.sustained && v.channel == channel && v.pitch == pitch &&
            (!target || v.order < target->order))
            target = &v;
    if (!target) return;
    if (channels_[static_cast<std::size_t>(channel)].sustain)
        target->sustained = true;
    else
        releaseVoice(target->released, target->sustained);
}

void Engine::controlChange(int channel, int cc, int value) {
    if (channel < 0 || channel >= kChannels) return;
    Channel& c = channels_[static_cast<std::size_t>(channel)];
    const double x = clampi(value, 0, 127) / 127.0;
    switch (cc) {
    case 7: c.volume = x; break;
    case 10: c.pan = x; break;
    case 11: c.expression = x; break;
    case 64: {
        const bool down = value >= 64;
        if (c.sustain && !down)
            for (Voice& v : voices_)
                if (v.active && v.channel == channel && v.sustained) releaseVoice(v.released, v.sustained);
        c.sustain = down;
        break;
    }
    case 120:
        for (Voice& v : voices_)
            if (v.active && v.channel == channel) {
                v.active = false;
                --c.voiceCount;
            }
        break;
    case 123:
        for (Voice& v : voices_)
            if (v.active && !v.drum && v.channel == channel) releaseVoice(v.released, v.sustained);
        break;
    default: break;
    }
}

void Engine::pitchBend(int channel, int value14) {
    if (channel < 0 || channel >= kChannels) return;
    channels_[static_cast<std::size_t>(channel)].bend =
        (clampi(value14, 0, 16383) - 8192) / 8192.0 * params_.bendRange;
}

void Engine::programChange(int channel, int program) {
    if (channel < 0 || channel >= kChannels) return;
    channels_[static_cast<std::size_t>(channel)].program = clampi(program, 0, 127);
}

void Engine::allNotesOff() {
    for (Voice& v : voices_)
        if (v.active && !v.drum) releaseVoice(v.released, v.sustained);
}

void Engine::allSoundOff() {
    for (Voice& v : voices_) v.active = false;
    for (Channel& c : channels_) c.voiceCount = 0;
}

bool Engine::handleMidi(const std::uint8_t* d, int size) {
    if (size < 1 || (d[0] & 0x80) == 0) return false;
    const int ch = d[0] & 0x0F;
    switch (d[0] & 0xF0) {
    case 0x80:
        if (size < 3) return false;
        noteOff(ch, d[1] & 0x7F);
        return true;
    case 0x90:
        if (size < 3) return false;
        noteOn(ch, d[1] & 0x7F, d[2] & 0x7F);
        return true;
    case 0xB0:
        if (size < 3) return false;
        controlChange(ch, d[1] & 0x7F, d[2] & 0x7F);
        return true;
    case 0xC0:
        if (size < 2) return false;
        programChange(ch, d[1] & 0x7F);
        return true;
    case 0xE0:
        if (size < 3) return false;
        pitchBend(ch, (d[1] & 0x7F) | ((d[2] & 0x7F) << 7));
        return true;
    default: return false;
    }
}

int Engine::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_) n += v.active ? 1 : 0;
    return n;
}

void Engine::renderVoice(Voice& v, float& outL, float& outR) {
    Channel& ch = channels_[static_cast<std::size_t>(v.channel)];
    double s = 0.0;
    if (v.drum) {
        if (v.kick) {
            const double freq = 48.0 + 80.0 * v.kickFreqEnv;
            v.kickPhase += kTwoPi * freq / fs_;
            v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
            const double noise = (static_cast<double>(v.rng) / 4294967295.0 * 2.0 - 1.0) * 1.732;
            s = std::sin(v.kickPhase) * v.env1 + 0.08 * noise * v.env2;
            v.kickFreqEnv *= v.kickFreqMul;
            v.env2 *= v.mul2;
        } else {
            v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
            const float noise = static_cast<float>(v.rng) / 4294967295.0f * 2.0f - 1.0f;
            const float y = processCascade(v.cascade, v.state, noise);
            v.lp += v.lpCoef * (y - v.lp);
            s = ((y - v.lp) + 0.08 * v.lp) * v.cascade.noiseNorm * v.env1;
        }
        v.env1 *= v.mul1;
        if (v.n < 80) s *= std::pow(v.n / 80.0, 2.0);
        const long remaining = v.length - v.n;
        if (remaining < 160) s *= std::pow(std::max(0L, remaining) / 160.0, 2.0);
        s *= v.gain;
        if (++v.n >= v.length) {
            v.active = false;
            --ch.voiceCount;
        }
    } else {
        double a = v.n < v.attackSamples ? static_cast<double>(v.n) / v.attackSamples : 1.0;
        a = a * a * (3.0 - 2.0 * a);
        const double vib = v.vibDepth > 0.0 ? 1.0 + v.vibDepth * std::sin(v.vibPhase) : 1.0;
        v.vibPhase += v.vibInc;
        v.phase += v.f0 * ch.bendRatio * vib / fs_;
        v.phase -= std::floor(v.phase);
        double w = readWavetable(v.bright->samples, v.phase);
        if (v.dark) {
            w = w * v.brightEnv + readWavetable(v.dark->samples, v.phase) * (1.0 - v.brightEnv);
            v.brightEnv *= v.brightMul;
        }
        const double env = a * v.decayEnv * v.relEnv;
        v.decayEnv *= v.decayMul;
        if (v.released) v.relEnv *= v.relMul;
        s = w * env * v.gain;
        ++v.n;
        if (v.decayEnv < 6.8e-5 || v.relEnv < 1e-4) {
            v.active = false;
            --ch.voiceCount;
        }
    }
    s *= ch.gain;
    outL += static_cast<float>(s * ch.left);
    outR += static_cast<float>(s * ch.right);
}

void Engine::process(const float* inL, const float* inR, float* outL, float* outR, int frames) {
    const double smooth = 1.0 / (0.006 * fs_);
    const bool synth = params_.mode != Mode::Filter;
    const bool filt = params_.mode != Mode::Synth;
    const std::size_t roomLen = roomBuffer_.size();
    for (int i = 0; i < frames; ++i) {
        float l = 0.0f, r = 0.0f;
        if (synth) {
            for (Channel& c : channels_) {
                if (c.voiceCount <= 0) continue;
                c.volumeSm += smooth * (c.volume - c.volumeSm);
                c.expressionSm += smooth * (c.expression - c.expressionSm);
                c.panSm += smooth * (c.pan - c.panSm);
                c.bendSm += smooth * (c.bend - c.bendSm);
                c.gain = std::pow(c.volumeSm, 1.25) * std::pow(c.expressionSm, 1.1);
                const double angle = c.panSm * kTwoPi / 4.0;
                c.left = std::cos(angle);
                c.right = std::sin(angle);
                c.bendRatio = std::exp2(c.bendSm / 12.0);
            }
            for (Voice& v : voices_)
                if (v.active) renderVoice(v, l, r);

            // Diffuse room from a low-passed mono send of the dry mix.
            const double send = 0.5 * (static_cast<double>(l) + r);
            roomLpState_ += roomLpCoef_ * (send - roomLpState_);
            roomBuffer_[roomPos_] = static_cast<float>(roomLpState_);
            float tl = l, tr = r;
            for (const auto& t : roomTaps_) {
                const std::size_t idx = (roomPos_ + roomLen - static_cast<std::size_t>(t[0])) % roomLen;
                const float add = static_cast<float>(params_.room * (t[1] * 1e-6)) * roomBuffer_[idx];
                (t[2] ? tr : tl) += add;
            }
            roomPos_ = (roomPos_ + 1) % roomLen;
            // 22 Hz high-pass, then master gain and soft clip.
            const double hl = hpCoef_ * (hpPrevOutL_ + tl - hpPrevInL_);
            const double hr = hpCoef_ * (hpPrevOutR_ + tr - hpPrevInR_);
            hpPrevInL_ = tl; hpPrevInR_ = tr; hpPrevOutL_ = hl; hpPrevOutR_ = hr;
            l = static_cast<float>(std::tanh(hl * params_.masterGain));
            r = static_cast<float>(std::tanh(hr * params_.masterGain));
        }
        if (filt) {
            const float dl = (inL ? inL[i] : 0.0f) + l;
            const float dr = (inR ? inR[i] : (inL ? inL[i] : 0.0f)) + r;
            float wl = dl, wr = dr;
            filter_->process(wl, wr);
            const float m = static_cast<float>(params_.filterMix);
            l = dl + m * (wl - dl);
            r = dr + m * (wr - dr);
        }
        outL[i] = l;
        outR[i] = r;
    }
}

}  // namespace downspout::pratt
