#include "VerificationQueuePopup.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>
#include <algorithm>
#include "../utils/Localization.hpp"
#include "BanListPopup.hpp"
#include "../utils/AnimatedGIFSprite.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// Forward declaration for creating the zoomable popup
// Defined in LevelInfoLayer.cpp
// Simple popup to view banner
class BannerViewPopup : public geode::Popup<CCNode*> {
protected:
    bool setup(CCNode* content) override {
        this->setTitle("Banner Preview");
        
        if (content) {
            content->setPosition(m_mainLayer->getContentSize() / 2);
            m_mainLayer->addChild(content);
            
            // Limit size if too big
            float maxW = 380.f;
            float maxH = 250.f;
            
            if (content->getContentWidth() > 0 && content->getContentHeight() > 0) {
                float scaleX = maxW / content->getContentWidth();
                float scaleY = maxH / content->getContentHeight();
                float scale = std::min(scaleX, scaleY);
                content->setScale(scale);
            }
        }
        
        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Close"),
            this,
            menu_selector(BannerViewPopup::onClose)
        );
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition({m_mainLayer->getContentSize().width / 2, 25.f});
        m_mainLayer->addChild(menu);
        
        return true;
    }
public:
    static BannerViewPopup* create(CCNode* content) {
        auto ret = new BannerViewPopup();
        if (ret && ret->initAnchored(420.f, 320.f, content)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

extern CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, const std::vector<Suggestion>& suggestions = {});

VerificationQueuePopup* VerificationQueuePopup::create() {
    auto ret = new VerificationQueuePopup();
    if (ret && ret->initAnchored(470.f, 300.f)) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret); return nullptr;
}

bool VerificationQueuePopup::setup() {
    this->setTitle(Localization::get().getString("queue.title"));
    auto content = this->m_mainLayer->getContentSize();

    // Tabs menu
    m_tabsMenu = CCMenu::create();
    m_tabsMenu->setPosition({content.width/2, content.height - 45.f});
    auto mkTab = [&](const char* title, SEL_MenuHandler sel){
        auto spr = ButtonSprite::create(title, 90, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
        spr->setScale(0.8f);
        return CCMenuItemSpriteExtra::create(spr, this, sel);
    };
    auto t1 = mkTab(Localization::get().getString("queue.verify_tab").c_str(), menu_selector(VerificationQueuePopup::onTabVerify));
    auto t2 = mkTab(Localization::get().getString("queue.update_tab").c_str(), menu_selector(VerificationQueuePopup::onTabUpdate));
    auto t3 = mkTab(Localization::get().getString("queue.report_tab").c_str(), menu_selector(VerificationQueuePopup::onTabReport));
    auto t4 = mkTab("Banners", menu_selector(VerificationQueuePopup::onTabBanner));
    t1->setTag((int)PendingCategory::Verify);
    t2->setTag((int)PendingCategory::Update);
    t3->setTag((int)PendingCategory::Report);
    t4->setTag((int)PendingCategory::Banner);
    m_tabsMenu->addChild(t1); m_tabsMenu->addChild(t2); m_tabsMenu->addChild(t3); m_tabsMenu->addChild(t4);

    // Ban list button (opens a popup with the mod ban list)
    // Connected to the tabs menu to avoid overlapping
    {
        auto btnSpr = ButtonSprite::create(Localization::get().getString("queue.banned_btn").c_str(), 80, true, "bigFont.fnt", "GJ_button_05.png", 30.f, 0.6f);
        btnSpr->setScale(0.8f);
        auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(VerificationQueuePopup::onViewBans));
        m_tabsMenu->addChild(btn);
    }

    m_tabsMenu->setLayout(RowLayout::create()->setGap(5.f)->setAxisAlignment(AxisAlignment::Center));
    this->m_mainLayer->addChild(m_tabsMenu);

    // List background and scroll
    auto panel = CCScale9Sprite::create("square02_001.png");
    panel->setColor({0,0,0}); panel->setOpacity(70);
    panel->setContentSize(CCSizeMake(content.width - 20.f, content.height - 95.f));
    panel->setPosition({content.width/2, content.height/2 - 15.f});
    this->m_mainLayer->addChild(panel);

    m_listMenu = CCMenu::create(); m_listMenu->setPosition({0,0});
    m_scroll = cocos2d::extension::CCScrollView::create();
    m_scroll->setViewSize(panel->getContentSize());
    m_scroll->setPosition(panel->getPosition() - panel->getContentSize()/2);
    m_scroll->setDirection(kCCScrollViewDirectionVertical);
    m_scroll->setContainer(m_listMenu);
    this->m_mainLayer->addChild(m_scroll, 5);

    switchTo(PendingCategory::Verify);
    return true;
}

void VerificationQueuePopup::onViewBans(cocos2d::CCObject*) {
    if (auto popup = BanListPopup::create()) {
        popup->show();
    }
}

void VerificationQueuePopup::switchTo(PendingCategory cat) {
    m_current = cat;
    // highlight selected tab (dim others)
    for (auto* n : CCArrayExt<CCNode*>(m_tabsMenu->getChildren())) {
        auto* it = static_cast<CCMenuItemSpriteExtra*>(n);
        bool active = it->getTag() == (int)cat;
        it->setScale(active ? 0.85f : 0.75f);
    }
    
    // Sync with server before rebuilding list
    this->retain();
    ThumbnailAPI::get().syncVerificationQueue(cat, [this, cat](bool success, const std::vector<PendingItem>& items) {
        if (!success) {
            log::warn("[VerificationQueuePopup] Failed to sync queue from server, using local data");
            m_items = PendingQueue::get().list(cat);
        } else {
            m_items = items;
        }
        if (this->getParent()) {
            rebuildList();
        }
        this->release();
    });
}

void VerificationQueuePopup::rebuildList() {
    try {
        m_listMenu->removeAllChildren();
        auto contentSize = m_scroll->getViewSize();
        


        auto const& items = m_items;
        
        // Get current username for claim check
        std::string currentUsername;
        if (auto gm = GameManager::sharedState()) {
            currentUsername = gm->m_playerName;
        }

        if (items.empty()) {
            auto lbl = CCLabelBMFont::create(Localization::get().getString("queue.no_items").c_str(), "goldFont.fnt");
            lbl->setScale(0.5f); lbl->setPosition({contentSize.width/2, contentSize.height/2});
            m_listMenu->addChild(lbl);
            m_listMenu->setContentSize(contentSize);
            return;
        }
        float rowH = 50.f;
        float totalH = rowH * items.size() + 20.f;
        m_listMenu->setContentSize(CCSizeMake(contentSize.width, std::max(contentSize.height, totalH)));
        float left = 14.f; float right = contentSize.width - 80.f;
        for (size_t i=0;i<items.size();++i) {
            auto const& r = items[i];
            float y = m_listMenu->getContentSize().height - 30.f - (float)i * rowH;
            auto rowBg = CCScale9Sprite::create("square02_001.png");
            rowBg->setColor({0,0,0}); rowBg->setOpacity(60);
            rowBg->setContentSize(CCSizeMake(contentSize.width - 39.f, 44.f));
            rowBg->setAnchorPoint({0,0.5f}); rowBg->setPosition({left - 5.f, y});
            m_listMenu->addChild(rowBg);

            // ID Label
            std::string idText = (m_current == PendingCategory::Banner) 
                ? fmt::format("Account ID: {}", r.levelID)
                : fmt::format(fmt::runtime(Localization::get().getString("queue.level_id")), r.levelID);
            auto idLbl = CCLabelBMFont::create(idText.c_str(), "goldFont.fnt");
            idLbl->setScale(0.45f); idLbl->setAnchorPoint({0,0.5f}); idLbl->setPosition({left, y + 2.f});
            m_listMenu->addChild(idLbl);
            
            // Claim status logic
            bool isClaimed = !r.claimedBy.empty();
            bool claimedByMe = isClaimed && (r.claimedBy == currentUsername);
            bool canInteract = claimedByMe; // Only allow interaction if claimed by current user

            // Show claimer name if claimed
            if (isClaimed) {
                std::string claimText = claimedByMe ? Localization::get().getString("queue.claimed_by_you") : fmt::format(fmt::runtime(Localization::get().getString("queue.claimed_by_user")), r.claimedBy);
                auto claimLbl = CCLabelBMFont::create(claimText.c_str(), "chatFont.fnt");
                claimLbl->setScale(0.4f);
                claimLbl->setColor(claimedByMe ? ccColor3B{100, 255, 100} : ccColor3B{255, 100, 100});
                claimLbl->setAnchorPoint({0, 0.5f});
                claimLbl->setPosition({left + 90.f, y + 2.f});
                m_listMenu->addChild(claimLbl);
            }

            // Replace inline thumbnails with a View button popup
            // Reserve space (optional visual marker could be added later)

            auto btnMenu = CCMenu::create(); btnMenu->setPosition({0,0});
            
            // Claim button (left side) - marks that a moderator is reviewing
            // If claimed by me, show green. If claimed by other, disable. If not claimed, show default.
            const char* claimBtnImg = claimedByMe ? "GJ_button_02.png" : "GJ_button_04.png";
            auto claimSpr = ButtonSprite::create("✋", 35, true, "bigFont.fnt", claimBtnImg, 30.f, 0.6f);
            claimSpr->setScale(0.45f);
            auto claimBtn = CCMenuItemSpriteExtra::create(claimSpr, this, menu_selector(VerificationQueuePopup::onClaimLevel));
            claimBtn->setTag(r.levelID);
            claimBtn->setID(fmt::format("claim-btn-{}", r.levelID));
            
            if (isClaimed && !claimedByMe) {
                claimBtn->setEnabled(false);
                claimSpr->setColor({100, 100, 100});
            }
            
            PaimonButtonHighlighter::registerButton(claimBtn);
            btnMenu->addChild(claimBtn);
            
            // Helper to gray out buttons
            auto setupBtn = [&](CCMenuItemSpriteExtra* btn, ButtonSprite* spr) {
                if (!canInteract) {
                    btn->setEnabled(false);
                    spr->setColor({100, 100, 100});
                    spr->setOpacity(150);
                }
                PaimonButtonHighlighter::registerButton(btn);
                btnMenu->addChild(btn);
            };

            // Open
            auto openSpr = ButtonSprite::create(Localization::get().getString("queue.open_button").c_str(), 70, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f); openSpr->setScale(0.45f);
            auto openBtn = CCMenuItemSpriteExtra::create(openSpr, this, menu_selector(VerificationQueuePopup::onOpenLevel)); openBtn->setTag(r.levelID);
            if (m_current == PendingCategory::Banner) {
                 openBtn->setTarget(this, menu_selector(VerificationQueuePopup::onOpenProfile));
            }
            setupBtn(openBtn, openSpr);

            // View thumbnail button
            auto viewTitle = Localization::get().getString("queue.view_btn");
            if (viewTitle.empty()) viewTitle = "Ver"; // fallback
            auto viewSpr = ButtonSprite::create(viewTitle.c_str(), 90, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f); viewSpr->setScale(0.45f);
            auto viewBtn = CCMenuItemSpriteExtra::create(viewSpr, this, menu_selector(VerificationQueuePopup::onViewThumb)); viewBtn->setTag(r.levelID);
            if (m_current == PendingCategory::Banner) {
                 viewBtn->setTarget(this, menu_selector(VerificationQueuePopup::onViewBanner));
            }
            setupBtn(viewBtn, viewSpr);

            // Accept only for verify/update/banner
            if (m_current != PendingCategory::Report) {
                auto accSpr = ButtonSprite::create(Localization::get().getString("queue.accept_button").c_str(), 70, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f); accSpr->setScale(0.45f);
                auto accBtn = CCMenuItemSpriteExtra::create(accSpr, this, menu_selector(VerificationQueuePopup::onAccept)); accBtn->setTag(r.levelID);
                setupBtn(accBtn, accSpr);
            }
            // When in Report tab, add a button to view the report text
            if (m_current == PendingCategory::Report) {
                auto viewSpr = ButtonSprite::create(Localization::get().getString("queue.view_report").c_str(), 90, true, "bigFont.fnt", "GJ_button_05.png", 30.f, 0.6f);
                viewSpr->setScale(0.45f);
                auto viewBtn = CCMenuItemSpriteExtra::create(viewSpr, this, menu_selector(VerificationQueuePopup::onViewReport));
                viewBtn->setTag(r.levelID);
                // Attach the note string so handler can read it quickly
                // Use CCString::createWithFormat to ensure a distinct object; setUserObject retains
                viewBtn->setUserObject(CCString::createWithFormat("%s", r.note.c_str()));
                setupBtn(viewBtn, viewSpr);
            }
            // Reject for all
            auto rejSpr = ButtonSprite::create(Localization::get().getString("queue.reject_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f); rejSpr->setScale(0.45f);
            auto rejBtn = CCMenuItemSpriteExtra::create(rejSpr, this, menu_selector(VerificationQueuePopup::onReject)); rejBtn->setTag(r.levelID);
            // Encode category into tag2 by storing integer in user data
            rejBtn->setUserData(reinterpret_cast<void*>(static_cast<uintptr_t>(m_current)));
            setupBtn(rejBtn, rejSpr);

            btnMenu->alignItemsHorizontallyWithPadding(8.f);
            btnMenu->setPosition({right - 55.f, y});
            m_listMenu->addChild(btnMenu);
        }
    } catch (const std::exception& e) {
        log::error("Exception in VerificationQueuePopup::rebuildList: {}", e.what());
    } catch (...) {
        log::error("Unknown exception in VerificationQueuePopup::rebuildList");
    }
}

CCSprite* VerificationQueuePopup::createThumbnailSprite(int levelID) {
    // Load thumbnail according to the current category (e.g. suggestions vs updates)
    
    std::string subdir = (m_current == PendingCategory::Update) ? "updates" : "sugeridos";
    auto cachedPath = geode::Mod::get()->getSaveDir() / "thumbnails" / subdir / fmt::format("{}.webp", levelID);
    
    if (std::filesystem::exists(cachedPath)) {
        auto texture = CCTextureCache::sharedTextureCache()->addImage(cachedPath.generic_string().c_str(), false);
        if (texture) {
            return CCSprite::createWithTexture(texture);
        }
    }
    
    // If not in cache, download from server asynchronously
    auto placeholder = CCSprite::create("GJ_square01.png");
    placeholder->setColor({100, 100, 100});
    placeholder->setOpacity(150);
    
    // Async download suggestion or update
    this->retain();
    if (m_current == PendingCategory::Update) {
        ThumbnailAPI::get().downloadUpdate(levelID, [this, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (success && texture && placeholder->getParent()) {
                placeholder->setTexture(texture);
                placeholder->setColor({255, 255, 255});
                placeholder->setOpacity(255);
            }
            this->release();
        });
    } else {
        ThumbnailAPI::get().downloadSuggestion(levelID, [this, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (success && texture && placeholder->getParent()) {
                placeholder->setTexture(texture);
                placeholder->setColor({255, 255, 255});
                placeholder->setOpacity(255);
            }
            this->release();
        });
    }
    
    return placeholder;
}

CCSprite* VerificationQueuePopup::createServerThumbnailSprite(int levelID) {
    // Load current official thumbnail (for updates and reports)
    // First check local cache
    auto localTex = LocalThumbs::get().loadTexture(levelID);
    if (localTex) {
        return CCSprite::createWithTexture(localTex);
    }
    
    // Try cache directory
    auto cacheDir = geode::Mod::get()->getSaveDir() / "cache";
    std::vector<std::string> extensions = {".webp", ".png", ".gif"};
    
    for (const auto& ext : extensions) {
        auto cachePath = cacheDir / (fmt::format("{}", levelID) + ext);
        if (std::filesystem::exists(cachePath)) {
            auto texture = CCTextureCache::sharedTextureCache()->addImage(cachePath.generic_string().c_str(), false);
            if (texture) {
                return CCSprite::createWithTexture(texture);
            }
        }
    }
    
    // If not in cache, download asynchronously
    auto placeholder = CCSprite::create("GJ_square01.png");
    placeholder->setColor({80, 80, 80});
    placeholder->setOpacity(150);
    
    // For Report category, use downloadReported; for Update, use regular download
    this->retain();
    if (m_current == PendingCategory::Report) {
        ThumbnailAPI::get().downloadReported(levelID, [this, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (success && texture && placeholder->getParent()) {
                placeholder->setTexture(texture);
                placeholder->setColor({255, 255, 255});
                placeholder->setOpacity(255);
            }
            this->release();
        });
    } else {
        ThumbnailAPI::get().downloadThumbnail(levelID, [this, levelID, placeholder](bool success, CCTexture2D* texture) {
            if (success && texture && placeholder->getParent()) {
                placeholder->setTexture(texture);
                placeholder->setColor({255, 255, 255});
                placeholder->setOpacity(255);
            }
            this->release();
        });
    }
    
    return placeholder;
}

void VerificationQueuePopup::onTabVerify(CCObject*) { switchTo(PendingCategory::Verify); }
void VerificationQueuePopup::onTabUpdate(CCObject*) { switchTo(PendingCategory::Update); }
void VerificationQueuePopup::onTabReport(CCObject*) { switchTo(PendingCategory::Report); }
void VerificationQueuePopup::onTabBanner(CCObject*) { switchTo(PendingCategory::Banner); }


void VerificationQueuePopup::onOpenLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // Mark flags before opening the level
    Mod::get()->setSavedValue("open-from-thumbs", true);
    Mod::get()->setSavedValue("open-from-report", m_current == PendingCategory::Report);
    Mod::get()->setSavedValue("open-from-verification-queue", true);
    Mod::get()->setSavedValue("verification-queue-category", static_cast<int>(m_current));
    Mod::get()->setSavedValue("verification-queue-levelid", lvl);
    
    // Try to get the level from cache first
    auto glm = GameLevelManager::sharedState();
    GJGameLevel* level = nullptr;
    
    // Buscar en niveles online descargados previamente
    auto onlineLevels = glm->m_onlineLevels;
    if (onlineLevels) {
        level = static_cast<GJGameLevel*>(onlineLevels->objectForKey(std::to_string(lvl)));
    }
    
    if (level && !level->m_levelName.empty()) {
        // We have the level with complete info in cache
        log::info("[VerificationQueuePopup] Opening level {} from cache: {}", lvl, level->m_levelName);
        
        CCDirector::sharedDirector()->replaceScene(
            CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false))
        );
    } else {
        // Not in cache - download first via GameLevelManager
        log::info("[VerificationQueuePopup] Downloading level {} before opening...", lvl);
        
        Notification::create("Downloading level...", NotificationIcon::Loading)->show();
        
        m_downloadCheckCount = 0; // Reset contador
        this->retain();
        glm->downloadLevel(lvl, false);
        
        // Use schedule to check when the level is ready
        this->schedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded), 0.1f);
    }
}

void VerificationQueuePopup::checkLevelDownloaded(float dt) {
    // Check if the level has been downloaded
    int lvl = -1;
    try {
        lvl = Mod::get()->getSavedValue<int>("verification-queue-levelid", -1);
    } catch (...) {
        log::error("[VerificationQueuePopup] Error obteniendo verification-queue-levelid");
    }
    
    if (lvl <= 0) {
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
        return;
    }
    
    // Timeout after 50 attempts (5 seconds)
    m_downloadCheckCount++;
    if (m_downloadCheckCount > 50) {
        log::warn("[VerificationQueuePopup] Timeout esperando descarga del nivel {}", lvl);
        Notification::create("Error: timed out downloading level", NotificationIcon::Error)->show();
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
        return;
    }
    
    try {
        auto glm = GameLevelManager::sharedState();
        if (!glm) {
            log::error("[VerificationQueuePopup] GameLevelManager es nulo");
            return;
        }
        
        auto onlineLevels = glm->m_onlineLevels;
        if (!onlineLevels) {
            return; // Seguir esperando
        }
        
        auto level = static_cast<GJGameLevel*>(onlineLevels->objectForKey(std::to_string(lvl)));
        
        if (!level) {
            return; // Seguir esperando
        }
        
        // Check that the nivel tenga al menos the nombre
        // Not Check m_creatorName porque puede causar access violation
        try {
            std::string levelName = level->m_levelName;
            if (levelName.empty()) {
                return; // Seguir esperando
            }
            
            // Nivel descargado exitosamente
            log::info("[VerificationQueuePopup] Nivel {} descargado: {}", lvl, levelName);
            
            this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
            m_downloadCheckCount = 0;
            
            // Abrir el nivel
            CCDirector::sharedDirector()->replaceScene(
                CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false))
            );
            
            this->release();
        } catch (const std::exception& e) {
            log::error("[VerificationQueuePopup] Error accediendo a propiedades del nivel: {}", e.what());
            return;
        } catch (...) {
            log::error("[VerificationQueuePopup] Error desconocido accediendo a propiedades del nivel");
            return;
        }
    } catch (const std::exception& e) {
        log::error("[VerificationQueuePopup] Exception en checkLevelDownloaded: {}", e.what());
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
    } catch (...) {
        log::error("[VerificationQueuePopup] Unknown exception en checkLevelDownloaded");
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        this->release();
    }
}

void VerificationQueuePopup::onAccept(CCObject* sender) {


    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // Get username
    std::string username;
    int accountID = 0;
    try {
        if (auto gm = GameManager::sharedState()) {
            username = gm->m_playerName;
            accountID = gm->m_playerUserID;
        }
    } catch(...) {}

    if (accountID <= 0) {
        Notification::create("Tienes que tener cuenta para subir", NotificationIcon::Error)->show();
        return;
    }

    // Always verify moderator status online before accepting
    Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
    this->retain();
    ThumbnailAPI::get().checkModeratorAccount(username, accountID, [this, lvl, username](bool isMod, bool isAdmin){
        if (!(isMod || isAdmin)) {
            Notification::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            log::warn("[VerificationQueuePopup] Usuario '{}' no es moderador - bloqueo de aceptación", username);
            this->release();
            return;
        }
        Notification::create(Localization::get().getString("queue.accepting").c_str(), NotificationIcon::Info)->show();
        ThumbnailAPI::get().acceptQueueItem(lvl, m_current, username, [this, lvl](bool success, const std::string& message) {
            if (success) {
                log::info("Item aceptado en servidor para nivel {}", lvl);
                Notification::create(Localization::get().getString("queue.accepted").c_str(), NotificationIcon::Success)->show();
                if (this->getParent()) switchTo(m_current);
            } else {
                Notification::create(Localization::get().getString("queue.accept_error").c_str(), NotificationIcon::Error)->show();
            }
            this->release();
        });
    });
}

void VerificationQueuePopup::onReject(CCObject* sender) {


    int lvl = static_cast<CCNode*>(sender)->getTag();
    int catInt = static_cast<int>(reinterpret_cast<uintptr_t>(static_cast<CCNode*>(sender)->getUserData()));
    auto cat = static_cast<PendingCategory>(catInt);
    
    // Get username
    std::string username;
    try {
        if (auto gm = GameManager::sharedState()) {
            username = gm->m_playerName;
        }
    } catch(...) {}
    
    Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
    this->retain();
    ThumbnailAPI::get().checkModerator(username, [this, lvl, cat, username](bool isMod, bool isAdmin){
        if (!(isMod || isAdmin)) {
            Notification::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            log::warn("[VerificationQueuePopup] Usuario '{}' no es moderador - bloqueo de rechazo", username);
            this->release();
            return;
        }
        Notification::create(Localization::get().getString("queue.rejecting").c_str(), NotificationIcon::Info)->show();
        ThumbnailAPI::get().rejectQueueItem(lvl, cat, username, "Rechazado por moderador", [this](bool success, const std::string& message) {
            if (success) {
                Notification::create(Localization::get().getString("queue.rejected").c_str(), NotificationIcon::Warning)->show();
                if (this->getParent()) switchTo(m_current);
            } else {
                Notification::create(Localization::get().getString("queue.reject_error").c_str(), NotificationIcon::Error)->show();
            }
            this->release();
        });
    });
}

void VerificationQueuePopup::onClaimLevel(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // Get username
    std::string username;
    try {
        if (auto gm = GameManager::sharedState()) {
            username = gm->m_playerName;
        }
    } catch(...) {}
    
    // Change button color immediately
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (auto btnSprite = dynamic_cast<ButtonSprite*>(btn->getNormalImage())) {
        btnSprite->updateBGImage("GJ_button_01.png"); // switch to green
    }
    
    Notification::create(Localization::get().getString("queue.claiming").c_str(), NotificationIcon::Info)->show();
    this->retain();
    
    ThumbnailAPI::get().claimQueueItem(lvl, m_current, username, [this, lvl, btn, username](bool success, const std::string& message) {
        if (success) {
            log::info("Level {} claimed by moderator", lvl);
            Notification::create(Localization::get().getString("queue.claimed").c_str(), NotificationIcon::Success)->show();
            
            // Update local item state
            for (auto& item : m_items) {
                if (item.levelID == lvl) {
                    item.claimedBy = username;
                    break;
                }
            }
            
            // Refresh UI to unlock buttons
            if (this->getParent()) {
                rebuildList();
            }
        } else {
            log::error("Error claiming level {}: {}", lvl, message);
            Notification::create(fmt::format(fmt::runtime(Localization::get().getString("queue.claim_error")), message).c_str(), NotificationIcon::Error)->show();
            // Revert button color
            if (auto btnSprite = dynamic_cast<ButtonSprite*>(btn->getNormalImage())) {
                btnSprite->updateBGImage("GJ_button_04.png"); // revert to gray
            }
        }
        this->release();
    });
}

void VerificationQueuePopup::onViewReport(CCObject* sender) {
    int lvl = static_cast<CCNode*>(sender)->getTag();
    std::string note;
    if (auto obj = static_cast<CCNode*>(sender)->getUserObject()) {
        if (auto s = typeinfo_cast<CCString*>(obj)) note = s->getCString();
    }
    if (note.empty()) {
        // Fallback: query current items list (server-synced if available)
        for (auto const& it : m_items) {
            if (it.levelID == lvl) { note = it.note; break; }
        }
    }
    if (note.empty()) note = "";
    FLAlertLayer::create(Localization::get().getString("queue.report_reason").c_str(), note.c_str(), Localization::get().getString("general.close").c_str())->show();
}

void VerificationQueuePopup::onViewThumb(CCObject* sender) {


    int lvl = static_cast<CCNode*>(sender)->getTag();
    
    // Determine whether the thumbnail can be accepted (only in verify/update mode, not reports)
    bool canAccept = (m_current == PendingCategory::Verify || m_current == PendingCategory::Update);
    
    // Store verification category and whether we came from reports
    Mod::get()->setSavedValue("from-report-popup", m_current == PendingCategory::Report);
    Mod::get()->setSavedValue("verification-category", static_cast<int>(m_current));
    
    // Find item to get suggestions
    std::vector<Suggestion> suggestions;
    for (const auto& item : m_items) {
        if (item.levelID == lvl) {
            suggestions = item.suggestions;
            break;
        }
    }

    // Use the new zoom-enabled version
    auto pop = createThumbnailViewPopup(lvl, canAccept, suggestions);
    if (pop) {
        // Show the popup (it's an FLAlertLayer)
        if (auto alertLayer = dynamic_cast<FLAlertLayer*>(pop)) {
            alertLayer->show();
        }
    } else {
        log::error("[VerificationQueuePopup] Failed to create ThumbnailViewPopup for level {}", lvl);
        Notification::create(Localization::get().getString("queue.cant_open").c_str(), NotificationIcon::Error)->show();
    }
}

void VerificationQueuePopup::onOpenProfile(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();
    ProfilePage::create(accountID, false)->show();
}

void VerificationQueuePopup::onViewBanner(CCObject* sender) {
    int accountID = static_cast<CCNode*>(sender)->getTag();
    
    auto loading = Notification::create("Loading banner...", NotificationIcon::Loading);
    loading->show();
    
    this->retain();
    // Assuming pending banners are stored in suggestions with accountID as ID
    ThumbnailAPI::get().downloadSuggestion(accountID, [this, loading](bool success, CCTexture2D* texture) {
        loading->hide();
        if (success && texture) {
            auto sprite = CCSprite::createWithTexture(texture);
            BannerViewPopup::create(sprite)->show();
        } else {
            Notification::create("Failed to load banner", NotificationIcon::Error)->show();
        }
        this->release();
    });
}


