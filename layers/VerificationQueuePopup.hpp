#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/DefaultInclude.hpp>
#include "../managers/PendingQueue.hpp"
#include "../utils/ThumbnailTypes.hpp"

namespace cocos2d { namespace extension { class CCScrollView; } }

class VerificationQueuePopup : public geode::Popup<> {
protected:
    PendingCategory m_current = PendingCategory::Verify;
    cocos2d::CCMenu* m_listMenu = nullptr;
    cocos2d::extension::CCScrollView* m_scroll = nullptr;
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    std::vector<PendingItem> m_items; // latest items (server or local fallback)

    int m_pendingLevelID = 0; // Level ID pending to open after download
    int m_downloadCheckCount = 0; // Counter for download check timeout
    
    // Guards for async callbacks and lifecycle
    void onExit() override { 
        this->unschedule(schedule_selector(VerificationQueuePopup::checkLevelDownloaded));
        if (m_listMenu) {
            m_listMenu->removeAllChildren();
        }
        geode::Popup<>::onExit(); 
    }

    bool setup() override;
    void rebuildList();
    void switchTo(PendingCategory cat);

    
    cocos2d::CCSprite* createThumbnailSprite(int levelID);
    cocos2d::CCSprite* createServerThumbnailSprite(int levelID);

    void onTabVerify(cocos2d::CCObject*);
    void onTabUpdate(cocos2d::CCObject*);
    void onTabReport(cocos2d::CCObject*);
    void onTabBanner(cocos2d::CCObject*);
    void onOpenLevel(cocos2d::CCObject* sender);
    void checkLevelDownloaded(float dt); // Scheduled function to check when level is downloaded
    void openStoredLevel(); // Legacy helper (no longer used)
    void onAccept(cocos2d::CCObject* sender);
    void onReject(cocos2d::CCObject* sender);
    void onViewReport(cocos2d::CCObject* sender);
    void onClaimLevel(cocos2d::CCObject* sender);
    void onViewThumb(cocos2d::CCObject* sender);
    void onViewBans(cocos2d::CCObject*);
    void onOpenProfile(cocos2d::CCObject* sender);
    void onViewBanner(cocos2d::CCObject* sender);

public:
    static VerificationQueuePopup* create();
};

