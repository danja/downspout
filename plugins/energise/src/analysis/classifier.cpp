#include "energise_analysis.hpp"

#include "energise_fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace downspout::energise {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Features {
    float lowRatio = 0.0f;
    float midRatio = 0.0f;
    float highRatio = 0.0f;
    float centroid = 1000.0f;
    float pitch = 0.0f;
    float pitchDrop = 1.0f;
    float tailSeconds = 0.05f;
    float attackRms = 0.0f;
    float noiseRatio = 0.0f;
    float burstiness = 0.0f;
    float pan = 0.0f;
    float width = 0.0f;
    float lowFlux = 1.0f;
    float midFlux = 1.0f;
    float highFlux = 1.0f;
};

[[nodiscard]] float smoothstep(const float lo, const float hi, const float x)
{
    if (hi <= lo)
        return x >= hi ? 1.0f : 0.0f;
    const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] float band(float lo, float mid, float hi, float x)
{
    return smoothstep(lo, mid, x) * (1.0f - smoothstep(mid, hi, x));
}

[[nodiscard]] float rmsOf(const std::vector<float>& signal, const std::size_t begin, const std::size_t end)
{
    if (end <= begin || begin >= signal.size())
        return 0.0f;
    const std::size_t last = std::min(end, signal.size());
    double sum = 0.0;
    for (std::size_t i = begin; i < last; ++i)
        sum += static_cast<double>(signal[i]) * signal[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(last - begin)));
}

[[nodiscard]] float dominantPitch(const std::vector<float>& lowpassed, const int sr,
                                  const std::size_t begin, const std::size_t end)
{
    if (end <= begin + 8 || begin >= lowpassed.size())
        return 0.0f;
    const std::size_t last = std::min(end, lowpassed.size());
    const int minLag = std::max(4, sr / 320);
    const int maxLag = std::min(sr / 60, static_cast<int>((last - begin) / 2));
    if (maxLag <= minLag)
        return 0.0f;
    int bestLag = 0;
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (std::size_t i = begin; i + static_cast<std::size_t>(lag) < last; ++i)
            sum += lowpassed[i] * lowpassed[i + static_cast<std::size_t>(lag)];
        if (sum > best) {
            best = sum;
            bestLag = lag;
        }
    }
    return bestLag > 0 && best > 1e-10 ? static_cast<float>(sr) / static_cast<float>(bestLag) : 0.0f;
}

[[nodiscard]] Features extractFeatures(const CaptureBuffer& capture, const double timeSec,
                                       std::vector<float>& lowpassedScratch)
{
    Features features {};
    const int sr = capture.sampleRate;
    const std::size_t frames = std::min(capture.left.size(), capture.right.size());
    if (sr <= 0 || frames == 0)
        return features;

    std::vector<float> mono(frames);
    for (std::size_t i = 0; i < frames; ++i)
        mono[i] = 0.5f * (capture.left[i] + capture.right[i]);

    const std::size_t centre = static_cast<std::size_t>(std::clamp(timeSec * sr, 0.0, static_cast<double>(frames - 1)));
    const std::size_t attackBegin = centre;
    const std::size_t attackEnd = std::min(frames, centre + static_cast<std::size_t>(0.025 * sr));
    const std::size_t tailBegin = attackEnd;
    const std::size_t tailEnd = std::min(frames, centre + static_cast<std::size_t>(0.120 * sr));
    const std::size_t longBegin = tailEnd;
    const std::size_t longEnd = std::min(frames, centre + static_cast<std::size_t>(0.500 * sr));

    const float lowCoefficient = static_cast<float>(1.0 - std::exp(-kPi * 2.0 * 220.0 / sr));
    const float highCoefficient = static_cast<float>(1.0 - std::exp(-kPi * 2.0 * 2500.0 / sr));
    float lowState = 0.0f;
    float highState = 0.0f;
    std::array<double, 3> attackBands {};
    std::array<double, 3> tailBands {};
    std::array<double, 3> longBands {};
    lowpassedScratch.assign(frames, 0.0f);

    const auto accumulate = [&](std::array<double, 3>& target, const float sample, const float low, const float high) {
        const float mid = sample - low - high;
        target[0] += low * low;
        target[1] += mid * mid;
        target[2] += high * high;
    };

    for (std::size_t i = 0; i < frames; ++i) {
        const float sample = mono[i];
        lowState += lowCoefficient * (sample - lowState);
        highState += highCoefficient * (sample - highState);
        const float high = sample - highState;
        lowpassedScratch[i] = lowState;
        if (i >= attackBegin && i < attackEnd)
            accumulate(attackBands, sample, lowState, high);
        if (i >= tailBegin && i < tailEnd)
            accumulate(tailBands, sample, lowState, high);
        if (i >= longBegin && i < longEnd)
            accumulate(longBands, sample, lowState, high);
    }

    const double attackTotal = std::max(1e-12, attackBands[0] + attackBands[1] + attackBands[2]);
    features.lowRatio = static_cast<float>(attackBands[0] / attackTotal);
    features.midRatio = static_cast<float>(attackBands[1] / attackTotal);
    features.highRatio = static_cast<float>(attackBands[2] / attackTotal);
    features.noiseRatio = static_cast<float>((attackBands[1] + attackBands[2]) / attackTotal);
    features.attackRms = static_cast<float>(std::sqrt(attackTotal / std::max<std::size_t>(1, attackEnd - attackBegin)));
    const float attackPeak = rmsOf(mono, attackBegin, attackEnd);

    std::size_t tailStop = longBegin;
    const std::size_t scanEnd = std::min(frames, centre + static_cast<std::size_t>(1.0 * sr));
    const std::size_t hop = static_cast<std::size_t>(std::max(1, sr / 200));
    for (std::size_t i = longBegin; i + hop < scanEnd; i += hop) {
        if (rmsOf(mono, i, i + hop) < attackPeak * 0.05f) {
            tailStop = i;
            break;
        }
        tailStop = i + hop;
    }
    features.tailSeconds = static_cast<float>(static_cast<double>(tailStop - centre) / sr);

    // Spectral centroid of the attack, which separates hats/cymbals from drums.
    constexpr int kFftSize = 1024;
    std::vector<float> window(kFftSize, 0.0f);
    for (int i = 0; i < kFftSize && attackBegin + static_cast<std::size_t>(i) < frames; ++i) {
        const double angle = kPi * 2.0 * i / (kFftSize - 1);
        const float w = static_cast<float>(0.5 - 0.5 * std::cos(angle));
        window[static_cast<std::size_t>(i)] = mono[attackBegin + static_cast<std::size_t>(i)] * w;
    }
    Fft fft {};
    fft.init(kFftSize);
    std::vector<float> re {};
    std::vector<float> im {};
    fft.forward(window.data(), re, im);
    double magnitudeSum = 0.0;
    double weightedSum = 0.0;
    for (int i = 1; i < kFftSize / 2; ++i) {
        const double magnitude = std::hypot(re[static_cast<std::size_t>(i)], im[static_cast<std::size_t>(i)]);
        magnitudeSum += magnitude;
        weightedSum += magnitude * static_cast<double>(i) * sr / kFftSize;
    }
    if (magnitudeSum > 1e-9)
        features.centroid = static_cast<float>(weightedSum / magnitudeSum);

    const std::size_t pitchStart = std::min(frames, centre + static_cast<std::size_t>(0.004 * sr));
    const std::size_t pitchEnd = std::min(frames, centre + static_cast<std::size_t>(0.045 * sr));
    const std::size_t lateStart = std::min(frames, centre + static_cast<std::size_t>(0.070 * sr));
    const std::size_t lateEnd = std::min(frames, centre + static_cast<std::size_t>(0.140 * sr));
    const float earlyPitch = dominantPitch(lowpassedScratch, sr, pitchStart, pitchEnd);
    const float latePitch = dominantPitch(lowpassedScratch, sr, lateStart, lateEnd);
    features.pitch = earlyPitch;
    if (earlyPitch > 30.0f && latePitch > 30.0f)
        features.pitchDrop = earlyPitch / latePitch;

    // Claps have several separated noise bursts in the first 100 ms.
    const std::size_t burstEnd = std::min(frames, centre + static_cast<std::size_t>(0.100 * sr));
    float burstPeak = 0.0f;
    std::vector<float> burstEnvelope {};
    for (std::size_t i = attackEnd; i + hop < burstEnd; i += hop) {
        const float value = rmsOf(mono, i, i + hop);
        burstEnvelope.push_back(value);
        burstPeak = std::max(burstPeak, value);
    }
    int bursts = 0;
    for (std::size_t i = 1; i + 1 < burstEnvelope.size(); ++i) {
        if (burstEnvelope[i] > burstPeak * 0.35f && burstEnvelope[i] >= burstEnvelope[i - 1]
            && burstEnvelope[i] > burstEnvelope[i + 1])
            ++bursts;
    }
    features.burstiness = bursts >= 3 ? 1.0f : (bursts == 2 ? 0.5f : 0.0f);

    float leftEnergy = 0.0f;
    float rightEnergy = 0.0f;
    float differenceEnergy = 0.0f;
    for (std::size_t i = attackBegin; i < attackEnd; ++i) {
        leftEnergy += capture.left[i] * capture.left[i];
        rightEnergy += capture.right[i] * capture.right[i];
        const float difference = capture.left[i] - capture.right[i];
        differenceEnergy += difference * difference;
    }
    const float sumEnergy = std::max(1e-9f, leftEnergy + rightEnergy);
    features.pan = std::clamp((rightEnergy - leftEnergy) / sumEnergy, -1.0f, 1.0f);
    features.width = std::clamp(differenceEnergy / sumEnergy, 0.0f, 1.0f);

    const double tailTotal = std::max(1e-12, tailBands[0] + tailBands[1] + tailBands[2]);
    const double longTotal = std::max(1e-12, longBands[0] + longBands[1] + longBands[2]);
    (void)tailTotal;
    (void)longTotal;
    return features;
}

[[nodiscard]] std::array<float, kClassCount> scoreClasses(const Features& f)
{
    std::array<float, kClassCount> scores {};
    const auto set = [&](DrumClass cls, float value) {
        scores[static_cast<std::size_t>(cls)] = std::max(0.0f, value);
    };

    set(DrumClass::Kick,
        2.2f * smoothstep(0.35f, 0.72f, f.lowRatio)
        + 1.0f * smoothstep(1.25f, 2.4f, f.pitchDrop)
        + 0.6f * smoothstep(0.60f, 0.85f, f.lowRatio)
        + 0.5f * (1.0f - smoothstep(0.35f, 0.75f, f.noiseRatio))
        - 0.7f * smoothstep(1800.0f, 6000.0f, f.centroid));

    set(DrumClass::Snare,
        1.5f * smoothstep(0.25f, 0.60f, f.noiseRatio)
        + 1.3f * smoothstep(0.10f, 0.35f, f.midRatio)
        + 1.2f * band(120.0f, 205.0f, 320.0f, f.pitch)
        + 0.6f * (1.0f - smoothstep(0.35f, 0.65f, f.lowRatio))
        - 1.8f * smoothstep(0.45f, 0.75f, f.lowRatio)
        - 0.4f * smoothstep(0.6f, 1.1f, f.tailSeconds));

    set(DrumClass::Clap,
        1.2f * smoothstep(0.45f, 0.75f, f.noiseRatio)
        + 1.0f * f.burstiness
        + 0.4f * band(0.04f, 0.11f, 0.22f, f.tailSeconds)
        - 0.8f * band(120.0f, 205.0f, 320.0f, f.pitch)
        - 1.2f * smoothstep(0.45f, 0.75f, f.highRatio));

    set(DrumClass::Rim,
        1.2f * smoothstep(0.45f, 0.80f, f.highRatio)
        + 0.8f * (1.0f - smoothstep(0.04f, 0.10f, f.tailSeconds))
        + 0.4f * f.burstiness
        - 0.7f * smoothstep(0.25f, 0.6f, f.lowRatio));

    const float hatFlux = smoothstep(0.9f, 1.6f, f.highFlux);
    const float hatMask = 1.0f - 0.9f * hatFlux;
    const float snareEvidence = smoothstep(0.15f, 0.30f, f.midRatio)
                              * band(120.0f, 205.0f, 320.0f, f.pitch)
                              * (1.0f - smoothstep(0.35f, 0.65f, f.lowRatio));
    const float midPenalty = 2.5f * smoothstep(0.12f, 0.25f, f.midRatio) * hatMask
                           + 4.0f * snareEvidence;
    const float lowPenalty = 0.5f * smoothstep(0.25f, 0.60f, f.lowRatio) * hatMask;
    set(DrumClass::ClosedHat,
        3.0f * smoothstep(0.30f, 0.65f, f.highRatio)
        + 1.0f * smoothstep(3200.0f, 7000.0f, f.centroid)
        + 1.0f * (1.0f - smoothstep(0.08f, 0.20f, f.tailSeconds))
        + 2.5f * hatFlux
        - lowPenalty - midPenalty);

    set(DrumClass::OpenHat,
        2.0f * smoothstep(0.30f, 0.65f, f.highRatio)
        + 1.0f * smoothstep(3200.0f, 7000.0f, f.centroid)
        + 1.0f * band(0.22f, 0.40f, 0.80f, f.tailSeconds)
        + 1.8f * hatFlux
        - lowPenalty - midPenalty);

    // Cymbals are distinguished from hats by a long decay, not just brightness.
    const float cymbal = smoothstep(0.45f, 0.95f, f.tailSeconds)
                       * (1.5f * smoothstep(0.35f, 0.70f, f.highRatio)
                          + 0.9f * smoothstep(3800.0f, 7500.0f, f.centroid)
                          + 0.8f * f.width
                          + 0.6f * smoothstep(0.45f, 1.2f, f.tailSeconds));
    set(DrumClass::Crash, cymbal * (0.7f + 0.6f * smoothstep(1.0f, 1.8f, f.tailSeconds)));
    set(DrumClass::Ride, cymbal * (0.7f + 0.4f * band(0.7f, 1.2f, 2.0f, f.tailSeconds)));

    const float tom = 1.4f * smoothstep(1.15f, 2.2f, f.pitchDrop)
                    + 1.3f * smoothstep(0.35f, 0.70f, f.lowRatio)
                    + 0.5f * band(80.0f, 150.0f, 260.0f, f.pitch)
                    - 1.4f * smoothstep(0.55f, 0.80f, f.lowRatio)
                    - 0.8f * smoothstep(0.55f, 0.85f, f.noiseRatio);
    set(DrumClass::LowTom, tom + 0.6f * band(90.0f, 160.0f, 230.0f, f.pitch));
    set(DrumClass::MidTom, tom + 0.5f * band(90.0f, 150.0f, 210.0f, f.pitch));
    set(DrumClass::HighTom, tom + 0.6f * smoothstep(160.0f, 240.0f, f.pitch));

    set(DrumClass::Percussion, 0.55f);
    set(DrumClass::Unknown, 0.1f);
    return scores;
}

[[nodiscard]] float median(const std::vector<float>& values)
{
    if (values.empty())
        return 0.0f;
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}

}  // namespace

std::vector<DrumEvent> classifyEvents(const CaptureBuffer& capture,
                                      const std::vector<DetectedOnset>& onsets,
                                      const GrooveMap&)
{
    std::vector<DrumEvent> events {};
    if (onsets.empty())
        return events;
    events.reserve(onsets.size());
    std::vector<Features> features {};
    features.reserve(onsets.size());
    std::vector<float> scratch {};

    for (std::size_t i = 0; i < onsets.size(); ++i) {
        Features f = extractFeatures(capture, onsets[i].timeSec, scratch);
        f.lowFlux = onsets[i].low;
        f.midFlux = onsets[i].mid;
        f.highFlux = onsets[i].high;
        features.push_back(f);
    }

    // A hit layered on top of another hit still shows up as an above-average
    // band flux, even when its own spectral energy is masked by the louder hit.
    std::vector<float> lowFlux {};
    std::vector<float> midFlux {};
    std::vector<float> highFlux {};
    for (const Features& f : features) {
        lowFlux.push_back(f.lowFlux);
        midFlux.push_back(f.midFlux);
        highFlux.push_back(f.highFlux);
    }
    const float lowMedian = median(lowFlux);
    const float midMedian = median(midFlux);
    const float highMedian = median(highFlux);

    for (std::size_t i = 0; i < features.size(); ++i) {
        Features& f = features[i];
        f.lowFlux = std::clamp(f.lowFlux / std::max(1e-8f, lowMedian), 0.0f, 3.0f);
        f.midFlux = std::clamp(f.midFlux / std::max(1e-8f, midMedian), 0.0f, 3.0f);
        f.highFlux = std::clamp(f.highFlux / std::max(1e-8f, highMedian), 0.0f, 3.0f);
#ifdef ENERGISE_DEBUG_FEATURES
        std::fprintf(stderr, "t=%7.4f low=%.3f mid=%.3f high=%.3f cent=%.0f pitch=%.0f drop=%.2f tail=%.3f noise=%.3f burst=%.1f pan=%.2f width=%.3f flux=%.2f/%.2f/%.2f\n",
                     onsets[i].timeSec, f.lowRatio, f.midRatio, f.highRatio, f.centroid, f.pitch,
                     f.pitchDrop, f.tailSeconds, f.noiseRatio, f.burstiness, f.pan, f.width,
                     f.lowFlux, f.midFlux, f.highFlux);
#endif

        DrumEvent event {};
        event.id = static_cast<std::uint32_t>(i + 1);
        event.sourceTime = onsets[i].timeSec;
        event.quantisedTime = onsets[i].timeSec;
        event.sourceDuration = f.tailSeconds;
        event.velocity = std::clamp(f.attackRms * 2.8f, 0.02f, 1.0f);
        event.pan = f.pan;
        event.scores = scoreClasses(f);
        int best = static_cast<int>(DrumClass::Unknown);
        int second = static_cast<int>(DrumClass::Unknown);
        float bestScore = -1.0f;
        float secondScore = -1.0f;
        for (std::size_t cls = 0; cls < kClassCount; ++cls) {
            const float value = event.scores[cls];
            if (value > bestScore) {
                secondScore = bestScore;
                second = best;
                bestScore = value;
                best = static_cast<int>(cls);
            } else if (value > secondScore) {
                secondScore = value;
                second = static_cast<int>(cls);
            }
        }
        event.primary = classFromIndex(best);
        (void)second;
        const float margin = bestScore > 0.0f ? (bestScore - std::max(0.0f, secondScore)) / bestScore : 0.0f;
        event.confidence = std::clamp(0.55f + 0.45f * margin, 0.0f, 1.0f);
        events.push_back(event);
    }

    std::array<std::vector<float>, kClassCount> velocities {};
    for (const DrumEvent& event : events)
        velocities[static_cast<std::size_t>(event.primary)].push_back(event.velocity);
    std::array<float, kClassCount> medians {};
    for (std::size_t cls = 0; cls < kClassCount; ++cls)
        medians[cls] = median(velocities[cls]);

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto cls = static_cast<std::size_t>(events[i].primary);
        events[i].ghost = medians[cls] > 0.0f && events[i].velocity < 0.45f * medians[cls] && events[i].velocity < 0.55f;
    }
    return events;
}

bool analyseCapture(const CaptureBuffer& capture, const int gridDivisions,
                    const float driftAllowance, GrooveMap& groove, std::vector<DrumEvent>& events)
{
    if (capture.sampleRate <= 0 || capture.left.empty() || capture.left.size() != capture.right.size())
        return false;
    const auto onsets = detectOnsets(capture);
    groove = fitTempoGrid(capture, onsets, gridDivisions, driftAllowance);
    events = classifyEvents(capture, onsets, groove);
    return true;
}

}  // namespace downspout::energise
