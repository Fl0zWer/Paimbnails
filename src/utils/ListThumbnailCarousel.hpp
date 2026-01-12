#pragma once
#include <cocos2d.h>
#include <vector>

#include <deque>
#include <algorithm>
#include <Geode/Geode.hpp>

class ListThumbnailCarousel : public cocos2d::CCNode {
protected:
    std::vector<int> m_levelIDs;
    int m_currentIndex = 0;
    cocos2d::CCSprite* m_currentSprite = nullptr;
    cocos2d::CCSprite* m_nextSprite = nullptr;
    cocos2d::CCSize m_size;
    GLubyte m_opacity = 255;
    cocos2d::CCSprite* m_loadingCircle = nullptr;
    float m_waitForImageTime = 0.0f;

    // Animation State
    cocos2d::CCRect m_panStartRect;
    cocos2d::CCRect m_panEndRect;
    float m_panElapsed = 0.0f;
    
    // Safety flag for async callbacks
    std::shared_ptr<bool> m_alive;

    bool init(const std::vector<int>& levelIDs, cocos2d::CCSize size);
    void tryShowNextImage();
    void updateCarousel(float dt);
    void updatePan(float dt);
    void onImageLoaded(cocos2d::CCTexture2D* texture, int index);

public:
    static ListThumbnailCarousel* create(const std::vector<int>& levelIDs, cocos2d::CCSize size);
    virtual ~ListThumbnailCarousel();
    
    void visit() override;

    void startCarousel();
    void setOpacity(GLubyte opacity);
};
