#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/Localization.hpp"
#include "../layers/ThumbnailSelectionPopup.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include "../layers/ThumbnailSelectionPopup.hpp"
#include "../layers/CapturePreviewPopup.hpp"
#include "../utils/Assets.hpp"
using namespace geode::prelude;

// Shaders for the mod's gaussian blur effect
constexpr auto vertexShader =
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

static CCGLProgram* getOrCreateShader(char const* key, char const* vertexSrc, char const* fragmentSrc) {
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

class $modify(PaimonLevelSelectLayer, LevelSelectLayer) {
    struct Fields {
        CCSprite* m_bgSprite = nullptr;
        int m_currentLevelID = 0;
        CCMenuItemSpriteExtra* m_uploadBtn = nullptr;
        float m_pageCheckTimer = 0.f;
    };

    bool init(int p0) {
        if (!LevelSelectLayer::init(p0)) return false;
        
        // Hide default background
        CCArray* children = this->getChildren();
        if (children) {
            for (int i = 0; i < children->count(); ++i) {
                auto node = static_cast<CCNode*>(children->objectAtIndex(i));
                if (node->getZOrder() < -1) {
                    node->setVisible(false);
                }
                
                // Hide Ground Layer as requested
                if (typeinfo_cast<GJGroundLayer*>(node)) {
                    node->setVisible(false);
                }
            }
        }
        
        this->schedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));
        
        return true;
    }

    void checkPageLoop(float dt) {
        if (!m_scrollLayer) return;

        // Better position detection:
        // Calculate page manually based on position to "look at what level is being viewed"
        // This is more precise than m_page which might lag during scroll.
        
        CCLayer* pagesLayer = m_scrollLayer->m_extendedLayer;
        if (!pagesLayer) return;

        float x = pagesLayer->getPositionX();
        float width = m_scrollLayer->getContentSize().width;
        
        // Page index based on X position.
        // x is usually negative as we scroll right.
        // page 0 = 0, page 1 = -width, etc.
        // so page = round(-x / width)
        
        int page = 0;
        if (width > 0) {
            page = static_cast<int>(std::round(-x / width));
        }
        
        // Implement "Virtual Circle" with 2 empty sections logic requested by user.
        // Cycle: [Level 1 ... Level 22] [Empty] [Empty] -> Repeat
        // Total Cycle Size = 22 + 2 = 24.
        
        const int totalLevels = 22;
        const int emptySections = 2; // Two empty sections before level 1.
        const int cycleSize = totalLevels + emptySections;
        
        // Normalize page to 0..23 range (handling negative pages correctly)
        // This creates an infinite virtual loop for background selection
        int cycleIndex = (page % cycleSize + cycleSize) % cycleSize;
        
        int levelID = -1;
        
        // 0 to 21 are Levels 1 to 22
        if (cycleIndex < totalLevels) {
            levelID = cycleIndex + 1;
        } 
        // 22 and 23 are the empty sections (levelID remains -1)

        if (m_fields->m_currentLevelID != levelID) {
            m_fields->m_currentLevelID = levelID;
            this->updateThumbnailBackground(levelID);
        }
    }
    
    cocos2d::ccColor3B colorForPage(int page) {
        // Just return black, as the ground layer is invisible.
        return {0, 0, 0};
    }
    
    // Override updatePageWithObject to do nothing or call base
    void updatePageWithObject(CCObject* p0, CCObject* p1) {
        LevelSelectLayer::updatePageWithObject(p0, p1);
    }
    
    void updateThumbnailBackground(int levelID) {
        // Range check for 22 Main Levels (1-22)
        bool isMainLevel = (levelID >= 1 && levelID <= 22);

        if (!isMainLevel) {
             // 2 Empty Sections -> Black Background
             this->applyBackground(nullptr, levelID); 
             return;
        }

        // Create filename
        std::string fileName = fmt::format("{}.png", levelID);
        
        auto selfPtr = this;
        this->retain();
        
        ThumbnailLoader::get().requestLoad(levelID, fileName, [selfPtr, levelID](CCTexture2D* tex, bool success) {
            // Check if still on the same level
            if (selfPtr->m_fields->m_currentLevelID == levelID) {
                if (success && tex) {
                    selfPtr->applyBackground(tex, levelID);
                } else {
                    selfPtr->applyBackground(nullptr, levelID);
                }
            }
            selfPtr->release();
        }, 5);
    }
    
    // Removed applyGroundColor logic
    
    void applyBackground(CCTexture2D* tex, int levelID = -1) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        CCSprite* finalSprite = nullptr;
        ccColor3B dominantColor = {0, 102, 255}; // Default

        if (tex) {
            // ... (Blur logic similar to before) ...
            CCSize texSize = tex->getContentSize();
            auto rt1 = CCRenderTexture::create(texSize.width, texSize.height);
            auto rt2 = CCRenderTexture::create(texSize.width, texSize.height);
            
            if (rt1 && rt2) {
                auto hShader = getOrCreateShader("paimon_blur_h", vertexShader, fragmentShaderHorizontal);
                auto vShader = getOrCreateShader("paimon_blur_v", vertexShader, fragmentShaderVertical);
                
                if (hShader && vShader) {
                    auto sprite = CCSprite::createWithTexture(tex);
                    sprite->setPosition(texSize / 2);
                    sprite->setFlipY(true);
                    applyBlurPass(sprite, rt1, hShader, texSize, 1.0f);
                    
                    auto sprite2 = CCSprite::createWithTexture(rt1->getSprite()->getTexture());
                    sprite2->setPosition(texSize / 2);
                    sprite2->setFlipY(true);
                    applyBlurPass(sprite2, rt2, vShader, texSize, 1.0f);
                    
                    auto finalTex = rt2->getSprite()->getTexture();
                    finalSprite = CCSprite::createWithTexture(finalTex);
                    
                    // SCALE logic
                    float scaleX = win.width / finalSprite->getContentSize().width;
                    float scaleY = win.height / finalSprite->getContentSize().height;
                    float scale = std::max(scaleX, scaleY);
                    
                    finalSprite->setScale(scale); // Initial scale
                    finalSprite->setPosition(win / 2);
                    finalSprite->setColor({100, 100, 100}); 
                    finalSprite->setZOrder(-10);
                    finalSprite->setOpacity(0);
                    
                    this->addChild(finalSprite);
                    finalSprite->runAction(CCFadeIn::create(0.5f));
                    
                    // ZOOM Effect
                    // Slowly zoom in/out slightly
                    // User requested "mas notable" (more notable), so increased factor to 1.3
                    auto zoomSeq = CCSequence::create(
                        CCScaleTo::create(10.0f, scale * 1.3f),
                        CCScaleTo::create(10.0f, scale),
                        nullptr
                    );
                    finalSprite->runAction(CCRepeatForever::create(zoomSeq));
                }
            }
        }
        
        if (m_fields->m_bgSprite) {
            m_fields->m_bgSprite->stopAllActions();
            m_fields->m_bgSprite->runAction(CCSequence::create(
                CCFadeOut::create(0.5f),
                CCRemoveSelf::create(),
                nullptr
            ));
        }
        
        m_fields->m_bgSprite = finalSprite;
        
        // If we failed to get dominant color, return default?
        // Actually, let's try to update ground color here if we had it.
        // For now, I will add a placeholder for color logic if I can't read pixels easily.
        // Wait, I can sample the RenderTexture! `rt2->newCCImage()`
        // But that touches CPU-GPU sync.
    }
    
    void updateButtons(int levelID) {
        // Button removed as requested
    }
    
    void onUpload(CCObject*) {
        // Upload logic removed
    }
};
