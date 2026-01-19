#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/GameManager.hpp>
#include "../utils/Localization.hpp"
#include "../utils/Debug.hpp"
#include <chrono>
#include <fstream>
#include "../utils/FileDialog.hpp"
#include "../managers/ProfileThumbs.hpp"
#include "../managers/ThumbsRegistry.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../layers/CapturePreviewPopup.hpp"
#include "../layers/VerificationQueuePopup.hpp"
#include "../layers/AddModeratorPopup.hpp"
#include "../layers/BanUserPopup.hpp"
#include "../utils/Assets.hpp"
#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/HttpClient.hpp"

using namespace geode::prelude;
using namespace cocos2d;

class $modify(PaimonProfilePage, ProfilePage) {
    struct Fields {
        CCMenu* m_extraMenu = nullptr;
        CCMenuItemSpriteExtra* m_gearBtn = nullptr;
        CCMenuItemSpriteExtra* m_verifyModBtn = nullptr;
        CCMenuItemSpriteExtra* m_addProfileBtn = nullptr;
        CCMenuItemSpriteExtra* m_editModeBtn = nullptr;
        CCMenuItemSpriteExtra* m_banBtn = nullptr;
        CCClippingNode* m_profileClip = nullptr;
        CCLayerColor* m_profileSeparator = nullptr;
        CCNode* m_profileGradient = nullptr;
        CCNode* m_profileBorder = nullptr;
        bool m_isApprovedMod = false;
        bool m_isAdmin = false;
    };

    bool canShowModerationControls() {
        // Show moderation controls if moderator was verified OR admin.
        return m_fields->m_isApprovedMod || m_fields->m_isAdmin;
    }

    std::string getViewedUsername() {
        // Read from labels. (Bindings don't expose an internal username field here.)
        if (this->m_mainLayer) {
            if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(this->m_mainLayer->getChildByIDRecursive("username-label"))) {
                if (lbl->getString()) return std::string(lbl->getString());
            }
            if (auto* lbl2 = typeinfo_cast<CCLabelBMFont*>(this->m_mainLayer->getChildByIDRecursive("username"))) {
                if (lbl2->getString()) return std::string(lbl2->getString());
            }
        }
        return "";
    }

    void refreshBanButtonVisibility() {
        if (!m_fields->m_banBtn) return;

        // Never show on own profile
        if (this->m_ownProfile) {
            m_fields->m_banBtn->setVisible(false);
            m_fields->m_banBtn->setEnabled(false);
            return;
        }

        bool show = canShowModerationControls();
        m_fields->m_banBtn->setVisible(show);
        m_fields->m_banBtn->setEnabled(show);

        // If we can see it, also disable it when the viewed profile is a moderator/admin.
        // We rely on the server's /api/moderators list to keep it consistent.
        auto targetName = getViewedUsername();
        if (show && !targetName.empty()) {
            auto targetLower = geode::utils::string::toLower(targetName);
            Ref<ProfilePage> self = this;
            HttpClient::get().get("/api/moderators", [self, targetLower](bool ok, const std::string& resp) {
                if (!ok) return;
                // Check if self is still valid (though Ref should keep it alive, check parent to be safe)
                if (!self || !self->getParent()) return;

                try {
                    auto parsed = matjson::parse(resp);
                    if (!parsed.isOk()) return;
                    auto root = parsed.unwrap();
                    auto mods = root["moderators"]; // [{ username, currentBanner }]
                    if (!mods.isArray()) return;
                    for (auto const& v : mods.asArray().unwrap()) {
                        if (!v.isObject()) continue;
                        auto u = v["username"]; 
                        if (!u.isString()) continue;
                        auto nameLower = geode::utils::string::toLower(u.asString().unwrap());
                        if (nameLower == targetLower) {
                            // ya estamos en el main thread
                            // usamos getChildByIDRecursive porque m_fields puede fallar si el nodo se muere
                            if (auto banBtn = static_cast<CCMenuItemSpriteExtra*>(self->getChildByIDRecursive("ban-user-button"))) {
                                banBtn->setEnabled(false);
                                banBtn->setOpacity(120);
                            }
                            return;
                        }
                    }
                } catch (...) {
                }
            });
        }
    }

    void onBanUser(CCObject*) {
        if (!canShowModerationControls()) {
            Notification::create(Localization::get().getString("ban.profile.mod_only"), NotificationIcon::Warning)->show();
            return;
        }
        if (this->m_ownProfile) {
            Notification::create(Localization::get().getString("ban.profile.self_ban"), NotificationIcon::Warning)->show();
            return;
        }

        // nombre de usuario en el perfil
        std::string target = getViewedUsername();
        if (target.empty()) {
            Notification::create(Localization::get().getString("ban.profile.read_error"), NotificationIcon::Error)->show();
            return;
        }
        
        BanUserPopup::create(target)->show();
    }

    void addOrUpdateProfileThumbOnPage(int accountID) {
        // visibilidad: si esta verificado, es mi perfil o soy mod
        bool has = ProfileThumbs::get().has(accountID);
        log::debug("[ProfilePage] addOrUpdateProfileThumbOnPage - accountID: {}, has: {}, ownProfile: {}", 
            accountID, has, this->m_ownProfile);
        
        bool isVerified = ThumbsRegistry::get().isVerified(ThumbKind::Profile, accountID);
        
        if (!(isVerified || this->m_ownProfile)) {
            log::debug("[ProfilePage] Profile thumbnail hidden (not verified, not own profile, mod mode off)");
            return;
        }

        CCTexture2D* tex = nullptr;
        auto cached = ProfileThumbs::get().getCachedProfile(accountID);
        if (cached && cached->texture) {
            tex = cached->texture;
        } else {
            tex = ProfileThumbs::get().loadTexture(accountID);
        }

        if (!tex) {
            if (this->m_ownProfile) {
                log::info("[ProfilePage] No local profile for current user, downloading...");
                // lo retenemos para que sobreviva
                this->retain();
                std::string username = GJAccountManager::sharedState()->m_username;
                ThumbnailAPI::get().downloadProfile(accountID, username, [this, accountID](bool success, CCTexture2D* texture) {
                    if (success && texture) {
                        // retenemos la textura
                        texture->retain();
                        ThumbnailAPI::get().downloadProfileConfig(accountID, [this, accountID, texture](bool s, const ProfileConfig& c) {
                            Loader::get()->queueInMainThread([this, accountID, texture, s, c]() {
                                try {
                                    ProfileThumbs::get().cacheProfile(accountID, texture, {255,255,255}, {255,255,255}, 0.5f);
                                    if (s) ProfileThumbs::get().cacheProfileConfig(accountID, c);
                                    
                                    // solo actualizamos si sigue en la escena
                                    if (this->getParent()) {
                                        this->addOrUpdateProfileThumbOnPage(accountID);
                                    }
                                } catch(...) {}
                                
                                // soltamos la textura y esto
                                texture->release();
                                this->release();
                            });
                        });
                    } else {
                        this->release();
                    }
                });
            } else {
                log::debug("[ProfilePage] No profile thumbnail found for account {}", accountID);
            }
            return;
        }
        
        log::info("[ProfilePage] Displaying profile thumbnail for account {}", accountID);

        auto f = m_fields.self();
        if (f->m_profileClip) { f->m_profileClip->removeFromParent(); f->m_profileClip = nullptr; }
        if (f->m_profileSeparator) { f->m_profileSeparator->removeFromParent(); f->m_profileSeparator = nullptr; }
        if (f->m_profileGradient) { f->m_profileGradient->removeFromParent(); f->m_profileGradient = nullptr; }
        if (f->m_profileBorder) { f->m_profileBorder->removeFromParent(); f->m_profileBorder = nullptr; }

        auto sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;

        // Use the visible layer size of the popup/main layer as reference
        auto layer = this->m_mainLayer ? this->m_mainLayer : static_cast<CCNode*>(this);
        auto cs = layer->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) cs = this->getContentSize();

        // Thumbnail in top-right corner with rounded edges
        // Size: compact square-ish thumbnail
        float thumbSize = 120.f; // Fixed square size for clean corner placement
        
        float scaleX = thumbSize / sprite->getContentWidth();
        float scaleY = thumbSize / sprite->getContentHeight();
        sprite->setScale(scaleX); // Use uniform scale for now, adjust if needed

        // Create rounded rectangle mask using CCScale9Sprite for smooth corners
        auto roundedMask = CCScale9Sprite::create("square02b_001.png"); // Rounded square sprite
        if (!roundedMask) {
            // Fallback: use CCLayerColor if rounded sprite not available
            roundedMask = CCScale9Sprite::create("square02_001.png");
        }
        roundedMask->setContentSize({thumbSize, thumbSize});
        roundedMask->setColor({255, 255, 255});
        roundedMask->setOpacity(255);

        auto clip = CCClippingNode::create();
        clip->setStencil(roundedMask);
        clip->setContentSize({thumbSize, thumbSize});
        clip->setAnchorPoint({1, 1}); // Anchor top-right
        
        // Position in top-right corner with small padding
        float rightX = cs.width - 10.f;
        float topY = cs.height - 10.f;
        clip->setPosition({rightX, topY});
        clip->setZOrder(10); // Above other elements
        try {
            clip->setID("paimon-profilepage-clip"_spr);
        } catch (...) {
            log::warn("[ProfilePage] Failed to set clip ID");
        }

        sprite->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(sprite);
        layer->addChild(clip);
        f->m_profileClip = clip;

        // Add subtle border around the thumbnail
        auto border = CCScale9Sprite::create("square02b_001.png");
        if (!border) border = CCScale9Sprite::create("square02_001.png");
        border->setContentSize({thumbSize + 4.f, thumbSize + 4.f});
        border->setColor({0, 0, 0});
        border->setOpacity(120);
        border->setAnchorPoint({1, 1});
        border->setPosition({rightX, topY});
        border->setZOrder(9); // Behind the clip
        try {
            border->setID("paimon-profilepage-border"_spr);
        } catch (...) {
            log::warn("[ProfilePage] Failed to set border ID");
        }
        layer->addChild(border);
        f->m_profileBorder = border;

        // Background Logic
        // layer and cs are already defined above, reuse them
        // auto layer = this->m_mainLayer ? this->m_mainLayer : static_cast<CCNode*>(this);
        // auto cs = layer->getContentSize();
        // if (cs.width <= 1.f) cs = this->getContentSize();

        // Remove old background if any
        if (f->m_profileGradient) { f->m_profileGradient->removeFromParent(); f->m_profileGradient = nullptr; }

        ProfileConfig config = ProfileThumbs::get().getProfileConfig(accountID);
        
        // Auto-enable thumbnail background if we have a texture/gif and config is default
        if (config.backgroundType == "gradient" && (tex || !config.gifKey.empty())) {
            config.backgroundType = "thumbnail";
        }

        // Define size for the banner
        CCSize bannerSize = {cs.width, std::min(cs.height * 0.5f, 200.f)};
        
        // Pass true to onlyBackground to avoid adding the thumbnail overlay and separator
        CCNode* bg = ProfileThumbs::get().createProfileNode(tex, config, bannerSize, true);
        
        if (bg) {
            bg->setAnchorPoint({0,1});
            bg->setPosition({0, cs.height});
            bg->setZOrder(3);
            bg->setID("paimon-profilepage-bg"_spr);
            layer->addChild(bg);
            f->m_profileGradient = bg;
        }
    }



    bool init(int accountID, bool ownProfile) {
        if (!ProfilePage::init(accountID, ownProfile)) return false;
        try {
            // Load button layouts on first init
            ButtonLayoutManager::get().load();

            // Always create our own compact vertical menu
            auto extraMenu = CCMenu::create();
            extraMenu->setID("paimon-profile-extra-menu");
            // Place menu at layer center; item positions will be set as offsets from center
            auto layer = this->m_mainLayer ? this->m_mainLayer : static_cast<CCNode*>(this);
            auto cs = layer->getContentSize();
            extraMenu->setPosition({ cs.width / 2.f, cs.height / 2.f });
            this->addChild(extraMenu, 20);
            m_fields->m_extraMenu = extraMenu;

            // Always start with moderator status as false
            m_fields->m_isApprovedMod = false;
            m_fields->m_isAdmin = false;
            PaimonDebug::log("[ProfilePage] Inicializando perfil - status moderador: false");

            // Hide menu by default, showing only if verified
            extraMenu->setVisible(false);

            // Ban button (only visible for verified moderators/admins, never on own profile)
            {
                auto banSpr = ButtonSprite::create("X", 40, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
                banSpr->setScale(0.5f);
                auto banBtn = CCMenuItemSpriteExtra::create(banSpr, this, menu_selector(PaimonProfilePage::onBanUser));
                banBtn->setID("ban-user-button");

                // Place near top-right-ish in our menu coordinate system
                banBtn->setPosition({ -195.f, 75.f });

                extraMenu->addChild(banBtn);
                m_fields->m_banBtn = banBtn;
                // Start hidden until verification says we're mod/admin
                refreshBanButtonVisibility();
            }

            // If we are already verified from a previous session, show moderation controls immediately
            try {
                bool wasVerified = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
                if (wasVerified) {
                    m_fields->m_isApprovedMod = true;
                    extraMenu->setVisible(true);
                    refreshBanButtonVisibility();
                } else if (ownProfile) {
                    // Check server if we have no local data and it's our profile
                    auto gm = GameManager::sharedState();
                    if (gm && !gm->m_playerName.empty()) {
                        std::string username = gm->m_playerName;
                        // Using ThumbnailAPI to check status securely
                        ThumbnailAPI::get().checkModerator(username, [this](bool isApproved, bool isAdmin) {
                            Loader::get()->queueInMainThread([this, isApproved, isAdmin]() {
                                if (isApproved) {
                                    m_fields->m_isApprovedMod = true;
                                    m_fields->m_isAdmin = isAdmin;
                                    
                                    // Save approved status
                                    Mod::get()->setSavedValue("is-verified-moderator", true);
                                    
                                    if (m_fields->m_extraMenu) {
                                        m_fields->m_extraMenu->setVisible(true);
                                    }
                                    refreshBanButtonVisibility();
                                }
                            });
                        });
                    }
                }
            } catch (...) {
            }

            // Add profile button (only for own profile)
            if (ownProfile) {
                PaimonDebug::log("[ProfilePage] Cargando perfil propio - configurando botones");
                
                /* REMOVED: Sync button moved to LeaderboardsLayer
                auto addSpr = Assets::loadButtonSprite(
                    "profile-add",
                    "frame:GJ_plus2Btn_001.png",
                    [](){
                        auto s = CCSprite::createWithSpriteFrameName("GJ_plus2Btn_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                        return s;
                    }
                );
                addSpr->setScale(1.0f);
                auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(PaimonProfilePage::onAddProfileThumb));
                addBtn->setID("add-profile-thumb-button");
                
                // Compute defaults and persist once
                ButtonLayout defAdd;
                defAdd.position = cocos2d::CCPoint(-195.f, 8.5f);
                defAdd.scale = 1.0f;
                defAdd.opacity = 1.0f;
                ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "add-profile-thumb-button", defAdd);

                // Load and apply saved layout or default
                auto savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "add-profile-thumb-button");
                if (savedLayout) {
                    addBtn->setPosition(savedLayout->position);
                    addBtn->setScale(savedLayout->scale);
                    addBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                } else {
                    addBtn->setPosition(defAdd.position);
                }

                extraMenu->addChild(addBtn);
                m_fields->m_addProfileBtn = addBtn;
                
 // Animar entrada of the Button
                addBtn->setScale(0.f);
                addBtn->runAction(CCSequence::create(
                    CCDelayTime::create(0.05f),
                    CCEaseBounceOut::create(CCScaleTo::create(0.5f, savedLayout ? savedLayout->scale : 1.0f)),
                    nullptr
                ));
                */

                // Moderator verify button (only for own profile)
                auto verifySpr = Assets::loadButtonSprite(
                    "verify-mod",
                    "frame:GJ_completesIcon_001.png",
                    [](){
                        auto s = CCSprite::createWithSpriteFrameName("GJ_completesIcon_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
                        if (!s) s = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
                        return s;
                    }
                );
                
                // Fix: Scale based on size
                float targetSize = 35.0f;
                float currentSize = std::max(verifySpr->getContentWidth(), verifySpr->getContentHeight());
                if (currentSize > 0) {
                    verifySpr->setScale(targetSize / currentSize);
                } else {
                    verifySpr->setScale(0.85f);
                }
                
                auto verifyBtn = CCMenuItemSpriteExtra::create(verifySpr, this, menu_selector(PaimonProfilePage::onVerifyModeratorStatus));
                verifyBtn->setID("verify-moderator-button");
                
                ButtonLayout defVerify;
                defVerify.position = cocos2d::CCPoint(-195.f, 45.5f);
                defVerify.scale = 0.85f;
                defVerify.opacity = 1.0f;
                ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "verify-moderator-button", defVerify);

                auto savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "verify-moderator-button");
                if (savedLayout) {
                    verifyBtn->setPosition(savedLayout->position);
                    verifyBtn->setScale(savedLayout->scale);
                    verifyBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                } else {
                    verifyBtn->setPosition(defVerify.position);
                }
                
                extraMenu->addChild(verifyBtn);
                m_fields->m_verifyModBtn = verifyBtn;
                
                // Animar entrada of the Button
                verifyBtn->setScale(0.f);
                verifyBtn->runAction(CCSequence::create(
                    CCDelayTime::create(0.15f),
                    CCEaseBounceOut::create(CCScaleTo::create(0.5f, savedLayout ? savedLayout->scale : 0.85f)),
                    nullptr
                ));

                // Load estado of moderator saved previamente
                bool wasPreviouslyVerified = false;
                try {
                    wasPreviouslyVerified = Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
                } catch(...) {
                    log::warn("[ProfilePage] No se pudo cargar estado de verificacion previo");
                }
                
                if (wasPreviouslyVerified) {
                    log::info("[ProfilePage] Usuario previamente verificado - restaurando boton gear");
                    m_fields->m_isApprovedMod = true;
                    
                    // Anadir Button gear if not exists
                    if (!m_fields->m_gearBtn) {
                        auto gearSpr = Assets::loadButtonSprite(
                            "profile-gear",
                            "frame:GJ_optionsBtn02_001.png",
                            [](){
                                auto s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
                                if (!s) s = CCSprite::create();
                                return s;
                            }
                        );
                        
                        // Fix: Scale based on size
                        float targetSize = 35.0f;
                        float currentSize = std::max(gearSpr->getContentWidth(), gearSpr->getContentHeight());
                        if (currentSize > 0) {
                            gearSpr->setScale(targetSize / currentSize);
                        } else {
                            gearSpr->setScale(1.0f);
                        }
                        
                        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(PaimonProfilePage::onOpenThumbsCenter));
                        gearBtn->setID("thumbs-gear-button");
                        
                        ButtonLayout defGear;
                        defGear.position = cocos2d::CCPoint(-195.f, -28.5f);
                        defGear.scale = 1.0f;
                        defGear.opacity = 1.0f;
                        ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "thumbs-gear-button", defGear);

                        savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "thumbs-gear-button");
                        if (savedLayout) {
                            gearBtn->setPosition(savedLayout->position);
                            gearBtn->setScale(savedLayout->scale);
                            gearBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                        } else {
                            gearBtn->setPosition(defGear.position);
                        }
                        
                        // Animar entrada of the Button (mismo efecto that ProfilePage original)
                        gearBtn->setScale(0.f);
                        gearBtn->runAction(CCSequence::create(
                            CCDelayTime::create(0.1f),
                            CCEaseBounceOut::create(CCScaleTo::create(0.5f, savedLayout ? savedLayout->scale : 1.0f)),
                            nullptr
                        ));
                        
                        extraMenu->addChild(gearBtn);
                        m_fields->m_gearBtn = gearBtn;
                        log::info("[ProfilePage] Boton gear restaurado exitosamente");
                    }
                } else {
                    log::info("[ProfilePage] Boton de verificacion anadido - esperando accion del usuario");
                }

                // Add edit mode toggle button if setting is enabled
                bool showEditor = Mod::get()->getSettingValue<bool>("show-button-editor");
                if (showEditor) {
                    // Same pattern as other buttons: packed _spr first, then Assets override, then sprite frames
                    CCSprite* editSpr = nullptr;
                    if (!editSpr) editSpr = CCSprite::create("botonMove.png"_spr);
                    if (!editSpr) {
                        editSpr = Assets::loadButtonSprite(
                            "botonMove",
                            "",
                            [](){
                                // usamos _spr para manejar mejor los recursos
                                CCSprite* s = CCSprite::create("botonMove.png"_spr);
                                if (!s) s = CCSprite::createWithSpriteFrameName("edit_eTabBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_04.png");
                                return s;
                            }
                        );
                    }
                    
                    // arreglo: escalar segun el tamanio
                    float targetSize = 30.0f;
                    float currentSize = std::max(editSpr->getContentWidth(), editSpr->getContentHeight());
                    if (currentSize > 0) {
                        editSpr->setScale(targetSize / currentSize);
                    } else {
                        editSpr->setScale(0.3f);
                    }
                    
                    auto editBtn = CCMenuItemSpriteExtra::create(editSpr, this, menu_selector(PaimonProfilePage::onToggleEditMode));
                    editBtn->setID("button-edit-toggle");
                    
                    ButtonLayout defEdit;
                    defEdit.position = cocos2d::CCPoint(-195.f, -55.5f);
                    defEdit.scale = 0.3f;
                    defEdit.opacity = 1.0f;
                    ButtonLayoutManager::get().setDefaultLayoutIfAbsent("ProfilePage", "button-edit-toggle", defEdit);

                    // migramos el default viejo de 0.7 al nuevo
                    if (auto oldDef = ButtonLayoutManager::get().getDefaultLayout("ProfilePage", "button-edit-toggle")) {
                        if (oldDef->scale > 0.31f) {
                            ButtonLayoutManager::get().setDefaultLayout("ProfilePage", "button-edit-toggle", defEdit);
                        }
                    }

                    savedLayout = ButtonLayoutManager::get().getLayout("ProfilePage", "button-edit-toggle");
                    if (savedLayout) {
                        editBtn->setPosition(savedLayout->position);
                        editBtn->setScale(savedLayout->scale);
                        editBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                    } else {
                        editBtn->setPosition(defEdit.position);
                    }
                    
                    extraMenu->addChild(editBtn);
                    m_fields->m_editModeBtn = editBtn;
                    
                    // animar entrada del boton
                    editBtn->setScale(0.f);
                    editBtn->runAction(CCSequence::create(
                        CCDelayTime::create(0.2f),
                        CCEaseBounceOut::create(CCScaleTo::create(0.5f, savedLayout ? savedLayout->scale : 0.3f)),
                        nullptr
                    ));
                }
            }

            // nada de layout automatico, las posiciones son fijas

            // pedir assets del server si hay
            try {
                auto gm = GameManager::sharedState();
                // evitar conversion ambigua en android
                std::string username;
                if (ownProfile && gm) {
                    // convertir a std::string expliciatmente
                    username = gm->m_playerName.c_str();
                }
                if (!username.empty()) {
                    log::info("Profile assets download disabled for {}", username);
                    // descarga del server desactivada
                } else {
                    log::info("no hay username, pasamos de descarga remota");
                }
            } catch (...) {
                log::warn("fallo pidiendo assets");
            }
        } catch (...) {}
        return true;
    }

    void onOpenAddModerator(CCObject*) {
        if (auto* popup = AddModeratorPopup::create(nullptr)) popup->show();
    }

    void onVerifyModeratorStatus(CCObject*) {
        log::info("[ProfilePage] Botón de verificación manual presionado");
        
        // Get player username from GameManager
        auto gm = GameManager::sharedState();
        if (!gm) {
            log::error("[ProfilePage] No se pudo obtener GameManager");
            return;
        }
        
        std::string username = gm->m_playerName;
        if (username.empty()) {
            log::error("[ProfilePage] Username vacío - no se puede verificar");
            FLAlertLayer::create(Localization::get().getString("general.error").c_str(), Localization::get().getString("profile.username_error").c_str(), Localization::get().getString("general.ok").c_str())->show();
            return;
        }

        log::info("[ProfilePage] Iniciando verificación manual para usuario: {}", username);
        
        // Show loading indicator
        auto loadingCircle = LoadingCircle::create();
        loadingCircle->setParentLayer(this);
        loadingCircle->show();

        // Check with server using new ThumbnailAPI
        ThumbnailAPI::get().checkModerator(username, [this, loadingCircle, username](bool isApproved, bool isAdmin) {
            log::info("[ProfilePage] Verificación manual completada para '{}': isApproved={}, isAdmin={}", username, isApproved, isAdmin);
            Loader::get()->queueInMainThread([this, loadingCircle, isApproved, isAdmin]() {
                loadingCircle->fadeAndRemove();
                
                m_fields->m_isApprovedMod = isApproved;
                m_fields->m_isAdmin = isAdmin;
                refreshBanButtonVisibility();

                // If admin, add "Add moderator" button
                if (isAdmin && m_fields->m_extraMenu) {
                    if (!m_fields->m_extraMenu->getChildByID("add-moderator-button")) {
                        auto addModSpr = Assets::loadButtonSprite(
                            "add-moderator",
                            "frame:GJ_plus2Btn_001.png",
                            [](){
                                auto s = CCSprite::createWithSpriteFrameName("GJ_plus2Btn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                                return s;
                            }
                        );
                        addModSpr->setScale(0.85f);
                        auto addModBtn = CCMenuItemSpriteExtra::create(addModSpr, this, menu_selector(PaimonProfilePage::onOpenAddModerator));
                        addModBtn->setID("add-moderator-button");
                        // Default position a bit above verify button
                        addModBtn->setPosition({ -195.f, 72.5f });
                        m_fields->m_extraMenu->addChild(addModBtn);
                        addModBtn->setScale(0.f);
                        addModBtn->runAction(CCEaseBounceOut::create(CCScaleTo::create(0.5f, 0.85f)));
                    }
                }
                
                if (isApproved) {
                    log::info("[ProfilePage] Usuario aprobado - guardando estado y mostrando botón gear");
                    
                    // Save moderator status persistently in multiple ways
                    Mod::get()->setSavedValue("is-verified-moderator", true);
                
                    // NEW: Save a verification file with a timestamp
                    try {
                        auto modDataPath = Mod::get()->getSaveDir() / "moderator_verification.dat";
                        std::ofstream modFile(modDataPath, std::ios::binary);
                        if (modFile) {
                            // Write current timestamp
                            auto now = std::chrono::system_clock::now();
                            auto timestamp = std::chrono::system_clock::to_time_t(now);
                            modFile.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
                            modFile.close();
                            log::info("[ProfilePage] Archivo de verificación de moderador guardado: {}", modDataPath.generic_string());
                        } else {
                            log::error("[ProfilePage] No se pudo crear archivo de verificación");
                        }
                    } catch (const std::exception& e) {
                        log::error("[ProfilePage] Error al guardar archivo de verificación: {}", e.what());
                    }
                    
                    // Add gear button for thumbs center
                    if (m_fields->m_extraMenu && !m_fields->m_gearBtn) {
                        log::info("[ProfilePage] Añadiendo botón gear al menú");
                        auto gearSpr = Assets::loadButtonSprite(
                            "profile-gear",
                            "frame:GJ_optionsBtn02_001.png",
                            [](){
                                auto s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
                                if (!s) s = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                                return s;
                            }
                        );
                        gearSpr->setScale(0.9f);
                        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(PaimonProfilePage::onOpenThumbsCenter));
                        gearBtn->setID("thumbs-gear-button");
                        gearBtn->setPosition({ -195.f, -18.5f });
                        m_fields->m_extraMenu->addChild(gearBtn);
                        m_fields->m_gearBtn = gearBtn;
                    } else {
                        log::info("[ProfilePage] Botón gear ya existe o menú no disponible");
                    }
                    
                    FLAlertLayer::create(
                        Localization::get().getString("profile.verified").c_str(),
                        Localization::get().getString("profile.verified_msg").c_str(),
                        Localization::get().getString("general.ok").c_str()
                    )->show();
                } else {
                    log::warn("[ProfilePage] Usuario NO aprobado como moderador");
                    FLAlertLayer::create(
                        Localization::get().getString("profile.not_verified").c_str(),
                        Localization::get().getString("profile.not_verified_msg").c_str(),
                        Localization::get().getString("general.ok").c_str()
                    )->show();
                }
            });
        });
    }

    void onOpenThumbsCenter(CCObject*) {
        // Ensure the user is a moderator before opening the panel
        if (!m_fields->m_isApprovedMod) {
            log::warn("[ProfilePage] Usuario NO es moderador, bloqueando acceso al centro de verificación");
            FLAlertLayer::create(
                Localization::get().getString("profile.access_denied").c_str(),
                Localization::get().getString("profile.moderators_only").c_str(),
                Localization::get().getString("general.ok").c_str()
            )->show();
            return;
        }
        
        // Open verification center with categories (Check, Update, Report)
        log::info("[ProfilePage] Abriendo centro de verificación para moderador");
        if (auto p = VerificationQueuePopup::create()) p->show();
    }

    void onToggleEditMode(CCObject*) {
        if (!m_fields->m_extraMenu) return;

        // Enable the highlight effect when entering edit mode
        PaimonButtonHighlighter::highlightAll();

        // Create and add the edit overlay to scene
        auto overlay = ButtonEditOverlay::create("ProfilePage", m_fields->m_extraMenu);
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            scene->addChild(overlay, 1000);
        }
    }

    void onAddProfileThumb(CCObject*) {
        // Use Geode file picker for all platforms to ensure compatibility
        geode::utils::file::pick(
            geode::utils::file::PickMode::OpenFile,
            {
                Mod::get()->getSaveDir() / "cache",
                { geode::utils::file::FilePickOptions::Filter {
                    "Images",
                    {"*.png", "*.jpg", "*.jpeg", "*.gif"}
                } }
            }
        ).listen([this](geode::Result<std::filesystem::path>* result) {
            if (!result || result->isErr()) {
                Notification::create(Localization::get().getString("profile.no_image_selected").c_str(), NotificationIcon::Warning)->show();
                return;
            }
            this->processProfileImage(result->unwrap());
        });
    }

    void processProfileImage(std::filesystem::path path) {
        // Check if it's a GIF
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".gif") {
            // Handle GIF upload
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                Notification::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }
            std::vector<uint8_t> gifData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            
            if (gifData.size() > 10 * 1024 * 1024) {
                 Notification::create("GIF too large (max 10MB)", NotificationIcon::Error)->show();
                 return;
            }

            int accountID = this->m_accountID;
            std::string username = GJAccountManager::sharedState()->m_username;
            
            // Upload GIF directly
            ThumbnailAPI::get().uploadProfileGIF(accountID, gifData, username, [this, accountID, gifData](bool success, const std::string& msg) {
                if (success) {
                    // Cache locally
                    std::string gifKey = fmt::format("profile_{}", accountID);
                    ProfileThumbs::get().cacheProfileGIF(accountID, gifKey, {255,255,255}, {255,255,255}, 0.6f);
                    
                    // Also create a static texture from the GIF data (first frame) so it can be displayed immediately
                    // by addOrUpdateProfileThumbOnPage which expects a texture in the cache entry.
                    CCImage img;
                    if (img.initWithImageData(const_cast<uint8_t*>(gifData.data()), gifData.size())) {
                        auto tex = new CCTexture2D();
                        if (tex->initWithImage(&img)) {
                            tex->autorelease();
                            // Update the cache entry with this texture
                            auto entry = ProfileThumbs::get().getCachedProfile(accountID);
                            if (entry) {
                                if (entry->texture) entry->texture->release();
                                entry->texture = tex;
                                entry->texture->retain();
                            }
                        }
                    }

                    // Save to disk for session persistence
                    // ProfileThumbs::saveRGB doesn't support GIF data directly, but cacheProfileGIF handles the memory cache.
                    // We should probably save the GIF to disk too if we want it to persist across scene changes if memory is cleared.
                    // But ProfileThumbs::saveRGB expects raw RGB.
                    // For now, memory cache is enough for the session.
                    
                    Notification::create(Localization::get().getString("profile.saved").c_str(), NotificationIcon::Success)->show();
                    this->addOrUpdateProfileThumbOnPage(accountID);
                } else {
                    Notification::create("Upload failed: " + msg, NotificationIcon::Error)->show();
                }
            });
            return;
        }

        // Load the image as CCImage then to RGB to reuse CapturePreviewPopup flow
        try {
            if (std::filesystem::file_size(path) > 7 * 1024 * 1024) {
                Notification::create("Image too large (max 7MB)", NotificationIcon::Error)->show();
                return;
            }
        } catch(...) {}

        std::vector<uint8_t> data;
        CCImage img;
        if (!img.initWithImageFile(path.generic_string().c_str())) { Notification::create(Localization::get().getString("profile.image_open_error").c_str(), NotificationIcon::Error)->show(); return; }

        int w = img.getWidth(); int h = img.getHeight();
        auto raw = img.getData();
        if (!raw) { Notification::create(Localization::get().getString("profile.invalid_image_data").c_str(), NotificationIcon::Error)->show(); return; }
        int bpp = 4;
        try {
            bpp = img.hasAlpha() ? 4 : 3;
        } catch (...) { bpp = 4; }
        // Prepare RGBA buffer for CapturePreviewPopup
        std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        const unsigned char* src = reinterpret_cast<const unsigned char*>(raw);
        
        if (bpp == 4) {
             memcpy(rgba.data(), src, static_cast<size_t>(w) * h * 4);
        } else { // bpp == 3
            for (int i = 0; i < w * h; i++) {
                rgba[i*4 + 0] = src[i*3 + 0];
                rgba[i*4 + 1] = src[i*3 + 1];
                rgba[i*4 + 2] = src[i*3 + 2];
                rgba[i*4 + 3] = 255;
            }
        }

        // Create texture for preview
        auto* tex = new CCTexture2D();
        if (!tex->initWithImage(&img)) { tex->release(); Notification::create(Localization::get().getString("profile.texture_error").c_str(), NotificationIcon::Error)->show(); return; }
        tex->autorelease();

        int accountID = this->m_accountID;
        auto buffer = std::shared_ptr<uint8_t>(new uint8_t[rgba.size()], std::default_delete<uint8_t[]>());
        memcpy(buffer.get(), rgba.data(), rgba.size());

        // Capture actual image dimensions to save correctly
        int imgWidth = w;
        int imgHeight = h;

        // Create popup
        auto popup = CapturePreviewPopup::create(
            tex, 
            accountID, 
            buffer, 
            imgWidth, 
            imgHeight,
            [this, accountID](bool ok, int id, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId){
                if (ok) {
                    // Convert back to RGB for ProfileThumbs storage
                    std::vector<uint8_t> rgb(w * h * 3);
                    if (buf) {
                        for(int i = 0; i < w * h; i++) {
                            rgb[i*3 + 0] = buf.get()[i*4 + 0];
                            rgb[i*3 + 1] = buf.get()[i*4 + 1];
                            rgb[i*3 + 2] = buf.get()[i*4 + 2];
                        }
                    }
                    
                    ProfileThumbs::get().saveRGB(accountID, rgb.data(), w, h);
                    ThumbsRegistry::get().mark(ThumbKind::Profile, accountID, false);
                    Mod::get()->setSavedValue("my-account-id", accountID);
                    Notification::create(Localization::get().getString("profile.saved").c_str(), NotificationIcon::Success)->show();
                    
                    // Refresh profile page to show the new thumbnail
                    this->addOrUpdateProfileThumbOnPage(accountID);
                }
            }
        );
        if (popup) popup->show();
    }
};


