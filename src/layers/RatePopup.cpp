#include "RatePopup.hpp"
#include <Geode/binding/ButtonSprite.hpp>

RatePopup* RatePopup::create(int levelID, std::string thumbnailId) {
    auto ret = new RatePopup();
    if (ret && ret->initAnchored(300, 200, levelID, thumbnailId)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RatePopup::setup(int levelID, std::string thumbnailId) {
    m_levelID = levelID;
    m_thumbnailId = thumbnailId;
    
    this->setTitle("Rate Thumbnail");

    auto menu = CCMenu::create();
    menu->setPosition({m_mainLayer->getContentSize().width / 2, m_mainLayer->getContentSize().height / 2});
    m_mainLayer->addChild(menu);

    float startX = -60;
    for (int i = 1; i <= 5; i++) {
        auto offSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        offSpr->setColor({100, 100, 100});
        offSpr->setScale(0.8f);
        
        auto onSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        onSpr->setScale(0.8f);
        
        auto btn = CCMenuItemSpriteExtra::create(offSpr, this, menu_selector(RatePopup::onStar));
        btn->setTag(i);
        btn->setPosition({startX + (i - 1) * 30.0f, 10});
        menu->addChild(btn);
        m_starBtns.push_back(btn);
    }

    auto submitSpr = ButtonSprite::create("Submit", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    auto submitBtn = CCMenuItemSpriteExtra::create(submitSpr, this, menu_selector(RatePopup::onSubmit));
    submitBtn->setPosition({0, -50});
    menu->addChild(submitBtn);

    return true;
}

void RatePopup::onStar(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    m_rating = btn->getTag();
    
    for (int i = 0; i < 5; i++) {
        auto b = m_starBtns[i];
        auto spr = static_cast<CCSprite*>(b->getNormalImage());
        if (i < m_rating) {
            spr->setColor({255, 255, 255});
        } else {
            spr->setColor({100, 100, 100});
        }
    }
}

void RatePopup::onSubmit(CCObject* sender) {
    if (m_rating == 0) {
        Notification::create("Please select a rating", NotificationIcon::Error)->show();
        return;
    }
    
    std::string username = "";
    if (auto gm = GameManager::sharedState()) {
        username = gm->m_playerName;
    }
    
    if (username.empty() || username == "unknown") {
        Notification::create("You must be logged in to vote", NotificationIcon::Error)->show();
        return;
    }
    
    ThumbnailAPI::get().submitVote(m_levelID, m_rating, username, m_thumbnailId, [this](bool success, const std::string& msg) {
        if (success) {
            Notification::create("Rating submitted!", NotificationIcon::Success)->show();
            if (m_onRateCallback) {
                m_onRateCallback();
            }
            this->onClose(nullptr);
        } else {
            // Show the actual error message from the server
            std::string errorMsg = "Failed to submit rating";
            if (!msg.empty()) {
                errorMsg += ": " + msg;
            }
            Notification::create(errorMsg.c_str(), NotificationIcon::Error)->show();
        }
    });
}
