#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>

class LeaderboardLayer : public cocos2d::CCLayer, public LevelManagerDelegate {
protected:
    bool init() override;
    void keyBackClicked() override;
    
    void onBack(cocos2d::CCObject* sender);
    void onTab(cocos2d::CCObject* sender);
    void loadLeaderboard(std::string type);
    void onLeaderboardLoaded(std::string type, std::string result);
    void createList(cocos2d::CCArray* items, std::string type);
    void onViewLevel(cocos2d::CCObject* sender);
    void fetchLeaderboardList(std::string type);
    
    // LevelManagerDelegate
    void loadLevelsFinished(cocos2d::CCArray* levels, const char* key) override;
    void loadLevelsFailed(const char* key) override;
    void setupPageInfo(std::string, const char*) override;
    
    cocos2d::extension::CCScrollView* m_scroll = nullptr;
    cocos2d::CCLayer* m_listMenu = nullptr;
    cocos2d::CCSprite* m_loadingSpinner = nullptr;
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    std::vector<CCMenuItemToggler*> m_tabs;
    std::string m_currentType = "daily";
    
    // Pagination
    cocos2d::CCArray* m_allItems = nullptr;
    int m_page = 0;
    const int ITEMS_PER_PAGE = 10;
    cocos2d::CCMenu* m_pageMenu = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;

    GJGameLevel* m_featuredLevel = nullptr;
    long long m_featuredExpiresAt = 0;
    
    geode::EventListener<geode::utils::web::WebTask> m_listener;

    cocos2d::CCSprite* m_bgSprite = nullptr;
    cocos2d::CCLayerColor* m_bgOverlay = nullptr;
    float m_blurTime = 0.f;

    void update(float dt) override;
    void updateBackground(int levelID);
    void applyBackground(cocos2d::CCTexture2D* texture);
    
    void refreshList();
    void onNextPage(cocos2d::CCObject*);
    void onPrevPage(cocos2d::CCObject*);
    void onRecalculate(cocos2d::CCObject* sender);
    void onReloadAllTime();
    void fetchGDBrowserLevel(int levelID);

public:
    ~LeaderboardLayer();
    static LeaderboardLayer* create();
    static cocos2d::CCScene* scene();
};
