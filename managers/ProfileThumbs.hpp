#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <string>

#include <unordered_set>

struct ProfileConfig {
    std::string backgroundType = "gradient";
    float blurIntensity = 3.0f;
    float darkness = 0.2f;
    bool useGradient = false;
    cocos2d::ccColor3B colorA = {255,255,255};
    cocos2d::ccColor3B colorB = {255,255,255};
    cocos2d::ccColor3B separatorColor = {0,0,0};
    int separatorOpacity = 50;
    float widthFactor = 0.60f; // Added widthFactor
    bool hasConfig = false;
    std::string gifKey = ""; // Added gifKey for local cache reference
};

struct ProfileCacheEntry {
    cocos2d::CCTexture2D* texture;
    std::string gifKey; // Added gifKey
    cocos2d::ccColor3B colorA;
    cocos2d::ccColor3B colorB;
    float widthFactor;
    std::chrono::steady_clock::time_point timestamp;
    ProfileConfig config;
    
    ProfileCacheEntry() : texture(nullptr), gifKey(""), colorA({255,255,255}), colorB({255,255,255}), 
                         widthFactor(0.5f), timestamp(std::chrono::steady_clock::now()) {}
    
    ProfileCacheEntry(cocos2d::CCTexture2D* tex, cocos2d::ccColor3B ca, cocos2d::ccColor3B cb, float w) 
        : texture(tex), gifKey(""), colorA(ca), colorB(cb), widthFactor(w), 
          timestamp(std::chrono::steady_clock::now()) {
        if (texture) texture->retain();
    }

    ProfileCacheEntry(const std::string& key, cocos2d::ccColor3B ca, cocos2d::ccColor3B cb, float w) 
        : texture(nullptr), gifKey(key), colorA(ca), colorB(cb), widthFactor(w), 
          timestamp(std::chrono::steady_clock::now()) {
    }
    
    ~ProfileCacheEntry() {
        if (texture) texture->release();
    }
    
    // Prevent copying
    ProfileCacheEntry(const ProfileCacheEntry&) = delete;
    ProfileCacheEntry& operator=(const ProfileCacheEntry&) = delete;
    
    // Allow moving
    ProfileCacheEntry(ProfileCacheEntry&& other) noexcept 
        : texture(other.texture), gifKey(std::move(other.gifKey)), colorA(other.colorA), colorB(other.colorB), 
          widthFactor(other.widthFactor), timestamp(other.timestamp), config(other.config) {
        other.texture = nullptr;
    }
    
    ProfileCacheEntry& operator=(ProfileCacheEntry&& other) noexcept {
        if (this != &other) {
            if (texture) texture->release();
            texture = other.texture;
            gifKey = std::move(other.gifKey);
            colorA = other.colorA;
            colorB = other.colorB;
            widthFactor = other.widthFactor;
            timestamp = other.timestamp;
            config = other.config;
            other.texture = nullptr;
        }
        return *this;
    }
};

class ProfileThumbs {
public:
    static ProfileThumbs& get();
    
    ~ProfileThumbs() {
        // Auto-clear cache on shutdown
        clearAllCache();
    }

    bool saveRGB(int accountID, const uint8_t* rgb, int width, int height);
    bool has(int accountID) const;
    void deleteProfile(int accountID); // Deletes local file and cache
    cocos2d::CCTexture2D* loadTexture(int accountID);
    bool loadRGB(int accountID, std::vector<uint8_t>& out, int& w, int& h);
    
    // Temporary cache for downloaded profiles
    void cacheProfile(int accountID, cocos2d::CCTexture2D* texture, 
                     cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor);
    
    void cacheProfileGIF(int accountID, const std::string& gifKey, 
                     cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor);

    void cacheProfileConfig(int accountID, const ProfileConfig& config);
    ProfileConfig getProfileConfig(int accountID);

    ProfileCacheEntry* getCachedProfile(int accountID);
    void clearCache(int accountID); // Removes specific entry
    void clearOldCache(); // Removes entries older than 14 days
    void clearAllCache(); // Clears all cached entries

    // Creates a node containing the profile background and image
    cocos2d::CCNode* createProfileNode(cocos2d::CCTexture2D* texture, const ProfileConfig& config, cocos2d::CCSize size, bool onlyBackground = false);

    // Queue a profile download
    void queueLoad(int accountID, const std::string& username, std::function<void(bool, cocos2d::CCTexture2D*)> callback);

    // Notify that a profile is currently visible on screen (prioritizes download)
    void notifyVisible(int accountID);

    // Negative cache management
    void markNoProfile(int accountID);
    void removeFromNoProfileCache(int accountID);
    bool isNoProfile(int accountID) const;
    void clearNoProfileCache();

private:
    ProfileThumbs() = default;
    std::string makePath(int accountID) const;
    void processQueue();
    
    std::unordered_map<int, ProfileCacheEntry> m_profileCache;
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_visibilityMap; // Tracks when a profile was last seen
    std::unordered_set<int> m_noProfileCache; // Cache for non-existent profiles
    std::unordered_map<int, std::string> m_usernameMap; // Map accountID to username for pending downloads
    static constexpr auto CACHE_DURATION = std::chrono::hours(24 * 14); // 14 days

    // Queue system
    std::deque<int> m_downloadQueue;
    std::unordered_map<int, std::vector<std::function<void(bool, cocos2d::CCTexture2D*)>>> m_pendingCallbacks;
    int m_activeDownloads = 0;
    const int MAX_CONCURRENT_DOWNLOADS = 50;
};

