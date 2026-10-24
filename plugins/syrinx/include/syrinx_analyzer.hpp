#pragma once

// Input analysis for Syrinx's audio mode: fundamental pitch, loudness, sibilance and
// syllable onsets from a mono voice. Portable C++, no framework dependency.
//
// Pitch is a McLeod normalised square-difference function on a copy of the signal
// decimated to ~6 kHz, so the cost stays at a few million multiply-adds per second.
// The result lags the input by about one analysis window (a few tens of ms), which is the
// price of a stable estimate on low voices.

#include <algorithm>
#include <array>
#include <cmath>

namespace downspout::syrinx {

class InputAnalyzer {
public:
    explicit InputAnalyzer(float sampleRate = 48000.0f) { setSampleRate(sampleRate); }

    void setSampleRate(float sr)
    {
        sampleRate_ = sr > 8000.0f ? sr : 48000.0f;
        decim_ = std::max(1, static_cast<int>(std::lround(sampleRate_ / kDecimRate)));
        decimRate_ = sampleRate_ / static_cast<float>(decim_);
        lpCoeff_ = 1.0f - std::exp(-2.0f * kPi * 1400.0f / sampleRate_);
        hpCoeff_ = std::exp(-2.0f * kPi * 4000.0f / sampleRate_);
        attackCoeff_ = coeffFor(0.003f);
        releaseCoeff_ = coeffFor(0.050f);
        fastCoeff_ = coeffFor(0.004f);
        slowCoeff_ = coeffFor(0.120f);
        peakDecay_ = std::exp(-1.0f / (3.0f * sampleRate_));
        refractory_ = static_cast<int>(0.060f * sampleRate_);
        reset();
    }

    // Centre of the voice. The search covers 0.5x to 2.5x this, within the tracker's limits.
    void setRange(float centreHz) { rangeHz_ = std::clamp(centreHz, 60.0f, 900.0f); }

    void reset()
    {
        lp1_ = lp2_ = 0.0f;
        hpX_ = hpY_ = 0.0f;
        buf_.fill(0.0f);
        head_ = 0;
        decimCount_ = 0;
        hopCount_ = 0;
        level_ = fast_ = slow_ = hfLevel_ = 0.0f;
        peak_ = 0.0f;
        hz_ = 0.0f;
        clarity_ = 0.0f;
        onset_ = false;
        armed_ = true;
        sinceOnset_ = refractory_;
    }

    void push(const float x)
    {
        // Level: attack/release follower on the rectified signal, plus a high-band copy.
        const float a = std::fabs(x);
        level_ += (a > level_ ? attackCoeff_ : releaseCoeff_) * (a - level_);

        const float hp = x - hpX_ + hpCoeff_ * hpY_;
        hpX_ = x;
        hpY_ = hp;
        hfLevel_ += (std::fabs(hp) > hfLevel_ ? attackCoeff_ : releaseCoeff_) * (std::fabs(hp) - hfLevel_);

        peak_ = std::max(level_, peak_ * peakDecay_);

        // Onset: a fast follower jumping above a slow one.
        fast_ += fastCoeff_ * (a - fast_);
        slow_ += slowCoeff_ * (a - slow_);
        ++sinceOnset_;
        // After an onset, wait for the slow follower to catch up (the sound has settled or dipped)
        // before arming again, so a held note is one onset and not one per refractory period.
        if (!armed_ && fast_ < slow_ * 1.25f) armed_ = true;
        if (armed_ && sinceOnset_ >= refractory_ && fast_ > 0.0008f && fast_ > slow_ * 1.9f + 0.0005f) {
            onset_ = true;
            armed_ = false;
            sinceOnset_ = 0;
        }

        // Pitch path: two one-pole lowpasses, then keep every decim_-th sample.
        lp1_ += lpCoeff_ * (x - lp1_);
        lp2_ += lpCoeff_ * (lp1_ - lp2_);
        if (++decimCount_ >= decim_) {
            decimCount_ = 0;
            buf_[head_] = lp2_;
            head_ = (head_ + 1) % kWindow;
            if (++hopCount_ >= kHop) {
                hopCount_ = 0;
                estimatePitch();
            }
        }
    }

    [[nodiscard]] float level() const { return level_; }
    // Loudness relative to the recent peak, 0-1. Makes the gate independent of input gain.
    [[nodiscard]] float relativeLevel() const { return peak_ > 1.0e-4f ? std::min(1.0f, level_ / peak_) : 0.0f; }
    [[nodiscard]] float hz() const { return hz_; }               // 0 when not voiced
    [[nodiscard]] float clarity() const { return clarity_; }     // 0-1 periodicity
    // 0 tonal ... 1 hissy: the share of energy above 4 kHz.
    [[nodiscard]] float sibilance() const { return level_ > 1.0e-5f ? std::clamp(hfLevel_ / level_ * 1.6f, 0.0f, 1.0f) : 0.0f; }
    [[nodiscard]] bool takeOnset() { const bool o = onset_; onset_ = false; return o; }

    static constexpr int kWindow = 384;   // decimated samples (64 ms at 6 kHz)
    static constexpr int kHop = 48;       // decimated samples (8 ms)

private:
    static constexpr float kPi = 3.14159265359f;
    static constexpr float kDecimRate = 6000.0f;

    [[nodiscard]] float coeffFor(const float seconds) const { return 1.0f - std::exp(-1.0f / (seconds * sampleRate_)); }

    void estimatePitch()
    {
        // Unroll the ring so frame[0] is the oldest sample.
        std::array<float, kWindow> frame {};
        for (int i = 0; i < kWindow; ++i)
            frame[static_cast<std::size_t>(i)] = buf_[(head_ + i) % kWindow];

        const int minLag = std::max(3, static_cast<int>(decimRate_ / std::min(rangeHz_ * 2.5f, 1200.0f)));
        const int maxLag = std::min(kWindow / 3, static_cast<int>(decimRate_ / std::max(rangeHz_ * 0.5f, 50.0f)));
        if (maxLag <= minLag + 2) {
            hz_ = 0.0f;
            return;
        }

        double energy = 0.0;
        for (int i = 0; i < kWindow; ++i)
            energy += static_cast<double>(frame[static_cast<std::size_t>(i)]) * frame[static_cast<std::size_t>(i)];
        if (energy < 1.0e-9) {
            hz_ = 0.0f;
            clarity_ = 0.0f;
            return;
        }

        std::array<float, kWindow / 3 + 2> nsdf {};
        for (int tau = 0; tau <= maxLag + 1; ++tau) {
            double acf = 0.0, m = 0.0;
            for (int i = 0; i + tau < kWindow; ++i) {
                const double a = frame[static_cast<std::size_t>(i)];
                const double b = frame[static_cast<std::size_t>(i + tau)];
                acf += a * b;
                m += a * a + b * b;
            }
            nsdf[static_cast<std::size_t>(tau)] = m > 0.0 ? static_cast<float>(2.0 * acf / m) : 0.0f;
        }

        // Key maxima: the highest point of each positive lobe.
        float best = 0.0f;
        std::array<int, 24> peaks {};
        int peakCount = 0;
        int tau = minLag;
        while (tau <= maxLag && nsdf[static_cast<std::size_t>(tau)] > 0.0f && tau > 1)
            ++tau;  // skip the zero-lag lobe
        for (; tau <= maxLag && peakCount < static_cast<int>(peaks.size()); ++tau) {
            if (nsdf[static_cast<std::size_t>(tau)] <= 0.0f)
                continue;
            int top = tau;
            while (tau + 1 <= maxLag + 1 && nsdf[static_cast<std::size_t>(tau + 1)] > 0.0f) {
                ++tau;
                if (nsdf[static_cast<std::size_t>(tau)] > nsdf[static_cast<std::size_t>(top)])
                    top = tau;
            }
            const auto at = [&](const int k) { return nsdf[static_cast<std::size_t>(k)]; };
            if (top > minLag && top <= maxLag && at(top) >= at(top - 1) && at(top) >= at(top + 1)) {
                peaks[static_cast<std::size_t>(peakCount++)] = top;
                best = std::max(best, at(top));
            }
        }

        if (peakCount == 0 || best < 0.55f) {
            hz_ = 0.0f;
            clarity_ = best;
            return;
        }
        // First peak within 90% of the best avoids octave-down errors.
        int pick = peaks[0];
        for (int i = 0; i < peakCount; ++i) {
            if (nsdf[static_cast<std::size_t>(peaks[static_cast<std::size_t>(i)])] >= 0.9f * best) {
                pick = peaks[static_cast<std::size_t>(i)];
                break;
            }
        }
        const float y0 = nsdf[static_cast<std::size_t>(pick - 1)];
        const float y1 = nsdf[static_cast<std::size_t>(pick)];
        const float y2 = nsdf[static_cast<std::size_t>(pick + 1)];
        const float denom = y0 - 2.0f * y1 + y2;
        const float shift = std::fabs(denom) > 1.0e-9f ? std::clamp(0.5f * (y0 - y2) / denom, -0.5f, 0.5f) : 0.0f;
        hz_ = decimRate_ / (static_cast<float>(pick) + shift);
        clarity_ = best;
    }

    float sampleRate_ = 48000.0f;
    int decim_ = 8;
    float decimRate_ = 6000.0f;
    float rangeHz_ = 220.0f;
    float lpCoeff_ = 0.0f, hpCoeff_ = 0.0f;
    float attackCoeff_ = 0.0f, releaseCoeff_ = 0.0f, fastCoeff_ = 0.0f, slowCoeff_ = 0.0f, peakDecay_ = 0.0f;
    int refractory_ = 2880;

    float lp1_ = 0.0f, lp2_ = 0.0f, hpX_ = 0.0f, hpY_ = 0.0f;
    std::array<float, kWindow> buf_ {};
    int head_ = 0, decimCount_ = 0, hopCount_ = 0;
    float level_ = 0.0f, fast_ = 0.0f, slow_ = 0.0f, hfLevel_ = 0.0f, peak_ = 0.0f;
    float hz_ = 0.0f, clarity_ = 0.0f;
    bool onset_ = false, armed_ = true;
    int sinceOnset_ = 0;
};

} // namespace downspout::syrinx
