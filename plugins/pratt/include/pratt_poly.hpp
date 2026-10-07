#pragma once

// Pratt polynomials and the all-pole filters built from them.
//
//   f_1(x) = 1,  f_2(x) = x,  f_p(x) = 1 + f_{p-1}(x)  (p odd prime),
//   f_n = prod_{p | n} f_p^{v_p(n)}                      (so f_mn = f_m f_n),
//   f_n(2) = n.
//
//   H_n(s) = n / f_n(2 + s/w0)  =  prod_j (2 - t_j) / (2 + s/w0 - t_j)
//
// where t_j are the roots of f_n. H_n(0) = 1, H_mn = H_m H_n, and every pole
// s_j = w0 (t_j - 2) lies in the open left half plane.
//
// Portable, framework-free, allocation-heavy: build tables and sections off
// the audio thread. Measured for n <= 8192: degree <= 13, |coefficient| <= 70,
// so int64 coefficients are exact with a very wide margin.

#include <complex>
#include <cstdint>
#include <vector>

namespace downspout::pratt {

using Complex = std::complex<double>;

// Largest index the library accepts. The synth uses n = (midi_pitch + 1) * base
// with pitch <= 127 and base <= 64.
inline constexpr int kMaxIndex = 8192;

// Ascending integer coefficients of f_n (c[0] + c[1] x + ...). n in [1, kMaxIndex].
// Cached; thread-safe.
const std::vector<std::int64_t>& polynomial(int n);

inline int degree(int n) { return static_cast<int>(polynomial(n).size()) - 1; }

// f_n at a complex point (Horner).
Complex evaluate(int n, Complex x);

// H_n at s/w0 = i * coordinate (dimensionless frequency), as in the Python
// reference `response(n, coordinate)`.
Complex response(int n, double coordinate);

// All roots t_j of f_n with multiplicity (size == degree(n)). Cached; thread-safe.
const std::vector<Complex>& roots(int n);

// Analog poles s_j = w0 (t_j - 2), w0 in rad/s.
std::vector<Complex> poles(int n, double omega0);

// Second-order section, direct form, a0 normalised to 1.
// First-order sections have b2 == a2 == 0.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

// Digital realisation of H_n (unity DC gain per section) via the bilinear
// transform with no prewarping: s = 2 fs (1 - z^-1)/(1 + z^-1). Conjugate pole
// pairs become one biquad each. Accurate well below fs/2; the response
// compresses towards Nyquist, so keep omega0 well under it.
std::vector<Biquad> makeSections(int n, double omega0, double sampleRate);

// Magnitude/phase of a section cascade at frequency hz.
Complex sectionsResponse(const std::vector<Biquad>& sections, double hz, double sampleRate);

// Transposed direct form II state for one section.
struct BiquadState {
    double z1 = 0.0, z2 = 0.0;
};

inline float processSection(const Biquad& s, BiquadState& st, float x) {
    const double y = s.b0 * x + st.z1;
    st.z1 = s.b1 * x - s.a1 * y + st.z2;
    st.z2 = s.b2 * x - s.a2 * y;
    return static_cast<float>(y);
}

}  // namespace downspout::pratt
