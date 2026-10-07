#include "pratt_wavetable.hpp"

#include "pratt_poly.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::pratt {

double pitchToHz(int pitch) { return 440.0 * std::pow(2.0, (pitch - 69) / 12.0); }

Wavetable buildWavetable(const TableParams& p) {
    Wavetable out;
    const double f0 = pitchToHz(p.pitch);
    out.harmonics = std::max(1, std::min(p.maxHarmonics, static_cast<int>(p.sampleRate * 0.42 / f0)));
    out.index = (p.pitch + 1) * p.base;
    const double xi = p.xi * (1.20 - 0.30 * p.velocity);

    // c_k = a_k * R(k); partial k contributes Re(-i c_k e^{i theta}) = y cos + x sin.
    std::vector<double> cx(out.harmonics + 1), cy(out.harmonics + 1);
    for (int k = 1; k <= out.harmonics; ++k) {
        double a = std::pow(static_cast<double>(k), -(p.roll + p.extraRoll));
        if (k % 2 == 0) a *= p.evenGain;
        if (p.upperStart > 0 && k > p.upperStart) a *= p.upperGain;
        const Complex c = a * response(out.index, xi * k);
        cx[k] = c.real();
        cy[k] = c.imag();
    }

    const int N = p.tableSize;
    out.samples.assign(static_cast<std::size_t>(N) + 1, 0.0f);
    std::vector<double> acc(N, 0.0);
    for (int k = 1; k <= out.harmonics; ++k) {
        // Rotate a unit phasor instead of calling sin/cos per sample.
        const double step = 2.0 * M_PI * k / N;
        const double cs = std::cos(step), sn = std::sin(step);
        double c = 1.0, s = 0.0;
        for (int i = 0; i < N; ++i) {
            acc[i] += cy[k] * c + cx[k] * s;
            const double nc = c * cs - s * sn;
            s = s * cs + c * sn;
            c = nc;
        }
    }
    double peak = 1e-20;
    for (double v : acc) peak = std::max(peak, std::abs(v));
    for (int i = 0; i < N; ++i) out.samples[i] = static_cast<float>(acc[i] / peak);
    out.samples[N] = out.samples[0];
    return out;
}

float readWavetable(const std::vector<float>& samples, double phase) {
    const int N = static_cast<int>(samples.size()) - 1;
    double frac = phase - std::floor(phase);
    const double pos = frac * N;
    int i = static_cast<int>(pos);
    if (i >= N) i = N - 1;
    const float t = static_cast<float>(pos - i);
    return samples[i] + t * (samples[i + 1] - samples[i]);
}

}  // namespace downspout::pratt
