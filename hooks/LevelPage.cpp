#include <Geode/modify/LevelPage.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Assets.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelPage, LevelPage) {
    struct Fields {
        CCNode* m_thumbClipper = nullptr;
        CCSprite* m_thumbSprite = nullptr;
        int m_levelID = 0;
    };

    void updateDynamicPage(GJGameLevel* level) {
        LevelPage::updateDynamicPage(level);
        
        if (!level) return;
        
        m_fields->m_levelID = level->m_levelID;
        
        // Only for main levels (ID > 0)
        if (level->m_levelID <= 0) return;
        
        if (this->m_levelDisplay) {
            // Request thumbnail
            std::string fileName = fmt::format("{}.png", level->m_levelID);
            
            auto selfPtr = this;
            this->retain();
            
            ThumbnailLoader::get().requestLoad(level->m_levelID, fileName, [selfPtr, level](CCTexture2D* tex, bool success) {
                if (success && tex && selfPtr->m_fields->m_levelID == level->m_levelID) {
                    selfPtr->applyThumbnail(tex);
                }
                selfPtr->release();
            }, 5); // Priority 5
        }
    }
    
    void applyThumbnail(CCTexture2D* tex) {
        if (!tex || !m_levelDisplay) return;
        
        if (m_fields->m_thumbClipper) {
            m_fields->m_thumbClipper->removeFromParent();
            m_fields->m_thumbClipper = nullptr;
            m_fields->m_thumbSprite = nullptr;
        }
        
        auto sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;
        
        CCSize boxSize = m_levelDisplay->getContentSize();
        
        // Create a clipping node to contain the thumbnail within the box
        // Use a rounded rect stencil (square02_001.png is a rounded square)
        auto stencil = CCScale9Sprite::create("square02_001.png");
        stencil->setContentSize(boxSize);
        stencil->setPosition(boxSize / 2);
        
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize(boxSize);
        clipper->setAnchorPoint({0.5f, 0.5f});
        clipper->setPosition(boxSize / 2); // Center in m_levelDisplay
        clipper->setAlphaThreshold(0.05f);
        
        // Scale sprite to cover the box
        float scaleX = boxSize.width / sprite->getContentSize().width;
        float scaleY = boxSize.height / sprite->getContentSize().height;
        float scale = std::max(scaleX, scaleY);
        
        sprite->setScale(scale);
        sprite->setPosition(boxSize / 2); // Center in the clipper
        sprite->setColor({150, 150, 150}); // Darken slightly
        
        clipper->addChild(sprite);
        
        // Add clipper to m_levelDisplay
        m_levelDisplay->addChild(clipper, -1);
        
        m_fields->m_thumbClipper = clipper;
        m_fields->m_thumbSprite = sprite;
    }
};
