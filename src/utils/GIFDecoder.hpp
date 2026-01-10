#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <cstdint>

/**
 * Simple GIF decoder for extracting frames.
 * Supports basic animated GIFs without complex LZW compression.
 */
class GIFDecoder {
public:
    struct Frame {
        int left;
        int top;
        int width;
        int height;
        int delayMs; // Frame duration in milliseconds
        std::vector<uint8_t> pixels; // RGBA data
    };

    struct GIFData {
        std::vector<Frame> frames;
        int width;
        int height;
        bool isAnimated;
    };

    /**
     * Decodes a GIF from in-memory data.
     * @param data GIF data
     * @param size Data size in bytes
     * @return GIFData with all frames, or an empty structure on failure
     */
    static GIFData decode(const uint8_t* data, size_t size);

    /**
     * Verifies whether the data is a valid GIF.
     */
    static bool isGIF(const uint8_t* data, size_t size);

    /**
     * Obtiene las dimensiones del GIF sin decodificarlo completo
     */
    static bool getDimensions(const uint8_t* data, size_t size, int& width, int& height);

private:
    // Internal decoding helpers
    static bool parseHeader(const uint8_t*& ptr, const uint8_t* end, int& width, int& height);
    static bool parseColorTable(const uint8_t*& ptr, const uint8_t* end, std::vector<uint8_t>& palette, int size);
    struct RawFrame {
        std::vector<uint8_t> pixels;
        int width, height, left, top;
    };
    
    static bool parseFrame(const uint8_t*& ptr, const uint8_t* end, RawFrame& frame, const std::vector<uint8_t>& globalPalette, int transparentIndex, bool hasTransparency);
};

