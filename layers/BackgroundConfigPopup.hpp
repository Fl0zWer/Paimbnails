#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/Slider.hpp>

class BackgroundConfigPopup : public geode::Popup<> {
protected:
    geode::TextInput* m_idInput;
    cocos2d::CCLayer* m_menuLayer;
    cocos2d::CCLayer* m_profileLayer;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    int m_selectedTab = 0;
    Slider* m_slider = nullptr;

    bool setup() override;
    
    // UI Helpers
    void createTabs();
    void onTab(cocos2d::CCObject* sender);
    void updateTabs();
    cocos2d::CCNode* createMenuTab();
    cocos2d::CCNode* createProfileTab();

    // Menu Actions
    void onCustomImage(cocos2d::CCObject* sender);
    void onDownloadedThumbnails(cocos2d::CCObject* sender);
    void onSetID(cocos2d::CCObject* sender);
    void onApply(cocos2d::CCObject* sender);
    void onDarkMode(cocos2d::CCObject* sender);
    void onIntensityChanged(cocos2d::CCObject* sender);

    // Profile Actions
    void onProfileCustomImage(cocos2d::CCObject* sender);
    void onProfileClear(cocos2d::CCObject* sender);

    // New Features
    void onDefaultMenu(cocos2d::CCObject* sender);
    void onAdaptiveColors(cocos2d::CCObject* sender);

    // Helper
    CCMenuItemSpriteExtra* createBtn(const char* text, cocos2d::CCPoint pos, cocos2d::SEL_MenuHandler handler, cocos2d::CCNode* parent);

public:
    static BackgroundConfigPopup* create();
};
