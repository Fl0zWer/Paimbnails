#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/Slider.hpp>

using namespace geode::prelude;

class BackgroundConfigPopup : public Popup<> {
protected:
    TextInput* m_idInput;
    CCLayer* m_menuLayer;
    CCLayer* m_profileLayer;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    int m_selectedTab = 0;
    Slider* m_slider = nullptr;

    bool setup() override;
    
    // UI Helpers
    void createTabs();
    void onTab(CCObject* sender);
    void updateTabs();
    CCNode* createMenuTab();
    CCNode* createProfileTab();

    // Menu Actions
    void onCustomImage(CCObject* sender);
    void onDownloadedThumbnails(CCObject* sender);
    void onSetID(CCObject* sender);
    void onApply(CCObject* sender);
    void onDarkMode(CCObject* sender);
    void onIntensityChanged(CCObject* sender);

    // Profile Actions
    void onProfileCustomImage(CCObject* sender);
    void onProfileClear(CCObject* sender);

    // New Features
    void onDefaultMenu(CCObject* sender);
    void onAdaptiveColors(CCObject* sender);

    // Helper
    CCMenuItemSpriteExtra* createBtn(const char* text, CCPoint pos, SEL_MenuHandler handler, CCNode* parent);

public:
    static BackgroundConfigPopup* create();
};
