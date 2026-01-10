#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class LocalThumbs {
public:
    static LocalThumbs& get();

    // returns full path to local thumb file if exists
    std::optional<std::string> getThumbPath(int32_t levelID) const;

    // returns full path to any valid thumbnail file (rgb, png, jpg, webp) in thumbnails or cache
    std::optional<std::string> findAnyThumbnail(int32_t levelID) const;

    bool has(int32_t levelID) const { return getThumbPath(levelID).has_value(); }

    // load texture for levelID; returns nullptr if not found
    cocos2d::CCTexture2D* loadTexture(int32_t levelID) const;

    // get all level IDs that have a local thumbnail
    std::vector<int32_t> getAllLevelIDs() const;

    // save raw RGB24 data with size; returns success
    bool saveRGB(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height);
    
    // save raw RGBA32 data (converts to RGB24 internally); returns success
    bool saveFromRGBA(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height);

    // Mapping system: levelID -> fileName (para nueva API)
    void storeFileMapping(int32_t levelID, const std::string& fileName);
    std::optional<std::string> getFileName(int32_t levelID) const;
    void loadMappings();
    void saveMappings();

private:
    LocalThumbs(); // Private constructor
    std::string dir() const;
    std::string mappingFile() const;
    std::unordered_map<int32_t, std::string> m_fileMapping;
    
    // Cache for fast lookup
    std::unordered_set<int32_t> m_availableLevels;
    mutable std::mutex m_mutex;
    bool m_cacheInitialized = false;
    void initCache();
};

