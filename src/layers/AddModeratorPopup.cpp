#include "AddModeratorPopup.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include <Geode/binding/GameManager.hpp>
#include "../utils/Localization.hpp"

AddModeratorPopup* AddModeratorPopup::create(std::function<void(bool, const std::string&)> callback) {
    auto ret = new AddModeratorPopup();
    if (ret && ret->init(callback)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool AddModeratorPopup::init(std::function<void(bool, const std::string&)> callback) {
    m_callback = callback;
    
    // Match ReportInputPopup sizing/behavior.
    if (!FLAlertLayer::init(this, 
        Localization::get().getString("addmod.title").c_str(),
        "",
        Localization::get().getString("general.cancel").c_str(), Localization::get().getString("addmod.add_btn").c_str(), 360.f, false, 240.f, 1.0f)) return false;
    
    // Resize the main layer for more space
    if (m_mainLayer) {
        auto currentSize = m_mainLayer->getContentSize();
        m_mainLayer->setContentSize({currentSize.width, currentSize.height + 40.f});
        
        // Resize background sprite
        auto bgSprite = dynamic_cast<CCScale9Sprite*>(m_mainLayer->getChildren()->objectAtIndex(0));
        if (bgSprite) {
            bgSprite->setContentSize({bgSprite->getContentSize().width, bgSprite->getContentSize().height + 40.f});
        }
    }
    
    // Info label
    auto infoLabel = CCLabelBMFont::create(Localization::get().getString("addmod.enter_username_label").c_str(), "bigFont.fnt");
    infoLabel->setScale(0.5f);
    infoLabel->setPosition({m_mainLayer->getContentWidth() / 2, m_mainLayer->getContentHeight() / 2 + 35.f});
    m_mainLayer->addChild(infoLabel);
    
    // Username input (matches ReportInputPopup style)
    auto inputBG = CCScale9Sprite::create("square02b_small.png");
    inputBG->setColor({40, 40, 40});
    inputBG->setOpacity(255);
    inputBG->setContentSize({280.f, 50.f});
    inputBG->setPosition({m_mainLayer->getContentWidth() / 2, m_mainLayer->getContentHeight() / 2 - 10.f});
    m_mainLayer->addChild(inputBG, 10);
    
    m_usernameInput = CCTextInputNode::create(260.f, 40.f, Localization::get().getString("addmod.enter_username").c_str(), "chatFont.fnt");
    m_usernameInput->setLabelPlaceholderColor({150, 150, 150});
    m_usernameInput->setLabelPlaceholderScale(0.8f);
    m_usernameInput->setAllowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    m_usernameInput->setMaxLabelLength(20);
    m_usernameInput->setPosition(inputBG->getPosition());
    m_usernameInput->setDelegate(this);
    m_usernameInput->setString("");
    m_mainLayer->addChild(m_usernameInput, 11);
    
    return true;
}

void AddModeratorPopup::FLAlert_Clicked(FLAlertLayer* layer, bool btn2) {
    if (btn2) {
        std::string username = m_usernameInput->getString();
        
        if (username.empty() || username == Localization::get().getString("addmod.enter_username")) {
            Notification::create(Localization::get().getString("addmod.enter_username").c_str(), NotificationIcon::Warning)->show();
            return;
        }
        
        auto gm = GameManager::get();
        std::string adminUser = gm->m_playerName;
        
        // Show loading (keep pointer for lifecycle safety)
        m_loadingCircle = LoadingCircle::create();
        if (m_loadingCircle) {
            m_loadingCircle->setParentLayer(this);
            m_loadingCircle->show();
        }
        
        // Retain until async finishes.
        this->retain();
        ThumbnailAPI::get().addModerator(username, adminUser, [this, username](bool success, const std::string& message) {
            if (m_loadingCircle && m_loadingCircle->getParent()) {
                m_loadingCircle->fadeAndRemove();
            }
            m_loadingCircle = nullptr;

            if (success) {
                FLAlertLayer::create(
                    Localization::get().getString("addmod.success_title").c_str(),
                    Localization::get().getString("addmod.success_msg").c_str(),
                    Localization::get().getString("general.ok").c_str()
                )->show();

                if (m_callback) {
                    m_callback(true, username);
                }

                if (!m_closed) {
                    FLAlertLayer::keyBackClicked();
                    m_closed = true;
                }
            } else if (!m_closed) {
                FLAlertLayer::create(
                    Localization::get().getString("addmod.error_title").c_str(),
                    message,
                    Localization::get().getString("general.ok").c_str()
                )->show();
            }
            this->release();
        });
    } else {
        if (m_callback) {
            m_callback(false, "");
        }
    }
}

