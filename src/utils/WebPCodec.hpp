#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <string>

// Minimal WebP codec interface. Implementations are optional (guarded by HAVE_WEBP).
// If HAVE_WEBP is not defined, functions return std::nullopt and log.
//
// RGBA pixel format is 8-bit per channel, row-major, size = width * height * 4.
namespace WebPCodec {
    struct ImageRGBA {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba; // size = w*h*4
    };

    // Encode RGBA buffer to WebP (lossy). Quality 0-100.
    // Returns encoded bytes on success.
    std::optional<std::vector<uint8_t>> encodeRGBAtoWebP(
        const uint8_t* rgba, int width, int height, int quality = 80
    );

    // Decode WebP bytes to RGBA buffer.
    std::optional<ImageRGBA> decodeWebPtoRGBA(
        const uint8_t* webpData, size_t size
    );
}

