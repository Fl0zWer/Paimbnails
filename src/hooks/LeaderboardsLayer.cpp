#include <Geode/modify/LeaderboardsLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/Localization.hpp"
#include <Geode/utils/cocos.hpp>
#include <fstream>
#include <algorithm>
#include <random>
#include "../managers/ProfileThumbs.hpp"
#include "../utils/FileDialog.hpp"
#include "../layers/ModeratorsLayer.hpp"
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;
using namespace cocos2d;

class ProfilePreviewPopup : public geode::Popup<> {
protected:
    std::vector<uint8_t> m_data;
    std::string m_username;
    std::function<void()> m_callback;

    bool setup() override {
        this->setTitle("Preview Profile");

        // Create texture from data
        auto image = new CCImage();
        if (!image->initWithImageData(const_cast<uint8_t*>(m_data.data()), m_data.size())) {
            image->release();
            return false;
        }
        auto texture = new CCTexture2D();
        texture->initWithImage(image);
        image->release();
        texture->autorelease();

        // Gather current config
        ProfileConfig config;
        try {
            config.backgroundType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
            config.blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
            config.darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
            config.useGradient = Mod::get()->getSavedValue<bool>("profile-use-gradient", false);
            config.colorA = Mod::get()->getSavedValue<ccColor3B>("profile-color-a", {255,255,255});
            config.colorB = Mod::get()->getSavedValue<ccColor3B>("profile-color-b", {255,255,255});
            config.separatorColor = Mod::get()->getSavedValue<ccColor3B>("score-separator-color", {0,0,0});
            config.separatorOpacity = Mod::get()->getSavedValue<int>("score-separator-opacity", 50);
        } catch (...) {}

        // Create preview node
        CCSize previewSize = {340, 50}; // Standard score cell size approx
        auto previewNode = ProfileThumbs::get().createProfileNode(texture, config, previewSize);
        
        if (previewNode) {
            previewNode->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint{0, 10});
            m_mainLayer->addChild(previewNode);
        }

        // Add Upload Button
        auto uploadBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Upload"),
            this,
            menu_selector(ProfilePreviewPopup::onUpload)
        );
        auto menu = CCMenu::create();
        menu->addChild(uploadBtn);
        menu->setPosition({m_mainLayer->getContentSize().width / 2, 30});
        m_mainLayer->addChild(menu);

        return true;
    }

    void onUpload(CCObject*) {
        if (m_callback) m_callback();
        this->onClose(nullptr);
    }

public:
    static ProfilePreviewPopup* create(const std::vector<uint8_t>& data, const std::string& username, std::function<void()> callback) {
        auto ret = new ProfilePreviewPopup();
        ret->m_data = data;
        ret->m_username = username;
        ret->m_callback = callback;
        if (ret && ret->initAnchored(360, 180)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};







class $modify(PaimonLeaderboardsLayer, LeaderboardsLayer) {
    struct Fields {
        // No fields needed for now
    };

    bool init(LeaderboardState state) {
        if (!LeaderboardsLayer::init(state)) return false;
        
        createPaimonButtons();
        updateTabColors(state);
        
        return true;
    }

    void updateTabColors(LeaderboardState state) {
        // Find the menu containing the tabs
        CCMenu* tabMenu = nullptr;
        CCArrayExt<CCNode*> children(this->getChildren());
        for (auto child : children) {
            if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                // Heuristic: The tab menu usually has the buttons for Top, Global, etc.
                // We can check if it has children with specific tags or just assume it's the one with ~4 items
                if (menu->getChildrenCount() >= 3) {
                    tabMenu = menu;
                    break;
                }
            }
        }

        if (!tabMenu) return;

        // Reset all buttons to white
        CCArrayExt<CCNode*> items(tabMenu->getChildren());
        for (auto item : items) {
            if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
                btn->setColor({255, 255, 255});
                // Also reset scale if we want to emphasize
                // btn->setScale(1.0f);
            }
        }

        // Highlight active button
        // Assuming tags match LeaderboardState values (0, 1, 2, 3)
        // Top=0, Global=1, Creators=2, Friends=3
        int stateInt = (int)state;
        
        // Map state to tag if necessary. 
        // In GD, tags for these buttons are often set to the state value.
        if (auto btn = tabMenu->getChildByTag(stateInt)) {
            if (auto spriteBtn = typeinfo_cast<CCMenuItemSpriteExtra*>(btn)) {
                spriteBtn->setColor({0, 255, 0}); // Green
            }
        }
    }

    void onTop(CCObject* sender) {
        LeaderboardsLayer::onTop(sender);
        updateTabColors((LeaderboardState)0); // Top
    }

    void onGlobal(CCObject* sender) {
        LeaderboardsLayer::onGlobal(sender);
        updateTabColors((LeaderboardState)1); // Global
    }

    void onCreators(CCObject* sender) {
        LeaderboardsLayer::onCreators(sender);
        updateTabColors((LeaderboardState)2); // Creators
    }

    // onFriends removed as it might not exist or be hookable
    
    void createPaimonButtons() {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        // Menu for buttons
        auto menu = CCMenu::create();
        menu->setPosition({30, 100}); // Slightly adjusted Y to fit both
        this->addChild(menu);

        // Moderator Button
        auto modSprite = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        if (modSprite) {
            modSprite->setScale(0.8f);
        }

        auto modBtn = CCMenuItemSpriteExtra::create(
            modSprite,
            this,
            menu_selector(PaimonLeaderboardsLayer::onOpenModerators)
        );
        modBtn->setPosition({0, 0});
        menu->addChild(modBtn);
        
        // Upload Banner Button (below moderator button)
        auto uploadSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        if (uploadSprite) {
            uploadSprite->setScale(0.7f);
        }
        
        auto uploadBtn = CCMenuItemSpriteExtra::create(
            uploadSprite,
            this,
            menu_selector(PaimonLeaderboardsLayer::onUploadBanner)
        );
        uploadBtn->setPosition({0, -35}); // Below mod button
        menu->addChild(uploadBtn);
    }

    void onOpenModerators(CCObject*) {
        ModeratorsLayer::create()->show();
    }

    void onUploadBanner(CCObject*) {
        // Check permissions for GIF
        bool isMod = false;
        bool isAdmin = false;
        try {
            isMod = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
            isAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
        } catch(...) {}
        
        bool canUploadGIF = isMod || isAdmin;
        // TODO: Add Donator check if possible

        std::unordered_set<std::string> extensions = {"*.png", "*.jpg", "*.jpeg"};
        if (canUploadGIF) {
            extensions.insert("*.gif");
        }

        geode::utils::file::pick(
            geode::utils::file::PickMode::OpenFile,
            {
                Mod::get()->getSaveDir() / "cache",
                { geode::utils::file::FilePickOptions::Filter {
                    canUploadGIF ? "Images & GIFs" : "Images",
                    extensions
                } }
            }
        ).listen([this, canUploadGIF](geode::Result<std::filesystem::path>* result) {
            if (!result || result->isErr()) {
                Notification::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
                return;
            }
            
            auto path = result->unwrap();
            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".gif") {
                if (!canUploadGIF) {
                    Notification::create("GIFs are restricted to Mods/Admins/Donators", NotificationIcon::Error)->show();
                    return;
                }
                this->processProfileGIF(path);
            } else {
                this->processProfileImage(path);
            }
        });
    }

    void processProfileGIF(std::filesystem::path path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            Notification::create("Failed to read GIF file", NotificationIcon::Error)->show();
            return;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        if (data.size() > 10 * 1024 * 1024) {
             Notification::create("GIF too large (max 10MB)", NotificationIcon::Error)->show();
             return;
        }

        int accountID = GJAccountManager::sharedState()->m_accountID;
        std::string username = GJAccountManager::sharedState()->m_username;

        Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

        ThumbnailAPI::get().uploadProfileGIF(accountID, data, username, [this](bool success, const std::string& msg) {
            if (success) {
                Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
            } else {
                Notification::create((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error)->show();
            }
        });
    }

    void processProfileImage(std::filesystem::path path) {
        // Load image to verify it's valid
        std::vector<uint8_t> data;
        CCImage img;
        if (!img.initWithImageFile(path.string().c_str())) { 
            Notification::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show(); 
            return; 
        }

        // Read raw bytes for upload
        std::ifstream file(path, std::ios::binary);
        if (!file) return;
        std::vector<uint8_t> rawData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (rawData.size() > 7 * 1024 * 1024) {
             Notification::create("Image too large (max 7MB)", NotificationIcon::Error)->show();
             return;
        }

        int accountID = GJAccountManager::sharedState()->m_accountID;
        std::string username = GJAccountManager::sharedState()->m_username;

        // Show preview popup before uploading? Or just upload?
        // User asked for "se pueda elegir todo tipo de imagenes compatible"
        // Let's just upload for now, or use the ProfilePreviewPopup defined above if we want preview.
        // The ProfilePreviewPopup logic I see in the file seems unused or for local preview.
        // Let's use it!
        
        ProfilePreviewPopup::create(rawData, username, [rawData, accountID, username]() {
            Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
            ThumbnailAPI::get().uploadProfile(accountID, rawData, username, [](bool success, const std::string& msg) {
                if (success) {
                    Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                } else {
                    Notification::create((Localization::get().getString("capture.upload_error") + ": " + msg).c_str(), NotificationIcon::Error)->show();
                }
            });
        })->show();
    }
    


    void onExit() {
        ProfileThumbs::get().clearAllCache();
        log::info("[LeaderboardsLayer] Profile cache cleared on exit");
        
        LeaderboardsLayer::onExit();
    }
};

