#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <cocos2d.h>

/**
 * Utility class for image format conversions.
 * Centralizes all RGB/RGBA/PNG conversion logic to avoid duplication.
 */
class ImageConverter {
public:
    /**
     * Convert RGB888 data to RGBA8888 format.
     * @param rgbData Input RGB data
     * @param width Image width
     * @param height Image height
     * @return RGBA data vector
     */
    static std::vector<uint8_t> rgbToRgba(const std::vector<uint8_t>& rgbData, uint32_t width, uint32_t height);
    
    /**
     * Convert RGB888 data to PNG format.
     * @param rgbData Input RGB data
     * @param width Image width
     * @param height Image height
     * @param outPngData Output PNG data vector
     * @return true if successful
     */
    static bool rgbToPng(const std::vector<uint8_t>& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData);
    
    /**
     * Load RGB data from file and convert to PNG.
     * @param rgbFilePath Path to RGB file with header (width, height, rgb data)
     * @param outPngData Output PNG data vector
     * @return true if successful
     */
    static bool loadRgbFileToPng(const std::string& rgbFilePath, std::vector<uint8_t>& outPngData);
    
    /**
     * Load RGB data from file and get dimensions.
     * @param rgbFilePath Path to RGB file
     * @param outRgbData Output RGB data
     * @param outWidth Output width
     * @param outHeight Output height
     * @return true if successful
     */
    static bool loadRgbFile(const std::string& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight);

private:
    struct RGBHeader {
        uint32_t width;
        uint32_t height;
    };
};

