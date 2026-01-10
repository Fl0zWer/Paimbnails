#include <Geode/Geode.hpp>
#include <Geode/modify/GauntletLayer.hpp>
#include <Geode/binding/GauntletLayer.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJMapPack.hpp>
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/Debug.hpp"

using namespace geode::prelude;

const char* g_vertexShader = R"(
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
    }
)";

// Shader source for blur
const char* g_fragmentShaderFastBlur = R"(
    #ifdef GL_ES
    precision mediump float;
    #endif
    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform vec2 u_texSize;
    
    void main() {
        vec2 texOffset = 1.0 / u_texSize;
        vec4 color = vec4(0.0);
        float total = 0.0;
        
        // 5x5 Box Blur (sparse)
        for(float x = -2.0; x <= 2.0; x += 1.0) {
            for(float y = -2.0; y <= 2.0; y += 1.0) {
                vec2 offset = vec2(x, y) * texOffset * 2.5;
                color += texture2D(u_texture, v_texCoord + offset);
                total += 1.0;
            }
        }
        
        gl_FragColor = (color / total) * v_fragmentColor;
    }
)";

class GauntletThumbnailNode : public CCNode {
    std::vector<int> m_levelIDs;
    std::vector<Ref<CCTexture2D>> m_loadedTextures;
    int m_currentIndex = 0;
    float m_timer = 0.f;
    
    // We reuse two sprites to avoid creating/destroying nodes constantly,
    // which seems to cause crashes with some mods (e.g. custom-keybinds)
    CCSprite* m_sprites[2] = {nullptr, nullptr};
    int m_activeSpriteIndex = 0; // The index of the currently visible (or fading out) sprite
    bool m_transitioning = false;
    float m_transitionTime = 0.f; // Track manual transition time
    bool m_loadingStarted = false;
    bool m_firstLoad = true;

public:
    static GauntletThumbnailNode* create(const std::vector<int>& levelIDs) {
        auto node = new GauntletThumbnailNode();
        if (node && node->init(levelIDs)) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    bool init(const std::vector<int>& levelIDs) {
        if (!CCNode::init()) return false;
        
        m_levelIDs = levelIDs;
        m_currentIndex = 0;
        
        // Setup initial black background
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto bg = CCLayerColor::create(ccc4(0, 0, 0, 200), winSize.width, winSize.height);
        this->addChild(bg, -1);
        
        this->setContentSize(winSize);
        this->setAnchorPoint({0.f, 0.f});
        this->setZOrder(-100); 

        // Create the two reusable sprites
        m_sprites[0] = CCSprite::create();
        m_sprites[1] = CCSprite::create();
        
        // Add them to scene but invisible
        m_sprites[0]->setOpacity(0);
        m_sprites[1]->setOpacity(0);
        // Important: Assign IDs just in case some mod expects it
        m_sprites[0]->setID("paimon-bg-sprite-1");
        m_sprites[1]->setID("paimon-bg-sprite-2");

        this->addChild(m_sprites[0], 0);
        this->addChild(m_sprites[1], 0);

        this->schedule(schedule_selector(GauntletThumbnailNode::updateSlide), 1.0f / 60.f);
        
        // Start loading
        this->loadAllThumbnails();
        
        return true;
    }

    void loadAllThumbnails() {
        if (m_loadingStarted) return;
        m_loadingStarted = true;

        for (int id : m_levelIDs) {
            ThumbnailLoader::get().requestLoad(id, "", [this, id](CCTexture2D* tex, bool success) {
                // Preload into cache
            }, 10, false); 
        }
    }

    void updateSlide(float dt) {
        if (m_levelIDs.empty()) return;

        m_timer += dt;
        
        // Handle transition timer if active
        if (m_transitioning) {
            m_transitionTime += dt;
            if (m_transitionTime >= 0.6f) { // Buffer of 0.1s over 0.5s fade
                onTransitionFinished();
            }
        }

        // Initial load
        if (m_firstLoad && m_timer > 0.1f) {
            showNextImage();
            m_timer = 0;
            m_firstLoad = false;
        }
        // Cycle
        else if (!m_firstLoad && m_timer > 3.0f && !m_transitioning) {
            m_currentIndex = (m_currentIndex + 1) % m_levelIDs.size();
            showNextImage();
            m_timer = 0;
        }
    }

    void showNextImage() {
        if (m_levelIDs.empty()) return;
        
        int id = m_levelIDs[m_currentIndex];
        
        // Check if loaded logic (same as before)
        if (!ThumbnailLoader::get().isLoaded(id)) {
            bool found = false;
            for (size_t i = 0; i < m_levelIDs.size(); i++) {
                int checkIdx = (m_currentIndex + i) % m_levelIDs.size();
                if (ThumbnailLoader::get().isLoaded(m_levelIDs[checkIdx])) {
                    m_currentIndex = checkIdx;
                    id = m_levelIDs[m_currentIndex];
                    found = true;
                    break;
                }
            }
            if (!found) return;
        }

        ThumbnailLoader::get().requestLoad(id, "", [this](CCTexture2D* tex, bool success) {
            if (success && tex) {
                transitionTo(tex);
            }
        }, 11, false);
    }

    void transitionTo(CCTexture2D* tex) {
        if (m_transitioning) return;
        m_transitioning = true;

        // Determine which sprite is next
        int nextIdx = 1 - m_activeSpriteIndex;
        CCSprite* nextSprite = m_sprites[nextIdx];
        CCSprite* currentSprite = m_sprites[m_activeSpriteIndex];

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // Update texture and shader for next sprite
        nextSprite->setTexture(tex);
        nextSprite->setTextureRect(CCRect(0, 0, tex->getContentSize().width, tex->getContentSize().height));

        // Apply Blur Shader
        auto shader = new CCGLProgram();
        shader->initWithVertexShaderByteArray(g_vertexShader, g_fragmentShaderFastBlur);
        
        shader->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        shader->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
        shader->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
        
        shader->link();
        shader->updateUniforms();

        shader->use();
        auto size = tex->getContentSizeInPixels();
        auto loc = shader->getUniformLocationForName("u_texSize");
        shader->setUniformLocationWith2f(loc, size.width, size.height);
        
        nextSprite->setShaderProgram(shader);
        shader->release();

        // Aspect Fill Layout
        float scaleX = winSize.width / nextSprite->getContentSize().width;
        float scaleY = winSize.height / nextSprite->getContentSize().height;
        float scale = std::max(scaleX, scaleY);
        
        nextSprite->setScale(scale);
        nextSprite->setPosition(winSize / 2);
        nextSprite->setColor({150, 150, 150});
        nextSprite->setOpacity(0);
        nextSprite->setZOrder(1); // Bring to front
        
        if (currentSprite) {
            currentSprite->setZOrder(0); // Send to back
        }

        // Run actions
        nextSprite->stopAllActions();
        nextSprite->runAction(CCFadeIn::create(0.5f));
        nextSprite->runAction(CCEaseSineOut::create(CCScaleTo::create(3.5f, scale * 1.05f)));

        if (currentSprite) {
            currentSprite->stopAllActions();
            currentSprite->runAction(CCFadeOut::create(0.5f));
        } else {
            // No current sprite (first run), just finish immediately
            onTransitionFinished();
        }
        
        // Reset manual transition timer
        m_transitionTime = 0.f;
        
        // Update active index
        m_activeSpriteIndex = nextIdx;
    }

    void onTransitionFinished() {
        m_transitioning = false;
        // Reset the old sprite to be safe?
        // Actually, we leave it be, it's opacity 0 and Z=0.
    }
};


class $modify(PaimonGauntletLayer, GauntletLayer) {
    bool init(GauntletType type) {
        if (!GauntletLayer::init(type)) return false;
        
        // 1. Hide default background
        if (auto bg = this->getChildByID("background")) {
            bg->setVisible(false);
        } else {
            // Fallback: likely the first child is the background sprite
            if (this->getChildrenCount() > 0) {
                 if (auto node = typeinfo_cast<CCNode*>(this->getChildren()->objectAtIndex(0))) {
                     node->setVisible(false);
                 }
            }
        }

        auto levelManager = GameLevelManager::sharedState();
        auto mapPack = levelManager->getSavedGauntlet(static_cast<int>(type));
        
        std::vector<int> ids;
        if (mapPack && mapPack->m_levels) {
            // m_levels is a CCArray of strings (level IDs)
            for (int i = 0; i < mapPack->m_levels->count(); ++i) {
                if (auto str = typeinfo_cast<CCString*>(mapPack->m_levels->objectAtIndex(i))) {
                    try {
                        ids.push_back(str->intValue());
                    } catch(...) {}
                }
            }
        }

        if (!ids.empty()) {
            auto bgNode = GauntletThumbnailNode::create(ids);
            if (bgNode) {
                bgNode->setID("paimon-gauntlet-background");
                this->addChild(bgNode, -100); 
            }
        }
        
        return true;
    }
};
