#pragma once
#include <cocos2d.h>
#include <vector>

#include <deque>
#include <algorithm>
#include <Geode/Geode.hpp>

using namespace cocos2d;
using namespace geode::prelude;

class ListThumbnailCarousel : public CCNode {
protected:
    std::vector<int> m_levelIDs;
    int m_currentIndex = 0;
    CCSprite* m_currentSprite = nullptr;
    CCSprite* m_nextSprite = nullptr;
    CCSize m_size;
    GLubyte m_opacity = 255;
    CCSprite* m_loadingCircle = nullptr;
    float m_waitForImageTime = 0.0f;

    // Animation State
    CCRect m_panStartRect;
    CCRect m_panEndRect;
    float m_panElapsed = 0.0f;
    
    // Safety flag for async callbacks
    std::shared_ptr<bool> m_alive;

    bool init(const std::vector<int>& levelIDs, CCSize size);
    void tryShowNextImage();
    void updateCarousel(float dt);
    void updatePan(float dt);
    void onImageLoaded(CCTexture2D* texture, int index);

public:
    static ListThumbnailCarousel* create(const std::vector<int>& levelIDs, CCSize size);
    virtual ~ListThumbnailCarousel();
    
    void visit() override;

    void startCarousel();
    void setOpacity(GLubyte opacity);
};
