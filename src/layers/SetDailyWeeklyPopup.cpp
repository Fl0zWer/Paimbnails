#include "SetDailyWeeklyPopup.hpp"
#include "../utils/HttpClient.hpp"
#include <Geode/utils/web.hpp>

bool SetDailyWeeklyPopup::setup(int levelID) {
    m_levelID = levelID;
    this->setTitle("Set Daily/Weekly");

    // Do NOT touch m_buttonMenu position or layout, as it contains the Close Button (X).
    // Instead, create a separate menu for our content buttons.
    
    auto actionMenu = CCMenu::create();
    actionMenu->setPosition(m_mainLayer->getContentSize() / 2);
    actionMenu->setContentSize({ 200.f, 160.f }); // width, height container
    actionMenu->ignoreAnchorPointForPosition(false); // Helper to visualize if needed, but centering is enough usually
    
    // We will use a ColumnLayout for this new menu
    actionMenu->setLayout(
        ColumnLayout::create()
            ->setGap(10.f)
            ->setAxisReverse(true) // Top to Bottom
    );
    
    m_mainLayer->addChild(actionMenu);

    // Set Daily Button
    auto dailyBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Set Daily", 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onSetDaily)
    );
    actionMenu->addChild(dailyBtn);

    // Set Weekly Button
    auto weeklyBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Set Weekly", 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onSetWeekly)
    );
    actionMenu->addChild(weeklyBtn);
    
    // Unset Button
    auto unsetBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Unset", 0, false, "goldFont.fnt", "GJ_button_06.png", 0, 1.0f),
        this,
        menu_selector(SetDailyWeeklyPopup::onUnset)
    );
    unsetBtn->setScale(0.8f);
    actionMenu->addChild(unsetBtn);

    // Apply layout to our action menu
    actionMenu->updateLayout();

    return true;
}

SetDailyWeeklyPopup* SetDailyWeeklyPopup::create(int levelID) {
    auto ret = new SetDailyWeeklyPopup();
    if (ret && ret->initAnchored(240, 200, levelID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void SetDailyWeeklyPopup::onSetDaily(CCObject* sender) {
    createQuickPopup(
        "Confirm",
        "Set this level as <cy>Daily</c>?",
        "Cancel", "Set",
        [this](auto, bool btn2) {
            if (btn2) {
                auto gm = GameManager::sharedState();
                std::string username = gm->m_playerName;

                matjson::Value json = matjson::makeObject({
                    {"levelID", m_levelID},
                    {"username", username}
                });
                
                Notification::create("Setting daily...", NotificationIcon::Info)->show();
                
                HttpClient::get().post("/api/daily/set", json.dump(), [this](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create("Daily set successfully", NotificationIcon::Success)->show();
                        this->onClose(nullptr);
                    } else {
                        Notification::create("Failed to set daily: " + msg, NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}

void SetDailyWeeklyPopup::onSetWeekly(CCObject* sender) {
    createQuickPopup(
        "Confirm",
        "Set this level as <cy>Weekly</c>?",
        "Cancel", "Set",
        [this](auto, bool btn2) {
            if (btn2) {
                auto gm = GameManager::sharedState();
                std::string username = gm->m_playerName;

                matjson::Value json = matjson::makeObject({
                    {"levelID", m_levelID},
                    {"username", username}
                });
                
                Notification::create("Setting weekly...", NotificationIcon::Info)->show();
                
                HttpClient::get().post("/api/weekly/set", json.dump(), [this](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create("Weekly set successfully", NotificationIcon::Success)->show();
                        this->onClose(nullptr);
                    } else {
                        Notification::create("Failed to set weekly: " + msg, NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}

void SetDailyWeeklyPopup::onUnset(CCObject* sender) {
    createQuickPopup(
        "Confirm",
        "Unset this level from Daily/Weekly?",
        "Cancel", "Unset",
        [this](auto, bool btn2) {
             if (btn2) {
                 matjson::Value json = matjson::makeObject({
                    {"levelID", m_levelID},
                    {"type", "unset"}
                });
                
                Notification::create("Unsetting...", NotificationIcon::Info)->show();
                
                HttpClient::get().post("/api/admin/set-daily", json.dump(), [this](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create("Unset successfully", NotificationIcon::Success)->show();
                        this->onClose(nullptr);
                    } else {
                        Notification::create("Failed to unset: " + msg, NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}
