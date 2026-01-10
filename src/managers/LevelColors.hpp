#pragma once

#include <Geode/DefaultInclude.hpp>
#include <optional>
#include <utility>
#include <mutex>

struct LevelColorPair {
    cocos2d::ccColor3B a;
    cocos2d::ccColor3B b;
};

class LevelColors {
public:
    static LevelColors& get();

    void set(int32_t levelID, cocos2d::ccColor3B a, cocos2d::ccColor3B b);
    std::optional<LevelColorPair> getPair(int32_t levelID) const;
    
    // Process all cached thumbnails and extract colors
    void extractColorsFromCache();
    
    // Extract colors from a loaded CCImage
    void extractFromImage(int32_t levelID, cocos2d::CCImage* image);

    // Extract colors from raw RGBA/RGB data
    void extractFromRawData(int32_t levelID, const uint8_t* data, int width, int height, bool hasAlpha);

private:
    LevelColors() = default;
    std::filesystem::path path() const;
    void load() const;
    void save() const;

    mutable bool m_loaded = false;
    mutable std::vector<std::pair<int32_t, LevelColorPair>> m_items;
    mutable std::mutex m_mutex;
};

