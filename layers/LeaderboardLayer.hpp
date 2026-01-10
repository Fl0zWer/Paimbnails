#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>

using namespace geode::prelude;

class LeaderboardLayer : public CCLayer, public LevelManagerDelegate {
protected:
    bool init() override;
    void keyBackClicked() override;
    
    void onBack(CCObject* sender);
    void onTab(CCObject* sender);
    void loadLeaderboard(std::string type);
    void onLeaderboardLoaded(std::string type, std::string result);
    void createList(CCArray* items, std::string type);
    void onViewLevel(CCObject* sender);
    void fetchLeaderboardList(std::string type);
    
    // LevelManagerDelegate
    void loadLevelsFinished(CCArray* levels, const char* key) override;
    void loadLevelsFailed(const char* key) override;
    void setupPageInfo(std::string, const char*) override;
    
    cocos2d::extension::CCScrollView* m_scroll = nullptr;
    CCLayer* m_listMenu = nullptr;
    CCSprite* m_loadingSpinner = nullptr;
    CCMenu* m_tabsMenu = nullptr;
    std::vector<CCMenuItemToggler*> m_tabs;
    std::string m_currentType = "daily";
    
    // Pagination
    CCArray* m_allItems = nullptr;
    int m_page = 0;
    const int ITEMS_PER_PAGE = 10;
    CCMenu* m_pageMenu = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;

    GJGameLevel* m_featuredLevel = nullptr;
    long long m_featuredExpiresAt = 0;
    
    EventListener<web::WebTask> m_listener;

    CCSprite* m_bgSprite = nullptr;
    CCLayerColor* m_bgOverlay = nullptr;
    float m_blurTime = 0.f;

    void update(float dt) override;
    void updateBackground(int levelID);
    void applyBackground(CCTexture2D* texture);
    
    void refreshList();
    void onNextPage(CCObject*);
    void onPrevPage(CCObject*);
    void onRecalculate(CCObject* sender);
    void onReloadAllTime();
    void fetchGDBrowserLevel(int levelID);

public:
    ~LeaderboardLayer();
    static LeaderboardLayer* create();
    static CCScene* scene();
};
