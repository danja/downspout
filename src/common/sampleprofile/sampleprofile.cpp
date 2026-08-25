#include "downspout/sampleprofile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// ── FFT — Cooley-Tukey radix-2 DIT, in-place ────────────────────────────────

using cx = std::complex<double>;

static void fft_inplace(cx* x, int n)
{
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        cx wstep(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cx w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                cx u = x[i + j];
                cx v = x[i + j + len/2] * w;
                x[i + j]         = u + v;
                x[i + j + len/2] = u - v;
                w *= wstep;
            }
        }
    }
}

/* Hann-windowed real→|magnitude| spectrum.
 * Input signal at signal[sig_offset .. sig_offset+n-1], zero-padded outside.
 * mag must hold at least n/2+1 doubles. */
static void hann_rfft_mag(const double* signal, int sig_len,
                           int sig_offset, int n, double* mag)
{
    std::vector<cx> buf(n, cx(0.0, 0.0));
    for (int i = 0; i < n; ++i) {
        int si = sig_offset + i;
        double s = (si >= 0 && si < sig_len) ? signal[si] : 0.0;
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / n));
        buf[i] = {s * w, 0.0};
    }
    fft_inplace(buf.data(), n);
    for (int k = 0; k <= n / 2; ++k)
        mag[k] = std::abs(buf[k]);
}

/* Same but the window length differs from the FFT size (for dominant-partial
 * analysis where we zero-pad signal of length win_len to fft_size). */
static void hann_rfft_mag_padded(const double* signal, int sig_len,
                                  int sig_offset, int win_len,
                                  int fft_size, double* mag)
{
    std::vector<cx> buf(fft_size, cx(0.0, 0.0));
    for (int i = 0; i < win_len; ++i) {
        int si = sig_offset + i;
        double s = (si >= 0 && si < sig_len) ? signal[si] : 0.0;
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / win_len));
        buf[i] = {s * w, 0.0};
    }
    fft_inplace(buf.data(), fft_size);
    for (int k = 0; k <= fft_size / 2; ++k)
        mag[k] = std::abs(buf[k]);
}

// ── Butterworth 2nd-order high-pass filter ───────────────────────────────────

struct Biquad {
    double b0, b1, b2, a1, a2;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double process(double x) noexcept {
        double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

static Biquad make_bw_hpf(double fc, double fs)
{
    double k    = std::tan(M_PI * fc / fs);
    double k2   = k * k;
    double sq2  = std::sqrt(2.0);
    double norm = 1.0 + sq2 * k + k2;
    Biquad f;
    f.b0 =  1.0 / norm;
    f.b1 = -2.0 / norm;
    f.b2 =  1.0 / norm;
    f.a1 =  2.0 * (k2 - 1.0) / norm;
    f.a2 = (1.0 - sq2 * k + k2) / norm;
    return f;
}

// ── Pearson correlation ───────────────────────────────────────────────────────

static double pearson(const double* a, const double* b, int n)
{
    if (n < 2) return 1.0;
    double sa = 0, sb = 0;
    for (int i = 0; i < n; ++i) { sa += a[i]; sb += b[i]; }
    double ma = sa / n, mb = sb / n;
    double num = 0, da2 = 0, db2 = 0;
    for (int i = 0; i < n; ++i) {
        double xa = a[i] - ma, xb = b[i] - mb;
        num += xa * xb;
        da2 += xa * xa;
        db2 += xb * xb;
    }
    double denom = std::sqrt(da2 * db2);
    return (denom < 1e-30) ? 1.0 : num / denom;
}

// ── Linear regression y = a + b*x ────────────────────────────────────────────

struct LinFit { double slope, intercept, r2; };

static LinFit linear_fit(const double* x, const double* y, int n)
{
    if (n < 2) return {0.0, (n == 1 ? y[0] : 0.0), 0.0};
    double sx = 0, sy = 0, sxy = 0, sxx = 0;
    for (int i = 0; i < n; ++i) {
        sx  += x[i]; sy  += y[i];
        sxy += x[i] * y[i]; sxx += x[i] * x[i];
    }
    double dn = (double)n;
    double denom = dn * sxx - sx * sx;
    if (std::abs(denom) < 1e-30) return {0.0, sy / dn, 0.0};
    double slope = (dn * sxy - sx * sy) / denom;
    double intercept = (sy - slope * sx) / dn;
    double ymean = sy / dn, ss_tot = 0, ss_res = 0;
    for (int i = 0; i < n; ++i) {
        double d = y[i] - ymean; ss_tot += d * d;
        double r = y[i] - (intercept + slope * x[i]); ss_res += r * r;
    }
    double r2 = (ss_tot < 1e-30) ? 1.0 : 1.0 - ss_res / ss_tot;
    return {slope, intercept, std::max(0.0, std::min(1.0, r2))};
}

// ── Spectral helpers ─────────────────────────────────────────────────────────

/* Spectral flatness over bins where f_k >= 50 Hz (§4.4). */
static double spec_flatness(const double* mag, int n2p1, double bin_hz)
{
    constexpr double eps = 1e-20;
    double log_sum = 0, lin_sum = 0;
    int cnt = 0;
    for (int k = 1; k < n2p1; ++k) {
        if (k * bin_hz < 50.0) continue;
        double pk = mag[k] * mag[k];
        log_sum += std::log(pk + eps);
        lin_sum += pk;
        ++cnt;
    }
    if (cnt == 0 || lin_sum < 1e-30) return 0.0;
    return std::exp(log_sum / cnt) / (lin_sum / cnt);
}

/* Spectral centroid over bins 1..n2-1 (excludes DC and Nyquist). */
static double spec_centroid(const double* mag, int n2, double bin_hz, double A)
{
    double c = 0;
    for (int k = 1; k < n2; ++k) c += k * bin_hz * mag[k];
    return c / A;
}

/* Lowest f_k where cumsum of mag[1..] reaches threshold*A. */
static double spec_rolloff(const double* mag, int n2p1, double bin_hz,
                            double threshold, double A)
{
    double cum = 0, target = threshold * A;
    for (int k = 1; k < n2p1; ++k) {
        cum += mag[k];
        if (cum >= target) return k * bin_hz;
    }
    return (n2p1 - 1) * bin_hz;
}

static constexpr double DB_FLOOR = -120.0;

static double safe_db(double linear)
{
    return (linear > 0.0) ? 20.0 * std::log10(linear) : DB_FLOOR;
}

// ── Amplitude envelope (§2.5) ─────────────────────────────────────────────────
// 5 ms rectangular window, 1 ms hop (both rounded to nearest sample),
// centred on hop position, zero-padded at boundaries.

struct Envelope {
    std::vector<double> rms;   /* linear RMS, one entry per hop */
    double hop_s;              /* hop duration in seconds */
};

static Envelope compute_envelope(const double* sig, int n, double sr)
{
    int win = std::max(1, (int)std::lround(0.005 * sr));
    int hop = std::max(1, (int)std::lround(0.001 * sr));
    int half = win / 2;

    Envelope env;
    env.hop_s = hop / sr;

    for (int j = 0; ; ++j) {
        int centre = j * hop;
        if (centre > n && j > 0) break;
        int lo = centre - half;
        int hi = lo + win;
        double acc = 0.0;
        for (int k = lo; k < hi; ++k) {
            double s = (k >= 0 && k < n) ? sig[k] : 0.0;
            acc += s * s;
        }
        env.rms.push_back(std::sqrt(acc / win));
    }
    return env;
}

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
// sp_analyse
// ════════════════════════════════════════════════════════════════════════════

sp_profile_t sp_analyse(
    const float* const* channels,
    int                 channel_count,
    size_t              frame_count,
    double              sample_rate,
    const sp_params_t*  params)
{
    sp_profile_t prof = {};
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < SP_VECTOR_SIZE; ++i) { prof.vector[i] = NaN; prof.valid[i] = false; }

    prof.sample_rate   = sample_rate;
    prof.channel_count = channel_count;
    prof.frame_count   = frame_count;
    prof.duration_seconds = (channel_count > 0 && sample_rate > 0)
        ? (double)frame_count / sample_rate : 0.0;

    const int FFT  = (params && params->fft_size > 0) ? params->fft_size : 1024;
    const int HOP  = (params && params->hop_size > 0) ? params->hop_size : 256;

    if (channel_count < 1 || frame_count < 1 || sample_rate <= 0.0) {
        prof.silent = true;
        return prof;
    }

    const int N = (int)frame_count;
    const int C = channel_count;

    // ── §4.10 Clipping — on raw input before DC removal ──────────────────────
    {
        constexpr double CLIP_THRESH = 1.0 - 1.0 / 32768.0;
        for (int c = 0; c < C && !prof.clipped; ++c) {
            int run = 0;
            for (int i = 0; i < N && !prof.clipped; ++i) {
                if (std::abs((double)channels[c][i]) >= CLIP_THRESH) {
                    if (++run >= 8) prof.clipped = true;
                } else { run = 0; }
            }
        }
    }

    // ── §2.2 DC removal (per channel) ────────────────────────────────────────
    std::vector<std::vector<double>> ch(C, std::vector<double>(N));
    for (int c = 0; c < C; ++c) {
        double mean = 0.0;
        for (int i = 0; i < N; ++i) mean += channels[c][i];
        mean /= N;
        for (int i = 0; i < N; ++i) ch[c][i] = channels[c][i] - mean;
    }

    // ── §2.3 Mono downmix ────────────────────────────────────────────────────
    std::vector<double> mono(N, 0.0);
    for (int c = 0; c < C; ++c)
        for (int i = 0; i < N; ++i) mono[i] += ch[c][i];
    if (C > 1)
        for (int i = 0; i < N; ++i) mono[i] /= C;

    // Phase warning on full signal (used for downmix channel selection)
    if (C >= 2) {
        double full_icc = pearson(ch[0].data(), ch[1].data(), N);
        if (full_icc < -0.5) {
            prof.phase_warning = true;
            double rms0 = 0, rms1 = 0;
            for (int i = 0; i < N; ++i) { rms0 += ch[0][i]*ch[0][i]; rms1 += ch[1][i]*ch[1][i]; }
            const auto& louder = (rms1 > rms0) ? ch[1] : ch[0];
            for (int i = 0; i < N; ++i) mono[i] = louder[i];
        }
    }

    // ── §2.4 Peak normalisation ───────────────────────────────────────────────
    double P = 0.0;
    for (int i = 0; i < N; ++i) P = std::max(P, std::abs(mono[i]));

    prof.peak_dbfs = safe_db(P);
    if (P > 1.0) prof.clipped = true;
    if (P <= 0.0) { prof.silent = true; return prof; }

    std::vector<double> norm(N);
    for (int i = 0; i < N; ++i) norm[i] = mono[i] / P;

    // ── §2.5 Amplitude envelope ───────────────────────────────────────────────
    Envelope env = compute_envelope(norm.data(), N, sample_rate);
    const int NE = (int)env.rms.size();

    // ── §2.6 Time anchors ─────────────────────────────────────────────────────
    constexpr double ONSET_LIN  = 1e-3;   // -60 dB
    constexpr double OFFSET_LIN = 1e-2;   // -40 dB

    int j_t0 = -1, j_peak = 0, j_tend = 0;
    for (int j = 0; j < NE; ++j) {
        if (env.rms[j] > ONSET_LIN) { j_t0 = j; break; }
    }
    if (j_t0 < 0) { prof.silent = true; return prof; }

    j_peak = j_t0;
    for (int j = j_t0; j < NE; ++j)
        if (env.rms[j] >= env.rms[j_peak]) j_peak = j;

    j_tend = j_peak;
    for (int j = j_peak; j < NE; ++j)
        if (env.rms[j] > OFFSET_LIN) j_tend = j;

    double t0   = j_t0   * env.hop_s;
    double t_pk = j_peak * env.hop_s;
    double t_end = j_tend * env.hop_s;

    // Sample-domain bounds (integer, clamped)
    auto to_samp = [&](double t) {
        return std::min((int)(t * sample_rate), N - 1);
    };
    int s_t0   = to_samp(t0);
    int s_tend = to_samp(t_end);
    int s_peak = to_samp(t_pk);

    prof.effective_duration_seconds = t_end - t0;
    prof.attack_time_seconds        = t_pk  - t0;
    prof.log_attack_time            = std::log10(std::max(prof.attack_time_seconds, 1e-4));

    // ── §4.2 rmsDbfs ─────────────────────────────────────────────────────────
    {
        double sum2 = 0.0;
        int cnt = s_tend - s_t0 + 1;
        for (int i = s_t0; i <= s_tend; ++i) sum2 += mono[i] * mono[i];
        prof.rms_dbfs = (cnt > 0) ? safe_db(std::sqrt(sum2 / cnt)) : DB_FLOOR;
    }

    // ── §4.1 Temporal centroid ────────────────────────────────────────────────
    {
        double num = 0, den = 0;
        for (int j = j_t0; j <= j_tend; ++j) {
            num += j * env.hop_s * env.rms[j];
            den += env.rms[j];
        }
        prof.temporal_centroid_normalised =
            (den > 1e-30 && prof.effective_duration_seconds > 1e-10)
            ? (num / den - t0) / prof.effective_duration_seconds
            : 0.5;
    }

    // ── §4.1 Crest factor ─────────────────────────────────────────────────────
    {
        double pk = 0, sum2 = 0;
        for (int i = s_t0; i <= s_tend; ++i) {
            double x = std::abs(norm[i]);
            if (x > pk) pk = x;
            sum2 += norm[i] * norm[i];
        }
        int cnt = s_tend - s_t0 + 1;
        if (cnt > 0 && sum2 > 1e-30)
            prof.crest_factor_db = safe_db(pk / std::sqrt(sum2 / cnt));
    }

    // ── §4.1 Decay slope and R² ───────────────────────────────────────────────
    bool decay_valid = false;
    {
        std::vector<double> tx, ty;
        for (int j = j_peak; j <= j_tend; ++j) {
            if (env.rms[j] > 0.0) {
                tx.push_back(j * env.hop_s);
                ty.push_back(20.0 * std::log10(env.rms[j]));
            }
        }
        if ((int)tx.size() >= 3) {  // §6: fewer than 2 frames *after* tPeak
            auto fit = linear_fit(tx.data(), ty.data(), (int)tx.size());
            prof.decay_slope_db_per_second = fit.slope;
            prof.decay_fit_r2              = fit.r2;
            decay_valid = true;
        }
    }

    // ── §4.10 Multiple onsets ─────────────────────────────────────────────────
    {
        double peak_e   = env.rms[j_peak];
        double local_min = peak_e;
        double drop12   = peak_e * std::pow(10.0, -12.0 / 20.0);
        double rise12   = std::pow(10.0,  12.0 / 20.0);
        for (int j = j_peak + 1; j <= j_tend; ++j) {
            double e = env.rms[j];
            if (e < local_min) local_min = e;
            if (local_min < drop12 && e > local_min * rise12) {
                prof.multiple_onsets = true;
                break;
            }
        }
    }

    // ── §4.10 Sustained ───────────────────────────────────────────────────────
    prof.sustained = decay_valid
        && prof.temporal_centroid_normalised > 0.45
        && prof.decay_fit_r2 < 0.5;

    // ════════════════════════════════════════════════════════════════════════
    // STFT analysis (§4.3)
    // ════════════════════════════════════════════════════════════════════════

    const int N2     = FFT / 2;
    const double BIN = sample_rate / FFT;

    // Collect candidate frames: centre in [t0, t_end]
    struct Frame {
        std::vector<double> mag;    // [0..N2]
        std::vector<double> mag_l2; // L2-normalised for flux
        std::vector<double> td;     // time-domain (unwindowed) for ZCR
        double total_mag;
        int    centre_samp;
    };

    std::vector<Frame> cands;
    double max_total = 0.0;

    int fi0 = (int)std::ceil((double)(s_t0 - FFT / 2) / HOP);
    if (fi0 < 0) fi0 = 0;

    for (int fi = fi0; ; ++fi) {
        int start  = fi * HOP;
        int centre = start + FFT / 2;
        double tc  = centre / sample_rate;
        if (tc > t_end + 1e-9) break;
        if (tc < t0    - 1e-9) continue;

        Frame fr;
        fr.centre_samp = centre;
        fr.td.resize(FFT);
        for (int i = 0; i < FFT; ++i) {
            int si = start + i;
            fr.td[i] = (si >= 0 && si < N) ? norm[si] : 0.0;
        }

        fr.mag.resize(N2 + 1);
        hann_rfft_mag(norm.data(), N, start, FFT, fr.mag.data());

        // total magnitude (bins 1..N2-1, excl DC and Nyquist)
        double tot = 0.0;
        for (int k = 1; k < N2; ++k) tot += fr.mag[k];
        fr.total_mag = tot;
        if (tot > max_total) max_total = tot;

        // L2-normalise for flux
        double l2 = 0.0;
        for (int k = 1; k < N2; ++k) l2 += fr.mag[k] * fr.mag[k];
        l2 = std::sqrt(l2);
        fr.mag_l2.resize(N2 + 1, 0.0);
        if (l2 > 1e-30)
            for (int k = 1; k < N2; ++k) fr.mag_l2[k] = fr.mag[k] / l2;

        cands.push_back(std::move(fr));
    }

    // Apply -60 dB threshold relative to loudest frame
    double thresh = max_total * 1e-3;
    std::vector<int> inc;
    for (int i = 0; i < (int)cands.size(); ++i)
        if (cands[i].total_mag >= thresh) inc.push_back(i);

    const bool stft_ok = ((int)inc.size() >= 3);

    // ── Per-frame descriptors (§4.4) ──────────────────────────────────────────
    // [0] centroid [1] spread [2] skewness [3] kurtosis
    // [4] rolloff85 [5] rolloff95 [6] flatness [7] flux [8] zcr
    std::vector<std::array<double, 9>> fdescs;
    std::vector<std::array<bool,   9>> fvalid;

    for (int ii = 0; ii < (int)inc.size(); ++ii) {
        const Frame& fr = cands[inc[ii]];
        const double* m = fr.mag.data();

        std::array<double, 9> d = {};
        std::array<bool,   9> v = {};

        double A = 0.0;
        for (int k = 1; k < N2; ++k) A += m[k];

        if (A > 1e-30) {
            double centroid = spec_centroid(m, N2, BIN, A);
            d[0] = centroid; v[0] = true;

            double var = 0.0;
            for (int k = 1; k < N2; ++k) {
                double df = k * BIN - centroid;
                var += df * df * m[k];
            }
            double spread = std::sqrt(var / A);
            d[1] = spread; v[1] = true;

            if (spread >= 1e-9) {
                double s3 = spread * spread * spread;
                double s4 = s3 * spread;
                double sk = 0, ku = 0;
                for (int k = 1; k < N2; ++k) {
                    double df = k * BIN - centroid;
                    double df2 = df * df;
                    sk += df2 * df  * m[k];
                    ku += df2 * df2 * m[k];
                }
                d[2] = sk / (A * s3); v[2] = true;
                d[3] = ku / (A * s4); v[3] = true;
            }

            d[4] = spec_rolloff(m, N2 + 1, BIN, 0.85, A); v[4] = true;
            d[5] = spec_rolloff(m, N2 + 1, BIN, 0.95, A); v[5] = true;
            d[6] = spec_flatness(m, N2 + 1, BIN);         v[6] = true;

            // ZCR in time domain (§4.4)
            int xings = 0;
            double last = 0.0;
            for (double s : fr.td) {
                if (s == 0.0) continue;
                double sgn = (s > 0.0) ? 1.0 : -1.0;
                if (last != 0.0 && sgn != last) ++xings;
                last = sgn;
            }
            d[8] = (double)xings / FFT * sample_rate;
            v[8] = true;
        }

        // Spectral flux (§4.4): only for ii > 0
        if (ii > 0) {
            const Frame& prev = cands[inc[ii - 1]];
            double flux2 = 0.0;
            for (int k = 1; k < N2; ++k) {
                double diff = fr.mag_l2[k] - prev.mag_l2[k];
                if (diff > 0.0) flux2 += diff * diff;
            }
            d[7] = std::sqrt(flux2);
            v[7] = true;
        }

        fdescs.push_back(d);
        fvalid.push_back(v);
    }

    // ── §4.5 Frame summary statistics ─────────────────────────────────────────
    if (stft_ok) {
        for (int di = 0; di < SP_STAT_COUNT; ++di) {
            std::vector<double> vals;
            for (int fi = 0; fi < (int)fdescs.size(); ++fi)
                if (fvalid[fi][di]) vals.push_back(fdescs[fi][di]);
            if (!vals.empty()) {
                double mean = 0.0;
                for (double v : vals) mean += v;
                mean /= (double)vals.size();
                double var = 0.0;
                for (double v : vals) var += (v - mean) * (v - mean);
                prof.frame_stat[di].mean    = mean;
                prof.frame_stat[di].std_dev = std::sqrt(var / (double)vals.size());
                prof.frame_stat[di].defined = true;
            }
        }
    }

    // ── §4.7 Band energies ────────────────────────────────────────────────────
    static const double BAND_EDGES[] = {20,60,120,250,500,1000,2500,6000,20000};
    if (stft_ok) {
        // Mean power spectrum over included frames
        std::vector<double> mpow(N2 + 1, 0.0);
        for (int idx : inc) {
            const auto& m = cands[idx].mag;
            for (int k = 0; k <= N2; ++k) mpow[k] += m[k] * m[k];
        }
        for (int k = 0; k <= N2; ++k) mpow[k] /= inc.size();

        double band_raw[SP_BAND_COUNT] = {};
        double total = 0.0;
        for (int b = 0; b < SP_BAND_COUNT; ++b) {
            double lo = BAND_EDGES[b], hi = BAND_EDGES[b + 1];
            for (int k = 1; k <= N2; ++k) {
                double fk = k * BIN;
                if (fk >= lo && fk < hi) {
                    band_raw[b] += mpow[k];
                    total       += mpow[k];
                }
            }
        }
        if (total > 1e-30)
            for (int b = 0; b < SP_BAND_COUNT; ++b)
                prof.band_energy[b] = band_raw[b] / total;
        // else all 0.0 (already zero-initialised)
    }

    // ── §4.6 Attack spectrum ──────────────────────────────────────────────────
    bool atk_ok = false;
    if (stft_ok) {
        std::vector<double> amag(N2 + 1);
        hann_rfft_mag(norm.data(), N, s_t0, FFT, amag.data());
        double A = 0.0;
        for (int k = 1; k < N2; ++k) A += amag[k];
        if (A > 1e-30) {
            prof.attack_centroid = spec_centroid(amag.data(), N2, BIN, A);
            prof.attack_flatness = spec_flatness(amag.data(), N2 + 1, BIN);
            atk_ok = true;
        }
    }

    // ── §4.6 Tail spectrum ────────────────────────────────────────────────────
    bool tail_ok = false;
    if (stft_ok && prof.effective_duration_seconds > 0.012) {
        double mid_t = t0 + 0.5 * prof.effective_duration_seconds;
        std::vector<double> tsum(N2 + 1, 0.0);
        int tcnt = 0;
        for (int idx : inc) {
            if (cands[idx].centre_samp / sample_rate >= mid_t - 1e-9) {
                for (int k = 0; k <= N2; ++k) tsum[k] += cands[idx].mag[k];
                ++tcnt;
            }
        }
        if (tcnt > 0) {
            for (int k = 0; k <= N2; ++k) tsum[k] /= tcnt;
            double A = 0.0;
            for (int k = 1; k < N2; ++k) A += tsum[k];
            if (A > 1e-30) {
                prof.tail_centroid            = spec_centroid(tsum.data(), N2, BIN, A);
                prof.tail_flatness            = spec_flatness(tsum.data(), N2 + 1, BIN);
                prof.attack_tail_centroid_delta = prof.attack_centroid - prof.tail_centroid;
                tail_ok = true;
            }
        }
    }

    // ── §4.6 Dominant partial ─────────────────────────────────────────────────
    bool dom_ok = false;
    if (stft_ok) {
        constexpr int LONG_FFT = 16384;
        const int     LONG_N2  = LONG_FFT / 2;
        const double  LONG_BIN = sample_rate / LONG_FFT;

        int s_dp  = std::min((int)((t_pk + 0.01) * sample_rate), N - 1);
        int win_l = std::min(4096, std::max(0, s_tend - s_dp));

        if (win_l >= 4) {
            std::vector<double> lmag(LONG_N2 + 1);
            hann_rfft_mag_padded(norm.data(), N, s_dp, win_l, LONG_FFT, lmag.data());

            int k30   = std::max(1, (int)std::ceil(30.0 / LONG_BIN));
            int k_max = k30;
            for (int k = k30 + 1; k < LONG_N2; ++k)
                if (lmag[k] > lmag[k_max]) k_max = k;

            if (k_max > 0 && k_max < LONG_N2 && lmag[k_max] > 0.0) {
                double al = (lmag[k_max-1] > 0) ? std::log(lmag[k_max-1]) : -200.0;
                double be = std::log(lmag[k_max]);
                double ga = (lmag[k_max+1] > 0) ? std::log(lmag[k_max+1]) : -200.0;
                double dn = al - 2.0*be + ga;
                double kr = k_max + (std::abs(dn) > 1e-10 ? 0.5*(al-ga)/dn : 0.0);
                prof.dominant_partial_frequency = kr * LONG_BIN;

                double mmag = 0.0;
                for (int k = 1; k < LONG_N2; ++k) mmag += lmag[k];
                mmag /= (LONG_N2 - 1);
                double ratio = (mmag > 1e-30) ? lmag[k_max] / mmag : 0.0;
                prof.dominant_partial_salience = 1.0 - 1.0 / (1.0 + ratio / 8.0);
                dom_ok = true;
            }
        }
    }

    // ── §4.8 Envelope segments ────────────────────────────────────────────────
    bool seg_ok = (prof.effective_duration_seconds >= 0.010);
    if (seg_ok) {
        Biquad hpf = make_bw_hpf(2000.0, sample_rate);
        std::vector<double> high(N);
        for (int i = 0; i < N; ++i) high[i] = hpf.process(norm[i]);

        int eff = s_tend - s_t0 + 1;
        double seg_rms[SP_ENV_SEG_COUNT]  = {};
        double seg_high[SP_ENV_SEG_COUNT] = {};

        for (int s = 0; s < SP_ENV_SEG_COUNT; ++s) {
            int lo = s_t0 + (int)((long long)s       * eff / SP_ENV_SEG_COUNT);
            int hi = s_t0 + (int)((long long)(s + 1) * eff / SP_ENV_SEG_COUNT);
            if (hi > s_tend + 1) hi = s_tend + 1;
            int cnt = hi - lo;
            if (cnt <= 0) continue;
            double sum2 = 0, hsum2 = 0;
            for (int i = lo; i < hi; ++i) { sum2 += norm[i]*norm[i]; hsum2 += high[i]*high[i]; }
            seg_rms[s]  = std::sqrt(sum2  / cnt);
            seg_high[s] = std::sqrt(hsum2 / cnt);
        }

        double max_r = *std::max_element(seg_rms,  seg_rms  + SP_ENV_SEG_COUNT);
        double max_h = *std::max_element(seg_high, seg_high + SP_ENV_SEG_COUNT);
        for (int s = 0; s < SP_ENV_SEG_COUNT; ++s) {
            prof.env_rms_db[s] = (max_r > 0 && seg_rms[s] > 0)
                ? std::max(-60.0, 20.0 * std::log10(seg_rms[s]  / max_r)) : -60.0;
            prof.env_high_band_rms_db[s] = (max_h > 0 && seg_high[s] > 0)
                ? std::max(-60.0, 20.0 * std::log10(seg_high[s] / max_h)) : -60.0;
        }
    }

    // ── §4.9 Stereo ───────────────────────────────────────────────────────────
    if (C >= 2) {
        int eff = s_tend - s_t0 + 1;
        if (eff > 0) {
            prof.inter_channel_correlation =
                pearson(ch[0].data() + s_t0, ch[1].data() + s_t0, eff);
            double ms2 = 0, ss2 = 0;
            for (int i = s_t0; i <= s_tend; ++i) {
                double mid  = 0.5 * (ch[0][i] + ch[1][i]);
                double side = 0.5 * (ch[0][i] - ch[1][i]);
                ms2 += mid * mid; ss2 += side * side;
            }
            double rms_m = std::sqrt(ms2 / eff), rms_s = std::sqrt(ss2 / eff);
            prof.mid_side_ratio_db = (rms_s > 1e-30)
                ? std::max(-96.0, std::min(96.0, 20.0 * std::log10(rms_m / rms_s)))
                : 96.0;
        }
    } else {
        prof.inter_channel_correlation = 1.0;
        prof.mid_side_ratio_db         = 96.0;
    }

    // ════════════════════════════════════════════════════════════════════════
    // §5 Canonical feature vector
    // ════════════════════════════════════════════════════════════════════════

    auto set_v = [&](int idx, double val, bool defined = true) {
        prof.vector[idx] = defined ? val : NaN;
        prof.valid[idx]  = defined;
    };

    set_v(0, prof.duration_seconds);
    set_v(1, prof.effective_duration_seconds);
    set_v(2, prof.attack_time_seconds);
    set_v(3, prof.log_attack_time);
    set_v(4, prof.temporal_centroid_normalised);
    set_v(5, prof.crest_factor_db);
    set_v(6, prof.decay_slope_db_per_second, decay_valid);
    set_v(7, prof.decay_fit_r2,              decay_valid);
    set_v(8, prof.peak_dbfs);
    set_v(9, prof.rms_dbfs);

    for (int di = 0; di < SP_STAT_COUNT; ++di) {
        bool ok = stft_ok && prof.frame_stat[di].defined;
        set_v(10 + di*2,     prof.frame_stat[di].mean,    ok);
        set_v(10 + di*2 + 1, prof.frame_stat[di].std_dev, ok);
    }

    set_v(28, prof.attack_centroid,           atk_ok);
    set_v(29, prof.tail_centroid,             tail_ok);
    set_v(30, prof.attack_tail_centroid_delta, tail_ok);
    set_v(31, prof.attack_flatness,           atk_ok);
    set_v(32, prof.tail_flatness,             tail_ok);
    set_v(33, prof.dominant_partial_frequency, dom_ok);
    set_v(34, prof.dominant_partial_salience,  dom_ok);

    for (int b = 0; b < SP_BAND_COUNT; ++b)
        set_v(35 + b, prof.band_energy[b], stft_ok);

    for (int s = 0; s < SP_ENV_SEG_COUNT; ++s) {
        set_v(43 + s, prof.env_rms_db[s],          seg_ok);
        set_v(53 + s, prof.env_high_band_rms_db[s], seg_ok);
    }

    set_v(63, prof.inter_channel_correlation);
    set_v(64, prof.mid_side_ratio_db);

    return prof;
}
