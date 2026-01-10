#include "LevelColors.hpp"
#include "../utils/PaimonFormat.hpp"
#include "../utils/DominantColors.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <cocos2d.h>
#include <sstream>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;

LevelColors& LevelColors::get() { static LevelColors lc; return lc; }

std::filesystem::path LevelColors::path() const {
    return Mod::get()->getSaveDir() / "thumbnails" / "level_colors.paimon";
}

void LevelColors::load() const {
    if (m_loaded) return; 
    m_loaded = true; 
    m_items.clear();
    
    auto p = path(); 
    if (!std::filesystem::exists(p)) return;
    
    // Load decrypted data from .paimon.
    auto data = PaimonFormat::load(p);
    if (data.empty()) return;
    
    // Parse CSV.
    std::string content(data.begin(), data.end());
    std::stringstream ss(content); 
    std::string line;
    
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string id,r1,g1,b1,r2,g2,b2;
        if (!std::getline(ls, id, ',')) continue;
        if (!std::getline(ls, r1, ',')) continue;
        if (!std::getline(ls, g1, ',')) continue;
        if (!std::getline(ls, b1, ',')) continue;
        if (!std::getline(ls, r2, ',')) continue;
        if (!std::getline(ls, g2, ',')) continue;
        if (!std::getline(ls, b2, ',')) continue;
        LevelColorPair pair{ 
            ccColor3B{(GLubyte)std::atoi(r1.c_str()), (GLubyte)std::atoi(g1.c_str()), (GLubyte)std::atoi(b1.c_str())},
            ccColor3B{(GLubyte)std::atoi(r2.c_str()), (GLubyte)std::atoi(g2.c_str()), (GLubyte)std::atoi(b2.c_str())} 
        };
        m_items.emplace_back((int32_t)std::atoi(id.c_str()), pair);
    }
}

void LevelColors::save() const {
    std::stringstream ss;
    for (auto const& it : m_items) {
        auto const& p = it.second;
        ss << it.first << "," << (int)p.a.r << "," << (int)p.a.g << "," << (int)p.a.b
           << "," << (int)p.b.r << "," << (int)p.b.g << "," << (int)p.b.b << "\n";
    }
    
    std::string content = ss.str();
    std::vector<uint8_t> data(content.begin(), content.end());
    
    // Save encrypted in .paimon format.
    auto p = path();
    PaimonFormat::save(p, data);
}

void LevelColors::set(int32_t levelID, ccColor3B a, ccColor3B b) {
    std::lock_guard<std::mutex> lock(m_mutex);
    load();
    for (auto& it : m_items) {
        if (it.first == levelID) { it.second = LevelColorPair{a,b}; save(); return; }
    }
    m_items.emplace_back(levelID, LevelColorPair{a,b});
    save();
}

std::optional<LevelColorPair> LevelColors::getPair(int32_t levelID) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    load();
    for (auto const& it : m_items) if (it.first == levelID) return it.second;
    return std::nullopt;
}

void LevelColors::extractFromImage(int32_t levelID, cocos2d::CCImage* image) {
    if (!image) return;

    unsigned char* imgData = image->getData();
    int w = image->getWidth();
    int h = image->getHeight();
    bool hasAlpha = image->hasAlpha();
    
    if (!imgData || w <= 0 || h <= 0) return;
    
    // Convert RGBA->RGB24 if needed.
    std::vector<uint8_t> rgb24;
    const uint8_t* rgbPtr = nullptr;
    
    if (hasAlpha) {
        rgb24.resize(w * h * 3);
        for (int i = 0; i < w * h; i++) {
            rgb24[i * 3 + 0] = imgData[i * 4 + 0];
            rgb24[i * 3 + 1] = imgData[i * 4 + 1];
            rgb24[i * 3 + 2] = imgData[i * 4 + 2];
        }
        rgbPtr = rgb24.data();
    } else {
        rgbPtr = imgData;
    }
    
    try {
        auto pair = DominantColors::extract(rgbPtr, w, h);
        cocos2d::ccColor3B colorA{pair.first.r, pair.first.g, pair.first.b};
        cocos2d::ccColor3B colorB{pair.second.r, pair.second.g, pair.second.b};
        
        this->set(levelID, colorA, colorB);
    } catch (const std::exception& e) {
        log::warn("[LevelColors] Error extracting colors for level {}: {}", levelID, e.what());
    }
}

void LevelColors::extractFromRawData(int32_t levelID, const uint8_t* imgData, int w, int h, bool hasAlpha) {
    if (!imgData || w <= 0 || h <= 0) return;
    
    // Convert RGBA->RGB24 if needed.
    std::vector<uint8_t> rgb24;
    const uint8_t* rgbPtr = nullptr;
    
    if (hasAlpha) {
        rgb24.resize(w * h * 3);
        for (int i = 0; i < w * h; i++) {
            rgb24[i * 3 + 0] = imgData[i * 4 + 0];
            rgb24[i * 3 + 1] = imgData[i * 4 + 1];
            rgb24[i * 3 + 2] = imgData[i * 4 + 2];
        }
        rgbPtr = rgb24.data();
    } else {
        rgbPtr = imgData;
    }
    
    try {
        auto pair = DominantColors::extract(rgbPtr, w, h);
        cocos2d::ccColor3B colorA{pair.first.r, pair.first.g, pair.first.b};
        cocos2d::ccColor3B colorB{pair.second.r, pair.second.g, pair.second.b};
        
        this->set(levelID, colorA, colorB);
    } catch (const std::exception& e) {
        log::warn("[LevelColors] Error extracting colors for level {}: {}", levelID, e.what());
    }
}

// Process one cached image.
void processCachedImage(const std::filesystem::path& filepath, int32_t levelID) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        log::warn("[LevelColors] Failed to open: {}", geode::utils::string::pathToString(filepath));
        return;
    }
    
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    if (data.empty()) return;
    
    auto image = new cocos2d::CCImage();
    if (!image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
        log::warn("[LevelColors] Failed to decode image: {}", geode::utils::string::pathToString(filepath));
        image->release();
        return;
    }
    
    LevelColors::get().extractFromImage(levelID, image);
    
    image->release();
}

void LevelColors::extractColorsFromCache() {
    log::info("[LevelColors] Extracting colors from cache...");
    
    auto cacheDir = Mod::get()->getSaveDir() / "thumbnails";
    if (!std::filesystem::exists(cacheDir)) {
        log::warn("[LevelColors] Cache directory does not exist");
        return;
    }
    
    int processed = 0;
    int success = 0;
    int skipped = 0;
    
    // Scan cached thumbnails (.png/.webp).
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir)) {
        if (!entry.is_regular_file()) continue;
        
        auto filepath = entry.path();
        auto ext = geode::utils::string::pathToString(filepath.extension());
        if (!(ext == ".png" || ext == ".webp")) continue;
        
        std::string filename = geode::utils::string::pathToString(filepath.stem());
        auto levelIDResult = geode::utils::numFromString<int32_t>(filename);
        if (!levelIDResult.isOk()) {
            log::debug("[LevelColors] Skipping non-numeric file: {}", filename);
            continue;
        }
        int32_t levelID = levelIDResult.unwrap();
        
        // Skip if already cached.
        if (getPair(levelID).has_value()) {
            skipped++;
            continue;
        }
        
        processed++;
        processCachedImage(filepath, levelID);
        
        // processCachedImage calls set().
        if (getPair(levelID).has_value()) {
            success++;
        }
        
        if (success % 10 == 0 && success > 0) {
            log::info("[LevelColors] Progress: {} ok, {} skipped", success, skipped);
        }
    }
    
    log::info("[LevelColors] Done: {} ok, {} skipped, {} processed", 
              success, skipped, processed);
}

