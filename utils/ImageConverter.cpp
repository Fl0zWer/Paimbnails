#include "ImageConverter.hpp"
#include <Geode/Geode.hpp>
#include <fstream>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;

std::vector<uint8_t> ImageConverter::rgbToRgba(const std::vector<uint8_t>& rgbData, uint32_t width, uint32_t height) {
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    
    for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = rgbData[i * 3 + 0]; // R
        rgba[i * 4 + 1] = rgbData[i * 3 + 1]; // G
        rgba[i * 4 + 2] = rgbData[i * 3 + 2]; // B
        rgba[i * 4 + 3] = 255;                // A (fully opaque)
    }
    
    return rgba;
}

bool ImageConverter::rgbToPng(const std::vector<uint8_t>& rgbData, uint32_t width, uint32_t height, std::vector<uint8_t>& outPngData) {
    // Check if data is RGB (3 bytes) or RGBA (4 bytes)
    // We assume RGBA if size matches 4 bytes per pixel
    bool isRgba = (rgbData.size() == static_cast<size_t>(width) * height * 4);
    
    std::vector<uint8_t> rgba;
    if (isRgba) {
        rgba = rgbData;
    } else {
        // Convert RGB to RGBA
        rgba = rgbToRgba(rgbData, width, height);
    }
    
    // Create CCImage from RGBA data
    CCImage img;
    if (!img.initWithImageData(rgba.data(), rgba.size(), CCImage::kFmtRawData, width, height)) {
        log::error("[ImageConverter] Failed to init CCImage with raw data");
        return false;
    }
    
    // Save to a mod-local temporary file (portable / writable on Android)
    auto tmpDir = Mod::get()->getSaveDir() / "tmp";
    std::error_code dirEc;
    std::filesystem::create_directories(tmpDir, dirEc);
    auto tempPath = tmpDir / fmt::format("temp_img_{}.png", (uintptr_t)&img);
    if (!img.saveToFile(geode::utils::string::pathToString(tempPath).c_str(), false)) {
        log::error("[ImageConverter] Failed to save image to temp file");
        return false;
    }
    
    // Read PNG bytes from temp file
    std::ifstream pngFile(tempPath, std::ios::binary);
    if (!pngFile) {
        log::error("[ImageConverter] Failed to open temp PNG file");
        std::filesystem::remove(tempPath);
        return false;
    }
    
    pngFile.seekg(0, std::ios::end);
    size_t pngSize = static_cast<size_t>(pngFile.tellg());
    pngFile.seekg(0, std::ios::beg);
    
    outPngData.resize(pngSize);
    pngFile.read(reinterpret_cast<char*>(outPngData.data()), pngSize);
    pngFile.close();

    // Clean up temporary file
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    if (ec) log::warn("[ImageConverter] Failed to remove temp file {}: {}", geode::utils::string::pathToString(tempPath), ec.message());
    
    return true;
}

bool ImageConverter::loadRgbFileToPng(const std::string& rgbFilePath, std::vector<uint8_t>& outPngData) {
    std::vector<uint8_t> rgbData;
    uint32_t width, height;
    
    if (!loadRgbFile(rgbFilePath, rgbData, width, height)) {
        return false;
    }
    
    return rgbToPng(rgbData, width, height, outPngData);
}

bool ImageConverter::loadRgbFile(const std::string& rgbFilePath, std::vector<uint8_t>& outRgbData, uint32_t& outWidth, uint32_t& outHeight) {
    std::ifstream in(rgbFilePath, std::ios::binary);
    if (!in) {
        log::error("[ImageConverter] Failed to open RGB file: {}", rgbFilePath);
        return false;
    }
    
    // Read header
    RGBHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || header.width == 0 || header.height == 0) {
        log::error("[ImageConverter] Invalid RGB header in file: {}", rgbFilePath);
        return false;
    }
    
    // Read RGB data
    size_t rgbSize = static_cast<size_t>(header.width) * header.height * 3;
    outRgbData.resize(rgbSize);
    in.read(reinterpret_cast<char*>(outRgbData.data()), rgbSize);
    
    if (!in) {
        log::error("[ImageConverter] Failed to read RGB data from file: {}", rgbFilePath);
        return false;
    }
    
    outWidth = header.width;
    outHeight = header.height;
    
    return true;
}

