#include "ThumbnailViewPopup.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Log.hpp>
#include "../utils/Localization.hpp"

using namespace geode::prelude;
using namespace cocos2d;

ThumbnailViewPopup* ThumbnailViewPopup::create(int levelID, PendingCategory cat) {
    auto ret = new ThumbnailViewPopup();
    ret->m_levelID = levelID;
    ret->m_category = cat;
    if (ret && ret->initAnchored(440.f, 290.f)) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret); return nullptr;
}

bool ThumbnailViewPopup::setup() {
    // Hide default background
    if (m_bgSprite) {
        m_bgSprite->setVisible(false);
        m_bgSprite->setOpacity(0);
    }
    if (m_closeBtn) {
        m_closeBtn->setVisible(false);
        m_closeBtn->setOpacity(0);
        m_closeBtn->removeFromParent(); // Remove it completely to be sure
    }
    this->setTitle(""); 

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto size = m_mainLayer->getContentSize();

    // Dark Overlay
    auto overlay = CCLayerColor::create({0, 0, 0, 200});
    overlay->setContentSize(winSize);
    overlay->ignoreAnchorPointForPosition(false);
    overlay->setAnchorPoint({0.5f, 0.5f});
    overlay->setPosition(size / 2);
    m_mainLayer->addChild(overlay, -10); // Ensure it's behind everything in mainLayer

    m_contentNode = CCNode::create();
    m_contentNode->setContentSize(size);
    m_contentNode->setPosition({0, 0});
    m_mainLayer->addChild(m_contentNode);

    loadThumbs();
    
    return true;
}

/*
void ThumbnailViewPopup::setupRating() {
    // ... (Removed)
}

void ThumbnailViewPopup::onRate(CCObject* sender) {
    // ... (Removed)
}
*/

static CCSprite* makePlaceholder(float w, float h) {
    auto spr = CCSprite::create("GJ_square01.png");
    spr->setColor({80,80,80});
    spr->setOpacity(160);
    float scale = std::min(w / spr->getContentSize().width, h / spr->getContentSize().height);
    spr->setScale(scale);
    return spr;
}

void ThumbnailViewPopup::loadThumbs() {
    auto size = m_mainLayer->getContentSize();
    
    // Image Area
    float imgWidth = 400.f;
    float imgHeight = 225.f; // 16:9 aspect ratio approx
    float centerX = size.width / 2.f;
    float centerY = size.height / 2.f + 20.f; // Shift up slightly

    // White Border (Rounded)
    auto border = CCScale9Sprite::create("square02_001.png");
    border->setContentSize({imgWidth + 6.f, imgHeight + 6.f});
    border->setPosition({centerX, centerY});
    border->setColor({255, 255, 255});
    border->setOpacity(255);
    m_contentNode->addChild(border);

    // Clipping Node for Image
    auto stencil = CCScale9Sprite::create("square02_001.png");
    stencil->setContentSize({imgWidth, imgHeight});
    stencil->setPosition({imgWidth / 2, imgHeight / 2});
    
    auto clipper = CCClippingNode::create(stencil);
    clipper->setContentSize({imgWidth, imgHeight});
    clipper->setAnchorPoint({0.5f, 0.5f});
    clipper->setPosition({centerX, centerY});
    clipper->setAlphaThreshold(0.05f);
    m_contentNode->addChild(clipper);

    // Placeholder / Image
    auto ph = makePlaceholder(imgWidth, imgHeight);
    ph->setPosition({imgWidth / 2, imgHeight / 2});
    clipper->addChild(ph);
    m_currentSprite = ph;

    // Buttons Menu (Bottom)
    auto menu = CCMenu::create();
    menu->setPosition({centerX, centerY - imgHeight / 2 - 15.f});
    m_mainLayer->addChild(menu);

    // Helper to create circular buttons
    auto createBtn = [&](const char* frame, SEL_MenuHandler selector, float scale = 1.0f) {
        auto spr = CCSprite::createWithSpriteFrameName(frame);
        spr->setScale(scale);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, selector);
        return btn;
    };

    // Report Button
    auto reportBtn = createBtn("GJ_reportBtn_001.png", menu_selector(ThumbnailViewPopup::onReport), 0.8f);
    menu->addChild(reportBtn);

    // Rate Button (Star)
    auto rateBtn = createBtn("GJ_starBtn_001.png", menu_selector(ThumbnailViewPopup::onRate), 0.8f);
    menu->addChild(rateBtn);

    // Download Button
    auto downloadBtn = createBtn("GJ_downloadBtn_001.png", menu_selector(ThumbnailViewPopup::onDownload), 0.8f);
    menu->addChild(downloadBtn);

    menu->alignItemsHorizontallyWithPadding(20.f);

    // Close Button (Red X) - Moved to top right of image
    auto closeMenu = CCMenu::create();
    closeMenu->setPosition({0, 0});
    m_mainLayer->addChild(closeMenu, 20);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.6f); // Smaller
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(ThumbnailViewPopup::onClose));
    closeBtn->setPosition({centerX + imgWidth / 2, centerY + imgHeight / 2});
    closeMenu->addChild(closeBtn);

    // Arrows (if gallery)
    if (m_category != PendingCategory::Update && m_category != PendingCategory::Verify) {
        auto arrowMenu = CCMenu::create();
        arrowMenu->setPosition({0, 0});
        m_mainLayer->addChild(arrowMenu);
        
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(ThumbnailViewPopup::onPrev));
        prevBtn->setPosition({centerX - imgWidth / 2 - 30.f, centerY});
        arrowMenu->addChild(prevBtn);
        
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        nextSpr->setFlipX(true);
        auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(ThumbnailViewPopup::onNext));
        nextBtn->setPosition({centerX + imgWidth / 2 + 30.f, centerY});
        arrowMenu->addChild(nextBtn);

        // Load thumbnails list
        this->retain();
        ThumbnailAPI::get().getThumbnails(m_levelID, [this](bool success, const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbs) {
            if (success && !thumbs.empty()) {
                m_thumbnails = thumbs;
                m_currentIndex = 0;
                this->displayThumbnail(0);
            } else {
                // Fallback to default
                ThumbnailAPI::ThumbnailInfo def;
                def.id = "0";
                def.url = ThumbnailAPI::get().getThumbnailURL(m_levelID);
                m_thumbnails.push_back(def);
                this->displayThumbnail(0);
            }
            this->release();
        });
    }
}

void ThumbnailViewPopup::displayThumbnail(int index) {
    if (index < 0 || index >= m_thumbnails.size()) return;
    
    auto& thumb = m_thumbnails[index];
    
    this->retain();
    ThumbnailAPI::get().downloadFromUrl(thumb.url, [this](bool success, CCTexture2D* tex) {
        if (success && tex && m_currentSprite && m_currentSprite->getParent()) {
            m_currentSprite->setTexture(tex);
            m_currentSprite->setColor({255,255,255});
            m_currentSprite->setOpacity(255);
            m_currentSprite->setTextureRect({0, 0, tex->getContentSize().width, tex->getContentSize().height});
            
            // Scale to cover (Crop) or Fit?
            // Concept shows full image. Let's fit.
            float imgWidth = 400.f;
            float imgHeight = 225.f;
            
            float sW = imgWidth / tex->getContentSize().width;
            float sH = imgHeight / tex->getContentSize().height;
            // Use max to cover, min to fit. Concept looks like cover/fill.
            // But for thumbnails, we probably want to see the whole thing.
            // Let's use max to fill the rounded rect.
            m_currentSprite->setScale(std::max(sW, sH));
            
            // Center it
            m_currentSprite->setPosition({imgWidth / 2, imgHeight / 2});
        }
        this->release();
    });
    
    // Update rating for this thumbnail
    // We need to re-fetch rating
    std::string username = "Unknown";
    if (auto gm = GameManager::sharedState()) username = gm->m_playerName;
    
    this->retain();
    ThumbnailAPI::get().getRating(m_levelID, username, thumb.id, [this](bool success, float average, int count, int userVote) {
        if (success && m_ratingLabel) {
            m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
            m_userVote = userVote;
            
            for (int i = 0; i < 5; i++) {
                if (i < m_stars.size()) {
                    auto star = m_stars[i];
                    if (i < (int)average) {
                        star->setColor({255, 255, 255});
                    } else {
                        star->setColor({100, 100, 100});
                    }
                }
            }
        }
        this->release();
    });
}

void ThumbnailViewPopup::onPrev(CCObject*) {
    if (m_thumbnails.empty()) return;
    m_currentIndex--;
    if (m_currentIndex < 0) m_currentIndex = m_thumbnails.size() - 1;
    displayThumbnail(m_currentIndex);
}

void ThumbnailViewPopup::onNext(CCObject*) {
    if (m_thumbnails.empty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_thumbnails.size();
    displayThumbnail(m_currentIndex);
}

void ThumbnailViewPopup::onClose(CCObject* sender) {
    Popup::onClose(sender);
}

void ThumbnailViewPopup::onReport(CCObject* sender) {
    if (m_thumbnails.empty()) return;
    auto& thumb = m_thumbnails[m_currentIndex];
    
    createQuickPopup(
        "Report Thumbnail",
        "Are you sure you want to report this thumbnail?",
        "Cancel", "Report",
        [this, thumb](auto, bool btn2) {
            if (btn2) {
                std::string username = "Unknown";
                if (auto gm = GameManager::sharedState()) username = gm->m_playerName;
                
                HttpClient::get().submitReport(m_levelID, username, "Reported via View Popup", [this](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create("Report Sent", NotificationIcon::Success)->show();
                    } else {
                        Notification::create("Failed to send report", NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}

void ThumbnailViewPopup::onRate(CCObject* sender) {
    Notification::create("Rating not implemented yet", NotificationIcon::Info)->show();
}

void ThumbnailViewPopup::onDownload(CCObject* sender) {
    if (m_thumbnails.empty()) return;
    auto& thumb = m_thumbnails[m_currentIndex];
    
    Notification::create("Downloading...", NotificationIcon::Info)->show();
    
    // Mock download
    Notification::create("Download started (Mock)", NotificationIcon::Success)->show();
}
