#include "BackgroundConfigPopup.hpp"
#include "../managers/LocalThumbs.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <filesystem>

using namespace geode::prelude;

BackgroundConfigPopup* BackgroundConfigPopup::create() {
    auto ret = new BackgroundConfigPopup();
    if (ret && ret->initAnchored(420, 290)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BackgroundConfigPopup::setup() {
    this->setTitle("Configuration");

    auto center = m_mainLayer->getContentSize() / 2;

    // Create Layers
    m_menuLayer = CCLayer::create();
    m_menuLayer->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(m_menuLayer);

    m_profileLayer = CCLayer::create();
    m_profileLayer->setContentSize(m_mainLayer->getContentSize());
    m_profileLayer->setVisible(false);
    m_mainLayer->addChild(m_profileLayer);

    // Build Content
    m_menuLayer->addChild(this->createMenuTab());
    m_profileLayer->addChild(this->createProfileTab());

    // Create Tabs
    this->createTabs();

    return true;
}

void BackgroundConfigPopup::createTabs() {
    auto winSize = m_mainLayer->getContentSize();
    float topY = winSize.height - 40.f; // Below Title
    float centerX = winSize.width / 2;
    float spacing = 100.f; 

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu);

    // Tab 1: Menu
    auto spr1 = ButtonSprite::create("Menu Config");
    spr1->setScale(0.6f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(BackgroundConfigPopup::onTab));
    tab1->setTag(0);
    tab1->setPosition({centerX - spacing, topY});
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    // Tab 2: Profile
    auto spr2 = ButtonSprite::create("Profile Config");
    spr2->setScale(0.6f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(BackgroundConfigPopup::onTab));
    tab2->setTag(1);
    tab2->setPosition({centerX + spacing, topY});
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    // Initial Update
    this->onTab(tab1);
}

void BackgroundConfigPopup::onTab(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    int tag = btn->getTag();
    m_selectedTab = tag;
    
    // Switch Visibility
    m_menuLayer->setVisible(tag == 0);
    m_profileLayer->setVisible(tag == 1);

    // Update Visuals manually
    for (auto tab : m_tabs) {
        auto spr = static_cast<ButtonSprite*>(tab->getNormalImage());
        if (tab->getTag() == tag) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
            tab->setEnabled(false); // Disable clicking selected
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(150);
            tab->setEnabled(true);
        }
    }
}

CCNode* BackgroundConfigPopup::createMenuTab() {
    auto node = CCNode::create();
    auto size = m_mainLayer->getContentSize();
    
    // --- Layout ---
    float centerY = size.height / 2;
    float centerX = size.width / 2;

    // 1. Source Section
    auto bgSection = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    bgSection->setContentSize({380, 110});
    bgSection->setColor({0, 0, 0});
    bgSection->setOpacity(60);
    bgSection->setPosition({centerX, centerY + 20});
    node->addChild(bgSection);

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    node->addChild(btnMenu);

    // Source Buttons
    createBtn("Custom Image", {centerX - 90, centerY + 40}, menu_selector(BackgroundConfigPopup::onCustomImage), btnMenu);
    createBtn("Random Levels", {centerX + 90, centerY + 40}, menu_selector(BackgroundConfigPopup::onDownloadedThumbnails), btnMenu);



    // ID Input Row
    auto inputBg = cocos2d::extension::CCScale9Sprite::create("square02_001.png");
    inputBg->setContentSize({100, 30});
    inputBg->setOpacity(100);
    inputBg->setPosition({centerX - 40, centerY - 10});
    node->addChild(inputBg);

    m_idInput = TextInput::create(90, "Level ID");
    m_idInput->setPosition({centerX - 40, centerY - 10});
    m_idInput->setFilter("0123456789");
    m_idInput->setScale(0.8f);
    node->addChild(m_idInput);
    
    createBtn("Set ID", {centerX + 50, centerY - 10}, menu_selector(BackgroundConfigPopup::onSetID), btnMenu);

    // 2. Options Section
    float optionsY = centerY - 70;
    
    // Dark Mode
    bool darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
    auto darkToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(BackgroundConfigPopup::onDarkMode), 0.7f);
    darkToggle->toggle(darkMode);
    darkToggle->setPosition({centerX - 100, optionsY});
    btnMenu->addChild(darkToggle);

    auto darkLbl = CCLabelBMFont::create("Dark Mode", "bigFont.fnt");
    darkLbl->setScale(0.4f);
    darkLbl->setPosition({centerX - 100, optionsY + 25});
    node->addChild(darkLbl);

    // Adaptive Colors (New)
    bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
    auto adaptToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(BackgroundConfigPopup::onAdaptiveColors), 0.7f);
    adaptToggle->toggle(adaptive);
    adaptToggle->setPosition({centerX, optionsY});
    btnMenu->addChild(adaptToggle);

    auto adaptLbl = CCLabelBMFont::create("Adaptive Colors", "bigFont.fnt");
    adaptLbl->setScale(0.4f);
    adaptLbl->setPosition({centerX, optionsY + 25});
    node->addChild(adaptLbl);

    // Intensity
    m_slider = Slider::create(this, menu_selector(BackgroundConfigPopup::onIntensityChanged), 0.6f);
    m_slider->setPosition({centerX + 100, optionsY}); // Shifted right
    float intensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
    m_slider->setValue(intensity);
    node->addChild(m_slider); 

    auto intLbl = CCLabelBMFont::create("Intensity", "bigFont.fnt");
    intLbl->setScale(0.4f);
    intLbl->setPosition({centerX + 100, optionsY + 25});
    node->addChild(intLbl);

    // Apply Button (Bottom)
    auto applySpr = ButtonSprite::create("Apply Changes", "goldFont.fnt", "GJ_button_01.png", .8f);
    auto applyBtn = CCMenuItemSpriteExtra::create(applySpr, this, menu_selector(BackgroundConfigPopup::onApply));
    applyBtn->setPosition({centerX - 60, 30}); // Move left slightly to make room
    btnMenu->addChild(applyBtn);

    // Default Menu Button (Moved)
    auto defSpr = ButtonSprite::create("Default", "goldFont.fnt", "GJ_button_04.png", .7f);
    defSpr->setScale(0.7f);
    auto defBtn = CCMenuItemSpriteExtra::create(defSpr, this, menu_selector(BackgroundConfigPopup::onDefaultMenu));
    defBtn->setPosition({centerX + 60, 30}); // Move to right of Apply
    btnMenu->addChild(defBtn);

    return node;
}

void BackgroundConfigPopup::onDefaultMenu(CCObject*) {
    Mod::get()->setSavedValue("bg-type", std::string("default"));
    Mod::get()->setSavedValue("bg-custom-path", std::string(""));
    Mod::get()->setSavedValue("bg-id", 0);
    (void)Mod::get()->saveData();
    Notification::create("Menu Reverted to Default", NotificationIcon::Success)->show();
}

void BackgroundConfigPopup::onAdaptiveColors(CCObject* sender) {
    auto toggle = static_cast<CCMenuItemToggler*>(sender);
    Mod::get()->setSavedValue("bg-adaptive-colors", !toggle->isToggled());
    (void)Mod::get()->saveData();
}

CCNode* BackgroundConfigPopup::createProfileTab() {
    auto node = CCNode::create();
    auto size = m_mainLayer->getContentSize();
    float centerX = size.width / 2;
    float centerY = size.height / 2;

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    node->addChild(btnMenu);

    // Info Text
    auto info = CCLabelBMFont::create("Set a custom background for your Profile.\nSupports Images and GIFs.", "chatFont.fnt");
    info->setAlignment(kCCTextAlignmentCenter);
    info->setScale(0.7f);
    info->setColor({200, 200, 200});
    info->setPosition({centerX, centerY + 50});
    node->addChild(info);

    // Buttons
    createBtn("Select Image / GIF", {centerX, centerY}, menu_selector(BackgroundConfigPopup::onProfileCustomImage), btnMenu);
    
    auto clearSpr = ButtonSprite::create("Clear Background", "goldFont.fnt", "GJ_button_05.png", .8f); // Red button
    clearSpr->setScale(0.6f);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(BackgroundConfigPopup::onProfileClear));
    clearBtn->setPosition({centerX, centerY - 50});
    btnMenu->addChild(clearBtn);

    return node;
}

CCMenuItemSpriteExtra* BackgroundConfigPopup::createBtn(const char* text, CCPoint pos, SEL_MenuHandler handler, CCNode* parent) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(0.6f);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
    btn->setPosition(pos);
    parent->addChild(btn);
    return btn;
}

// --- Menu Actions ---

void BackgroundConfigPopup::onCustomImage(CCObject*) {
    file::FilePickOptions options;
    options.defaultPath = Mod::get()->getSaveDir() / "cache";
    options.filters = { { "Images & GIFs", { "*.png", "*.jpg", "*.jpeg", "*.gif" } } };
    file::pick(file::PickMode::OpenFile, options).listen([this](Result<std::filesystem::path>* result) {
        if (result && result->isOk()) {
            auto path = result->unwrap();
            if (!path.empty()) {
                auto pathStr = path.generic_string();
                std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                
                Mod::get()->setSavedValue<std::string>("bg-type", "custom");
                Mod::get()->setSavedValue<std::string>("bg-custom-path", pathStr);
                auto res = Mod::get()->saveData();
                if (res.isErr()) {
                    Notification::create("Failed to save settings!", NotificationIcon::Error)->show();
                } else {
                    Notification::create("Custom Menu Image Set", NotificationIcon::Success)->show();
                }
            }
        }
    });
}

void BackgroundConfigPopup::onDownloadedThumbnails(CCObject*) {
    Mod::get()->setSavedValue("bg-type", std::string("thumbnails"));
    (void)Mod::get()->saveData();
    Notification::create("Menu set to Random", NotificationIcon::Success)->show();
}

void BackgroundConfigPopup::onSetID(CCObject*) {
    std::string idStr = m_idInput->getString();
    if (idStr.empty()) return;

    if (auto res = geode::utils::numFromString<int>(idStr)) {
        int id = res.unwrap();
        Mod::get()->setSavedValue("bg-type", std::string("id"));
        Mod::get()->setSavedValue("bg-id", id);
        (void)Mod::get()->saveData();
        Notification::create("Menu ID Set", NotificationIcon::Success)->show();
    } else {
        Notification::create("Invalid ID", NotificationIcon::Error)->show();
    }
}

void BackgroundConfigPopup::onDarkMode(CCObject* sender) {
    auto toggle = static_cast<CCMenuItemToggler*>(sender);
    Mod::get()->setSavedValue("bg-dark-mode", !toggle->isToggled());
    (void)Mod::get()->saveData();
}

void BackgroundConfigPopup::onIntensityChanged(CCObject* sender) {
    if (m_slider) {
        Mod::get()->setSavedValue("bg-dark-intensity", m_slider->getValue());
        (void)Mod::get()->saveData();
    }
}

void BackgroundConfigPopup::onApply(CCObject*) {
    CCDirector::sharedDirector()->replaceScene(MenuLayer::scene(false));
    this->onClose(nullptr);
}

// --- Profile Actions ---

void BackgroundConfigPopup::onProfileCustomImage(CCObject*) {
    file::FilePickOptions options;
    options.defaultPath = Mod::get()->getSaveDir() / "cache";
    options.filters = { { "Images & GIFs", { "*.png", "*.jpg", "*.jpeg", "*.gif" } } };
    
    file::pick(file::PickMode::OpenFile, options).listen([this](Result<std::filesystem::path>* result) {
        if (result && result->isOk()) {
            auto path = result->unwrap();
            if (!path.empty()) {
                auto pathStr = path.generic_string();
                std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

                Mod::get()->setSavedValue<std::string>("profile-bg-type", "custom");
                Mod::get()->setSavedValue<std::string>("profile-bg-path", pathStr);
                auto res = Mod::get()->saveData();
                if (res.isErr()) {
                    Notification::create("Failed to save settings!", NotificationIcon::Error)->show();
                } else {
                    Notification::create("Profile Background Set!", NotificationIcon::Success)->show();
                }
            }
        }
    });
}

void BackgroundConfigPopup::onProfileClear(CCObject*) {
    Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
    Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
    (void)Mod::get()->saveData();
    Notification::create("Profile Background Cleared", NotificationIcon::Success)->show();
}
