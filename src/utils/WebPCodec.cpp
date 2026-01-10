#include "WebPCodec.hpp"
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

namespace WebPCodec {

#if defined(HAVE_WEBP)
// If libwebp is available, include headers and implement.
#include <webp/encode.h>
#include <webp/decode.h>

    std::optional<std::vector<uint8_t>> encodeRGBAtoWebP(
        const uint8_t* rgba, int width, int height, int quality
    ) {
        if (!rgba || width <= 0 || height <= 0) return std::nullopt;
        uint8_t* outData = nullptr;
        size_t outSize = WebPEncodeRGBA(rgba, width, height, width * 4, quality, &outData);
        if (outSize == 0 || outData == nullptr) {
            log::error("WebPEncodeRGBA failed");
            return std::nullopt;
        }
        std::vector<uint8_t> bytes(outData, outData + outSize);
        WebPFree(outData);
        return bytes;
    }

    std::optional<ImageRGBA> decodeWebPtoRGBA(
        const uint8_t* webpData, size_t size
    ) {
        if (!webpData || size == 0) return std::nullopt;
        int w = 0, h = 0;
        if (!WebPGetInfo(webpData, size, &w, &h) || w <= 0 || h <= 0) {
            log::error("WebPGetInfo failed");
            return std::nullopt;
        }
        ImageRGBA img;
        img.width = w;
        img.height = h;
        img.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        if (!WebPDecodeRGBAInto(webpData, size, img.rgba.data(), img.rgba.size(), w * 4)) {
            log::error("WebPDecodeRGBAInto failed");
            return std::nullopt;
        }
        return img;
    }

#else // !HAVE_WEBP

    std::optional<std::vector<uint8_t>> encodeRGBAtoWebP(
        const uint8_t* rgba, int width, int height, int quality
    ) {
        (void)rgba; (void)width; (void)height; (void)quality;
        log::warn("WebP encode requested but HAVE_WEBP not enabled. Returning nullopt.");
        return std::nullopt;
    }

    std::optional<ImageRGBA> decodeWebPtoRGBA(
        const uint8_t* webpData, size_t size
    ) {
        (void)webpData; (void)size;
        log::warn("WebP decode requested but HAVE_WEBP not enabled. Returning nullopt.");
        return std::nullopt;
    }

#endif
}

