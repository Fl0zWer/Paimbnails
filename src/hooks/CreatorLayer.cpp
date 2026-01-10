#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include "../layers/LeaderboardLayer.hpp"

using namespace geode::prelude;

class $modify(MyCreatorLayer, CreatorLayer) {
    bool init() {
        if (!CreatorLayer::init()) return false;

        CCSprite* spr = CCSprite::create("Daily.png"_spr);
        if (!spr) {
            spr = CCSprite::createWithSpriteFrameName("GJ_trophyBtn_001.png");
        } else {
            float targetSize = 35.0f;
            float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
            if (currentSize > 0) {
                spr->setScale(targetSize / currentSize);
            }
        }

        auto btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(MyCreatorLayer::onLeaderboard)
        );
        btn->setID("paimon-leaderboard-btn");

        bool added = false;
        if (auto menu = this->getChildByID("bottom-right-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
            btn->setPositionY(115.0f);
            added = true;
        } else if (auto menu = this->getChildByID("creator-buttons-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
            added = true;
        } else if (auto menu = this->getChildByID("exit-menu")) {
             // Fallback for some texture packs or older versions
            menu->addChild(btn);
            menu->updateLayout();
            added = true;
        }
        
        if (!added) {
            log::warn("Could not find menu to add leaderboard button in CreatorLayer");
        }

        return true;
    }

    void onLeaderboard(CCObject*) {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, LeaderboardLayer::scene()));
    }
};
