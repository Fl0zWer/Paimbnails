#include "TempThumbnails.hpp"

using namespace cocos2d;

TempThumbnails& TempThumbnails::get() {
    static TempThumbnails instance;
    return instance;
}

void TempThumbnails::store(int levelID, CCTexture2D* texture) {
    if (texture) {
        m_cache[levelID] = texture;
    }
}

CCTexture2D* TempThumbnails::get(int levelID) {
    auto it = m_cache.find(levelID);
    if (it != m_cache.end()) {
        return it->second;
    }
    return nullptr;
}

bool TempThumbnails::has(int levelID) const {
    return m_cache.find(levelID) != m_cache.end();
}

void TempThumbnails::clear() {
    m_cache.clear();
}

