#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace downspout::fresco {

// A picture reduced to what the painter needs: one brightness byte per pixel (0 black .. 255 white), row 0 at the top.
// Decoders are written from the file format specifications (PNG, RFC 1950/1951; Netpbm; BMP) and share no code with any library.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0 || luma.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height); }
    [[nodiscard]] std::uint8_t at(const int x, const int y) const noexcept { return luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
};

struct DecodeResult {
    bool ok = false;
    std::string error;       // set when !ok
    Image image;
};

inline constexpr int kMaxSourceDimension = 8192;            // pixels, either way
inline constexpr std::size_t kMaxSourcePixels = 16u << 20;  // 16 Mpixel
inline constexpr int kMaxStoredDimension = 2048;            // larger pictures are reduced to this
inline constexpr std::size_t kMaxFileBytes = 64u << 20;

// Brightness of a colour: Rec. 709 weights on the stored (gamma-encoded) values, which is what a viewer shows.
[[nodiscard]] std::uint8_t lumaOf(int r, int g, int b) noexcept;

// PNG (all colour types and bit depths, no interlacing), Netpbm P2/P3/P5/P6, and uncompressed BMP (8, 24 and 32 bit).
// JPEG, GIF, WebP and the rest are refused with a message that says so. Transparent pixels are treated as black.
[[nodiscard]] DecodeResult decodeImage(const std::uint8_t* data, std::size_t size);
[[nodiscard]] DecodeResult loadImageFile(const std::string& path);

// RFC 1951 (deflate) inside an RFC 1950 (zlib) wrapper. `limit` stops runaway output. Returns false on any error.
[[nodiscard]] bool inflateZlib(const std::uint8_t* data, std::size_t size, std::size_t limit, std::vector<std::uint8_t>& out);

// Reduces a picture so neither side exceeds `maxDimension` (area average).
[[nodiscard]] Image reduced(const Image& image, int maxDimension);

// The picture shown before any file is loaded: concentric rings and a cross.
[[nodiscard]] Image defaultPicture();

}  // namespace downspout::fresco
