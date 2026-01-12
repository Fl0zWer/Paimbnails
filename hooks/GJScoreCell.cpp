#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>

#include "../managers/ProfileThumbs.hpp"
#include "../managers/ThumbsRegistry.hpp"
#include "../managers/LocalThumbs.hpp"
#include <Geode/ui/Notification.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/AnimatedGIFSprite.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// Shaders reutilizados del efecto de blur del mod (ver LevelCell.cpp)
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

constexpr auto fragmentShaderBlurCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec4 color = vec4(0.0);
    float blurSize = u_intensity; 
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    
    float totalWeight = 0.0;
    
    // Improved Gaussian-ish blur for single pass
    for (float x = -2.0; x <= 2.0; x+=1.0) {
        for (float y = -2.0; y <= 2.0; y+=1.0) {
            float distSq = x*x + y*y;
            // Gaussian weight: exp(-dist^2 / (2 * sigma^2))
            // Using sigma ~ 1.5
            float weight = exp(-distSq / 4.5);
            
            vec4 sample = texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel * blurSize);
            color += sample * weight;
            totalWeight += weight;
        }
    }
    
    gl_FragColor = (color / totalWeight) * v_fragmentColor;
})";

constexpr auto fragmentShaderHorizontal =
R"(
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

// Helper sprite for static blur
class BlurSprite : public CCSprite {
public:
    float m_intensity = 0.0f;
    CCSize m_texSize = {0,0};
    AnimatedGIFSprite* m_syncTarget = nullptr;
    
    static BlurSprite* createWithTexture(CCTexture2D* tex) {
        auto ret = new BlurSprite();
        if (ret && ret->initWithTexture(tex)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void update(float dt) override {
        if (m_syncTarget) {
            auto frame = m_syncTarget->displayFrame();
            if (frame) {
                // Always set the frame to ensure sync, checking for equality might be tricky with different frame objects
                this->setDisplayFrame(frame);
            }
        }
        CCSprite::update(dt);
    }
    
    void draw() override {
        if (getShaderProgram()) {
            getShaderProgram()->use();
            getShaderProgram()->setUniformsForBuiltins();
            
            GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
            if (intensityLoc != -1) {
                getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
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
        program->release();
        return nullptr;
    }

    program->updateUniforms();
    shaderCache->addProgram(program, key);
    program->release();
    return program;
}

// Premium effects helpers
namespace {
    // Add premium particles around the username
    void addPremiumParticlesToUsername(CCNode* parent, const CCPoint& namePos, float nameWidth) {
        // Create 3 small particles around the name
        for (int i = 0; i < 3; ++i) {
            auto particle = CCSprite::createWithSpriteFrameName("star_small01_001.png");
            if (!particle) continue;
            
            float offsetX = (i - 1) * (nameWidth / 2.5f);
            float offsetY = 8.0f + (rand() % 5);
            particle->setPosition({namePos.x + offsetX, namePos.y + offsetY});
            particle->setScale(0.25f);
            particle->setOpacity(150);
            particle->setColor({255, 215, 0}); // Gold
            parent->addChild(particle, 200);
            
            // Floating animation
            float duration = 1.0f + (rand() % 50) / 100.0f;
            particle->runAction(CCRepeatForever::create(CCSequence::create(
                CCSpawn::create(
                    CCMoveBy::create(duration, {0, 5}),
                    CCSequence::create(
                        CCFadeTo::create(duration / 2, 220),
                        CCFadeTo::create(duration / 2, 120),
                        nullptr
                    ),
                    nullptr
                ),
                CCPlace::create({namePos.x + offsetX, namePos.y + offsetY}),
                nullptr
            )));
        }
    }
}

// Static cache to optimize button movement
namespace {
    struct ButtonMoveCache {
        bool initialized = false;
        float buttonOffset = 30.f;
        std::unordered_set<int> processedCells; // IDs of already-processed cells
        
        void reset() {
            initialized = false;
            processedCells.clear();
        }
    };
    
    ButtonMoveCache g_buttonCache;
}

class $modify(PaimonGJScoreCell, GJScoreCell) {
    struct Fields {
        CCClippingNode* m_profileClip = nullptr;
        CCLayerColor* m_profileSeparator = nullptr;
        CCNode* m_profileBg = nullptr;
        CCLayerColor* m_darkOverlay = nullptr;
        bool m_buttonsMoved = false; // Flag to avoid moving buttons multiple times
        CCSprite* m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false; // Flag to avoid operating on destroyed cells
    };
    
    void showLoadingSpinner() {
        auto f = m_fields.self();
        
        // Remove existing spinner if present
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
        
        // Create a loading indicator
        auto spinner = CCSprite::create("loadingCircle.png");
        if (!spinner) {
            spinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
        }
        if (!spinner) {
            // Last resort: draw a simple circle
            spinner = CCSprite::create();
            auto circle = CCLayerColor::create({100, 100, 100, 200});
            circle->setContentSize({40, 40});
            spinner->addChild(circle);
        }
        
        spinner->setScale(0.25f);
        spinner->setOpacity(200);
        
        // Place it on the right side where the thumbnail would be
        auto cs = this->getContentSize();
        if (cs.width <= 1.f || cs.height <= 1.f) {
            cs.width = this->m_width;
            cs.height = this->m_height;
        }
        spinner->setPosition({35.f, cs.height / 2.f + 20.f});
        spinner->setZOrder(999);
        
        try {
            spinner->setID("paimon-loading-spinner"_spr);
        } catch (...) {}
        
        this->addChild(spinner);
        f->m_loadingSpinner = spinner;
        
        // Animate rotation
        auto rotateAction = CCRepeatForever::create(
            CCRotateBy::create(1.0f, 360.0f)
        );
        spinner->runAction(rotateAction);
    }
    
    void hideLoadingSpinner() {
        auto f = m_fields.self();
        if (f->m_loadingSpinner) {
            f->m_loadingSpinner->stopAllActions();
            f->m_loadingSpinner->removeFromParent();
            f->m_loadingSpinner = nullptr;
        }
    }





    // Move the game's CCLayerColor behind our background
    void pushGameColorLayersBehind(CCNode* node) {
        if (!node) return;
        // Don't touch our own nodes (IDs with the "paimon-" prefix)
        auto id = node->getID();
        if (!id.empty() && id.find("paimon-") == 0) return;

        bool isBackground = false;
        if (geode::cast::typeinfo_cast<CCLayerColor*>(node) != nullptr) isBackground = true;
        else if (geode::cast::typeinfo_cast<CCScale9Sprite*>(node) != nullptr) isBackground = true;

        if (isBackground) {
            // Only push if it's not already far back in the stack
            if (node->getZOrder() > -20) {
                if (auto parent = node->getParent()) parent->reorderChild(node, -20);
                else node->setZOrder(-20);
            }
        }
        // Recurse
        auto children = CCArrayExt<CCNode*>(node->getChildren());
        for (auto* ch : children) pushGameColorLayersBehind(ch);
    }

    void addOrUpdateProfileThumb(CCTexture2D* texture) {
        // Allow texture to be null if we have a GIF key
        
        try {
            // CRITICAL VALIDATION: ensure the cell has a valid parent
            if (!this->getParent()) {
                log::warn("[GJScoreCell] Cell has no parent, skipping addOrUpdateProfileThumb");
                return;
            }
            
            log::info("[GJScoreCell] addOrUpdateProfileThumb called");

            auto f = m_fields.self();
            if (!f) {
                log::error("[GJScoreCell] Fields are null in addOrUpdateProfileThumb");
                return;
            }
            
            // Check if the cell is being destroyed
            if (f->m_isBeingDestroyed) {
                log::debug("[GJScoreCell] Cell marked as destroyed, skipping thumbnail update");
                return;
            }
            
            log::debug("[GJScoreCell] Starting profile thumbnail update");
            
            // Aggressive cleanup of old nodes by ID to prevent stacking
            if (auto children = this->getChildren()) {
                for (int i = children->count() - 1; i >= 0; i--) {
                    if (auto node = static_cast<CCNode*>(children->objectAtIndex(i))) {
                        std::string id = node->getID();
                        if (id == "paimon-profile-bg"_spr || 
                            id == "paimon-profile-clip"_spr || 
                            id == "paimon-profile-thumb"_spr ||
                            id == "paimon-score-bg-clipper"_spr ||
                            id == "paimon-profile-separator"_spr) {
                            node->removeFromParent();
                        }
                    }
                }
            }
            
            f->m_profileClip = nullptr;
            f->m_profileSeparator = nullptr;
            f->m_profileBg = nullptr;
            f->m_darkOverlay = nullptr;

            // Base geometry derived from the cell size
            auto cs = this->getContentSize();
            if (cs.width <= 0 || cs.height <= 0) {
                log::error("[GJScoreCell] Invalid cell content size: {}x{}", cs.width, cs.height);
                return;
            }
            if (cs.width <= 1.f || cs.height <= 1.f) {
                cs.width = this->m_width;
                cs.height = this->m_height;
            }

            // --- Background Logic (Gradient vs Blurred Thumbnail) ---
            std::string bgType = "gradient";
            float blurIntensity = 3.0f;
            float darkness = 0.2f;
            bool useGradient = false;
            ccColor3B colorA = {255,255,255};
            ccColor3B colorB = {255,255,255};
            std::string gifKey = "";

            bool isCurrentUser = false;
            if (this->m_score) isCurrentUser = this->m_score->isCurrentUser();
            
            int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
            auto config = ProfileThumbs::get().getProfileConfig(accountID);

            if (isCurrentUser) {
                // For current user, ALWAYS use local settings (SavedValue now)
                try { bgType = Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail"); } catch (...) {}
                try { blurIntensity = Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f); } catch (...) {}
                try { darkness = Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f); } catch (...) {}
                
                // GIF key comes from cache (set by BannerConfigPopup)
                if (config.hasConfig && !config.gifKey.empty()) {
                    gifKey = config.gifKey;
                }
            } else {
                // For other users, try to get config from cache
                if (config.hasConfig) {
                    bgType = config.backgroundType;
                    blurIntensity = config.blurIntensity;
                    darkness = config.darkness;
                    useGradient = config.useGradient;
                    colorA = config.colorA;
                    colorB = config.colorB;
                    gifKey = config.gifKey;
                } else {
                    // Fallback to defaults if no config found (do NOT use local settings for others)
                    bgType = "thumbnail"; // Default to blurred thumbnail
                    // Keep other defaults
                }
            }
            
            // Validate inputs
            if (!texture && gifKey.empty()) {
                log::error("[GJScoreCell] No texture and no GIF key available for account {}", accountID);
                return;
            }

            // Force thumbnail mode if we have a texture/gif and config is default "gradient"
            if (bgType == "gradient" && (texture || !gifKey.empty())) {
                bgType = "thumbnail";
            }

            BlurSprite* pendingBlurSprite = nullptr;

            if (bgType == "none") {
                // Do nothing - use game default
            }
            else if (bgType == "thumbnail") {
                // Create blurred background
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);

                CCNode* bgNode = nullptr;

                // Try GIF background first
                if (!gifKey.empty()) {
                    auto bgSprite = AnimatedGIFSprite::createFromCache(gifKey);
                    if (bgSprite) {
                        // Use a standard CCSprite for the background to avoid double-update issues
                        // We will sync its frame with the foreground GIF in update()
                        auto tex = bgSprite->getTexture();
                        auto staticBg = CCSprite::createWithTexture(tex);
                        
                        float scaleX = targetSize.width / bgSprite->getContentSize().width;
                        float scaleY = targetSize.height / bgSprite->getContentSize().height;
                        float scale = std::max(scaleX, scaleY);
                        
                        staticBg->setScale(scale);
                        staticBg->setPosition(targetSize * 0.5f);
                        
                        // Apply improved single-pass blur shader
                        auto shader = getOrCreateShaderCell("paimon_cell_blur", vertexShaderCell, fragmentShaderBlurCell);
                        if (shader) {
                            staticBg->setShaderProgram(shader);
                        }
                        
                        auto blurSprite = BlurSprite::createWithTexture(tex);
                        if (blurSprite) {
                            blurSprite->setScale(scale);
                            blurSprite->setPosition(targetSize * 0.5f);
                            blurSprite->m_intensity = blurIntensity;
                            
                            // Schedule update on the sprite itself to sync frames
                            blurSprite->scheduleUpdate();

                            if (shader) {
                                blurSprite->setShaderProgram(shader);
                            }
                            bgNode = blurSprite;
                            pendingBlurSprite = blurSprite;
                        } else {
                            bgNode = staticBg;
                        }
                        
                        if (bgNode) {
                            try { bgNode->setID("paimon-bg-sprite"_spr); } catch(...) {}
                        }
                    }
                }
                
                // Fallback to texture background
                if (!bgNode && texture) {
                    // Static image - apply high quality blur (Multi-pass Gaussian)
                    CCSize targetSize = cs;
                    
                    // Ensure reasonable minimum size for blur quality
                    targetSize.width = std::max(targetSize.width, 512.f);
                    targetSize.height = std::max(targetSize.height, 256.f);

                    auto tempSprite = CCSprite::createWithTexture(texture);
                    
                    // Scale temp sprite to cover the target size (Aspect Fill)
                    float scaleX = targetSize.width / texture->getContentSize().width;
                    float scaleY = targetSize.height / texture->getContentSize().height;
                    float scale = std::max(scaleX, scaleY);
                    
                    tempSprite->setScale(scale);
                    tempSprite->setPosition(targetSize * 0.5f);
                    
                    auto rtA = CCRenderTexture::create(targetSize.width, targetSize.height);
                    auto rtB = CCRenderTexture::create(targetSize.width, targetSize.height);
                    auto blurH = getOrCreateShaderCell("blur-horizontal"_spr, vertexShaderCell, fragmentShaderHorizontal);
                    auto blurV = getOrCreateShaderCell("blur-vertical"_spr, vertexShaderCell, fragmentShaderVertical);
                    
                    if (blurH && blurV && rtA && rtB) {
                        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                        texture->setTexParameters(&params);
                        
                        float normalizedIntensity = std::clamp((blurIntensity - 1.0f) / 9.0f, 0.0f, 1.0f);
                        float radius = 0.02f + (normalizedIntensity * 0.25f);
                        
                        applyBlurPass(tempSprite, rtA, blurH, targetSize, radius);
                        applyBlurPass(rtA->getSprite(), rtB, blurV, targetSize, radius);
                        
                        auto bgSprite = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
                        bgSprite->setFlipY(true);
                        bgSprite->getTexture()->setTexParameters(&params);
                        
                        bgNode = bgSprite;
                    } else {
                        bgNode = CCSprite::createWithTexture(texture);
                    }
                }

                if (bgNode) {
                    // Create Clipper for the background
                    auto stencil = CCDrawNode::create();
                    CCPoint rect[4];
                    rect[0] = ccp(0, 0);
                    rect[1] = ccp(cs.width, 0);
                    rect[2] = ccp(cs.width, cs.height);
                    rect[3] = ccp(0, cs.height);
                    ccColor4F white = {1, 1, 1, 1};
                    stencil->drawPolygon(rect, 4, white, 0, white);
                    
                    auto clipper = CCClippingNode::create(stencil);
                    clipper->setContentSize(cs);
                    clipper->setPosition({0,0});
                    clipper->setZOrder(-2); // Behind everything
                    try { clipper->setID("paimon-score-bg-clipper"_spr); } catch (...) {}

                    float targetW = cs.width;
                    float targetH = cs.height;
                    float finalScale = std::max(
                        targetW / bgNode->getContentSize().width,
                        targetH / bgNode->getContentSize().height
                    );
                    bgNode->setScale(finalScale);
                    bgNode->setPosition(cs / 2);
                    
                    clipper->addChild(bgNode);
                    this->addChild(clipper);
                    f->m_profileBg = clipper;

                    // Add darkness overlay
                    if (darkness > 0.0f) {
                        auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(darkness * 255)});
                        overlay->setContentSize(cs);
                        overlay->setPosition({0, 0});
                        overlay->setZOrder(-1); 
                        this->addChild(overlay);
                        f->m_darkOverlay = overlay;
                    }

                    // Ensure original background is pushed back
                    pushGameColorLayersBehind(this);
                }
            }

            // --- Main Sprite Logic ---
            CCNode* mainNode = nullptr;
            float contentW = 0, contentH = 0;

            // Try GIF first
            if (!gifKey.empty()) {
                auto gifSprite = AnimatedGIFSprite::createFromCache(gifKey);
                if (gifSprite) {
                    mainNode = gifSprite;
                    contentW = gifSprite->getContentSize().width;
                    contentH = gifSprite->getContentSize().height;
                    gifSprite->scheduleUpdate();
                    try { gifSprite->setID("paimon-profile-thumb-gif"_spr); } catch(...) {}
                    log::debug("[GJScoreCell] Created GIF sprite from key: {}", gifKey);
                    
                    // Link background sprite to foreground sprite for sync
                    if (pendingBlurSprite) {
                        pendingBlurSprite->m_syncTarget = gifSprite;
                    }
                }
            }
            
            // Fallback to texture
            if (!mainNode && texture) {
                auto sprite = CCSprite::createWithTexture(texture);
                if (sprite) {
                    mainNode = sprite;
                    contentW = sprite->getContentWidth();
                    contentH = sprite->getContentHeight();
                    try { sprite->setID("paimon-profile-thumb"_spr); } catch(...) {}
                }
            }

            if (!mainNode) {
                log::error("[GJScoreCell] Failed to create main sprite");
                return;
            }

        // Ensure the cell still exists and has a parent before continuing
        if (!this->getParent()) {
            log::warn("[GJScoreCell] Cell was destroyed before thumbnail could be added");
            return;
        }


        
        log::debug("[GJScoreCell] Cell size: {}x{}", cs.width, cs.height);

            // Width-only scaling: fixed height and width proportional to factor
            float factor = 0.80f;
            
            if (isCurrentUser) {
                try { factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f); } catch (...) {}
            } else {
                // Use cached config for other users
                int accountID = (this->m_score) ? this->m_score->m_accountID : 0;
                auto config = ProfileThumbs::get().getProfileConfig(accountID);
                if (config.hasConfig) {
                    factor = config.widthFactor;
                } else {
                    // Fallback to default if no config
                    factor = 0.60f; 
                }
            }
            
            factor = std::max(0.30f, std::min(0.95f, factor));
            float desiredWidth = cs.width * factor;

            float scaleY = cs.height / contentH;
            float scaleX = desiredWidth / contentW;

            mainNode->setScaleY(scaleY);
            mainNode->setScaleX(scaleX);

        // Angled clipping that simulates the right-side cut
    constexpr float angle = 18.f;
        CCSize scaledSize{ desiredWidth, contentH * scaleY };
        auto mask = CCLayerColor::create({255,255,255});
        mask->setContentSize(scaledSize);
        mask->setAnchorPoint({1,0});
        mask->setSkewX(angle);

        auto clip = CCClippingNode::create();
        clip->setStencil(mask);
        clip->setContentSize(scaledSize);
        clip->setAnchorPoint({1,0});
        // Stick to the right edge with a small downward offset to avoid seams
        clip->setPosition({ cs.width, 0.3f });
        try {
            clip->setID("paimon-profile-clip"_spr);
        } catch (...) {}
    // Place behind labels/icons so it doesn't cover stats
    clip->setZOrder(-1);

        mainNode->setPosition(clip->getContentSize() * 0.5f);
        clip->addChild(mainNode);
        
        // Validar nuevamente before of Add the clipping
        /*
        if (!this->getParent()) {
            log::warn("[GJScoreCell] Cell destroyed before clipping node could be added");
            clip->removeFromParent();
            return;
        }
        */
        
        this->addChild(clip);
        f->m_profileClip = clip;
        
        // Apply premium effects if the user has premium banners
        bool isPremiumUser = false;
        /*
        if (auto score = this->m_score) {
             // ... logic removed ...
        }
        */

        // Add a border around the thumbnail (premium gold or normal black)
        float borderThickness = 2.f;
        ccColor4B borderColor = isPremiumUser ? ccc4(255, 215, 0, 200) : ccc4(0, 0, 0, 120);
        
        // Top border
        auto topBorder = CCLayerColor::create(borderColor);
        topBorder->setContentSize({scaledSize.width, borderThickness});
        topBorder->setAnchorPoint({1,0});
        topBorder->setSkewX(angle);
        topBorder->setPosition({ cs.width, 0.3f + scaledSize.height });
        topBorder->setZOrder(-1);
        try {
            topBorder->setID("paimon-profile-border-top"_spr);
        } catch (...) {}
        this->addChild(topBorder);
        
        // Glow animation for premium users
        if (isPremiumUser) {
            topBorder->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.8f, 255),
                CCFadeTo::create(0.8f, 180),
                nullptr
            )));
        }
        
        // Bottom border
        auto bottomBorder = CCLayerColor::create(borderColor);
        bottomBorder->setContentSize({scaledSize.width, borderThickness});
        bottomBorder->setAnchorPoint({1,0});
        bottomBorder->setSkewX(angle);
        bottomBorder->setPosition({ cs.width, 0.3f - borderThickness });
        bottomBorder->setZOrder(-1);
        try {
            bottomBorder->setID("paimon-profile-border-bottom"_spr);
        } catch (...) {}
        this->addChild(bottomBorder);
        
        if (isPremiumUser) {
            bottomBorder->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.8f, 255),
                CCFadeTo::create(0.8f, 180),
                nullptr
            )));
        }
        
        // Right border
        auto rightBorder = CCLayerColor::create(borderColor);
        rightBorder->setContentSize({borderThickness, scaledSize.height + borderThickness * 2});
        rightBorder->setAnchorPoint({1,0});
        rightBorder->setPosition({ cs.width, 0.3f - borderThickness });
        rightBorder->setZOrder(-1);
        try {
            rightBorder->setID("paimon-profile-border-right"_spr);
        } catch (...) {}
        this->addChild(rightBorder);
        
        if (isPremiumUser) {
            rightBorder->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.8f, 255),
                CCFadeTo::create(0.8f, 180),
                nullptr
            )));
        }

    // Separator behind the image (fixed style)
    auto sep = CCLayerColor::create(ccc4(0, 0, 0, 50));
    sep->setScaleX(0.45f);
        sep->ignoreAnchorPointForPosition(false);
        sep->setSkewX(angle * 2);
        sep->setContentSize(scaledSize);
        sep->setAnchorPoint({1,0});
        sep->setPosition({ cs.width - sep->getContentSize().width / 2 - 16.f, 0.3f });
    sep->setZOrder(-2);
        try {
            sep->setID("paimon-profile-separator"_spr);
        } catch (...) {}
        this->addChild(sep);
        f->m_profileSeparator = sep;

        // (The background was already added above)
        log::debug("[GJScoreCell] Profile thumbnail added successfully");
        } catch (std::exception& e) {
            log::error("[GJScoreCell] Exception in addOrUpdateProfileThumb: {}", e.what());
        } catch (...) {
            log::error("[GJScoreCell] Unknown exception in addOrUpdateProfileThumb");
        }
    }



    $override void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        
        // Push the game's color layers back so the gradient becomes visible
        try { pushGameColorLayersBehind(this); } catch (...) {}
        
        try {
            if (!score) return;

            int accountID = score->m_accountID;
            if (accountID <= 0) return;

            bool isCurrent = score->isCurrentUser();
            bool loadedLocally = false;
            
            // Current user: previously tried to load local profile first
            if (isCurrent) {
                // MODIFIED: Ignore local disk so we always use the temporary cache or download from the server.
                // This ensures consistency with BannerConfigPopup and avoids showing stale images.
                loadedLocally = false;
            }

            if (!loadedLocally) {
                // Other users (or current user without local): try cache first and only download if needed
                std::string username = score->m_userName;
                if (username.empty()) {
                    log::warn("[GJScoreCell] Username empty for account {}", accountID);
                    return;
                }
                
                // Check cache first
                auto cachedProfile = ProfileThumbs::get().getCachedProfile(accountID);
                if (cachedProfile && (cachedProfile->texture || !cachedProfile->gifKey.empty())) {
                    log::debug("[GJScoreCell] Found cached profile for account {}", accountID);
                    // Load from cache asynchronously
                    Loader::get()->queueInMainThread([this, accountID]() {
                        try {
                            auto mod = Mod::get();
                            auto oldWidth = mod->getSavedValue<float>("profile-thumb-width", 0.6f);
                            
                            auto cached = ProfileThumbs::get().getCachedProfile(accountID);
                            if (cached) {
                                addOrUpdateProfileThumb(cached->texture);
                            } else {
                                log::warn("[GJScoreCell] Cache entry disappeared for account {}", accountID);
                            }
                            
                            // Restore settings
                            mod->setSavedValue("profile-thumb-width", oldWidth);
                        } catch (...) {}
                    });
                    return;
                }
                
                log::debug("[GJScoreCell] No cache for account {}, downloading...", accountID);
                
                // Not in cache: download from the server
                log::debug("[GJScoreCell] Profile not in cache for user: {} - Downloading...", username);
                
                // Show loading spinner only if enabled
                bool enableSpinners = true;
                // try {
                //     enableSpinners = Mod::get()->getSettingValue<bool>("enable-loading-spinners");
                // } catch (...) {}
                
                if (enableSpinners) {
                    showLoadingSpinner();
                }
                
                // Retain the cell to avoid crashes if destroyed during download
                this->retain();
                
                // Use queueLoad instead of direct download
                ProfileThumbs::get().queueLoad(accountID, username, [this, accountID, enableSpinners](bool success, CCTexture2D* texture) {
                    if (!success || !texture) {
                        if (enableSpinners) this->hideLoadingSpinner();
                        log::warn("[GJScoreCell] Failed to download profile for account {}", accountID);
                        this->release();
                        return;
                    }

                    // Retain texture to prevent it from being autoreleased during the next async call
                    texture->retain();

                    // Download config
                    ThumbnailAPI::get().downloadProfileConfig(accountID, [this, accountID, texture, enableSpinners](bool success2, const ProfileConfig& config) {
                        try {
                            if (enableSpinners) this->hideLoadingSpinner();
                            
                            // Save to cache
                            ProfileThumbs::get().cacheProfile(accountID, texture, {255,255,255}, {255,255,255}, 0.5f);
                            if (success2) {
                                ProfileThumbs::get().cacheProfileConfig(accountID, config);
                            }
                            
                            // Apply texture
                            this->addOrUpdateProfileThumb(texture);
                        } catch (...) {
                            log::error("[GJScoreCell] Error handling profile download callback");
                        }
                        
                        // Release texture after use
                        texture->release();
                        this->release();
                    });
                });
            }
        } catch (...) {}
        
        // Move the player's profile button (optimized with cache)
        auto f = m_fields.self();
        if (!f->m_buttonsMoved) {
            f->m_buttonsMoved = true; // Mark as processed immediately
            
            try {
                // Initialize cache only once
                if (!g_buttonCache.initialized) {
                        g_buttonCache.buttonOffset = 0.0f;
                    g_buttonCache.initialized = true;
                    log::debug("[GJScoreCell] Button cache initialized with offset: {}", g_buttonCache.buttonOffset);
                }
                
                // If offset is 0, do nothing
                if (g_buttonCache.buttonOffset <= 0.01f) {
                    return;
                }
                
                // Find and move buttons only among direct children (optimized)
                auto children = this->getChildren();
                if (!children) return;
                
                bool foundButton = false;
                
                // Limit the search to the first 10 nodes (menus are usually near the start)
                int maxSearch = std::min(10, (int)children->count());
                
                for (int i = 0; i < maxSearch && !foundButton; i++) {
                    auto child = typeinfo_cast<CCMenu*>(children->objectAtIndex(i));
                    if (!child) continue;
                    
                    auto menuChildren = child->getChildren();
                    if (!menuChildren) continue;
                    
                    // Search only in the first 5 menu items
                    int maxMenuSearch = std::min(5, (int)menuChildren->count());
                    
                    for (int j = 0; j < maxMenuSearch; j++) {
                        auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(menuChildren->objectAtIndex(j));
                        if (!btn) continue;
                        
                        auto btnID = btn->getID();
                        
                        // Ignore our buttons (optimized: prefix check only)
                        if (btnID.empty() || btnID.compare(0, 7, "paimon-") != 0) {
                            auto currentPos = btn->getPosition();
                            
                            // Only move if it's in a reasonable position (optimized: fewer checks)
                            if (currentPos.x > 50.f && currentPos.x < 400.f) {
                                btn->setPosition({currentPos.x - g_buttonCache.buttonOffset, currentPos.y});
                                foundButton = true;
                                log::debug("[GJScoreCell] Moved button: {}x{} -> {}x{}", 
                                         currentPos.x, currentPos.y, 
                                         currentPos.x - g_buttonCache.buttonOffset, currentPos.y);
                                break;
                            }
                        }
                    }
                }
            } catch (std::exception& e) {
                log::error("[GJScoreCell] Exception moving button: {}", e.what());
            } catch (...) {
                // Swallow exceptions to avoid crashes
            }
        }
    }

    // draw hook removed to prevent crashes
};


