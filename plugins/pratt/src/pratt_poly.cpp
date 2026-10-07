#include "pratt_poly.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>

namespace downspout::pratt {

namespace {

using Poly = std::vector<std::int64_t>;

std::mutex gMutex;
std::map<int, Poly> gPolys;
std::map<int, std::vector<Complex>> gRoots;

std::map<int, int> factorize(int n) {
    std::map<int, int> out;
    for (int p = 2; p * p <= n; ++p) {
        while (n % p == 0) {
            ++out[p];
            n /= p;
        }
    }
    if (n > 1) ++out[n];
    return out;
}

Poly multiply(const Poly& a, const Poly& b) {
    Poly c(a.size() + b.size() - 1, 0);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j) c[i + j] += a[i] * b[j];
    return c;
}

void checkIndex(int n) {
    if (n < 1 || n > kMaxIndex) throw std::out_of_range("pratt index out of range");
}

// Caller holds gMutex.
const Poly& polyLocked(int n) {
    auto it = gPolys.find(n);
    if (it != gPolys.end()) return it->second;
    Poly result;
    if (n == 1) {
        result = {1};
    } else if (n == 2) {
        result = {0, 1};
    } else {
        const auto f = factorize(n);
        if (f.size() == 1 && f.begin()->second == 1) {
            // Prime (odd, since 2 was handled above): f_n = 1 + f_{n-1}.
            result = polyLocked(n - 1);
            result[0] += 1;
        } else {
            result = {1};
            for (const auto& [p, k] : f)
                for (int i = 0; i < k; ++i) result = multiply(result, polyLocked(p));
        }
    }
    return gPolys.emplace(n, std::move(result)).first->second;
}

Complex horner(const std::vector<Complex>& c, Complex x) {
    Complex acc = 0.0;
    for (std::size_t i = c.size(); i-- > 0;) acc = acc * x + c[i];
    return acc;
}

// Roots of one prime-index polynomial (squarefree assumed; zero roots are
// stripped exactly first). Durand-Kerner, then Newton polish.
std::vector<Complex> primeRoots(const Poly& poly) {
    std::size_t low = 0;
    while (low < poly.size() && poly[low] == 0) ++low;
    std::vector<Complex> out(low, Complex(0.0, 0.0));
    const std::size_t deg = poly.size() - 1 - low;
    if (deg == 0) return out;

    std::vector<Complex> monic(deg + 1);
    const double lead = static_cast<double>(poly.back());
    for (std::size_t i = 0; i <= deg; ++i) monic[i] = static_cast<double>(poly[low + i]) / lead;
    std::vector<Complex> dmonic(deg);
    for (std::size_t i = 1; i <= deg; ++i) dmonic[i - 1] = monic[i] * static_cast<double>(i);

    double radius = 0.0;
    for (std::size_t i = 0; i < deg; ++i) radius = std::max(radius, std::abs(monic[i]));
    radius = 1.0 + radius;

    std::vector<Complex> z(deg);
    for (std::size_t i = 0; i < deg; ++i)
        z[i] = std::polar(0.5 * radius, 2.0 * M_PI * (static_cast<double>(i) + 0.25) / static_cast<double>(deg));

    for (int iter = 0; iter < 500; ++iter) {
        double change = 0.0;
        for (std::size_t i = 0; i < deg; ++i) {
            Complex denom = 1.0;
            for (std::size_t j = 0; j < deg; ++j)
                if (j != i) denom *= (z[i] - z[j]);
            const Complex delta = horner(monic, z[i]) / denom;
            z[i] -= delta;
            change = std::max(change, std::abs(delta));
        }
        if (change < 1e-15) break;
    }
    for (auto& r : z) {
        for (int k = 0; k < 4; ++k) {
            const Complex d = horner(dmonic, r);
            if (std::abs(d) < 1e-300) break;
            r -= horner(monic, r) / d;
        }
        out.push_back(r);
    }
    return out;
}

}  // namespace

const std::vector<std::int64_t>& polynomial(int n) {
    checkIndex(n);
    std::lock_guard<std::mutex> lock(gMutex);
    return polyLocked(n);  // std::map nodes are stable, so the reference outlives the lock
}

Complex evaluate(int n, Complex x) {
    const auto& c = polynomial(n);
    Complex acc = 0.0;
    for (std::size_t i = c.size(); i-- > 0;) acc = acc * x + static_cast<double>(c[i]);
    return acc;
}

Complex response(int n, double coordinate) {
    return static_cast<double>(n) / evaluate(n, Complex(2.0, coordinate));
}

const std::vector<Complex>& roots(int n) {
    checkIndex(n);
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gRoots.find(n);
        if (it != gRoots.end()) return it->second;
    }
    // Root finding runs outside the lock so a background warm-up never holds the
    // mutex across real work while the audio thread waits on it. A lost race just
    // repeats the (deterministic) computation; emplace keeps the first result.
    std::vector<Complex> all;
    if (n > 1) {
        for (const auto& [p, k] : factorize(n)) {
            const auto r = primeRoots(polynomial(p));
            for (int i = 0; i < k; ++i) all.insert(all.end(), r.begin(), r.end());
        }
    }
    std::lock_guard<std::mutex> lock(gMutex);
    return gRoots.emplace(n, std::move(all)).first->second;
}

std::vector<Complex> poles(int n, double omega0) {
    std::vector<Complex> out;
    for (const Complex& t : roots(n)) out.push_back(omega0 * (t - 2.0));
    return out;
}

std::vector<Biquad> makeSections(int n, double omega0, double sampleRate) {
    const double K = 2.0 * sampleRate;
    std::vector<Complex> p = poles(n, omega0);
    // Poles at the analog origin cannot occur (Re(t_j) < 2 strictly), so every
    // pole is either real-negative or half of a conjugate pair.
    std::vector<bool> used(p.size(), false);
    std::vector<Biquad> out;
    const double realTol = 1e-9 * std::max(1.0, omega0);
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        Biquad s;
        if (std::abs(p[i].imag()) <= realTol) {
            const double q = -p[i].real();  // H = q / (s + q)
            const double a0 = K + q;
            s.b0 = q / a0;
            s.b1 = q / a0;
            s.a1 = (q - K) / a0;
            out.push_back(s);
            continue;
        }
        // Find the conjugate partner.
        std::size_t best = i;
        double bestDist = 1e300;
        for (std::size_t j = i + 1; j < p.size(); ++j) {
            if (used[j]) continue;
            const double d = std::abs(p[j] - std::conj(p[i]));
            if (d < bestDist) {
                bestDist = d;
                best = j;
            }
        }
        if (best == i) throw std::logic_error("unpaired complex pole");
        used[best] = true;
        const double a = -2.0 * p[i].real();  // H = w2 / (s^2 + a s + w2)
        const double w2 = std::norm(p[i]);
        const double a0 = K * K + a * K + w2;
        s.b0 = w2 / a0;
        s.b1 = 2.0 * w2 / a0;
        s.b2 = w2 / a0;
        s.a1 = (2.0 * w2 - 2.0 * K * K) / a0;
        s.a2 = (K * K - a * K + w2) / a0;
        out.push_back(s);
    }
    return out;
}

Complex sectionsResponse(const std::vector<Biquad>& sections, double hz, double sampleRate) {
    const Complex zi = std::polar(1.0, -2.0 * M_PI * hz / sampleRate);
    Complex h = 1.0;
    for (const Biquad& s : sections)
        h *= (s.b0 + s.b1 * zi + s.b2 * zi * zi) / (1.0 + s.a1 * zi + s.a2 * zi * zi);
    return h;
}

}  // namespace downspout::pratt
