#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/modify/CommentCell.hpp>
#include "../managers/ThumbnailAPI.hpp"
#include <Geode/binding/FLAlertLayer.hpp>

using namespace geode::prelude;

// Cache for moderator status: username -> pair<isModerator, isAdmin>
static std::map<std::string, std::pair<bool, bool>> g_moderatorCache;

// Keep track of the active ProfilePage to update it safely
static ProfilePage* s_activeProfilePage = nullptr;

void showBadgeInfoPopup(CCNode* sender) {
    std::string title = "Unknown Rank";
    std::string desc = "No description available.";
    
    if (sender->getID() == "paimon-admin-badge") {
        title = "Paimbnails Admin";
        desc = "A <cj>Paimbnails Admin</c> is a developer or manager of the <cg>Paimbnails</c> mod. They have full control over the mod's infrastructure and content.";
    } else if (sender->getID() == "paimon-moderator-badge") {
        title = "Paimbnails Moderator";
        desc = "A <cj>Paimbnails Moderator</c> is a trusted user who helps review and manage thumbnails for the <cg>Paimbnails</c> mod. They ensure that content follows the guidelines.";
    }
    
    FLAlertLayer::create(title.c_str(), desc.c_str(), "OK")->show();
}

class $modify(BadgeCommentCell, CommentCell) {
    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    void loadFromComment(GJComment* comment) {
        CommentCell::loadFromComment(comment);
        
        if (!comment) return;
        std::string username = comment->m_userName;
        
        // Check cache first
        if (g_moderatorCache.contains(username)) {
            auto [isMod, isAdmin] = g_moderatorCache[username];
            if (isMod || isAdmin) {
                this->addBadgeToComment(isMod, isAdmin);
            }
            return;
        }

        // Not in cache, fetch from server
        this->retain();
        ThumbnailAPI::get().checkUserStatus(username, [this, username](bool isMod, bool isAdmin) {
            g_moderatorCache[username] = {isMod, isAdmin};
            
            Loader::get()->queueInMainThread([this, username, isMod, isAdmin]() {
                // Verify this cell is still displaying the same user
                if (this->getParent() && this->m_comment && this->m_comment->m_userName == username) {
                     if (isMod || isAdmin) {
                        this->addBadgeToComment(isMod, isAdmin);
                    }
                }
                this->release();
            });
        });
    }

    void addBadgeToComment(bool isMod, bool isAdmin) {
        // Find the username menu (standardized by Geode)
        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;
        
        // Check if badge already exists
        if (menu->getChildByID("paimon-moderator-badge")) return;
        if (menu->getChildByID("paimon-admin-badge")) return;

        CCSprite* badgeSprite = nullptr;
        std::string badgeID;

        if (isAdmin) {
            badgeSprite = CCSprite::create("Admin.png"_spr);
            badgeID = "paimon-admin-badge";
        } else if (isMod) {
            badgeSprite = CCSprite::create("Moderador.png"_spr);
            badgeID = "paimon-moderator-badge";
        }

        if (!badgeSprite) return;

        // Scale down the badge
        float targetHeight = 15.5f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(BadgeCommentCell::onPaimonBadge)
        );
        btn->setID(badgeID);
        
        auto menuNode = static_cast<CCMenu*>(menu);
        
        // Insertar antes del label de porcentaje si existe (mantener un orden consistente en la UI)
        if (auto percentage = this->getChildByIDRecursive("percentage-label")) {
            menuNode->insertBefore(btn, percentage);
        } else {
            menuNode->addChild(btn);
        }
        
        menuNode->updateLayout();
    }
};

class $modify(BadgeProfilePage, ProfilePage) {
    void onPaimonBadge(CCObject* sender) {
        if (auto node = typeinfo_cast<CCNode*>(sender)) {
            showBadgeInfoPopup(node);
        }
    }

    void addModeratorBadge(bool isMod, bool isAdmin) {
        // Buscar the menú of the username (ID provisto by the layout of Geode / BadgesAPI)
        auto menu = this->getChildByIDRecursive("username-menu");
        if (!menu) return;
        
        // Check if badge already exists to avoid duplicates
        if (menu->getChildByID("paimon-moderator-badge")) return;
        if (menu->getChildByID("paimon-admin-badge")) return;

        CCSprite* badgeSprite = nullptr;
        std::string badgeID;

        if (isAdmin) {
            badgeSprite = CCSprite::create("Admin.png"_spr);
            badgeID = "paimon-admin-badge";
        } else if (isMod) {
            badgeSprite = CCSprite::create("Moderador.png"_spr);
            badgeID = "paimon-moderator-badge";
        }

        if (!badgeSprite) return;

        // Log to confirm we are using the clean version
        log::info("Adding badge (Clickable) - Admin: {}, Mod: {}", isAdmin, isMod);

        // Scale down the badge to fit nicely (standard badges are small)
        float targetHeight = 20.0f;
        float scale = targetHeight / badgeSprite->getContentSize().height;
        badgeSprite->setScale(scale);

        auto btn = CCMenuItemSpriteExtra::create(
            badgeSprite,
            this,
            menu_selector(BadgeProfilePage::onPaimonBadge)
        );
        btn->setID(badgeID);
        
        static_cast<CCMenu*>(menu)->addChild(btn);
        static_cast<CCMenu*>(menu)->updateLayout();
    }

    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        
        // Remove global pointer tracking for async safety
        // s_activeProfilePage = this; 

        std::string username = score->m_userName;
        
        // Check cache first (optional, but good for instant feedback)
        bool cachedStatus = false;
        if (g_moderatorCache.contains(username)) {
            auto [isMod, isAdmin] = g_moderatorCache[username];
            if (isMod || isAdmin) {
                this->addModeratorBadge(isMod, isAdmin);
                cachedStatus = true;
            }
        }

        // Always fetch from server for ProfilePage to ensure up-to-date status
        // Retain self to ensure validity during async operation
        this->retain();
        ThumbnailAPI::get().checkUserStatus(username, [this, username](bool isMod, bool isAdmin) {
            // Update cache
            g_moderatorCache[username] = {isMod, isAdmin};
            
            Loader::get()->queueInMainThread([this, username, isMod, isAdmin]() {
                // Check if we are still relevant
                if (this->getParent()) {
                    if (isMod || isAdmin) {
                       this->addModeratorBadge(isMod, isAdmin);
                    }
                }
                
                // Release self
                this->release();
            });
        });
    }

    void onExit() {
        // Global pointer no longer used for async management
        // if (s_activeProfilePage == this) s_activeProfilePage = nullptr;
        ProfilePage::onExit();
    }
};
