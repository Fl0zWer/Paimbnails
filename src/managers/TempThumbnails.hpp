#pragma once

#include <cocos2d.h>
#include <unordered_map>
#include <memory>

// Manages temporary thumbnails downloaded from server (memory only, cleared on restart)
class TempThumbnails {
public:
    static TempThumbnails& get();
    
    void store(int levelID, cocos2d::CCTexture2D* texture);
    cocos2d::CCTexture2D* get(int levelID);
    bool has(int levelID) const;
    void clear();
    
private:
    TempThumbnails() = default;
    std::unordered_map<int, geode::Ref<cocos2d::CCTexture2D>> m_cache;
};

