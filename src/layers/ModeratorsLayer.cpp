#include "ModeratorsLayer.hpp"
#include "../utils/Localization.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/Debug.hpp"
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GJScoreCell.hpp>
#include <Geode/binding/CustomListView.hpp>
#include <Geode/binding/GJListLayer.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <Geode/modify/GJScoreCell.hpp>

ModeratorsLayer* ModeratorsLayer::s_instance = nullptr;

ModeratorsLayer* ModeratorsLayer::create() {
    auto ret = new ModeratorsLayer();
    if (ret && ret->initAnchored(420.f, 280.f)) {
        ret->autorelease();
        s_instance = ret;
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

ModeratorsLayer::~ModeratorsLayer() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
    if (m_scores) {
        m_scores->release();
    }
    if (GameLevelManager::sharedState()->m_userInfoDelegate == this) {
        GameLevelManager::sharedState()->m_userInfoDelegate = nullptr;
    }
}

bool ModeratorsLayer::isScoreInList(GJUserScore* score) {
    if (!m_scores || !score) return false;
    return m_scores->containsObject(score);
}

bool ModeratorsLayer::setup() {
    // Title is handled by GJListLayer
    this->fetchModerators();

    // Hide the main popup background to avoid "double popup" look
    if (m_bgSprite) {
        m_bgSprite->setVisible(false);
    }
    
    // Fallback: iterate children to find any other background sprites (CCScale9Sprite)
    // This ensures the outer brown box is definitely hidden
    auto children = this->getChildren();
    if (children) {
        CCObject* obj;
        CCARRAY_FOREACH(children, obj) {
            auto sprite = dynamic_cast<CCScale9Sprite*>(obj);
            if (sprite) {
                sprite->setVisible(false);
            }
        }
    }

    if (m_closeBtn) {
        m_closeBtn->setPosition(m_closeBtn->getPosition() + ccp(13.f, 0.f));
    }
    return true;
}

static void sortScoresByPriority(CCArray* scores, const std::vector<std::string>& priority) {
    if (!scores) return;
    auto toVec = std::vector<GJUserScore*>();
    toVec.reserve(scores->count());
    for (unsigned i = 0; i < scores->count(); ++i) {
        auto obj = static_cast<GJUserScore*>(scores->objectAtIndex(i));
        obj->retain(); // Retain to prevent deletion when removing from array
        toVec.push_back(obj);
    }

    auto toLower = [](std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return str;
    };

    auto indexOf = [&](const std::string& name) {
        std::string lowerName = toLower(name);
        for (size_t i = 0; i < priority.size(); ++i) {
            if (toLower(priority[i]) == lowerName) return static_cast<int>(i);
        }
        return static_cast<int>(priority.size());
    };

    std::stable_sort(toVec.begin(), toVec.end(), [&](GJUserScore* a, GJUserScore* b) {
        return indexOf(a->m_userName) < indexOf(b->m_userName);
    });

    scores->removeAllObjects();
    for (auto* s : toVec) {
        scores->addObject(s);
        s->release(); // Release the extra retain we added
    }
}

void ModeratorsLayer::fetchModerators() {
    m_loadingCircle = LoadingCircle::create();
    m_loadingCircle->setParentLayer(this);
    m_loadingCircle->show();

    m_scores = CCArray::create();
    m_scores->retain();

    this->retain(); // Keep alive for callback
    HttpClient::get().getModerators([this](bool success, const std::vector<std::string>& moderators) {
        if (!success || moderators.empty()) {
            if (m_loadingCircle) {
                m_loadingCircle->fadeAndRemove();
                m_loadingCircle = nullptr;
            }
            this->createList();
            this->release(); // Release callback retain
            return;
        }

        m_moderatorNames = moderators;
        m_pendingRequests = moderators.size();
        for (const auto& mod : moderators) {
            this->fetchGDBrowserProfile(mod);
        }
        this->release(); // Release callback retain
    });
}

void ModeratorsLayer::fetchGDBrowserProfile(const std::string& username) {
    std::string url = "https://gdbrowser.com/api/profile/" + username;
    
    this->retain(); // Keep alive for callback
    web::WebRequest req;
    req.get(url).listen([this, username](web::WebResponse* response) {
        if (response->ok()) {
            auto data = response->string().unwrapOr("");
            Loader::get()->queueInMainThread([this, username, data]() {
                this->onProfileFetched(username, data);
                this->release(); // Release callback retain
            });
        } else {
            Loader::get()->queueInMainThread([this, username]() {
                log::error("Failed to fetch profile for {}", username);
                this->onProfileFetched(username, "");
                this->release(); // Release callback retain
            });
        }
    });
}

void ModeratorsLayer::onProfileFetched(const std::string& username, const std::string& response) {
    if (!response.empty()) {
        try {
            auto res = matjson::parse(response);
            if (res.isOk()) {
                auto json = res.unwrap();
                
                auto parseInt = [](matjson::Value const& val) -> int {
                    if (val.isNumber()) return val.asInt().unwrapOr(0);
                    if (val.isString()) {
                        try { return std::stoi(val.asString().unwrapOr("0")); } catch(...) { return 0; }
                    }
                    return 0;
                };

                auto score = GJUserScore::create();
                score->m_userName = json["username"].asString().unwrapOr(username);
                score->m_accountID = parseInt(json["accountID"]);
                if (json.contains("playerID")) score->m_userID = parseInt(json["playerID"]);
                
                score->m_stars = parseInt(json["stars"]);
                score->m_diamonds = parseInt(json["diamonds"]);
                score->m_secretCoins = parseInt(json["coins"]);
                score->m_userCoins = parseInt(json["userCoins"]);
                score->m_demons = parseInt(json["demons"]);
                score->m_creatorPoints = parseInt(json["cp"]);
                score->m_globalRank = parseInt(json["rank"]);
                score->m_moons = parseInt(json["moons"]);
                
                score->m_iconID = parseInt(json["icon"]);
                score->m_color1 = parseInt(json["col1"]);
                score->m_color2 = parseInt(json["col2"]);
                score->m_playerCube = score->m_iconID;
                score->m_iconType = IconType::Cube;
                score->m_glowEnabled = json["glow"].asBool().unwrapOr(false);
                score->m_modBadge = parseInt(json["modBadge"]); 
                
                GameLevelManager::sharedState()->storeUserName(score->m_userID, score->m_accountID, score->m_userName);
                
                m_scores->addObject(score);
            }
        } catch (...) {
            log::error("Failed to parse profile for {}", username);
        }
    }
    
    m_pendingRequests--;
    if (m_pendingRequests <= 0) {
        this->onAllProfilesFetched();
    }
}

void ModeratorsLayer::onAllProfilesFetched() {
    if (m_loadingCircle) {
        m_loadingCircle->fadeAndRemove();
        m_loadingCircle = nullptr;
    }
    
    sortScoresByPriority(m_scores, m_moderatorNames);
    this->createList();
}

void ModeratorsLayer::createList() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    if (!m_scores) {
        m_scores = CCArray::create();
        m_scores->retain();
    }

    // Create CustomListView with Score type for better icon display
    m_listView = CustomListView::create(
        m_scores,
        BoomListType::Score, 
        220.f, 
        360.f
    );
    
    m_listLayer = GJListLayer::create(
        m_listView,
        "Paimbnails Mods",
        {0, 0, 0, 180}, 
        360.f, 
        220.f, 
        0
    );
    
    m_listLayer->setPosition(m_mainLayer->getContentSize() / 2 - m_listLayer->getScaledContentSize() / 2);
    m_mainLayer->addChild(m_listLayer);
}

void ModeratorsLayer::getUserInfoFinished(GJUserScore* score) {
    PaimonDebug::log("getUserInfoFinished: Received data for account {}", score->m_accountID);
    
    // Process any of the moderators
    if (score->m_accountID == 17046382 || score->m_accountID == 23785880 || 
        score->m_accountID == 4315943 || score->m_accountID == 4098680 || 
        score->m_accountID == 25339555) {
        
        PaimonDebug::log("  m_userName: {}", score->m_userName);
        PaimonDebug::log("  m_playerCube: {}", score->m_playerCube);
        PaimonDebug::log("  m_iconType: {}", (int)score->m_iconType);
        PaimonDebug::log("  m_color1: {}, m_color2: {}", score->m_color1, score->m_color2);
        
        auto newScore = GJUserScore::create();
        newScore->m_userName = score->m_userName;
        newScore->m_userID = score->m_userID;
        newScore->m_accountID = score->m_accountID;
        
        // Copy ALL icon-related fields
        newScore->m_color1 = score->m_color1;
        newScore->m_color2 = score->m_color2;
        newScore->m_color3 = score->m_color3;
        newScore->m_special = score->m_special;
        newScore->m_iconType = score->m_iconType;
        newScore->m_playerCube = score->m_playerCube;
        newScore->m_playerShip = score->m_playerShip;
        newScore->m_playerBall = score->m_playerBall;
        newScore->m_playerUfo = score->m_playerUfo;
        newScore->m_playerWave = score->m_playerWave;
        newScore->m_playerRobot = score->m_playerRobot;
        newScore->m_playerSpider = score->m_playerSpider;
        newScore->m_playerSwing = score->m_playerSwing;
        newScore->m_playerJetpack = score->m_playerJetpack;
        newScore->m_playerStreak = score->m_playerStreak;
        newScore->m_playerExplosion = score->m_playerExplosion;
        newScore->m_glowEnabled = score->m_glowEnabled;
        
        // CRITICAL: Set m_iconID based on current icon type
        switch (newScore->m_iconType) {
            case IconType::Cube: newScore->m_iconID = score->m_playerCube; break;
            case IconType::Ship: newScore->m_iconID = score->m_playerShip; break;
            case IconType::Ball: newScore->m_iconID = score->m_playerBall; break;
            case IconType::Ufo: newScore->m_iconID = score->m_playerUfo; break;
            case IconType::Wave: newScore->m_iconID = score->m_playerWave; break;
            case IconType::Robot: newScore->m_iconID = score->m_playerRobot; break;
            case IconType::Spider: newScore->m_iconID = score->m_playerSpider; break;
            case IconType::Swing: newScore->m_iconID = score->m_playerSwing; break;
            case IconType::Jetpack: newScore->m_iconID = score->m_playerJetpack; break;
            default: newScore->m_iconID = score->m_playerCube; break;
        }
        
        PaimonDebug::log("  Set m_iconID to: {}", newScore->m_iconID);
        
        // Copy real stats from server
        newScore->m_stars = score->m_stars;
        newScore->m_moons = score->m_moons;
        newScore->m_diamonds = score->m_diamonds;
        newScore->m_secretCoins = score->m_secretCoins;
        newScore->m_userCoins = score->m_userCoins;
        newScore->m_demons = score->m_demons;
        newScore->m_creatorPoints = score->m_creatorPoints;
        newScore->m_globalRank = score->m_globalRank;
        
        // Set badge
        newScore->m_modBadge = 2; // Elder Mod

        // Update m_scores: remove placeholder and insert new
        if (m_scores) {
            // Remove any existing entry for this account
            for (int i = m_scores->count() - 1; i >= 0; i--) {
                auto s = static_cast<GJUserScore*>(m_scores->objectAtIndex(i));
                if (s->m_accountID == score->m_accountID) {
                    m_scores->removeObjectAtIndex(i);
                }
            }
            // Insert at correct position
            m_scores->insertObject(newScore, 0);
            // Reapply hierarchy ordering
            sortScoresByPriority(m_scores, {"FlozWer", "Gabriv4", "Debihan", "SirExcelDj", "Robert55GD"});
        }

        PaimonDebug::log("Recreating list with updated data...");
        
        // Completely recreate the list to ensure update
        if (m_listLayer) {
            m_listLayer->removeFromParent();
            m_listLayer = nullptr;
        }

        m_listView = CustomListView::create(
            m_scores,
            BoomListType::Score, 
            220.f, 
            360.f
        );
        
        m_listLayer = GJListLayer::create(
            m_listView,
            Localization::get().getString("mods.title").c_str(),
            {0, 0, 0, 180}, 
            360.f, 
            220.f, 
            0
        );
        
        m_listLayer->setPosition(m_mainLayer->getContentSize() / 2 - m_listLayer->getScaledContentSize() / 2);
        m_mainLayer->addChild(m_listLayer);
        
        PaimonDebug::log("List recreated successfully");
    }
}

class $modify(GJScoreCell) {
    void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);

        if (ModeratorsLayer::s_instance && ModeratorsLayer::s_instance->isScoreInList(score)) {
            Ref<GJScoreCell> self = this;
            Loader::get()->queueInMainThread([self]() {
                if (auto rankLabel = self->getChildByID("rank-label")) {
                    rankLabel->setVisible(false);
                }
                
                if (auto menu = self->getChildByID("cvolton.betterinfo/player-icon-menu")) {
                    menu->setPositionY(menu->getPositionY() - 6.f);
                }
            });
        }
    }
};

void ModeratorsLayer::getUserInfoFailed(int type) {
    PaimonDebug::warn("getUserInfoFailed: type {}", type);
}

void ModeratorsLayer::userInfoChanged(GJUserScore* score) {}
