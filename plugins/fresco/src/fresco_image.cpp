#include "fresco_image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace downspout::fresco {

namespace {

// ---- inflate ---------------------------------------------------------------------------------------------------------------------------------

struct BitReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
    std::uint32_t bitBuffer = 0;
    int bitCount = 0;
    bool failed = false;

    std::uint32_t bits(const int n) noexcept
    {
        while (bitCount < n) {
            if (pos >= size) { failed = true; return 0; }
            bitBuffer |= static_cast<std::uint32_t>(data[pos++]) << bitCount;
            bitCount += 8;
        }
        const std::uint32_t v = bitBuffer & ((1u << n) - 1u);
        bitBuffer >>= n;
        bitCount -= n;
        return v;
    }
};

struct Huffman {
    std::array<std::uint16_t, 16> count {};
    std::array<std::uint16_t, 288> symbol {};
};

// Canonical Huffman code from code lengths. Returns the number of unused code space (>= 0 for a valid, possibly incomplete code).
int build(Huffman& h, const std::uint8_t* lengths, const int n)
{
    h.count.fill(0);
    for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; ++len) {
        left <<= 1;
        left -= h.count[static_cast<std::size_t>(len)];
        if (left < 0) return left;
    }
    std::array<std::uint16_t, 16> offset {};
    for (int len = 1; len < 15; ++len) offset[static_cast<std::size_t>(len + 1)] = static_cast<std::uint16_t>(offset[static_cast<std::size_t>(len)] + h.count[static_cast<std::size_t>(len)]);
    for (int s = 0; s < n; ++s)
        if (lengths[s] != 0) h.symbol[offset[lengths[s]]++] = static_cast<std::uint16_t>(s);
    return left;
}

int decodeSymbol(BitReader& in, const Huffman& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= static_cast<int>(in.bits(1));
        if (in.failed) return -1;
        const int count = h.count[static_cast<std::size_t>(len)];
        if (code - count < first) return h.symbol[static_cast<std::size_t>(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

constexpr std::array<std::uint16_t, 29> kLengthBase = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> kDistBase = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateCodes(BitReader& in, std::vector<std::uint8_t>& out, const std::size_t limit, const Huffman& lit, const Huffman& dist)
{
    for (;;) {
        const int sym = decodeSymbol(in, lit);
        if (sym < 0) return false;
        if (sym < 256) {
            if (out.size() >= limit) return false;
            out.push_back(static_cast<std::uint8_t>(sym));
        } else if (sym == 256) {
            return true;
        } else {
            const int li = sym - 257;
            if (li >= 29) return false;
            const int len = kLengthBase[static_cast<std::size_t>(li)] + static_cast<int>(in.bits(kLengthExtra[static_cast<std::size_t>(li)]));
            const int ds = decodeSymbol(in, dist);
            if (ds < 0 || ds >= 30) return false;
            const std::size_t distance = kDistBase[static_cast<std::size_t>(ds)] + in.bits(kDistExtra[static_cast<std::size_t>(ds)]);
            if (in.failed || distance > out.size() || out.size() + static_cast<std::size_t>(len) > limit) return false;
            for (int i = 0; i < len; ++i) out.push_back(out[out.size() - distance]);
        }
    }
}

bool inflateRaw(BitReader& in, std::vector<std::uint8_t>& out, const std::size_t limit)
{
    bool last = false;
    while (!last) {
        last = in.bits(1) != 0;
        const std::uint32_t type = in.bits(2);
        if (in.failed) return false;
        if (type == 0) {
            in.bitBuffer = 0; in.bitCount = 0;   // stored: byte aligned
            if (in.pos + 4 > in.size) return false;
            const unsigned len = in.data[in.pos] | (in.data[in.pos + 1] << 8);
            const unsigned nlen = in.data[in.pos + 2] | (in.data[in.pos + 3] << 8);
            in.pos += 4;
            if ((len ^ 0xFFFFu) != nlen || in.pos + len > in.size || out.size() + len > limit) return false;
            out.insert(out.end(), in.data + in.pos, in.data + in.pos + len);
            in.pos += len;
        } else if (type == 1) {
            std::array<std::uint8_t, 288> lengths {};
            for (int i = 0; i < 144; ++i) lengths[static_cast<std::size_t>(i)] = 8;
            for (int i = 144; i < 256; ++i) lengths[static_cast<std::size_t>(i)] = 9;
            for (int i = 256; i < 280; ++i) lengths[static_cast<std::size_t>(i)] = 7;
            for (int i = 280; i < 288; ++i) lengths[static_cast<std::size_t>(i)] = 8;
            Huffman lit, dist;
            build(lit, lengths.data(), 288);
            std::array<std::uint8_t, 30> d {};
            d.fill(5);
            build(dist, d.data(), 30);
            if (!inflateCodes(in, out, limit, lit, dist)) return false;
        } else if (type == 2) {
            const int nlen = static_cast<int>(in.bits(5)) + 257, ndist = static_cast<int>(in.bits(5)) + 1, ncode = static_cast<int>(in.bits(4)) + 4;
            if (in.failed || nlen > 286 || ndist > 30) return false;
            static constexpr std::array<std::uint8_t, 19> order = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::array<std::uint8_t, 19> codeLengths {};
            for (int i = 0; i < ncode; ++i) codeLengths[order[static_cast<std::size_t>(i)]] = static_cast<std::uint8_t>(in.bits(3));
            Huffman lencode;
            if (build(lencode, codeLengths.data(), 19) != 0) return false;
            std::array<std::uint8_t, 320> lengths {};
            int index = 0;
            while (index < nlen + ndist) {
                int symbol = decodeSymbol(in, lencode);
                if (symbol < 0) return false;
                if (symbol < 16) {
                    lengths[static_cast<std::size_t>(index++)] = static_cast<std::uint8_t>(symbol);
                } else {
                    int len = 0, repeat;
                    if (symbol == 16) {
                        if (index == 0) return false;
                        len = lengths[static_cast<std::size_t>(index - 1)];
                        repeat = 3 + static_cast<int>(in.bits(2));
                    } else if (symbol == 17) {
                        repeat = 3 + static_cast<int>(in.bits(3));
                    } else {
                        repeat = 11 + static_cast<int>(in.bits(7));
                    }
                    if (index + repeat > nlen + ndist) return false;
                    while (repeat--) lengths[static_cast<std::size_t>(index++)] = static_cast<std::uint8_t>(len);
                }
            }
            if (in.failed || lengths[256] == 0) return false;
            Huffman lit, dist;
            const int le = build(lit, lengths.data(), nlen);
            if (le < 0 || (le > 0 && nlen - lit.count[0] != 1)) return false;
            const int de = build(dist, lengths.data() + nlen, ndist);
            if (de < 0 || (de > 0 && ndist - dist.count[0] != 1)) return false;
            if (!inflateCodes(in, out, limit, lit, dist)) return false;
        } else {
            return false;
        }
        if (in.failed) return false;
    }
    return true;
}

// ---- helpers ---------------------------------------------------------------------------------------------------------------------------------

DecodeResult fail(const char* message)
{
    DecodeResult r;
    r.error = message;
    return r;
}

DecodeResult finish(Image image)
{
    DecodeResult r;
    r.ok = true;
    r.image = reduced(image, kMaxStoredDimension);
    return r;
}

bool sizeOk(const std::uint64_t w, const std::uint64_t h) { return w > 0 && h > 0 && w <= kMaxSourceDimension && h <= kMaxSourceDimension && w * h <= kMaxSourcePixels; }

std::uint32_t be32(const std::uint8_t* p) { return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3]; }
std::uint32_t le32(const std::uint8_t* p) { return p[0] | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
std::uint32_t le16(const std::uint8_t* p) { return p[0] | (static_cast<std::uint32_t>(p[1]) << 8); }

std::uint8_t over(const std::uint8_t luma, const int alpha) { return static_cast<std::uint8_t>((luma * alpha + 127) / 255); }

// ---- PNG -------------------------------------------------------------------------------------------------------------------------------------

DecodeResult decodePng(const std::uint8_t* d, const std::size_t size)
{
    std::size_t pos = 8;
    int width = 0, height = 0, depth = 0, colour = -1, interlace = 0;
    std::vector<std::uint8_t> palette, idat;
    bool haveHeader = false, sawEnd = false;
    while (pos + 12 <= size) {
        const std::uint32_t len = be32(d + pos);
        const char* type = reinterpret_cast<const char*>(d + pos + 4);
        if (len > size || pos + 12 + static_cast<std::size_t>(len) > size) return fail("PNG is truncated");
        const std::uint8_t* body = d + pos + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) return fail("PNG header is the wrong size");
            width = static_cast<int>(be32(body)); height = static_cast<int>(be32(body + 4));
            depth = body[8]; colour = body[9]; interlace = body[12];
            if (body[10] != 0 || body[11] != 0) return fail("PNG uses an unknown compression or filter method");
            haveHeader = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(body, body + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
            if (idat.size() > (kMaxFileBytes)) return fail("PNG is too large");
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            sawEnd = true;
            break;
        }
        pos += 12 + static_cast<std::size_t>(len);
    }
    if (!haveHeader) return fail("PNG has no header");
    if (!sawEnd && idat.empty()) return fail("PNG has no image data");
    if (!sizeOk(static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height))) return fail("PNG dimensions are zero or too large");
    if (interlace != 0 && interlace != 1) return fail("PNG interlace method is not valid");
    int channels;
    switch (colour) {
    case 0: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) return fail("PNG bit depth is not valid"); break;
    case 2: channels = 3; if (depth != 8 && depth != 16) return fail("PNG bit depth is not valid"); break;
    case 3: channels = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return fail("PNG bit depth is not valid"); break;
    case 4: channels = 2; if (depth != 8 && depth != 16) return fail("PNG bit depth is not valid"); break;
    case 6: channels = 4; if (depth != 8 && depth != 16) return fail("PNG bit depth is not valid"); break;
    default: return fail("PNG colour type is not valid");
    }
    if (colour == 3 && palette.empty()) return fail("PNG palette is missing");
    const std::size_t bitsPerPixel = static_cast<std::size_t>(channels) * static_cast<std::size_t>(depth);
    struct Pass { int x, y, dx, dy; };
    static constexpr std::array<Pass, 7> adam7 = {{{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                                    {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}}};
    const Pass full {0, 0, 1, 1};
    const int passCount = interlace == 1 ? static_cast<int>(adam7.size()) : 1;
    const auto extent = [](const int fullSize, const int start, const int step) {
        return fullSize <= start ? 0 : (fullSize - start + step - 1) / step;
    };
    const auto passAt = [&](const int i) -> const Pass& { return interlace == 1 ? adam7[static_cast<std::size_t>(i)] : full; };
    std::size_t expected = 0;
    for (int i = 0; i < passCount; ++i) {
        const Pass& pass = passAt(i);
        const int passWidth = extent(width, pass.x, pass.dx), passHeight = extent(height, pass.y, pass.dy);
        if (passWidth == 0 || passHeight == 0) continue;
        const std::size_t stride = (static_cast<std::size_t>(passWidth) * bitsPerPixel + 7) / 8;
        expected += (stride + 1) * static_cast<std::size_t>(passHeight);
    }

    std::vector<std::uint8_t> raw;
    raw.reserve(expected);
    if (!inflateZlib(idat.data(), idat.size(), expected, raw) || raw.size() != expected) return fail("PNG image data is damaged");

    const std::size_t bpp = std::max<std::size_t>(1, bitsPerPixel / 8);
    Image image;
    image.width = width; image.height = height;
    image.luma.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::size_t rawPos = 0;
    for (int i = 0; i < passCount; ++i) {
        const Pass& pass = passAt(i);
        const int passWidth = extent(width, pass.x, pass.dx), passHeight = extent(height, pass.y, pass.dy);
        if (passWidth == 0 || passHeight == 0) continue;
        const std::size_t stride = (static_cast<std::size_t>(passWidth) * bitsPerPixel + 7) / 8;
        std::vector<std::uint8_t> prev(stride, 0), cur(stride, 0);
        for (int py = 0; py < passHeight; ++py) {
            const std::uint8_t* line = raw.data() + rawPos;
            rawPos += stride + 1;
            const int filter = line[0];
            const std::uint8_t* src = line + 1;
            for (std::size_t byte = 0; byte < stride; ++byte) {
                const int a = byte >= bpp ? cur[byte - bpp] : 0, b = prev[byte], c = byte >= bpp ? prev[byte - bpp] : 0;
                int v = src[byte];
                switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: {
                    const int predictor = a + b - c, pa = std::abs(predictor - a), pb = std::abs(predictor - b), pc = std::abs(predictor - c);
                    v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default: return fail("PNG has an unknown row filter");
                }
                cur[byte] = static_cast<std::uint8_t>(v);
            }
            for (int x = 0; x < passWidth; ++x) {
                int r = 0, g = 0, bl = 0, alpha = 255;
                if (colour == 0 || colour == 3) {
                    int sample;
                    if (depth == 8) sample = cur[static_cast<std::size_t>(x)];
                    else if (depth == 16) sample = cur[static_cast<std::size_t>(x) * 2];
                    else {
                        const int perByte = 8 / depth, shift = 8 - depth * (x % perByte + 1);
                        sample = (cur[static_cast<std::size_t>(x / perByte)] >> shift) & ((1 << depth) - 1);
                    }
                    if (colour == 3) {
                        if (static_cast<std::size_t>(sample) * 3 + 2 >= palette.size()) return fail("PNG uses a colour outside its palette");
                        r = palette[static_cast<std::size_t>(sample) * 3]; g = palette[static_cast<std::size_t>(sample) * 3 + 1]; bl = palette[static_cast<std::size_t>(sample) * 3 + 2];
                    } else {
                        const int v = depth == 8 || depth == 16 ? sample : sample * 255 / ((1 << depth) - 1);
                        r = g = bl = v;
                    }
                } else {
                    const std::size_t step = depth == 8 ? 1 : 2;
                    const std::uint8_t* px = cur.data() + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels) * step;
                    if (colour == 4) { r = g = bl = px[0]; alpha = px[step]; }
                    else { r = px[0]; g = px[step]; bl = px[2 * step]; if (colour == 6) alpha = px[3 * step]; }
                }
                const int imageX = pass.x + x * pass.dx, imageY = pass.y + py * pass.dy;
                image.luma[static_cast<std::size_t>(imageY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(imageX)] = over(lumaOf(r, g, bl), alpha);
            }
            std::swap(prev, cur);
        }
    }
    return finish(std::move(image));
}

// ---- Netpbm ----------------------------------------------------------------------------------------------------------------------------------

DecodeResult decodePnm(const std::uint8_t* d, const std::size_t size)
{
    std::size_t pos = 2;
    const int kind = d[1] - '0';   // 2, 3, 5, 6
    const auto skipSpace = [&] {
        while (pos < size) {
            if (d[pos] == '#') { while (pos < size && d[pos] != '\n') ++pos; }
            else if (d[pos] == ' ' || d[pos] == '\t' || d[pos] == '\r' || d[pos] == '\n') ++pos;
            else break;
        }
    };
    const auto number = [&](long& v) {
        skipSpace();
        if (pos >= size || d[pos] < '0' || d[pos] > '9') return false;
        v = 0;
        while (pos < size && d[pos] >= '0' && d[pos] <= '9') { v = v * 10 + (d[pos] - '0'); if (v > 1000000000L) return false; ++pos; }
        return true;
    };
    long w, h, maxval;
    if (!number(w) || !number(h) || !number(maxval)) return fail("PNM header is damaged");
    if (!sizeOk(static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h))) return fail("PNM dimensions are zero or too large");
    if (maxval < 1 || maxval > 65535) return fail("PNM maximum value is not valid");
    const bool binary = kind == 5 || kind == 6;
    const int channels = (kind == 3 || kind == 6) ? 3 : 1;
    const int bytes = maxval > 255 ? 2 : 1;
    if (binary) {
        if (pos >= size) return fail("PNM is truncated");
        ++pos;   // the single whitespace after maxval
        if (size - pos < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(channels) * static_cast<std::size_t>(bytes)) return fail("PNM is truncated");
    }
    Image image;
    image.width = static_cast<int>(w); image.height = static_cast<int>(h);
    image.luma.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (std::size_t i = 0; i < image.luma.size(); ++i) {
        int c[3] = {0, 0, 0};
        for (int k = 0; k < channels; ++k) {
            long v;
            if (binary) {
                v = bytes == 2 ? (static_cast<long>(d[pos]) << 8) | d[pos + 1] : d[pos];
                pos += static_cast<std::size_t>(bytes);
            } else if (!number(v)) {
                return fail("PNM is truncated");
            }
            c[k] = static_cast<int>(std::min<long>(255, (v * 255 + maxval / 2) / maxval));
        }
        image.luma[i] = channels == 3 ? lumaOf(c[0], c[1], c[2]) : static_cast<std::uint8_t>(c[0]);
    }
    return finish(std::move(image));
}

// ---- BMP -------------------------------------------------------------------------------------------------------------------------------------

DecodeResult decodeBmp(const std::uint8_t* d, const std::size_t size)
{
    if (size < 54) return fail("BMP is truncated");
    const std::uint32_t offset = le32(d + 10), header = le32(d + 14);
    if (header < 40) return fail("this BMP header is not supported");
    const std::int32_t w = static_cast<std::int32_t>(le32(d + 18)), hRaw = static_cast<std::int32_t>(le32(d + 22));
    const int bits = static_cast<int>(le16(d + 28));
    const std::uint32_t compression = le32(d + 30), colours = le32(d + 46);
    const bool topDown = hRaw < 0;
    const std::int32_t h = topDown ? -hRaw : hRaw;
    if (w <= 0 || !sizeOk(static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h))) return fail("BMP dimensions are zero or too large");
    if (compression != 0 && !(compression == 3 && bits == 32)) return fail("compressed BMP is not supported");
    if (bits != 8 && bits != 24 && bits != 32) return fail("BMP bit depth is not supported (use 8, 24 or 32)");
    std::vector<std::uint8_t> pal;
    if (bits == 8) {
        const std::size_t entries = colours == 0 ? 256 : std::min<std::uint32_t>(colours, 256);
        const std::size_t at = 14 + header;
        if (at + entries * 4 > size) return fail("BMP palette is truncated");
        pal.assign(d + at, d + at + entries * 4);
    }
    const std::size_t stride = ((static_cast<std::size_t>(w) * static_cast<std::size_t>(bits) + 31) / 32) * 4;
    if (offset > size || size - offset < stride * static_cast<std::size_t>(h)) return fail("BMP pixel data is truncated");
    Image image;
    image.width = w; image.height = h;
    image.luma.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int row = 0; row < h; ++row) {
        const int y = topDown ? row : h - 1 - row;
        const std::uint8_t* line = d + offset + static_cast<std::size_t>(row) * stride;
        for (int x = 0; x < w; ++x) {
            int r, g, b, a = 255;
            if (bits == 8) {
                const std::size_t i = static_cast<std::size_t>(line[x]) * 4;
                if (i + 3 >= pal.size()) return fail("BMP uses a colour outside its palette");
                b = pal[i]; g = pal[i + 1]; r = pal[i + 2];
            } else if (bits == 24) {
                b = line[x * 3]; g = line[x * 3 + 1]; r = line[x * 3 + 2];
            } else {
                b = line[x * 4]; g = line[x * 4 + 1]; r = line[x * 4 + 2]; a = line[x * 4 + 3];
                if (compression == 0 && a == 0) a = 255;   // many writers leave the unused byte at 0
            }
            image.luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = over(lumaOf(r, g, b), a);
        }
    }
    return finish(std::move(image));
}

}  // namespace

std::uint8_t lumaOf(const int r, const int g, const int b) noexcept
{
    const double v = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return static_cast<std::uint8_t>(std::min(255.0, std::max(0.0, std::floor(v + 0.5))));
}

bool inflateZlib(const std::uint8_t* data, const std::size_t size, const std::size_t limit, std::vector<std::uint8_t>& out)
{
    if (size < 2 || (data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20) != 0) return false;
    BitReader in {data + 2, size - 2};
    return inflateRaw(in, out, limit);
}

Image reduced(const Image& image, const int maxDimension)
{
    if (image.empty() || (image.width <= maxDimension && image.height <= maxDimension)) return image;
    const double scale = static_cast<double>(maxDimension) / static_cast<double>(std::max(image.width, image.height));
    Image out;
    out.width = std::max(1, static_cast<int>(std::lround(image.width * scale)));
    out.height = std::max(1, static_cast<int>(std::lround(image.height * scale)));
    out.luma.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height));
    for (int y = 0; y < out.height; ++y) {
        const int y0 = static_cast<int>(static_cast<std::int64_t>(y) * image.height / out.height);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<std::int64_t>(y + 1) * image.height / out.height));
        for (int x = 0; x < out.width; ++x) {
            const int x0 = static_cast<int>(static_cast<std::int64_t>(x) * image.width / out.width);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<std::int64_t>(x + 1) * image.width / out.width));
            std::uint64_t sum = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) sum += image.at(xx, yy);
            out.luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>((sum + static_cast<std::uint64_t>((y1 - y0) * (x1 - x0)) / 2) / static_cast<std::uint64_t>((y1 - y0) * (x1 - x0)));
        }
    }
    return out;
}

DecodeResult decodeImage(const std::uint8_t* data, const std::size_t size)
{
    if (data == nullptr || size < 8) return fail("the file is too short to be a picture");
    static constexpr std::uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(data, png, 8) == 0) return decodePng(data, size);
    if (data[0] == 'P' && (data[1] == '2' || data[1] == '3' || data[1] == '5' || data[1] == '6')) return decodePnm(data, size);
    if (data[0] == 'B' && data[1] == 'M') return decodeBmp(data, size);
    if (data[0] == 0xFF && data[1] == 0xD8) return fail("JPEG is not supported yet: save the picture as PNG");
    if (std::memcmp(data, "GIF8", 4) == 0) return fail("GIF is not supported: save the picture as PNG");
    if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0) return fail("WebP is not supported: save the picture as PNG");
    return fail("unrecognised file: use PNG, PNM (PGM or PPM) or BMP");
}

DecodeResult loadImageFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return fail("the file could not be opened");
    const std::streamsize size = file.tellg();
    if (size <= 0) return fail("the file is empty");
    if (static_cast<std::size_t>(size) > kMaxFileBytes) return fail("the file is too large");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return fail("the file could not be read");
    return decodeImage(bytes.data(), bytes.size());
}

Image defaultPicture()
{
    Image img;
    img.width = 512;
    img.height = 256;
    img.luma.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height), 0);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const double dx = (x - img.width / 2.0) / 2.0, dy = y - img.height / 2.0;   // the picture is wider than tall: squash x so the rings are round
            const double r = std::sqrt(dx * dx + dy * dy);
            double v = 0.0;
            const double ring = std::abs(std::fmod(r, 40.0) - 20.0);   // rings every 40 pixels
            if (r < 118.0 && ring > 14.0) v = 1.0;
            if (std::abs(dy) < 2.0 || std::abs(x - img.width / 2.0) < 2.0) v = std::max(v, 0.7);
            img.luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(v * 255.0);
        }
    }
    return img;
}

}  // namespace downspout::fresco
