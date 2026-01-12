#include "LeaderboardLayer.hpp"
#include <Geode/modify/CreatorLayer.hpp>
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Localization.hpp"
#include "../utils/HttpClient.hpp"
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

// Shaders
constexpr auto vertexShaderCell =
R"(
attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texCoord;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

void main()
{
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
})";

constexpr auto fragmentShaderHorizontal = R"(
#ifdef GL_ES
precision mediump float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_radius;

void main() {
    float scaledRadius = u_radius * u_screenSize.y * 0.5;
    vec2 texOffset = 1.0 / u_screenSize;
    vec2 direction = vec2(texOffset.x, 0.0);

    vec3 result = texture2D(u_texture, v_texCoord).rgb;
    float weightSum = 1.0;
    float weight = 1.0;

    float fastScale = u_radius * 10.0 / ((u_radius * 10.0 + 1.0) * (u_radius * 10.0 + 1.0) - 1.0);
    scaledRadius *= fastScale;

    for (int i = 1; i < 64; i++) {
        if (float(i) >= scaledRadius) break;

        weight -= 1.0 / scaledRadius;
        if (weight <= 0.0) break;

        vec2 offset = direction * float(i);
        result += texture2D(u_texture, v_texCoord + offset).rgb * weight;
        result += texture2D(u_texture, v_texCoord - offset).rgb * weight;
        weightSum += 2.0 * weight;
    }

    result /= weightSum;
    gl_FragColor = vec4(result, 1.0) * v_fragmentColor;
})";

constexpr auto fragmentShaderVertical = R"(
#ifdef GL_ES
precision mediump float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_radius;

void main() {
    float scaledRadius = u_radius * u_screenSize.y * 0.5;
    vec2 texOffset = 1.0 / u_screenSize;
    vec2 direction = vec2(0.0, texOffset.y);

    vec3 result = texture2D(u_texture, v_texCoord).rgb;
    float weightSum = 1.0;
    float weight = 1.0;

    float fastScale = u_radius * 10.0 / ((u_radius * 10.0 + 1.0) * (u_radius * 10.0 + 1.0) - 1.0);
    scaledRadius *= fastScale;

    for (int i = 1; i < 64; i++) {
        if (float(i) >= scaledRadius) break;

        weight -= 1.0 / scaledRadius;
        if (weight <= 0.0) break;

        vec2 offset = direction * float(i);
        result += texture2D(u_texture, v_texCoord + offset).rgb * weight;
        result += texture2D(u_texture, v_texCoord - offset).rgb * weight;
        weightSum += 2.0 * weight;
    }

    result /= weightSum;
    gl_FragColor = vec4(result, 1.0) * v_fragmentColor;
})";

static void applyBlurPass(CCSprite* input, CCRenderTexture* output, CCGLProgram* program, CCSize const& size, float radius) {
    input->setShaderProgram(program);
    input->setPosition(size * 0.5f);

    program->use();
    program->setUniformsForBuiltins();
    program->setUniformLocationWith2f(
        program->getUniformLocationForName("u_screenSize"),
        size.width, size.height
    );
    program->setUniformLocationWith1f(
        program->getUniformLocationForName("u_radius"),
        radius
    );

    output->begin();
    input->visit();
    output->end();
}

constexpr auto fragmentShaderSaturationCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity; // Saturation: 1.0 = normal
uniform float u_brightness; // Brightness: 1.0 = normal

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    
    // Saturation
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 grayColor = vec3(gray);
    vec3 saturated = mix(grayColor, color.rgb, u_intensity);
    
    // Brightness
    vec3 finalRGB = saturated * u_brightness;
    
    gl_FragColor = vec4(finalRGB, color.a);
})";

constexpr auto fragmentShaderAtmosphere = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity; // 0.0 to 1.0

void main() {
    // Simple 4-tap blur for atmosphere
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    float offset = u_intensity * 2.0;
    
    vec4 color = texture2D(u_texture, v_texCoord);
    color += texture2D(u_texture, v_texCoord + vec2(offset, offset) * onePixel);
    color += texture2D(u_texture, v_texCoord + vec2(-offset, offset) * onePixel);
    color += texture2D(u_texture, v_texCoord + vec2(offset, -offset) * onePixel);
    color += texture2D(u_texture, v_texCoord + vec2(-offset, -offset) * onePixel);
    
    gl_FragColor = (color / 5.0) * v_fragmentColor;
})";

static CCGLProgram* getOrCreateShaderCell(char const* key, char const* vertexSrc, char const* fragmentSrc) {
    auto shaderCache = CCShaderCache::sharedShaderCache();
    if (auto program = shaderCache->programForKey(key)) {
        return program;
    }

    auto program = new CCGLProgram();
    program->initWithVertexShaderByteArray(vertexSrc, fragmentSrc);
    program->addAttribute("a_position", kCCVertexAttrib_Position);
    program->addAttribute("a_color", kCCVertexAttrib_Color);
    program->addAttribute("a_texCoord", kCCVertexAttrib_TexCoords);

    if (!program->link()) {
        log::error("Failed to link shader: {}", key);
        program->release();
        return nullptr;
    }

    program->updateUniforms();
    shaderCache->addProgram(program, key);
    program->release();
    return program;
}

namespace {
    class LeaderboardPaimonSprite : public CCSprite {
    public:
        float m_intensity = 1.0f;
        float m_brightness = 1.0f;
        CCSize m_texSize = {0, 0};
        
        static LeaderboardPaimonSprite* create() {
            auto sprite = new LeaderboardPaimonSprite();
            if (sprite && sprite->init()) {
                sprite->autorelease();
                return sprite;
            }
            CC_SAFE_DELETE(sprite);
            return nullptr;
        }

        static LeaderboardPaimonSprite* createWithTexture(CCTexture2D* texture) {
            auto sprite = new LeaderboardPaimonSprite();
            if (sprite && sprite->initWithTexture(texture)) {
                sprite->autorelease();
                return sprite;
            }
            CC_SAFE_DELETE(sprite);
            return nullptr;
        }

        void draw() override {
            if (getShaderProgram()) {
                getShaderProgram()->use();
                getShaderProgram()->setUniformsForBuiltins();
                
                GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
                if (intensityLoc != -1) {
                    getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
                }
                
                GLint brightLoc = getShaderProgram()->getUniformLocationForName("u_brightness");
                if (brightLoc != -1) {
                    getShaderProgram()->setUniformLocationWith1f(brightLoc, m_brightness);
                }

                GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
                if (sizeLoc != -1) {
                    if (m_texSize.width == 0 && getTexture()) {
                        m_texSize = getTexture()->getContentSizeInPixels();
                    }
                    float w = m_texSize.width > 0 ? m_texSize.width : 1.0f;
                    float h = m_texSize.height > 0 ? m_texSize.height : 1.0f;
                    getShaderProgram()->setUniformLocationWith2f(sizeLoc, w, h);
                }
            }
            CCSprite::draw();
        }
    };
}

static LeaderboardPaimonSprite* createBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float blurRadius = 0.04f) {
    if (!texture) return nullptr;
    
    auto rtA = CCRenderTexture::create(targetSize.width, targetSize.height);
    auto rtB = CCRenderTexture::create(targetSize.width, targetSize.height);
    
    auto blurH = getOrCreateShaderCell("blur-horizontal", vertexShaderCell, fragmentShaderHorizontal);
    auto blurV = getOrCreateShaderCell("blur-vertical", vertexShaderCell, fragmentShaderVertical);

    if (blurH && blurV && rtA && rtB) {
        auto tempSprite = CCSprite::createWithTexture(texture);
        
        // Scale tempSprite to cover targetSize
        float scaleX = targetSize.width / texture->getContentSize().width;
        float scaleY = targetSize.height / texture->getContentSize().height;
        float scale = std::max(scaleX, scaleY);
        tempSprite->setScale(scale);
        tempSprite->setPosition(targetSize / 2);

        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        texture->setTexParameters(&params);

        applyBlurPass(tempSprite, rtA, blurH, targetSize, blurRadius);

        auto spriteA = rtA->getSprite();
        spriteA->getTexture()->setAntiAliasTexParameters();
        applyBlurPass(spriteA, rtB, blurV, targetSize, blurRadius);

        auto finalTexture = rtB->getSprite()->getTexture();
        auto finalSprite = LeaderboardPaimonSprite::createWithTexture(finalTexture);
        finalSprite->setFlipY(true);
        return finalSprite;
    }
    return nullptr;
}

static void calculateLevelCellThumbScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float& outScaleX, float& outScaleY) {
    if (!sprite) return;
    
    const float contentWidth = sprite->getContentSize().width;
    const float contentHeight = sprite->getContentSize().height;
    const float desiredWidth = bgWidth * widthFactor;
    
    outScaleY = bgHeight / contentHeight;
    
    float minScaleX = outScaleY; 
    float desiredScaleX = desiredWidth / contentWidth;
    outScaleX = std::max(minScaleX, desiredScaleX);
}

LeaderboardLayer* LeaderboardLayer::create() {
    auto ret = new LeaderboardLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* LeaderboardLayer::scene() {
    auto scene = CCScene::create();
    auto layer = LeaderboardLayer::create();
    scene->addChild(layer);
    return scene;
}

bool LeaderboardLayer::init() {
    if (!CCLayer::init()) return false;
    
    m_page = 0;
    m_allItems = nullptr;
    
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // Background
    auto bg = CCSprite::create("GJ_gradientBG.png");
    bg->setID("background");
    bg->setPosition(winSize / 2);
    bg->setScaleX(winSize.width / bg->getContentSize().width);
    bg->setScaleY(winSize.height / bg->getContentSize().height);
    bg->setColor({20, 20, 20}); // Darker base
    bg->setZOrder(-10); // Ensure it's behind
    this->addChild(bg);

    // Dynamic Background Sprite
    m_bgSprite = LeaderboardPaimonSprite::create(); 
    m_bgSprite->setPosition(winSize / 2);
    m_bgSprite->setVisible(false);
    m_bgSprite->setZOrder(-5); // On top of bg
    this->addChild(m_bgSprite);

    // Overlay for transitions
    m_bgOverlay = CCLayerColor::create({0, 0, 0, 0});
    m_bgOverlay->setContentSize(winSize);
    m_bgOverlay->setZOrder(-4); // On top of sprite
    this->addChild(m_bgOverlay);

    this->scheduleUpdate();

    // Back Button
    auto menu = CCMenu::create();
    menu->setPosition(0, 0);
    this->addChild(menu);

    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this,
        menu_selector(LeaderboardLayer::onBack)
    );
    backBtn->setPosition(25, winSize.height - 25);
    menu->addChild(backBtn);

    // Title removed

    // Tabs
    auto tabMenu = CCMenu::create();
    tabMenu->setPosition(0, 0); // Manual positioning
    tabMenu->setZOrder(10);
    this->addChild(tabMenu);
    m_tabsMenu = tabMenu;

    auto createTab = [&](const char* text, const char* id, CCPoint pos) -> CCMenuItemToggler* {
        // Manual construction to avoid ButtonSprite file loading crash
        auto createBtn = [&](const char* frameName) -> CCNode* {
            auto sprite = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName(frameName);
            sprite->setContentSize({100.f, 30.f});
            
            auto label = CCLabelBMFont::create(text, "goldFont.fnt");
            label->setScale(0.6f);
            label->setPosition({sprite->getContentSize().width / 2, sprite->getContentSize().height / 2 + 2.f});
            sprite->addChild(label);
            
            return sprite;
        };

        auto onSprite = createBtn("GJ_longBtn01_001.png");
        auto offSprite = createBtn("GJ_longBtn02_001.png");
        
        auto tab = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(LeaderboardLayer::onTab));
        tab->setUserObject(CCString::create(id));
        tab->setPosition(pos);
        m_tabs.push_back(tab);
        return tab;
    };

    // Layout Calculation
    float topY = winSize.height - 40.f; // Lowered from -20.f to -40.f
    float centerX = winSize.width / 2;
    float btnSpacing = 105.f; // Horizontal spacing

    // Horizontal Buttons at Top
    // Order: Daily | Weekly | All Time | Creators
    // Positions: -1.5, -0.5, 0.5, 1.5 units from center
    
    auto dailyBtn = createTab(Localization::get().getString("leaderboard.daily").c_str(), "daily", {centerX - btnSpacing * 1.5f, topY});
    dailyBtn->toggle(true); 
    tabMenu->addChild(dailyBtn);

    auto weeklyBtn = createTab(Localization::get().getString("leaderboard.weekly").c_str(), "weekly", {centerX - btnSpacing * 0.5f, topY});
    tabMenu->addChild(weeklyBtn);

    auto allTimeBtn = createTab(Localization::get().getString("leaderboard.all_time").c_str(), "alltime", {centerX + btnSpacing * 0.5f, topY});
    tabMenu->addChild(allTimeBtn);

    auto creatorsBtn = createTab(Localization::get().getString("leaderboard.creators").c_str(), "creators", {centerX + btnSpacing * 1.5f, topY});
    tabMenu->addChild(creatorsBtn);

    // Loading Spinner
    m_loadingSpinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
    if (!m_loadingSpinner) {
        m_loadingSpinner = CCSprite::create("loadingCircle.png");
    }
    if (m_loadingSpinner) {
        m_loadingSpinner->setPosition(winSize / 2);
        m_loadingSpinner->setScale(1.0f);
        m_loadingSpinner->setVisible(false);
        this->addChild(m_loadingSpinner, 100);
    }

    this->setKeypadEnabled(true);

    // Show initial loading
    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(true);
        m_loadingSpinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.0f, 360.f)));
    }

    loadLeaderboard("daily");

    return true;
}

void LeaderboardLayer::onBack(CCObject*) {
    CC_SAFE_RELEASE(m_allItems);
    CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, CreatorLayer::scene()));
}

void LeaderboardLayer::keyBackClicked() {
    onBack(nullptr);
}

void LeaderboardLayer::onTab(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    auto type = static_cast<CCString*>(toggler->getUserObject())->getCString();
    
    if (m_currentType == type) {
        toggler->toggle(true); // Keep it on
        return;
    }
    m_currentType = type;
    
    // Reset pagination
    m_page = 0;
    CC_SAFE_RELEASE_NULL(m_allItems);

    // Update other tabs
    for (auto tab : m_tabs) {
        tab->toggle(tab == toggler);
    }

    // Clear existing list
    if (this->getChildByTag(999)) {
        this->removeChildByTag(999);
    }
    m_scroll = nullptr;
    m_listMenu = nullptr;

    // Show loading spinner
    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(true);
        m_loadingSpinner->stopAllActions();
        m_loadingSpinner->runAction(CCRepeatForever::create(CCRotateBy::create(1.0f, 360.f)));
    }
    
    loadLeaderboard(type);
}

void LeaderboardLayer::loadLeaderboard(std::string type) {
    if (m_featuredLevel) {
        m_featuredLevel->release();
        m_featuredLevel = nullptr;
    }
    m_featuredExpiresAt = 0;

    if (type == "daily" || type == "weekly") {
        std::string url = "https://paimon-thumbnails-server.paimonalcuadrado.workers.dev/api/" + type + "/current";
        
        m_listener.bind([this, type](web::WebTask::Event* e) {
            if (auto res = e->getValue()) {
                if (res->ok()) {
                    auto json = res->string().unwrapOr("{}");
                    auto dataRes = matjson::parse(json);
                    if (dataRes.isOk()) {
                        auto data = dataRes.unwrap();
                        if (data["success"].asBool().unwrapOr(false)) {
                            auto levelData = data["data"];
                            int levelID = levelData["levelID"].asInt().unwrapOr(0);
                            m_featuredExpiresAt = (long long)levelData["expiresAt"].asDouble().unwrapOr(0);

                            if (levelID > 0) {
                                auto level = GJGameLevel::create();
                                level->m_levelID = levelID;
                                level->m_levelName = Localization::get().getString("leaderboard.loading");
                                level->m_creatorName = Localization::get().getString("leaderboard.loading");
                                
                                auto saved = GameLevelManager::sharedState()->getSavedLevel(levelID);
                                if (saved) {
                                    level->m_levelName = saved->m_levelName;
                                    level->m_creatorName = saved->m_creatorName;
                                    level->m_stars = saved->m_stars;
                                    level->m_difficulty = saved->m_difficulty;
                                    level->m_demon = saved->m_demon;
                                    level->m_demonDifficulty = saved->m_demonDifficulty;
                                }
                                level->retain();
                                m_featuredLevel = level;
                                
                                // Fetch details from RobTop servers (GameLevelManager)
                                auto searchObj = GJSearchObject::create(SearchType::MapPackOnClick, std::to_string(levelID));
                                auto glm = GameLevelManager::sharedState();
                                glm->m_levelManagerDelegate = this;
                                glm->getOnlineLevels(searchObj);
                            }
                        }
                    }
                }
                // For Daily/Weekly, we ONLY show the featured level now.
                // So we call createList with nullptr items.
                if (m_loadingSpinner) {
                    m_loadingSpinner->setVisible(false);
                    m_loadingSpinner->stopAllActions();
                }
                
                if (m_featuredLevel) {
                    this->updateBackground(m_featuredLevel->m_levelID);
                } else {
                    this->updateBackground(0);
                }

                this->createList(nullptr, type);
            } else if (e->isCancelled()) {
                if (m_loadingSpinner) {
                    m_loadingSpinner->setVisible(false);
                    m_loadingSpinner->stopAllActions();
                }
            }
        });
        auto req = web::WebRequest();
        m_listener.setFilter(req.get(url));
    } else {
        fetchLeaderboardList(type);
    }
}

void LeaderboardLayer::fetchLeaderboardList(std::string type) {
    std::string url = "https://paimon-thumbnails-server.paimonalcuadrado.workers.dev/api/leaderboard?type=" + type;
    
    m_listener.bind([this, type](web::WebTask::Event* e) {
        if (auto res = e->getValue()) {
            if (res->ok()) {
                auto strRes = res->string();
                if (strRes) {
                    onLeaderboardLoaded(type, strRes.unwrap());
                } else {
                    if (m_loadingSpinner) {
                        m_loadingSpinner->setVisible(false);
                        m_loadingSpinner->stopAllActions();
                    }
                    FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.read_error").c_str(), "OK")->show();
                }
            } else {
                if (m_loadingSpinner) {
                    m_loadingSpinner->setVisible(false);
                    m_loadingSpinner->stopAllActions();
                }
                FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), fmt::format(fmt::runtime(Localization::get().getString("leaderboard.load_error")), res->code()).c_str(), "OK")->show();
            }
        } else if (e->isCancelled()) {
            if (m_loadingSpinner) {
                m_loadingSpinner->setVisible(false);
                m_loadingSpinner->stopAllActions();
            }
        }
    });

    auto req = web::WebRequest();
    m_listener.setFilter(req.get(url));
}

void LeaderboardLayer::onLeaderboardLoaded(std::string type, std::string json) {
    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(false);
        m_loadingSpinner->stopAllActions();
    }
    
    try {
        auto dataRes = matjson::parse(json);
        if (!dataRes) {
            FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.parse_error").c_str(), "OK")->show();
            return;
        }
        auto data = dataRes.unwrap();

        if (!data.contains("success") || !data["success"].asBool().unwrapOr(false)) {
            FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.server_error").c_str(), "OK")->show();
            return;
        }

        if (!data["data"].isArray()) {
             FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.invalid_format").c_str(), "OK")->show();
             return;
        }

        auto items = data["data"].asArray().unwrap();
        auto ccArray = CCArray::create();

        std::vector<int> levelIDs;
        std::string searchIDs = "";
        bool needsSearch = false;

        for (auto& item : items) {
            if (type == "creators") {
                auto score = GJUserScore::create();
                score->m_userName = item["username"].asString().unwrapOr(std::string(Localization::get().getString("leaderboard.unknown")));
                score->m_stars = item["totalStars"].asInt().unwrapOr(0); 
                score->m_globalRank = item["totalVotes"].asInt().unwrapOr(0); 
                score->m_iconID = 1; // Default icon
                score->m_color1 = 10;
                score->m_color2 = 11;
                score->m_iconType = IconType::Cube;
                ccArray->addObject(score);
            } else {
                int levelID = item["levelId"].asInt().unwrapOr(0);
                if (levelID > 0) levelIDs.push_back(levelID);

                auto level = GJGameLevel::create();
                level->m_levelID = levelID;
                level->m_levelName = Localization::get().getString("leaderboard.loading");
                level->m_creatorName = item["uploadedBy"].asString().unwrapOr(std::string(Localization::get().getString("leaderboard.unknown")));
                
                // Store rating info in UserObject
                double rating = item["rating"].asDouble().unwrapOr(0.0);
                int count = item["count"].asInt().unwrapOr(0);
                auto ratingStr = CCString::createWithFormat("%.1f/5 (%d)", rating, count);
                level->setUserObject(ratingStr);

                // Try to find saved level info
                auto savedLevel = GameLevelManager::sharedState()->getSavedLevel(levelID);
                if (savedLevel && savedLevel->m_levelName.length() > 0) {
                    level->m_levelName = savedLevel->m_levelName;
                    level->m_creatorName = savedLevel->m_creatorName;
                    level->m_stars = savedLevel->m_stars;
                    level->m_difficulty = savedLevel->m_difficulty;
                    level->m_demon = savedLevel->m_demon;
                    level->m_demonDifficulty = savedLevel->m_demonDifficulty;
                    level->m_userID = savedLevel->m_userID;
                    level->m_accountID = savedLevel->m_accountID;
                } else {
                    if (!searchIDs.empty()) searchIDs += ",";
                    searchIDs += std::to_string(levelID);
                    needsSearch = true;
                }

                ccArray->addObject(level);
            }
        }

        if (m_allItems) m_allItems->release();
        m_allItems = ccArray;
        m_allItems->retain();

        if (needsSearch) {
            auto searchObj = GJSearchObject::create(SearchType::MapPackOnClick, searchIDs);
            auto glm = GameLevelManager::sharedState();
            glm->m_levelManagerDelegate = this;
            glm->getOnlineLevels(searchObj);
        }

        // Set random background from the list
        if (!levelIDs.empty()) {
            // Simple random pick
            int idx = rand() % levelIDs.size();
            this->updateBackground(levelIDs[idx]);
        }

        refreshList();

    } catch (std::exception& e) {
        FLAlertLayer::create(Localization::get().getString("leaderboard.error").c_str(), Localization::get().getString("leaderboard.parse_error").c_str(), "OK")->show();
    }
}

void LeaderboardLayer::refreshList() {
    if (!m_allItems) {
        createList(nullptr, m_currentType);
        return;
    }

    // If not paginated type, just show all (or if it's daily/weekly which are special)
    if (m_currentType == "daily" || m_currentType == "weekly") {
        createList(m_allItems, m_currentType);
        return;
    }

    int totalItems = m_allItems->count();
    int start = m_page * ITEMS_PER_PAGE;
    int end = std::min(start + ITEMS_PER_PAGE, totalItems);
    
    auto pageItems = CCArray::create();
    for (int i = start; i < end; i++) {
        pageItems->addObject(m_allItems->objectAtIndex(i));
    }
    
    createList(pageItems, m_currentType);
}

void LeaderboardLayer::onNextPage(CCObject*) {
    if (!m_allItems) return;
    int totalPages = (m_allItems->count() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (m_page < totalPages - 1) {
        m_page++;
        refreshList();
    }
}

void LeaderboardLayer::onPrevPage(CCObject*) {
    if (m_page > 0) {
        m_page--;
        refreshList();
    }
}

void LeaderboardLayer::createList(CCArray* items, std::string type) {
    static int LIST_CONTAINER_TAG = 999;
    this->removeChildByTag(LIST_CONTAINER_TAG);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    float width = 380.f;
    float height = 210.f;
    float xPos = winSize.width / 2 - width / 2;
    float yPos = winSize.height / 2 - height / 2 - 20.f;

    auto container = CCNode::create();
    container->setTag(LIST_CONTAINER_TAG);
    this->addChild(container);

    // Background Panel
    // Background Panel
    auto panel = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
    if (!panel) {
        // Fallback or try loading sheet? 
        // Try creating without frame name if it's a file, but it likely isn't.
        // Let's try to force load the sheet just in case
        CCSpriteFrameCache::sharedSpriteFrameCache()->addSpriteFramesWithFile("GJ_GameSheet03.plist");
        panel = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
    }
    
    if (panel) {
        panel->setColor({0, 0, 0});
        panel->setOpacity(20);
        panel->setContentSize({width, height});
        panel->setPosition({winSize.width / 2, winSize.height / 2 - 20.f});
        container->addChild(panel);
    } else {
        // Fallback: LayerColor
        auto fallback = CCLayerColor::create({0, 0, 0, 20});
        fallback->setContentSize({width, height});
        fallback->setPosition({winSize.width / 2 - width/2, winSize.height / 2 - 20.f - height/2});
        container->addChild(fallback);
    }

    m_listMenu = CCLayer::create();
    m_listMenu->setPosition({0, 0});

    m_scroll = cocos2d::extension::CCScrollView::create();
    m_scroll->setViewSize({width, height});
    m_scroll->setPosition({xPos, yPos});
    m_scroll->setDirection(cocos2d::extension::kCCScrollViewDirectionVertical);
    m_scroll->setContainer(m_listMenu);
    container->addChild(m_scroll);

    if (type == "alltime") {
        auto gm = GameManager::sharedState();
        if (gm) {
            auto username = gm->m_playerName;
            auto accountID = gm->m_playerUserID;
            
            // Create button but hide it initially
            auto recalcSpr = ButtonSprite::create("Recalculate", 0, false, "goldFont.fnt", "GJ_button_01.png", 0, 0.6f);
            auto recalcBtn = CCMenuItemSpriteExtra::create(recalcSpr, this, menu_selector(LeaderboardLayer::onRecalculate));
            recalcBtn->setPosition({winSize.width / 2 + width / 2 + 40.f, winSize.height / 2}); // Right of list
            recalcBtn->setVisible(false);
            
            auto menu = CCMenu::create();
            menu->setPosition({0, 0});
            menu->addChild(recalcBtn);
            container->addChild(menu);
            
            Ref<LeaderboardLayer> self = this;
            Ref<CCMenuItemSpriteExtra> btnRef = recalcBtn;
            ThumbnailAPI::get().checkModeratorAccount(username, accountID, [self, btnRef](bool isMod, bool isAdmin) {
                if (isAdmin) {
                    btnRef->setVisible(true);
                }
            });
        }
    }

    // Pagination Controls
    if (m_allItems && m_allItems->count() > ITEMS_PER_PAGE && type != "daily" && type != "weekly") {
        auto menu = CCMenu::create();
        menu->setPosition({winSize.width / 2, winSize.height / 2 - 20.f}); // Center of list
        container->addChild(menu);
        
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        prevSpr->setScale(0.8f);
        auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(LeaderboardLayer::onPrevPage));
        prevBtn->setPosition({-210.f, 0.f}); // Left of list
        if (m_page == 0) prevBtn->setVisible(false);
        menu->addChild(prevBtn);
        
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        nextSpr->setFlipX(true);
        nextSpr->setScale(0.8f);
        auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(LeaderboardLayer::onNextPage));
        nextBtn->setPosition({210.f, 0.f}); // Right of list
        
        int totalPages = (m_allItems->count() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        if (m_page >= totalPages - 1) nextBtn->setVisible(false);
        menu->addChild(nextBtn);
        
        auto pageLbl = CCLabelBMFont::create(fmt::format("Page {}/{}", m_page + 1, totalPages).c_str(), "goldFont.fnt");
        pageLbl->setScale(0.5f);
        pageLbl->setPosition({0.f, -125.f}); // Bottom of list + padding
        menu->addChild(pageLbl);
    }


    // Helper to create a cell
    auto createCell = [&](CCObject* obj, float w, float h, float y, bool isFeatured, int rank) {
        // Cell Container
        auto cell = CCNode::create();
        cell->setContentSize({w, h});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({width / 2, y});
        m_listMenu->addChild(cell);

        // Entry Animation
        cell->setScale(0.5f);
        cell->runAction(CCEaseBackOut::create(CCScaleTo::create(0.4f, 1.0f)));

        // Background (Rounded)
        // Background (Rounded)
        auto rowBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
        if (rowBg) {
            rowBg->setColor({0, 0, 0});
            rowBg->setOpacity(10);
            rowBg->setContentSize(cell->getContentSize());
            rowBg->setPosition(cell->getContentSize() / 2);
            rowBg->setZOrder(1); // Ensure it's on top
            cell->addChild(rowBg);
        } else {
             auto rowFallback = CCLayerColor::create({0, 0, 0, 30}); // slightly higher opacity for fallback
             rowFallback->setContentSize(cell->getContentSize());
             rowFallback->setZOrder(1);
             cell->addChild(rowFallback);
        }

        // Data extraction
        std::string nameStr = "Unknown";
        std::string creatorStr = "";
        std::string ratingStr = "";
        int levelID = 0;
        
        if (type == "creators") {
            auto score = static_cast<GJUserScore*>(obj);
            nameStr = score->m_userName;
            creatorStr = fmt::format("{} stars", score->m_stars);
        } else {
            auto level = static_cast<GJGameLevel*>(obj);
            nameStr = level->m_levelName;
            creatorStr = level->m_creatorName;
            levelID = level->m_levelID;
            
            auto ratingObj = static_cast<CCString*>(level->getUserObject());
            if (ratingObj) ratingStr = ratingObj->getCString();
        }

        // Skewed Thumbnail
        if (levelID > 0) {
            auto texture = LocalThumbs::get().loadTexture(levelID);
            
            // If texture is missing, request download
            if (!texture) {
                std::string fileName = fmt::format("{}.png", levelID);
                cell->retain(); // Keep cell alive
                ThumbnailLoader::get().requestLoad(levelID, fileName, [cell, levelID](CCTexture2D* tex, bool) {
                    geode::Loader::get()->queueInMainThread([cell, levelID, tex] {
                        // Check if cell is still valid (basic check)
                        if (cell->getParent()) {
                    if (tex) {
                        // 1. Update Background Blur
                        // Increased blur by 90% (0.076 * 1.9 = 0.144)
                        CCSprite* bgSprite = createBlurredSprite(tex, cell->getContentSize(), 0.144f);
                        if (!bgSprite) {
                             bgSprite = CCSprite::createWithTexture(tex);
                        }
                        
                        // Clipper for background
                        auto bgStencil = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
                        bgStencil->setContentSize(cell->getContentSize());
                        bgStencil->setAnchorPoint({0, 0});
                        bgStencil->setPosition({0, 0});

                        auto bgClipper = CCClippingNode::create(bgStencil);
                        // bgClipper->setAlphaThreshold(0.05f); // Removed to avoid issues with scale9sprite
                        bgClipper->setContentSize(cell->getContentSize());
                        bgClipper->setPosition({0, 0});
                        bgClipper->setZOrder(0); // Behind rowBg
                        bgClipper->setTag(104);

                        // Scale to cover
                        float scaleX = cell->getContentSize().width / bgSprite->getContentSize().width;
                        float scaleY = cell->getContentSize().height / bgSprite->getContentSize().height;
                        bgSprite->setScale(std::max(scaleX, scaleY));
                        bgSprite->setPosition(cell->getContentSize() / 2);
                        bgSprite->setColor({255, 255, 255}); 
                        
                        bgClipper->addChild(bgSprite);

                        // Remove default gradient
                        cell->removeChildByTag(102);
                        // Remove old default background
                        cell->removeChildByTag(104);
                        
                        cell->addChild(bgClipper);
                        
                        // Add overlay gradient (clipped) - 40% opacidad
                        auto overlay = CCLayerGradient::create({0, 0, 0, 102}, {0, 0, 0, 51});
                        overlay->setContentSize({cell->getContentSize().width, cell->getContentSize().height + 2.f});
                        overlay->setPosition({0, -1.f});
                        overlay->setVector({1, 0});
                        bgClipper->addChild(overlay, 10);


                        // 2. Re-create thumbnail part
                        auto sprite = LeaderboardPaimonSprite::createWithTexture(tex);
                        auto shader = getOrCreateShaderCell("paimon_cell_saturation", vertexShaderCell, fragmentShaderSaturationCell);
                        if (shader) {
                            sprite->setShaderProgram(shader);
                            sprite->m_intensity = 0.8f;
                            sprite->m_brightness = 0.6f;
                        }                                float bgWidth = cell->getContentSize().width;
                                float bgHeight = cell->getContentSize().height;
                                float kThumbWidthFactor = 0.65f;

                                float thumbScaleX, thumbScaleY;
                                calculateLevelCellThumbScale(sprite, bgWidth, bgHeight, kThumbWidthFactor, thumbScaleX, thumbScaleY);
                                sprite->setScaleX(thumbScaleX);
                                sprite->setScaleY(thumbScaleY);

                                float desiredWidth = bgWidth * kThumbWidthFactor;
                                float angle = 18.f;
                                CCSize scaledSize{ desiredWidth, sprite->getContentHeight() * thumbScaleY };

                                auto mask = CCLayerColor::create({255, 255, 255});
                                mask->setContentSize(scaledSize);
                                mask->setAnchorPoint({1, 0});
                                mask->setSkewX(angle);

                                auto clipper = CCClippingNode::create();
                                clipper->setStencil(mask);
                                clipper->setContentSize(scaledSize);
                                clipper->setAnchorPoint({1, 0});
                                clipper->setPosition({bgWidth, 0.f});
                                clipper->setZOrder(1);
                                
                                sprite->setPosition(clipper->getContentSize() * 0.5f);
                                clipper->addChild(sprite);
                                
                                // Remove old clipper if exists (tag 101)
                                cell->removeChildByTag(101);
                                clipper->setTag(101);
                                
                                cell->addChild(clipper);
                                
                                // Update Separator Position
                                if (auto sep = cell->getChildByTag(103)) {
                                    float sepX = bgWidth - desiredWidth;
                                    sep->setPosition({sepX, 0.f});
                                }
                            }
                        }
                        cell->release(); // Release reference
                    });
                });
            }

            if (texture) {
                // 1. Background Blur
                // Increased blur by 90% (0.076 * 1.9 = 0.144)
                CCSprite* bgSprite = createBlurredSprite(texture, cell->getContentSize(), 0.144f);
                if (!bgSprite) {
                     bgSprite = CCSprite::createWithTexture(texture);
                }
                
                // Clipper for background
                auto bgStencil = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName("square02_001.png");
                bgStencil->setContentSize(cell->getContentSize());
                bgStencil->setAnchorPoint({0, 0});
                bgStencil->setPosition({0, 0});

                auto bgClipper = CCClippingNode::create(bgStencil);
                // bgClipper->setAlphaThreshold(0.05f); // Removed
                bgClipper->setContentSize(cell->getContentSize());
                bgClipper->setPosition({0, 0});
                bgClipper->setZOrder(0); // Behind rowBg
                bgClipper->setTag(104);

                // Scale to cover
                float scaleX = cell->getContentSize().width / bgSprite->getContentSize().width;
                float scaleY = cell->getContentSize().height / bgSprite->getContentSize().height;
                bgSprite->setScale(std::max(scaleX, scaleY));
                bgSprite->setPosition(cell->getContentSize() / 2);
                bgSprite->setColor({255, 255, 255}); 
                
                bgClipper->addChild(bgSprite);

                // Remove default gradient
                cell->removeChildByTag(102);
                
                // Remove default background (replace with blurred bg)
                cell->removeChildByTag(104);
                
                // Add blurred bg
                cell->addChild(bgClipper);
                
                // Add overlay gradient (clipped) - 40% opacidad
                auto overlay = CCLayerGradient::create({0, 0, 0, 102}, {0, 0, 0, 51});
                overlay->setContentSize({cell->getContentSize().width, cell->getContentSize().height + 2.f});
                overlay->setPosition({0, -1.f});
                overlay->setVector({1, 0});
                bgClipper->addChild(overlay, 10);

                // 2. Thumbnail
                auto sprite = LeaderboardPaimonSprite::createWithTexture(texture);
                
                auto shader = getOrCreateShaderCell("paimon_cell_saturation", vertexShaderCell, fragmentShaderSaturationCell);
                if (shader) {
                    sprite->setShaderProgram(shader);
                    sprite->m_intensity = 0.8f;
                    sprite->m_brightness = 0.6f;
                }

                float bgWidth = cell->getContentSize().width;
                float bgHeight = cell->getContentSize().height;
                float kThumbWidthFactor = 0.65f;

                float thumbScaleX, thumbScaleY;
                calculateLevelCellThumbScale(sprite, bgWidth, bgHeight, kThumbWidthFactor, thumbScaleX, thumbScaleY);
                sprite->setScaleX(thumbScaleX);
                sprite->setScaleY(thumbScaleY);

                float desiredWidth = bgWidth * kThumbWidthFactor;
                float angle = 18.f;
                CCSize scaledSize{ desiredWidth, sprite->getContentHeight() * thumbScaleY };

                auto mask = CCLayerColor::create({255, 255, 255});
                mask->setContentSize(scaledSize);
                mask->setAnchorPoint({1, 0});
                mask->setSkewX(angle);

                auto clipper = CCClippingNode::create();
                clipper->setStencil(mask);
                clipper->setContentSize(scaledSize);
                clipper->setAnchorPoint({1, 0});
                clipper->setPosition({bgWidth, 0.f});
                clipper->setZOrder(1);
                clipper->setTag(101);
                
                sprite->setPosition(clipper->getContentSize() * 0.5f);
                clipper->addChild(sprite);
                
                cell->addChild(clipper);
            }
        }

        // Separator Line (LevelCell style)
        if (type != "creators") {
            float angle = 18.f;
            float bgWidth = cell->getContentSize().width;
            float kThumbWidthFactor = 0.65f;
            float desiredWidth = bgWidth * kThumbWidthFactor;
            
            auto separator = CCLayerColor::create({0, 0, 0, 100});
            separator->setContentSize({5.f, cell->getContentSize().height});
            separator->setAnchorPoint({1, 0});
            separator->setSkewX(angle);
            
            separator->setPosition({bgWidth - desiredWidth, 0.f}); 
            
            separator->setZOrder(2);
            separator->setTag(103);
            cell->addChild(separator);
        }

        // Rank
        if (rank > 0) {
            auto rankLbl = CCLabelBMFont::create(std::to_string(rank).c_str(), "goldFont.fnt");
            rankLbl->setScale(0.5f);
            rankLbl->setPosition({15.f, h / 2});
            
            if (rank == 1) {
                rankLbl->setColor({255, 215, 0}); // Gold
                rankLbl->setScale(0.6f);
            } else if (rank == 2) {
                rankLbl->setColor({192, 192, 192}); // Silver
                rankLbl->setScale(0.55f);
            } else if (rank == 3) {
                rankLbl->setColor({205, 127, 50}); // Bronze
                rankLbl->setScale(0.55f);
            }
            
            cell->addChild(rankLbl, 10);
        }

        // Text Labels
        float textX = 40.f;
        float maxTextWidth = 160.f;
        float scaleMult = isFeatured ? 1.5f : 1.0f;

        auto nameLbl = CCLabelBMFont::create(nameStr.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.4f * scaleMult);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({textX, h / 2 + 8.f * scaleMult});
        if (nameLbl->getScaledContentSize().width > maxTextWidth * scaleMult) {
            nameLbl->setScale(0.4f * scaleMult * ((maxTextWidth * scaleMult) / nameLbl->getScaledContentSize().width));
        }
        cell->addChild(nameLbl, 10);

        auto extraLbl = CCLabelBMFont::create(creatorStr.c_str(), "goldFont.fnt");
        extraLbl->setScale(0.35f * scaleMult);
        extraLbl->setAnchorPoint({0.f, 0.5f});
        extraLbl->setPosition({textX, h / 2 - 8.f * scaleMult});
        cell->addChild(extraLbl, 10);

        // View Button (if level)
        if (type != "creators") {
             auto level = static_cast<GJGameLevel*>(obj);
             auto btnSpr = ButtonSprite::create("View", 40, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.4f * scaleMult);
             auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(LeaderboardLayer::onViewLevel));
             btn->setUserObject(level);
             
             // Create menu for button
             auto cellMenu = CCMenu::create();
             cellMenu->setPosition({0, 0});
             cell->addChild(cellMenu, 20);
             
             float btnX = w - 35.f * scaleMult;
             btn->setPosition({btnX, h / 2}); // Relative to cell
             cellMenu->addChild(btn);

             if (!ratingStr.empty()) {
                 auto ratingLbl = CCLabelBMFont::create(ratingStr.c_str(), "goldFont.fnt");
                 ratingLbl->setScale(0.4f * scaleMult);
                 ratingLbl->setAnchorPoint({1.f, 0.5f});
                 
                 float ratingX = w - 65.f * scaleMult;
                 ratingLbl->setPosition({ratingX, h / 2}); // Relative to cell
                 ratingLbl->setColor({255, 255, 255});
                 cell->addChild(ratingLbl, 20);
             }
        }
        
        // Countdown for featured
        if (isFeatured && m_featuredExpiresAt > 0) {
            long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            long long diff = m_featuredExpiresAt - now;
            if (diff > 0) {
                int hours = diff / (1000 * 60 * 60);
                int mins = (diff % (1000 * 60 * 60)) / (1000 * 60);
                std::string timeStr = fmt::format("Next in: {}h {}m", hours, mins);
                
                auto timeLbl = CCLabelBMFont::create(timeStr.c_str(), "goldFont.fnt");
                timeLbl->setScale(0.4f);
                timeLbl->setPosition({w / 2, 10.f});
                cell->addChild(timeLbl, 30);
            }
        }
    };

    // Logic for Daily/Weekly: Show ONLY the featured level as a big card
    if (type == "daily" || type == "weekly") {
        if (m_featuredLevel) {
            float cardH = 140.f; // Reduced from 180.f
            float cardW = width - 10.f;
            m_listMenu->setContentSize({width, height}); // No scroll needed if it fits, or minimal
            
            // Center vertically
            float y = height / 2;
            createCell(m_featuredLevel, cardW, cardH, y, true, 0);
        } else {
            auto lbl = CCLabelBMFont::create("No Featured Level", "goldFont.fnt");
            lbl->setScale(0.6f);
            lbl->setPosition({winSize.width / 2, winSize.height / 2 - 20.f});
            container->addChild(lbl);
        }
        return;
    }

    // Logic for All Time / Creators: Show List
    if (!items || items->count() == 0) {
        auto lbl = CCLabelBMFont::create("No items found", "goldFont.fnt");
        lbl->setScale(0.6f);
        lbl->setPosition({winSize.width / 2, winSize.height / 2 - 20.f});
        container->addChild(lbl);
        return;
    }

    float rowH = 50.f;
    float totalH = std::max(height, items->count() * rowH);
    m_listMenu->setContentSize({width, totalH});

    for (int i = 0; i < items->count(); ++i) {
        CCObject* obj = items->objectAtIndex(i);
        float y = totalH - (i + 1) * rowH + rowH / 2;
        createCell(obj, width - 10.f, rowH - 4.f, y, false, i + 1);
    }
    
    m_scroll->setContentOffset({0, height - totalH});
}

void LeaderboardLayer::onViewLevel(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto level = static_cast<GJGameLevel*>(btn->getUserObject());
    if (level) {
        auto layer = LevelInfoLayer::create(level, false);
        auto scene = CCScene::create();
        scene->addChild(layer);
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    }
}

void LeaderboardLayer::update(float dt) {
    m_blurTime += dt;
    if (m_bgSprite) {
        // Check if it's our custom sprite
        if (auto paimonSprite = dynamic_cast<LeaderboardPaimonSprite*>(m_bgSprite)) {
             // Variable blur intensity: 0.0 to 1.5 (small extra blur)
             float intensity = 0.75f + std::sin(m_blurTime * 1.0f) * 0.75f;
             paimonSprite->m_intensity = intensity;
        }
    }
}

void LeaderboardLayer::applyBackground(CCTexture2D* texture) {
    if (!texture) return;

    log::info("[LeaderboardLayer] Applying background texture: {}", texture);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    // 1. Create new sprite with baked blur
    // Increased blur by 40% more (0.068 * 1.4 = 0.095)
    auto newSprite = createBlurredSprite(texture, winSize, 0.095f);
    
    if (newSprite) {
        newSprite->setPosition(winSize / 2);
        newSprite->setZOrder(-5); // Same Z as m_bgSprite
        newSprite->setOpacity(0);
        
        // Apply Atmosphere Shader (Variable Blur)
        auto shader = getOrCreateShaderCell("paimon_atmosphere", vertexShaderCell, fragmentShaderAtmosphere);
        if (shader) {
            newSprite->setShaderProgram(shader);
            newSprite->m_intensity = 0.0f; // Start at 0
            newSprite->m_texSize = newSprite->getTexture()->getContentSizeInPixels();
        }
        
        this->addChild(newSprite);
        
        // 2. Transition
        float duration = 0.5f;
        newSprite->runAction(CCFadeIn::create(duration));
        
        // 3. Atmosphere Animation (Breathing Scale)
        auto breathe = CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(6.0f, 1.05f),
            CCScaleTo::create(6.0f, 1.0f),
            nullptr
        ));
        newSprite->runAction(breathe);
        
        // Fade out old sprite
        if (m_bgSprite) {
            m_bgSprite->stopAllActions();
            m_bgSprite->runAction(CCSequence::create(
                CCFadeOut::create(duration),
                CCCallFunc::create(m_bgSprite, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
        }
        
        m_bgSprite = newSprite;
    }
    
    // Fade in overlay if needed
    if (m_bgOverlay) {
        m_bgOverlay->stopAllActions();
        m_bgOverlay->runAction(CCFadeTo::create(0.5f, 100)); // Semi-transparent black
    }
}

void LeaderboardLayer::updateBackground(int levelID) {
    log::info("[LeaderboardLayer] Updating background for levelID: {}", levelID);

    if (levelID <= 0) {
        // Fade to default (transparent overlay, hide sprite)
        if (m_bgSprite) {
            m_bgSprite->stopAllActions();
            m_bgSprite->runAction(CCSequence::create(
                CCFadeOut::create(0.5f),
                CCCallFunc::create(m_bgSprite, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
            m_bgSprite = nullptr;
        }
        if (m_bgOverlay) {
            m_bgOverlay->stopAllActions();
            m_bgOverlay->runAction(CCFadeTo::create(0.5f, 0));
        }
        return;
    }

    auto texture = LocalThumbs::get().loadTexture(levelID);
    if (texture) {
        applyBackground(texture);
    } else {
        // Request download
        std::string fileName = fmt::format("{}.png", levelID);
        Ref<LeaderboardLayer> self = this;
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool) {
            if (tex) tex->retain();
            geode::Loader::get()->queueInMainThread([self, tex] {
                if (self->getParent() || self->isRunning()) {
                    if (tex) {
                        self->applyBackground(tex);
                    }
                }
                if (tex) tex->release();
            });
        });
    }
}

void LeaderboardLayer::loadLevelsFinished(CCArray* levels, const char* key) {
    for (int i = 0; i < levels->count(); ++i) {
        auto downloadedLevel = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
        
        // Update featured level if it matches
        if (m_featuredLevel && m_featuredLevel->m_levelID == downloadedLevel->m_levelID) {
            m_featuredLevel->m_levelName = downloadedLevel->m_levelName;
            m_featuredLevel->m_creatorName = downloadedLevel->m_creatorName;
            m_featuredLevel->m_stars = downloadedLevel->m_stars;
            m_featuredLevel->m_difficulty = downloadedLevel->m_difficulty;
            m_featuredLevel->m_demon = downloadedLevel->m_demon;
            m_featuredLevel->m_demonDifficulty = downloadedLevel->m_demonDifficulty;
            m_featuredLevel->m_userID = downloadedLevel->m_userID;
            m_featuredLevel->m_accountID = downloadedLevel->m_accountID;
            m_featuredLevel->m_levelString = downloadedLevel->m_levelString;
        }

        if (m_allItems) {
            // Find matching level in m_allItems and update it
            for (int j = 0; j < m_allItems->count(); ++j) {
                auto item = m_allItems->objectAtIndex(j);
                // Check if it's a GJGameLevel (not GJUserScore)
                if (auto level = dynamic_cast<GJGameLevel*>(item)) {
                    if (level->m_levelID == downloadedLevel->m_levelID) {
                        level->m_levelName = downloadedLevel->m_levelName;
                        level->m_creatorName = downloadedLevel->m_creatorName;
                        level->m_stars = downloadedLevel->m_stars;
                        level->m_difficulty = downloadedLevel->m_difficulty;
                        level->m_demon = downloadedLevel->m_demon;
                        level->m_demonDifficulty = downloadedLevel->m_demonDifficulty;
                        level->m_userID = downloadedLevel->m_userID;
                        level->m_accountID = downloadedLevel->m_accountID;
                        break;
                    }
                }
            }
        }
    }
    
    refreshList();
}

void LeaderboardLayer::loadLevelsFailed(const char* key) {
    // Just ignore failures, keep "Loading..." or whatever
}

void LeaderboardLayer::setupPageInfo(std::string, const char*) {
    // Not needed
}

void LeaderboardLayer::onReloadAllTime() {
    this->loadLeaderboard("alltime");
}

void LeaderboardLayer::onRecalculate(CCObject* sender) {
    Ref<LeaderboardLayer> self = this;
    createQuickPopup(
        "Confirm",
        "Recalculate <cy>All Time</c> Leaderboard?",
        "Cancel", "Yes",
        [self](FLAlertLayer*, bool btn2) {
            if (btn2) {
                Notification::create("Recalculating...", NotificationIcon::Info)->show();
                
                HttpClient::get().post("/api/admin/recalculate-alltime", "{}", [self](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create("Recalculation started", NotificationIcon::Success)->show();
                        
                        // Reload the list after a short delay to allow server to process
                        self->runAction(CCSequence::create(
                            CCDelayTime::create(3.0f),
                            CCCallFunc::create(self.data(), callfunc_selector(LeaderboardLayer::onReloadAllTime)), 
                            nullptr
                        ));
                        
                    } else {
                        Notification::create("Failed: " + msg, NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}

void LeaderboardLayer::fetchGDBrowserLevel(int levelID) {
    std::string url = "https://gdbrowser.com/api/level/" + std::to_string(levelID);
    
    m_listener.bind([this, levelID](web::WebTask::Event* e) {
        if (auto res = e->getValue()) {
            if (res->ok()) {
                auto json = res->string().unwrapOr("{}");
                auto dataRes = matjson::parse(json);
                if (dataRes.isOk()) {
                    auto data = dataRes.unwrap();
                    if (m_featuredLevel && m_featuredLevel->m_levelID == levelID) {
                        m_featuredLevel->m_levelName = data["name"].asString().unwrapOr(m_featuredLevel->m_levelName);
                        m_featuredLevel->m_creatorName = data["author"].asString().unwrapOr(m_featuredLevel->m_creatorName);
                        m_featuredLevel->m_stars = data["stars"].asInt().unwrapOr(0);
                        m_featuredLevel->m_downloads = data["downloads"].asInt().unwrapOr(0);
                        m_featuredLevel->m_likes = data["likes"].asInt().unwrapOr(0);
                        
                        std::string diffStr = data["difficulty"].asString().unwrapOr("NA");
                        bool isDemon = diffStr.find("Demon") != std::string::npos;
                        bool isAuto = diffStr == "Auto";
                        
                        m_featuredLevel->m_demon = isDemon;
                        m_featuredLevel->m_autoLevel = isAuto;
                        
                        if (isDemon) {
                            m_featuredLevel->m_difficulty = (GJDifficulty)50; 
                            if (diffStr == "Easy Demon") m_featuredLevel->m_demonDifficulty = 3;
                            else if (diffStr == "Medium Demon") m_featuredLevel->m_demonDifficulty = 4;
                            else if (diffStr == "Hard Demon") m_featuredLevel->m_demonDifficulty = 5;
                            else if (diffStr == "Insane Demon") m_featuredLevel->m_demonDifficulty = 6;
                            else if (diffStr == "Extreme Demon") m_featuredLevel->m_demonDifficulty = 7;
                        } else if (isAuto) {
                            m_featuredLevel->m_difficulty = GJDifficulty::Auto;
                        } else {
                            if (diffStr == "Easy") m_featuredLevel->m_difficulty = GJDifficulty::Easy;
                            else if (diffStr == "Normal") m_featuredLevel->m_difficulty = GJDifficulty::Normal;
                            else if (diffStr == "Hard") m_featuredLevel->m_difficulty = GJDifficulty::Hard;
                            else if (diffStr == "Harder") m_featuredLevel->m_difficulty = GJDifficulty::Harder;
                            else if (diffStr == "Insane") m_featuredLevel->m_difficulty = GJDifficulty::Insane;
                            else m_featuredLevel->m_difficulty = GJDifficulty::NA;
                        }
                        
                        this->refreshList();
                    }
                }
            }
        }
    });
    auto req = web::WebRequest();
    m_listener.setFilter(req.get(url));
}

LeaderboardLayer::~LeaderboardLayer() {
    if (GameLevelManager::sharedState()->m_levelManagerDelegate == this) {
        GameLevelManager::sharedState()->m_levelManagerDelegate = nullptr;
    }
    if (m_featuredLevel) {
        m_featuredLevel->release();
        m_featuredLevel = nullptr;
    }
    if (m_allItems) {
        m_allItems->release();
        m_allItems = nullptr;
    }
}
