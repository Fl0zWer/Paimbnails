#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include "../utils/PaimonButtonHighlighter.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// Simplified: keep GD behavior but preserve the original scale.
class $modify(PaimonMenuItemScaleFix, CCMenuItemSpriteExtra) {
    struct Fields {
        float m_originalScale = 1.0f;
        bool m_scaleCaptured = false;
    };

    void selected() {
        // Only capture scale for our registered buttons.
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (!m_fields->m_scaleCaptured) {
                m_fields->m_originalScale = this->getScale();
                m_fields->m_scaleCaptured = true;
            }
        }
        
        CCMenuItemSpriteExtra::selected();
    }

    void unselected() {
        CCMenuItemSpriteExtra::unselected();
        
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (m_fields->m_scaleCaptured) {
                // Stop GD's default scaling animation.
                this->stopAllActions();
                
                // Ease back to original scale.
                auto scaleTo = CCScaleTo::create(0.2f, m_fields->m_originalScale);
                this->runAction(CCEaseSineOut::create(scaleTo));
            }
        }
    }
    
    void activate() {
        CCMenuItemSpriteExtra::activate();
        
        if (PaimonButtonHighlighter::isRegisteredButton(this)) {
            if (m_fields->m_scaleCaptured) {
                // Restore original scale immediately on activate.
                this->stopAllActions();
                this->setScale(m_fields->m_originalScale);
            }
        }
    }
};

