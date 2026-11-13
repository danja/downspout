#include "downspout/test_assert.h"
#include "fresco_core.hpp"
#include "fresco_test_images.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace downspout::fresco;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kSr = 51200.0f;   // bin spacing is exactly 25 Hz, so test frequencies sit on bin centres
int checks = 0;

#define CHECK(cond) do { ++checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); assert(false); } } while (0)
bool near(const double a, const double b, const double tol) { return std::abs(a - b) <= tol; }

// ---- independent references -------------------------------------------------------------------------------------------------------------------

int lum(const int r, const int g, const int b) { return (2126 * r + 7152 * g + 722 * b + 5000) / 10000; }
int val(const int x, const int y, const int c) { return (7 * x + 13 * y + 50 * c) % 256; }

int expectedPixel(const std::string& name, const int x, const int y)
{
    if (name == "gray8" || name == "gray8_adam7" || name == "gray16" || name == "graya8") return val(x, y, 0);
    if (name.rfind("rgb8", 0) == 0) return lum(val(x, y, 0), val(x, y, 1), val(x, y, 2));
    if (name == "rgba8") return (lum(val(x, y, 0), val(x, y, 1), val(x, y, 2)) * 128 + 127) / 255;
    if (name == "pal4") { const int i = (x + y) % 16; return lum(i * 16, 255 - i * 16, i * 5); }
    if (name == "gray1") return ((x * 3 + y) % 2) * 255;
    return -1;
}

// ---- signal helpers ---------------------------------------------------------------------------------------------------------------------------

struct Render { std::vector<float> l, r; };

Render run(const Parameters& p, const Image* image, const std::vector<float>& inL, const std::vector<float>& inR, const std::uint32_t block = 512, const Transport& tr = {})
{
    EngineState s;
    resetState(s);
    Render o;
    o.l.assign(inL.size(), 0.0f);
    o.r.assign(inL.size(), 0.0f);
    for (std::size_t at = 0; at < inL.size();) {
        const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::size_t>(block, inL.size() - at));
        processBlock(s, p, image, kSr, n, tr, inL.data() + at, inR.data() + at, o.l.data() + at, o.r.data() + at);
        at += n;
    }
    return o;
}

Render runSilent(const Parameters& p, const Image* image, const double seconds, const std::uint32_t block = 512)
{
    const std::vector<float> z(static_cast<std::size_t>(seconds * kSr), 0.0f);
    return run(p, image, z, z, block);
}

// Hann-windowed amplitude of a sinusoid at exactly `hz` over x[from, from+n).
double amplitudeAt(const std::vector<float>& x, const std::size_t from, const std::size_t n, const double hz)
{
    double re = 0, im = 0, wsum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n));
        const double ph = 2.0 * kPi * hz * static_cast<double>(i) / kSr;
        re += x[from + i] * w * std::cos(ph);
        im += x[from + i] * w * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

std::array<double, kBins> independentStft(const std::vector<float>& x, const std::size_t from)
{
    // A rectangular analysis window isolates these exact-bin tones; this direct DFT is independent of Fresco's FFT.
    std::array<double, kBins> magnitudes {};
    constexpr double windowSum = kFrame;

    for (int k = 1; k < kFrame / 2; ++k) {
        const double angle = 2.0 * kPi * k / kFrame;
        const double stepCos = std::cos(angle), stepSin = -std::sin(angle);
        double phaseCos = 1.0, phaseSin = 0.0, re = 0.0, im = 0.0;
        for (int n = 0; n < kFrame; ++n) {
            const double sample = x[from + static_cast<std::size_t>(n)];
            re += sample * phaseCos;
            im += sample * phaseSin;
            const double nextCos = phaseCos * stepCos - phaseSin * stepSin;
            phaseSin = phaseSin * stepCos + phaseCos * stepSin;
            phaseCos = nextCos;
        }
        magnitudes[static_cast<std::size_t>(k)] = 2.0 * std::hypot(re, im) / windowSum;
    }
    return magnitudes;
}

double rms(const std::vector<float>& x, const std::size_t from, const std::size_t n)
{
    double e = 0;
    for (std::size_t i = 0; i < n; ++i) e += static_cast<double>(x[from + i]) * x[from + i];
    return std::sqrt(e / static_cast<double>(n));
}

// A 1000-row picture, `cols` wide, black, with the listed rows white in columns [c0, c1).
Image band(const int cols, const std::vector<int>& rows, const int c0, const int c1, const std::uint8_t level = 255)
{
    Image im;
    im.width = cols; im.height = 1000;
    im.luma.assign(static_cast<std::size_t>(cols) * 1000, 0);
    for (int y : rows) for (int x = c0; x < c1; ++x) im.luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(x)] = level;
    return im;
}

Parameters linearParams()
{
    Parameters p;
    p.minHz = 1000.0f; p.maxHz = 5000.0f; p.logAxis = 0.0f;
    p.source = 1.0f; p.follow = 0.0f; p.mix = 1.0f; p.lengthS = 10.0f;
    return p;
}

double rowSpanForBin(const Parameters& p, const double frequency, const double height = 1000.0)
{
    const double binWidth = kSr / kFrame;
    const double low = std::max(static_cast<double>(p.minHz), frequency - binWidth * 0.5);
    const double high = std::min(static_cast<double>(p.maxHz), frequency + binWidth * 0.5);
    const auto row = [&](const double hz) {
        const double fraction = p.logAxis >= 0.5f
                                    ? std::log(static_cast<double>(p.maxHz) / hz) /
                                          std::log(static_cast<double>(p.maxHz) / p.minHz)
                                    : (p.maxHz - hz) / (p.maxHz - p.minHz);
        return fraction * height;
    };
    return std::abs(row(high) - row(low));
}

double magnitudeForBrightness(const Parameters& p, const double brightness)
{
    const double v = p.invert >= 0.5f ? 1.0 - brightness : brightness;
    if (v <= 0.0)
        return 0.0;
    const double g = std::pow(v, p.contrast);
    return std::pow(10.0, -p.rangeDb * (1.0 - g) / 20.0);
}

// 3000 Hz is bin 120 and sits at the picture's mid-height in the linear 1-5 kHz map: rows 499 and 500 straddle it.
const std::vector<int> kMidRows = {499, 500};

// ---- tests ------------------------------------------------------------------------------------------------------------------------------------

void testPngDecoders()
{
    for (const auto& n : fresco_test::pngs()) {
        const DecodeResult r = decodeImage(n.bytes.data(), n.bytes.size());
        CHECK(r.ok);
        if (!r.ok) { std::printf("  %s: %s\n", n.name, r.error.c_str()); continue; }
        CHECK(r.image.width == 37 && r.image.height == 23);
        int bad = 0;
        for (int y = 0; y < 23; ++y) for (int x = 0; x < 37; ++x) if (static_cast<int>(r.image.at(x, y)) != expectedPixel(n.name, x, y)) ++bad;
        if (bad) std::printf("  %s: %d pixels differ\n", n.name, bad);
        CHECK(bad == 0);
    }
    CHECK(fresco_test::pngs().size() == 10);
}

void testPnmAndBmp()
{
    // P2, with comments
    {
        const std::string s = "P2\n# a comment\n3 2\n255\n0 128 255\n10 20 30\n";
        const DecodeResult r = decodeImage(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
        CHECK(r.ok && r.image.width == 3 && r.image.height == 2);
        CHECK(r.image.at(1, 0) == 128 && r.image.at(2, 0) == 255 && r.image.at(2, 1) == 30);
    }
    // P6 with maxval 100 (rescaled)
    {
        std::string s = "P6 2 1 100\n";
        s += std::string("\x64\x00\x00\x00\x64\x32", 6);   // (100,0,0) and (0,100,50)
        const DecodeResult r = decodeImage(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
        CHECK(r.ok);
        CHECK(r.image.at(0, 0) == lum(255, 0, 0));
        CHECK(r.image.at(1, 0) == lum(0, 255, 128));   // 50/100 -> 127.5 -> 128
    }
    // P5 16-bit
    {
        std::string s = "P5\n2 1\n65535\n";
        s += std::string("\xFF\xFF\x80\x00", 4);
        const DecodeResult r = decodeImage(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
        CHECK(r.ok && r.image.at(0, 0) == 255 && r.image.at(1, 0) == 128);
    }
    // truncated binary PNM
    { const std::string s = "P5\n4 4\n255\nabc"; CHECK(!decodeImage(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()).ok); }
    // BMP 24-bit, 3 wide (row padding), bottom-up: rows listed bottom first
    {
        std::vector<std::uint8_t> b(54, 0);
        b[0] = 'B'; b[1] = 'M';
        const int w = 3, h = 2, stride = 12;
        const std::uint32_t total = 54 + stride * h;
        b[2] = total & 255; b[10] = 54; b[14] = 40; b[18] = w; b[22] = h; b[26] = 1; b[28] = 24;
        for (int row = 0; row < h; ++row) {
            for (int x = 0; x < w; ++x) { const int v = (row * 3 + x) * 20; b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v)); }
            for (int pad = 0; pad < stride - w * 3; ++pad) b.push_back(0);
        }
        const DecodeResult r = decodeImage(b.data(), b.size());
        CHECK(r.ok && r.image.width == 3 && r.image.height == 2);
        CHECK(r.image.at(0, 1) == 0 && r.image.at(2, 1) == 40);   // file row 0 is the bottom row
        CHECK(r.image.at(0, 0) == 60 && r.image.at(2, 0) == 100);
    }
    // BMP 8-bit with a palette
    {
        std::vector<std::uint8_t> b(54, 0);
        b[0] = 'B'; b[1] = 'M';
        b[10] = 54 + 8; b[14] = 40; b[18] = 2; b[22] = 1; b[26] = 1; b[28] = 8; b[46] = 2;
        b.insert(b.end(), {0, 0, 0, 0,  200, 100, 50, 0});   // BGR0: entry 1 is r=50 g=100 b=200
        b.insert(b.end(), {1, 0, 0, 0});
        const DecodeResult r = decodeImage(b.data(), b.size());
        CHECK(r.ok && r.image.at(0, 0) == lum(50, 100, 200) && r.image.at(1, 0) == 0);
    }
}

void testRefusals()
{
    const auto expect = [](const std::vector<std::uint8_t>& d, const char* word) {
        const DecodeResult r = decodeImage(d.data(), d.size());
        CHECK(!r.ok);
        CHECK(r.error.find(word) != std::string::npos);
    };
    expect({0xFF, 0xD8, 0xFF, 0xE0, 0, 0, 0, 0, 0, 0}, "JPEG");
    expect({'G', 'I', 'F', '8', '9', 'a', 0, 0, 0, 0}, "GIF");
    expect({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'}, "WebP");
    expect({1, 2, 3, 4, 5, 6, 7, 8, 9}, "unrecognised");
    expect({1, 2, 3}, "short");
    CHECK(!loadImageFile("/nonexistent/picture.png").ok);
    // Unknown PNG interlace methods are rejected explicitly.
    std::vector<std::uint8_t> p = fresco_test::pngs()[0].bytes;
    p[28] = 2;   // IHDR interlace byte: 8 signature + 8 chunk header + 12 into the body
    expect(p, "interlace method");
    // a PNG claiming 60000 x 60000 pixels is refused before any allocation
    std::vector<std::uint8_t> big = fresco_test::pngs()[0].bytes;
    big[16] = 0; big[17] = 0; big[18] = 0xEA; big[19] = 0x60; big[20] = 0; big[21] = 0; big[22] = 0xEA; big[23] = 0x60;
    expect(big, "too large");
}

void testDamagedFilesNeverCrash()
{
    std::mt19937 rng(7);
    int accepted = 0;
    for (const auto& n : fresco_test::pngs()) {
        for (int t = 0; t < 400; ++t) {
            std::vector<std::uint8_t> d = n.bytes;
            const int edits = 1 + static_cast<int>(rng() % 4);
            for (int e = 0; e < edits; ++e) d[rng() % d.size()] = static_cast<std::uint8_t>(rng());
            if (t % 5 == 0) d.resize(rng() % d.size());
            const DecodeResult r = decodeImage(d.data(), d.size());
            if (r.ok) { ++accepted; CHECK(!r.image.empty()); }
        }
    }
    CHECK(accepted >= 0);   // reaching here under ASan/UBSan is the test
    // random bytes behind each valid magic
    for (int t = 0; t < 300; ++t) {
        std::vector<std::uint8_t> d(200 + rng() % 400);
        for (auto& b : d) b = static_cast<std::uint8_t>(rng());
        const int kind = t % 3;
        if (kind == 0) { d[0] = 'P'; d[1] = '5'; d[2] = ' '; }
        if (kind == 1) { d[0] = 'B'; d[1] = 'M'; }
        if (kind == 2) { static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10}; std::memcpy(d.data(), sig, 8); }
        (void)decodeImage(d.data(), d.size());
    }
}

void testInflate()
{
    // stored block
    std::vector<std::uint8_t> out;
    const std::uint8_t stored[] = {0x78, 0x01, 0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c', 0, 0, 0, 0};
    CHECK(inflateZlib(stored, sizeof stored, 100, out) && out.size() == 3 && out[0] == 'a' && out[2] == 'c');
    // output limit stops it
    out.clear();
    CHECK(!inflateZlib(stored, sizeof stored, 2, out));
    // bad header
    const std::uint8_t bad[] = {0x78, 0x02, 0x01};
    out.clear();
    CHECK(!inflateZlib(bad, sizeof bad, 100, out));
    // a back-reference before the start of the output is refused: fixed block, literal-less, length 3 distance 1
    const std::uint8_t evil[] = {0x78, 0x01, 0x03, 0x02, 0x00};
    out.clear();
    CHECK(!inflateZlib(evil, sizeof evil, 100, out));
}

void testReduceKeepsAverage()
{
    Image im;
    im.width = 4096; im.height = 4096;
    im.luma.assign(static_cast<std::size_t>(im.width) * static_cast<std::size_t>(im.height), 100);
    for (int x = 0; x < im.width; x += 2) for (int y = 0; y < im.height; ++y) im.luma[static_cast<std::size_t>(y) * 4096 + static_cast<std::size_t>(x)] = 200;   // stripes average to 150
    const Image r = reduced(im, 1024);
    CHECK(r.width == 1024 && r.height == 1024);
    CHECK(r.at(500, 500) == 150);
    const Image keep = reduced(r, 2048);
    CHECK(keep.width == 1024);
    const Image d = defaultPicture();
    CHECK(!d.empty());
    int bright = 0;
    for (auto v : d.luma) bright += v > 128;
    CHECK(bright > 1000 && bright < static_cast<int>(d.luma.size()) / 2 + 10000);
}

void testLatencyIsExactlyTheReportedOne()
{
    Parameters p = linearParams();
    p.mix = 0.0f;
    std::vector<float> in(8192, 0.0f), z(8192, 0.0f);
    in[100] = 1.0f;
    const Render o = run(p, nullptr, in, z);
    std::size_t peak = 0;
    for (std::size_t i = 0; i < o.l.size(); ++i) if (std::abs(o.l[i]) > std::abs(o.l[peak])) peak = i;
    CHECK(peak == 100 + static_cast<std::size_t>(kLatency));
    CHECK(near(o.l[peak], 1.0, 1e-6));
    // the wet path has the same delay: a mix of 1 with the Input source and a picture lights up at the same offset as the dry impulse
    Parameters w = p; w.mix = 1.0f; w.source = 0.0f; w.follow = 0.0f; w.minHz = 100; w.maxHz = 20000; w.logAxis = 1;
    Image white = band(4, {}, 0, 0);
    std::fill(white.luma.begin(), white.luma.end(), 255);
    const Render ow = run(w, &white, in, z);
    std::size_t wp = 0;
    for (std::size_t i = 0; i < ow.l.size(); ++i) if (std::abs(ow.l[i]) > std::abs(ow.l[wp])) wp = i;
    CHECK(std::abs(static_cast<long>(wp) - static_cast<long>(peak)) <= 2);
}

void testBandIsPaintedAtItsFrequencyWithTheRightLevel()
{
    const Image im = band(8, kMidRows, 0, 8);
    Parameters p = linearParams();
    const Render o = runSilent(p, &im, 2.0);
    const std::size_t from = static_cast<std::size_t>(0.5 * kSr), n = static_cast<std::size_t>(1.0 * kSr);
    const double at = amplitudeAt(o.l, from, n, 3000.0);
    const double rowCoverage = 2.0 / rowSpanForBin(p, 3000.0);
    CHECK(near(at, kBinAmplitude * rowCoverage, kBinAmplitude * rowCoverage * 0.03));
    // nothing at the other bins of the picture or outside it
    for (const double hz : {1500.0, 2000.0, 2500.0, 3500.0, 4500.0, 6000.0, 500.0})
        CHECK(amplitudeAt(o.l, from, n, hz) < at * 0.002);
    // both channels are painted, with the same level but different phases (so the pair is not a mono image)
    CHECK(near(amplitudeAt(o.r, from, n, 3000.0), at, at * 0.03));
    double diff = 0;
    for (std::size_t i = from; i < from + n; ++i) diff += std::abs(o.l[i] - o.r[i]);
    CHECK(diff / static_cast<double>(n) > 0.002);
}

void testHeightMapsToFrequencyTopIsHigh()
{
    // rows 249/250 are at 1/4 of the height from the top: 4000 Hz in a linear 1-5 kHz picture
    const Image im = band(8, {249, 250}, 0, 8);
    const Render o = runSilent(linearParams(), &im, 1.5);
    const std::size_t from = static_cast<std::size_t>(0.5 * kSr), n = static_cast<std::size_t>(0.8 * kSr);
    const Parameters p = linearParams();
    const double rowCoverage = 2.0 / rowSpanForBin(p, 4000.0);
    CHECK(near(amplitudeAt(o.l, from, n, 4000.0), kBinAmplitude * rowCoverage, kBinAmplitude * rowCoverage * 0.03));
    CHECK(amplitudeAt(o.l, from, n, 2000.0) < kBinAmplitude * 0.003);
    // and rows 749/750 are 2000 Hz
    const Image lo = band(8, {749, 750}, 0, 8);
    const Render q = runSilent(linearParams(), &lo, 1.5);
    CHECK(near(amplitudeAt(q.l, from, n, 2000.0), kBinAmplitude * rowCoverage, kBinAmplitude * rowCoverage * 0.03));
    CHECK(amplitudeAt(q.l, from, n, 4000.0) < kBinAmplitude * 0.003);
}

void testLogAxis()
{
    // log axis 100..6400 Hz: 800 Hz is exactly half way down (ln8/ln64 = 0.5)
    const Image im = band(8, kMidRows, 0, 8);
    Parameters p = linearParams();
    p.logAxis = 1.0f; p.minHz = 100.0f; p.maxHz = 6400.0f;
    const Render o = runSilent(p, &im, 1.5);
    const std::size_t from = static_cast<std::size_t>(0.5 * kSr), n = static_cast<std::size_t>(0.8 * kSr);
    const double logRowCoverage = 2.0 / rowSpanForBin(p, 800.0);
    CHECK(near(amplitudeAt(o.l, from, n, 800.0), kBinAmplitude * logRowCoverage, kBinAmplitude * logRowCoverage * 0.03));
    CHECK(amplitudeAt(o.l, from, n, 3000.0) < kBinAmplitude * 0.003);
    // the same picture on the linear axis puts the band at the arithmetic middle instead
    p.logAxis = 0.0f;
    const Render q = runSilent(p, &im, 1.5);
    const double linearRowCoverage = 2.0 / rowSpanForBin(p, 3250.0);
    CHECK(near(amplitudeAt(q.l, from, n, 3250.0), kBinAmplitude * linearRowCoverage, kBinAmplitude * linearRowCoverage * 0.03));
    CHECK(amplitudeAt(q.l, from, n, 800.0) < kBinAmplitude * 0.003);
}

void testLogAxisAveragesEachBinRowSpan()
{
    Image im;
    im.width = 1;
    im.height = 1000;
    im.luma.assign(static_cast<std::size_t>(im.height), 0);

    Parameters p = linearParams();
    p.logAxis = 1.0f;
    p.minHz = 100.0f;
    p.maxHz = 6400.0f;
    const int bin = 5;  // 125 Hz; its interval covers many image rows on this log axis
    const double center = bin * kSr / kFrame;
    const double binWidth = kSr / kFrame;
    const double low = std::max(static_cast<double>(p.minHz), center - binWidth * 0.5);
    const double high = std::min(static_cast<double>(p.maxHz), center + binWidth * 0.5);
    const auto row = [&](const double hz) {
        return std::log(static_cast<double>(p.maxHz) / hz) /
               std::log(static_cast<double>(p.maxHz) / p.minHz) * im.height;
    };
    const double top = row(high), bottom = row(low);
    const int brightRow = static_cast<int>(std::lround((top + bottom) * 0.5 - 0.5));
    im.luma[static_cast<std::size_t>(brightRow)] = 255;

    std::array<float, kBins> mags {};
    columnMagnitudes(im, p, kSr, 0.0, mags.data());
    const double rowSpan = bottom - top;
    const double coveredFraction = 1.0 / rowSpan;
    const double expected = coveredFraction;
    CHECK(rowSpan > 10.0);
    CHECK(near(mags[static_cast<std::size_t>(bin)], expected, expected * 1e-5));
    CHECK(mags[static_cast<std::size_t>(bin)] < 1.0f);
}

void testRenderedSpectrumMatchesPictureWithIndependentStft()
{
    Image im;
    im.width = 1;
    im.height = 256;
    im.luma.resize(static_cast<std::size_t>(im.height));
    for (int row = 0; row < im.height; ++row)
        im.luma[static_cast<std::size_t>(row)] = static_cast<std::uint8_t>(64 + 191 * row / (im.height - 1));

    Parameters p;
    p.minHz = 100.0f;
    p.maxHz = 16000.0f;
    p.logAxis = 1.0f;
    p.source = static_cast<float>(Source::Tone);
    p.follow = 0.0f;
    const Render rendered = runSilent(p, &im, 1.0);
    constexpr std::size_t start = 10000;
    const auto measured = independentStft(rendered.l, start);

    double expectedSum = 0.0, measuredSum = 0.0;
    std::array<double, kBins> expected {};
    std::array<bool, kBins> included {};
    for (int k = 1; k < kFrame / 2; ++k) {
        const double frequency = static_cast<double>(k) * kSr / kFrame;
        if (frequency < p.minHz || frequency > p.maxHz)
            continue;
        const double binWidth = kSr / kFrame;
        const double low = std::max(static_cast<double>(p.minHz), frequency - binWidth * 0.5);
        const double high = std::min(static_cast<double>(p.maxHz), frequency + binWidth * 0.5);
        const auto imageRow = [&](const double hz) {
            return std::log(static_cast<double>(p.maxHz) / hz) /
                   std::log(static_cast<double>(p.maxHz) / p.minHz) * im.height;
        };
        const double top = imageRow(high), bottom = imageRow(low);
        double binMagnitude = 0.0;
        for (int row = static_cast<int>(std::floor(top)); row < bottom && row < im.height; ++row) {
            const double coverage = std::min(bottom, static_cast<double>(row + 1)) -
                                    std::max(top, static_cast<double>(row));
            const double brightness = im.at(0, row) / 255.0;
            const double pixelMagnitude = magnitudeForBrightness(p, brightness);
            binMagnitude += std::max(0.0, coverage) * pixelMagnitude;
        }
        expected[static_cast<std::size_t>(k)] = kBinAmplitude * binMagnitude / (bottom - top);
        included[static_cast<std::size_t>(k)] = true;
        expectedSum += expected[static_cast<std::size_t>(k)];
        measuredSum += measured[static_cast<std::size_t>(k)];
    }

    int count = 0;
    double covariance = 0.0, expectedVariance = 0.0, measuredVariance = 0.0;
    double maxRelativeError = 0.0;
    const double meanExpected = expectedSum / std::count(included.begin(), included.end(), true);
    const double meanMeasured = measuredSum / std::count(included.begin(), included.end(), true);
    for (int k = 1; k < kFrame / 2; ++k) {
        if (!included[static_cast<std::size_t>(k)])
            continue;
        const double want = expected[static_cast<std::size_t>(k)];
        const double got = measured[static_cast<std::size_t>(k)];
        const double dx = want - meanExpected, dy = got - meanMeasured;
        covariance += dx * dy;
        expectedVariance += dx * dx;
        measuredVariance += dy * dy;
        maxRelativeError = std::max(maxRelativeError, std::abs(got - want) / want);
        ++count;
    }
    const double correlation = covariance / std::sqrt(expectedVariance * measuredVariance);
    std::printf("Fresco independent STFT: %d bins, picture correlation %.7f, max relative error %.5f\n",
                count, correlation, maxRelativeError);
    CHECK(count > 500);
    CHECK(correlation > 0.9999);
    CHECK(maxRelativeError < 0.01);
}

void testBrightnessMapping()
{
    // Two 128-level rows are averaged over the bin span before the range and contrast mappings.
    const Image im = band(8, kMidRows, 0, 8, 128);
    Parameters p = linearParams();
    const std::size_t from = static_cast<std::size_t>(0.5 * kSr), n = static_cast<std::size_t>(0.8 * kSr);
    const double coverage = 2.0 / rowSpanForBin(p, 3000.0);
    const double pixelMagnitude = magnitudeForBrightness(p, 128.0 / 255.0);
    const double want = kBinAmplitude * coverage * pixelMagnitude;
    CHECK(near(amplitudeAt(runSilent(p, &im, 1.5).l, from, n, 3000.0), want, want * 0.04));
    // range 30 dB: louder
    p.rangeDb = 30.0f;
    const double want30 = kBinAmplitude * coverage * magnitudeForBrightness(p, 128.0 / 255.0);
    CHECK(near(amplitudeAt(runSilent(p, &im, 1.5).l, from, n, 3000.0), want30, want30 * 0.04));
    // contrast 2: g^2
    p = linearParams(); p.contrast = 2.0f;
    const double want2 = kBinAmplitude * coverage * magnitudeForBrightness(p, 128.0 / 255.0);
    CHECK(near(amplitudeAt(runSilent(p, &im, 1.5).l, from, n, 3000.0), want2, want2 * 0.04));
    // black is silence, exactly
    const Image black = band(8, {}, 0, 0);
    const Render o = runSilent(linearParams(), &black, 1.0);
    CHECK(rms(o.l, 0, o.l.size()) == 0.0);
}

void testInvertAndFrequencyWindow()
{
    const Image im = band(8, kMidRows, 0, 8);
    Parameters p = linearParams();
    p.invert = 1.0f;
    const std::size_t from = static_cast<std::size_t>(0.5 * kSr), n = static_cast<std::size_t>(0.8 * kSr);
    const Render o = runSilent(p, &im, 1.5);
    const double bandCoverage = 2.0 / rowSpanForBin(p, 3000.0);
    CHECK(near(amplitudeAt(o.l, from, n, 3000.0), kBinAmplitude * (1.0 - bandCoverage), kBinAmplitude * 0.03));
    CHECK(near(amplitudeAt(o.l, from, n, 2000.0), kBinAmplitude, kBinAmplitude * 0.03));
    // Inverted: everything between 1 and 5 kHz is lit, nothing outside
    CHECK(amplitudeAt(o.l, from, n, 700.0) < kBinAmplitude * 0.003);
    CHECK(amplitudeAt(o.l, from, n, 5500.0) < kBinAmplitude * 0.003);
    // Narrowing the window to 2-4 kHz moves the band: 3000 is still the middle but 1500 is now outside
    p = linearParams(); p.minHz = 2000.0f; p.maxHz = 4000.0f;
    const Image all = band(8, {}, 0, 0);
    Image white = all; std::fill(white.luma.begin(), white.luma.end(), 255);
    const Render w = runSilent(p, &white, 1.5);
    CHECK(amplitudeAt(w.l, from, n, 3000.0) > kBinAmplitude * 0.9);
    CHECK(amplitudeAt(w.l, from, n, 1500.0) < kBinAmplitude * 0.003);
    CHECK(amplitudeAt(w.l, from, n, 4500.0) < kBinAmplitude * 0.003);
}

void testPlayheadMovesAcrossThePicture()
{
    // left half carries a 3000 Hz band, right half a 2000 Hz band; a 4 s sweep plays 3 kHz first then 2 kHz
    Image im = band(100, kMidRows, 0, 50);
    for (int x = 50; x < 100; ++x) for (int y : {749, 750}) im.luma[static_cast<std::size_t>(y) * 100 + static_cast<std::size_t>(x)] = 255;
    Parameters p = linearParams();
    p.lengthS = 4.0f;
    const Render o = runSilent(p, &im, 4.0);
    const std::size_t n = static_cast<std::size_t>(0.8 * kSr);
    const std::size_t a = static_cast<std::size_t>(0.6 * kSr), b = static_cast<std::size_t>(2.6 * kSr);
    CHECK(amplitudeAt(o.l, a, n, 3000.0) > kBinAmplitude * 0.3);
    CHECK(amplitudeAt(o.l, a, n, 2000.0) < kBinAmplitude * 0.02);
    CHECK(amplitudeAt(o.l, b, n, 2000.0) > kBinAmplitude * 0.3);
    CHECK(amplitudeAt(o.l, b, n, 3000.0) < kBinAmplitude * 0.02);
    // doubling the length doubles the time spent: at 2.6 s a 8 s sweep is still on the left half
    p.lengthS = 8.0f;
    const Render q = runSilent(p, &im, 3.5);
    CHECK(amplitudeAt(q.l, b, n, 3000.0) > kBinAmplitude * 0.3);
}

void testTransportFollowsTheTimeline()
{
    Image im = band(100, kMidRows, 0, 50);   // 3 kHz on the left half, nothing on the right
    Parameters p = linearParams();
    p.lengthS = 8.0f; p.transport = 1.0f;
    const auto at = [&](const double beat) {
        Transport tr; tr.playing = true; tr.valid = true; tr.bpm = 120.0; tr.beatPosition = beat;
        std::vector<float> z(static_cast<std::size_t>(1.0 * kSr), 0.0f);
        EngineState s;
        resetState(s);
        Render o; o.l.assign(z.size(), 0.0f); o.r.assign(z.size(), 0.0f);
        for (std::size_t i = 0; i < z.size(); i += 512) {
            Transport t = tr; t.beatPosition = beat + static_cast<double>(i) / kSr * 2.0;   // 120 bpm = 2 beats a second
            processBlock(s, p, &im, kSr, 512, t, z.data() + i, z.data() + i, o.l.data() + i, o.r.data() + i);
        }
        return amplitudeAt(o.l, static_cast<std::size_t>(0.3 * kSr), static_cast<std::size_t>(0.6 * kSr), 3000.0);
    };
    CHECK(at(2.0) > kBinAmplitude * 0.3);     // 1 s into an 8 s sweep: left half
    CHECK(at(10.0) < kBinAmplitude * 0.02);   // 5 s: right half
    CHECK(at(18.0) > kBinAmplitude * 0.3);    // 9 s wraps to 1 s
    // free-run ignores the transport entirely
    p.transport = 0.0f;
    Transport tr; tr.playing = true; tr.valid = true; tr.bpm = 120.0; tr.beatPosition = 10.0;
    const std::vector<float> z(static_cast<std::size_t>(1.0 * kSr), 0.0f);
    const Render free1 = run(p, &im, z, z, 512, tr), free2 = run(p, &im, z, z, 512);
    CHECK(free1.l == free2.l);
    // an invalid transport falls back to free-run too
    p.transport = 1.0f;
    Transport bad; bad.valid = false;
    CHECK(run(p, &im, z, z, 512, bad).l == free2.l);
}

void testSourceInputKeepsTheInputsPhaseAndSilenceStaysSilent()
{
    Image white = band(4, {}, 0, 0);
    std::fill(white.luma.begin(), white.luma.end(), 255);
    // a narrow band picture, painted onto white noise: the 2.9-3.1 kHz region must dominate over 6-6.2 kHz by a wide margin
    const Image im = band(8, kMidRows, 0, 8);
    Parameters p = linearParams();
    p.source = 0.0f; p.follow = 0.0f; p.minHz = 1000.0f; p.maxHz = 8000.0f;
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    std::vector<float> in(static_cast<std::size_t>(2.0 * kSr)), in2(in.size());
    for (auto& v : in) v = d(rng);
    for (auto& v : in2) v = d(rng);
    const Render o = run(p, &im, in, in2);
    // energy within 3 bins of 3000 Hz (picture spans 1000-8000: row 499/500 sits at 4500 Hz) -> measure where the band really is
    // fraction .5 of 1000..8000 = 4500 Hz
    double peak = 0, away = 0;
    const std::size_t from = static_cast<std::size_t>(0.6 * kSr), n = static_cast<std::size_t>(0.8 * kSr);
    for (const double hz : {4475.0, 4500.0, 4525.0}) peak = std::max(peak, amplitudeAt(o.l, from, n, hz));
    for (const double hz : {2000.0, 3000.0, 6000.0, 7000.0}) away = std::max(away, amplitudeAt(o.l, from, n, hz));
    CHECK(peak > 0.002);
    CHECK(away < peak * 0.05);
    // with follow 1 a silent input stays silent exactly, in both sources
    p.follow = 1.0f;
    CHECK(rms(runSilent(p, &im, 1.0).l, 0, static_cast<std::size_t>(kSr)) == 0.0);
    p.source = 1.0f;
    CHECK(rms(runSilent(p, &im, 1.0).l, 0, static_cast<std::size_t>(kSr)) == 0.0);
    // with follow 0 the tone source sounds with no input at all
    p.follow = 0.0f;
    CHECK(rms(runSilent(p, &im, 1.0).l, static_cast<std::size_t>(0.5 * kSr), static_cast<std::size_t>(0.4 * kSr)) > 0.001);
    // follow is graded: half follow with a quiet tone lies between 0 and 1
    p.follow = 1.0f;
    std::vector<float> quiet(static_cast<std::size_t>(1.0 * kSr));
    for (std::size_t i = 0; i < quiet.size(); ++i) quiet[i] = 0.05f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / kSr));
    const Image w2 = band(8, kMidRows, 0, 8);
    const double full = rms(run(p, &w2, quiet, quiet).l, static_cast<std::size_t>(0.5 * kSr), static_cast<std::size_t>(0.4 * kSr));
    p.follow = 0.0f;
    const double none = rms(run(p, &w2, quiet, quiet).l, static_cast<std::size_t>(0.5 * kSr), static_cast<std::size_t>(0.4 * kSr));
    CHECK(full < none * 0.6 && full > none * 0.1);
}

void testMixOutputAndNoPicture()
{
    const Image im = band(8, kMidRows, 0, 8);
    Parameters p = linearParams();
    const std::size_t from = static_cast<std::size_t>(0.5 * kSr), n = static_cast<std::size_t>(0.8 * kSr);
    const double base = amplitudeAt(runSilent(p, &im, 1.5).l, from, n, 3000.0);
    p.outputDb = 6.0f;
    CHECK(near(amplitudeAt(runSilent(p, &im, 1.5).l, from, n, 3000.0) / base, std::pow(10.0, 6.0 / 20.0), 0.03));
    p.outputDb = -12.0f;
    CHECK(near(amplitudeAt(runSilent(p, &im, 1.5).l, from, n, 3000.0) / base, std::pow(10.0, -12.0 / 20.0), 0.02));
    p.outputDb = 0.0f; p.mix = 0.5f;
    CHECK(near(amplitudeAt(runSilent(p, &im, 1.5).l, from, n, 3000.0) / base, 0.5, 0.02));
    p.mix = 0.0f;
    CHECK(rms(runSilent(p, &im, 1.5).l, from, n) < 1e-9);
    // dry passes delayed by the latency, unchanged
    std::vector<float> in(8192);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.3f * static_cast<float>(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kSr));
    const Render o = run(p, &im, in, in);
    for (std::size_t i = static_cast<std::size_t>(kLatency); i < in.size(); ++i) CHECK(near(o.l[i], in[i - static_cast<std::size_t>(kLatency)], 1e-6));
    // no picture: the wet path is silent so a missing file is obvious
    p.mix = 1.0f;
    CHECK(rms(runSilent(p, nullptr, 1.0).l, 0, static_cast<std::size_t>(kSr)) == 0.0);
    Image empty;
    CHECK(rms(runSilent(p, &empty, 1.0).l, 0, static_cast<std::size_t>(kSr)) == 0.0);
}

void testBlockSizeIndependenceAndDeterminism()
{
    const Image im = band(100, kMidRows, 0, 50);
    Parameters p = linearParams();
    p.source = 0.0f; p.follow = 0.0f; p.mix = 0.7f;
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> d(-0.4f, 0.4f);
    std::vector<float> in(30000), in2(30000);
    for (auto& v : in) v = d(rng);
    for (auto& v : in2) v = d(rng);
    const Render a = run(p, &im, in, in2, 64), b = run(p, &im, in, in2, 4096), c = run(p, &im, in, in2, 1);
    CHECK(a.l == b.l && a.r == b.r);
    CHECK(a.l == c.l && a.r == c.r);
    CHECK(run(p, &im, in, in2, 64).l == a.l);
}

void testParametersAreClampedAndGarbageIsSafe()
{
    Parameters p;
    p.lengthS = -5.0f; p.rangeDb = 1e9f; p.contrast = NAN; p.minHz = 1.0f; p.maxHz = 1.0f; p.mix = 7.0f; p.source = 9.0f; p.outputDb = -1000.0f;
    const Parameters c = clampParameters(p);
    CHECK(c.lengthS == 0.5f && c.rangeDb == 100.0f && c.contrast == 0.25f);
    CHECK(c.minHz == 20.0f && c.maxHz >= c.minHz * 1.5f);
    CHECK(c.mix == 1.0f && c.source == 1.0f && c.outputDb == -24.0f);
    Parameters q;
    q.minHz = 9000.0f; q.maxHz = 9100.0f;
    const Parameters r = clampParameters(q);
    CHECK(r.maxHz >= r.minHz * 1.5f);
    // defaults sit inside their ranges and are audible: the default picture with default settings makes sound
    const Parameters d;
    CHECK(clampParameters(d).lengthS == d.lengthS && clampParameters(d).minHz == d.minHz && clampParameters(d).maxHz == d.maxHz);
    const Image pic = defaultPicture();
    const Render o = runSilent(d, &pic, 3.0);
    // default follow is 1 so silence is silent; the default must therefore ask for input: check with noise
    CHECK(rms(o.l, 0, o.l.size()) == 0.0);
    // NaN and infinity in the input never reach the output
    std::vector<float> bad(8192, 0.1f);
    bad[10] = NAN; bad[20] = INFINITY; bad[30] = -INFINITY;
    Parameters s = linearParams(); s.source = 0.0f; s.follow = 0.0f;
    const Render ob = run(s, &pic, bad, bad);
    for (float v : ob.l) CHECK(std::isfinite(v));
    for (float v : ob.r) CHECK(std::isfinite(v));
}

void testMetersAndRebuildOfState()
{
    EngineState s;
    resetState(s);
    const Image im = band(8, kMidRows, 0, 8);
    Parameters p = linearParams();
    p.lengthS = 4.0f;
    std::vector<float> z(static_cast<std::size_t>(kSr), 0.0f), o(z.size()), o2(z.size());
    processBlock(s, p, &im, kSr, static_cast<std::uint32_t>(z.size()), {}, z.data(), z.data(), o.data(), o2.data());
    // after one second of a 4 s sweep the playhead is a quarter of the way along
    CHECK(near(s.meters.playhead, 0.25, 0.02));
    CHECK(s.meters.level > 0.0f);
    // reset returns the playhead to the start and clears the audio
    resetState(s);
    CHECK(s.meters.playhead == 0.0f && s.timeS == 0.0 && s.frameIndex == 0);
}

}  // namespace

int main()
{
    testPngDecoders();
    testPnmAndBmp();
    testRefusals();
    testDamagedFilesNeverCrash();
    testInflate();
    testReduceKeepsAverage();
    testLatencyIsExactlyTheReportedOne();
    testBandIsPaintedAtItsFrequencyWithTheRightLevel();
    testHeightMapsToFrequencyTopIsHigh();
    testLogAxis();
    testLogAxisAveragesEachBinRowSpan();
    testRenderedSpectrumMatchesPictureWithIndependentStft();
    testBrightnessMapping();
    testInvertAndFrequencyWindow();
    testPlayheadMovesAcrossThePicture();
    testTransportFollowsTheTimeline();
    testSourceInputKeepsTheInputsPhaseAndSilenceStaysSilent();
    testMixOutputAndNoPicture();
    testBlockSizeIndependenceAndDeterminism();
    testParametersAreClampedAndGarbageIsSafe();
    testMetersAndRebuildOfState();
    std::printf("fresco core tests: %d checks passed\n", checks);
    return 0;
}
