#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/GameManager.hpp>
#include <fstream>
#include <vector>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <algorithm>

#include "../managers/LocalThumbs.hpp"
#include "../managers/PendingQueue.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../layers/ThumbnailSelectionPopup.hpp"
#include "../managers/LevelColors.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../layers/RatePopup.hpp"

#include "../utils/FileDialog.hpp"
#include "../utils/Assets.hpp"
#include "../utils/Localization.hpp"
#include "../utils/Localization.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"

#include "../utils/UIBorderHelper.hpp"
#include "../utils/Constants.hpp"
#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include <Geode/binding/DailyLevelPage.hpp>
#include "../layers/SetDailyWeeklyPopup.hpp"

using namespace geode::prelude;
using namespace cocos2d;

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

// Shader for grayscale
constexpr auto fragmentShaderGrayscale = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 result = mix(color.rgb, vec3(gray), u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// Shader for sepia
constexpr auto fragmentShaderSepia = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 sepia;
    sepia.r = dot(color.rgb, vec3(0.393, 0.769, 0.189));
    sepia.g = dot(color.rgb, vec3(0.349, 0.686, 0.168));
    sepia.b = dot(color.rgb, vec3(0.272, 0.534, 0.131));
    vec3 result = mix(color.rgb, sepia, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// Shader for vignette
constexpr auto fragmentShaderVignette = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec2 pos = v_texCoord - 0.5;
    float dist = length(pos);
    float vignette = smoothstep(0.8, 0.3 * (1.0 - u_intensity), dist);
    gl_FragColor = vec4(color.rgb * vignette, color.a) * v_fragmentColor;
})";

// Shader for scanlines
constexpr auto fragmentShaderScanlines = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float scanline = sin(v_texCoord.y * u_screenSize.y * 3.14159 * (1.0 + u_intensity * 2.0)) * 0.5 + 0.5;
    scanline = mix(1.0, scanline, u_intensity * 0.5);
    gl_FragColor = vec4(color.rgb * scanline, color.a) * v_fragmentColor;
})";

// Shader for bloom
constexpr auto fragmentShaderBloom = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 bloom = vec3(0.0);
    vec2 texOffset = 1.0 / u_screenSize;
    float radius = u_intensity * 3.0;
    
    for (float x = -radius; x <= radius; x += 1.0) {
        for (float y = -radius; y <= radius; y += 1.0) {
            vec2 offset = vec2(x, y) * texOffset;
            vec4 sample = texture2D(u_texture, v_texCoord + offset);
            float bright = max(max(sample.r, sample.g), sample.b);
            if (bright > 0.8) {
                bloom += sample.rgb * (bright - 0.8) * 5.0;
            }
        }
    }
    
    bloom /= (radius * 2.0 + 1.0) * (radius * 2.0 + 1.0);
    vec3 result = color.rgb + bloom * u_intensity * 0.5;
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// Shader for chromatic aberration
constexpr auto fragmentShaderChromatic = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 offset = (v_texCoord - 0.5) * u_intensity * 0.01;
    float r = texture2D(u_texture, v_texCoord + offset).r;
    float g = texture2D(u_texture, v_texCoord).g;
    float b = texture2D(u_texture, v_texCoord - offset).b;
    float a = texture2D(u_texture, v_texCoord).a;
    gl_FragColor = vec4(r, g, b, a) * v_fragmentColor;
})";

// Shader for radial blur
constexpr auto fragmentShaderRadialBlur = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 center = vec2(0.5, 0.5);
    vec2 direction = v_texCoord - center;
    vec4 color = vec4(0.0);
    float samples = 10.0 + u_intensity * 5.0;
    
    for (float i = 0.0; i < samples; i += 1.0) {
        float scale = 1.0 - (u_intensity * 0.05 * (i / samples));
        color += texture2D(u_texture, center + direction * scale);
    }
    
    color /= samples;
    gl_FragColor = color * v_fragmentColor;
})";

// Shader for glitch
constexpr auto fragmentShaderGlitch = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texCoord;
    float glitchStrength = u_intensity * 0.1;
    
    // Random per-line horizontal displacement
    float lineNoise = rand(vec2(floor(uv.y * 100.0), 0.0));
    if (lineNoise > 0.95 - u_intensity * 0.05) {
        uv.x += (rand(vec2(uv.y, 0.0)) - 0.5) * glitchStrength;
    }
    
    vec4 color = texture2D(u_texture, uv);
    
    // Color channel separation
    if (rand(vec2(uv.y, 1.0)) > 0.98 - u_intensity * 0.02) {
        color.r = texture2D(u_texture, uv + vec2(0.01 * u_intensity, 0.0)).r;
        color.b = texture2D(u_texture, uv - vec2(0.01 * u_intensity, 0.0)).b;
    }
    
    gl_FragColor = color * v_fragmentColor;
})";

// Shader for pixelate (for GIFs)
constexpr auto fragmentShaderPixelate = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_intensity;

void main() {
    float pixelSize = 2.0 + u_intensity * 15.0;
    vec2 coord = floor(v_texCoord * u_screenSize / pixelSize) * pixelSize / u_screenSize;
    gl_FragColor = texture2D(u_texture, coord) * v_fragmentColor;
})";

// Shader for simple blur (for GIFs)
constexpr auto fragmentShaderBlurSinglePass = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_intensity;

void main() {
    vec4 color = vec4(0.0);
    float blurSize = u_intensity * 2.0; 
    vec2 onePixel = vec2(1.0, 1.0) / u_screenSize;
    
    float totalWeight = 0.0;
    for (float x = -2.0; x <= 2.0; x+=1.0) {
        for (float y = -2.0; y <= 2.0; y+=1.0) {
            float weight = 1.0 / (1.0 + x*x + y*y);
            vec4 sample = texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel * blurSize);
            color += sample * weight;
            totalWeight += weight;
        }
    }
    gl_FragColor = (color / totalWeight) * v_fragmentColor;
})";

// Shader for posterize
constexpr auto fragmentShaderPosterize = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float levels = mix(32.0, 3.0, u_intensity);
    vec3 result;
    result.r = floor(color.r * levels) / levels;
    result.g = floor(color.g * levels) / levels;
    result.b = floor(color.b * levels) / levels;
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
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

// Simple popup for reporting thumbnails
class ReportInputPopup : public FLAlertLayer, public TextInputDelegate, public FLAlertLayerProtocol {
protected:
    int m_levelID = 0;
    CCTextInputNode* m_textInput = nullptr;
    std::function<void(std::string)> m_callback;
    
    bool init(int levelID, std::function<void(std::string)> callback) {
        m_levelID = levelID;
        m_callback = callback;
        
        if (!FLAlertLayer::init(this, 
            Localization::get().getString("report.title").c_str(),
            "",
            Localization::get().getString("report.cancel").c_str(), Localization::get().getString("report.send").c_str(), 420.f, false, 280.f, 1.0f)) return false;
        
        // Adjust popup size
        if (m_mainLayer) {
            auto currentSize = m_mainLayer->getContentSize();
            m_mainLayer->setContentSize({currentSize.width, currentSize.height + 60.f});
            
            // Adjust background
            auto bgSprite = dynamic_cast<CCScale9Sprite*>(m_mainLayer->getChildren()->objectAtIndex(0));
            if (bgSprite) {
                bgSprite->setContentSize({bgSprite->getContentSize().width, bgSprite->getContentSize().height + 60.f});
            }
        }
        
        // Adjust button menu position
        if (m_buttonMenu) {
            m_buttonMenu->setPositionY(m_buttonMenu->getPositionY() - 5.f);
        }
        
        // Adjust label positions
        if (m_mainLayer && m_mainLayer->getChildrenCount() > 0) {
            auto children = m_mainLayer->getChildren();
            for (unsigned int i = 0; i < children->count(); i++) {
                auto child = dynamic_cast<CCLabelBMFont*>(children->objectAtIndex(i));
                if (child) {
                    child->setPositionY(child->getPositionY() + 5.f);
                }
            }
        }
        
        // Text field
        auto inputBG = CCScale9Sprite::create("square02b_small.png");
        inputBG->setColor({40, 40, 40});
        inputBG->setOpacity(255);
        inputBG->setContentSize({360.f, 50.f});
        inputBG->setPosition({m_mainLayer->getContentWidth() / 2, m_mainLayer->getContentHeight() / 2 - 20.f});
        m_mainLayer->addChild(inputBG, 10);
        
        m_textInput = CCTextInputNode::create(340.f, 40.f, Localization::get().getString("report.placeholder").c_str(), "chatFont.fnt");
        m_textInput->setLabelPlaceholderColor({150, 150, 150});
        m_textInput->setLabelPlaceholderScale(0.8f);
        m_textInput->setAllowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;:!?()-_");
        m_textInput->setMaxLabelLength(120);
        m_textInput->setPosition(inputBG->getPosition());
        m_textInput->setDelegate(this);
        m_textInput->setString("");
        m_mainLayer->addChild(m_textInput, 11);
        
        return true;
    }
    
    void FLAlert_Clicked(FLAlertLayer* layer, bool btn2) override {
        if (btn2) {
            std::string reason = m_textInput->getString();
            if (reason.empty() || reason == Localization::get().getString("report.placeholder")) {
                Notification::create(Localization::get().getString("report.empty_reason").c_str(), NotificationIcon::Warning)->show();
                return;
            }
            
            if (m_callback) {
                m_callback(reason);
            }
        }
    }
    
public:
    static ReportInputPopup* create(int levelID, std::function<void(std::string)> callback) {
        auto ret = new ReportInputPopup();
        if (ret && ret->init(levelID, callback)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// Popup to show the thumbnail at full size with zoom/pan support
class LocalThumbnailViewPopup : public geode::Popup<std::pair<int32_t, bool>> {
protected:
    int32_t m_levelID;
    bool m_canAcceptUpload = false;
    CCTexture2D* m_thumbnailTexture = nullptr;
    CCClippingNode* m_clippingNode = nullptr;
    CCNode* m_thumbnailSprite = nullptr;
    float m_initialScale = 1.0f;
    float m_maxScale = 4.0f;
    float m_minScale = 0.5f;
    std::unordered_set<cocos2d::CCTouch*> m_touches;
    float m_initialDistance = 0.0f;
    float m_savedScale = 1.0f;
    CCPoint m_touchMidPoint = {0, 0};
    bool m_wasZooming = false;
    bool m_isExiting = false;
    int m_verificationCategory = -1; // -1 = not a verification entry, 0=Verify, 1=Update, 2=Report
    
    // Rating members
    cocos2d::CCMenu* m_ratingMenu = nullptr;
    cocos2d::CCMenu* m_buttonMenu = nullptr;
    cocos2d::CCLabelBMFont* m_ratingLabel = nullptr;
    int m_userVote = 0;
    int m_initialUserVote = 0;
    bool m_isVoting = false;

    // Gallery members
    std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;

    void onPrev(CCObject*) {
        if (m_thumbnails.empty()) return;
        m_currentIndex--;
        if (m_currentIndex < 0) m_currentIndex = m_thumbnails.size() - 1;
        loadThumbnailAt(m_currentIndex);
    }

    void onNext(CCObject*) {
        if (m_thumbnails.empty()) return;
        m_currentIndex++;
        if (m_currentIndex >= m_thumbnails.size()) m_currentIndex = 0;
        loadThumbnailAt(m_currentIndex);
    }

    void loadThumbnailAt(int index) {
        if (index < 0 || index >= m_thumbnails.size()) return;
        
        auto& thumb = m_thumbnails[index];
        std::string url = thumb.url;
        
        std::string username = "Unknown";
        if (auto gm = GameManager::sharedState()) username = gm->m_playerName;
        
        // Update rating UI
        this->retain();
        ThumbnailAPI::get().getRating(m_levelID, username, thumb.id, [this](bool success, float average, int count, int userVote) {
            if (success && m_ratingLabel) {
                m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
                if (count == 0) {
                    m_ratingLabel->setColor({255, 100, 100});
                } else {
                    m_ratingLabel->setColor({255, 255, 255});
                }
                m_userVote = userVote;
                m_initialUserVote = userVote;
            }
            this->release();
        });

        // Download and display
        this->retain();
        ThumbnailAPI::get().downloadFromUrl(url, [this](bool success, CCTexture2D* tex) {
            if (success && tex) {
                auto content = m_mainLayer->getContentSize();
                float maxWidth = content.width - 40.f;
                float maxHeight = content.height - 70.f;
                this->displayThumbnail(tex, maxWidth, maxHeight, content, false);
            }
            this->release();
        });
    }
    std::vector<Suggestion> m_suggestions;
    int m_currentIndex = 0;
    CCMenuItemSpriteExtra* m_leftArrow = nullptr;
    CCMenuItemSpriteExtra* m_rightArrow = nullptr;
    CCLabelBMFont* m_counterLabel = nullptr;
    
    // Destructor to release resources
    ~LocalThumbnailViewPopup() {
        log::info("[ThumbnailViewPopup] Destructor - liberando textura retenida");
        if (m_thumbnailTexture) {
            m_thumbnailTexture->release();
            m_thumbnailTexture = nullptr;
        }
        m_touches.clear();
    }
    
    public:
    void setSuggestions(const std::vector<Suggestion>& suggestions) {
        m_suggestions = suggestions;
        if (!m_suggestions.empty()) {
            m_currentIndex = 0;
            this->loadCurrentSuggestion();
        }
    }
    protected:

    void loadCurrentSuggestion() {
        if (m_suggestions.empty()) return;
        
        auto& suggestion = m_suggestions[m_currentIndex];
        log::info("[ThumbnailViewPopup] Loading suggestion {}/{} - {}", m_currentIndex + 1, m_suggestions.size(), suggestion.filename);
        
        // Update counter label
        if (m_counterLabel) {
            m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str());
        }
        
        // Update arrows visibility
        if (m_leftArrow) m_leftArrow->setVisible(m_suggestions.size() > 1);
        if (m_rightArrow) m_rightArrow->setVisible(m_suggestions.size() > 1);
        
        // Download image
        std::string url = "https://paimon-thumbnails.paimonalcuadrado.workers.dev/" + suggestion.filename;
        
        LocalThumbnailViewPopup* popupPtr = this;
        this->retain();
        
        ThumbnailAPI::get().downloadFromUrl(url, [popupPtr](bool success, CCTexture2D* tex) {
             if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                 if (popupPtr) popupPtr->release();
                 return;
             }
             
             if (success && tex) {
                 auto content = popupPtr->m_mainLayer->getContentSize();
                 float maxWidth = content.width - 40.f;
                 float maxHeight = content.height - 70.f;
                 
                 popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, false);
             }
             popupPtr->release();
        });
    }

    void onNextSuggestion(CCObject*) {
        if (m_suggestions.empty()) return;
        m_currentIndex++;
        if (m_currentIndex >= m_suggestions.size()) {
            m_currentIndex = 0;
        }
        loadCurrentSuggestion();
    }

    void onPrevSuggestion(CCObject*) {
        if (m_suggestions.empty()) return;
        m_currentIndex--;
        if (m_currentIndex < 0) {
            m_currentIndex = m_suggestions.size() - 1;
        }
        loadCurrentSuggestion();
    }

    // Basic lifecycle handling
    void onExit() override {
        log::info("[ThumbnailViewPopup] onExit() comenzando");
        
        m_touches.clear();

        if (m_isExiting) {
            log::warn("[ThumbnailViewPopup] onExit() ya fue llamado, evitando re-entrada");
            return;
        }
        m_isExiting = true;
        
        // FIX: Explicitly remove children to prevent crash during destruction
        // This ensures buttons are unregistered from touch dispatcher while the popup is still valid
        if (m_mainLayer) {
            m_mainLayer->removeAllChildren();
        }
        m_ratingMenu = nullptr;
        m_buttonMenu = nullptr;
        m_ratingLabel = nullptr;
        m_counterLabel = nullptr;
        m_leftArrow = nullptr;
        m_rightArrow = nullptr;
        
        // Null out pointers - Cocos2d cleans the node hierarchy automatically
        m_thumbnailSprite = nullptr;
        m_clippingNode = nullptr;
        
        // Don't release the texture here - it will be released in the destructor
        
        log::info("[ThumbnailViewPopup] Llamando a parent onExit");
        
        // Call the parent - it handles cleaning up the node hierarchy
        geode::Popup<std::pair<int32_t, bool>>::onExit();
    }

    /*
    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -128, true);
    }
    */
    
    void setupRating() {
        if (auto node = m_mainLayer->getChildByID("rating-container")) {
            node->removeFromParent();
        }

        auto contentSize = m_mainLayer->getContentSize();
        
        // Rating Container (Top Center) - Visual Only
        auto ratingContainer = CCNode::create();
        ratingContainer->setID("rating-container");
        ratingContainer->setPosition({contentSize.width / 2.f, 237.f});
        m_mainLayer->addChild(ratingContainer, 100); // High Z-order
        
        // Background for rating
        auto bg = CCScale9Sprite::create("square02_001.png");
        bg->setContentSize({74.f, 16.f}); // Reduced by ~30% (105 -> 74, 23 -> 16)
        bg->setColor({0, 0, 0});
        bg->setOpacity(125);
        bg->setPosition({0.f, 0.f}); // Center of container
        ratingContainer->addChild(bg, -1); // Behind content
        
        // Star Icon (Visual Only)
        auto starSpr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png"); 
        if (!starSpr) starSpr = CCSprite::createWithSpriteFrameName("star_small01_001.png");
        starSpr->setScale(0.34f); // Reduced by 30% (0.48 -> 0.34)
        starSpr->setPosition({-20.f, 0.f}); // Adjusted position
        ratingContainer->addChild(starSpr);
        
        // Average Label
        m_ratingLabel = CCLabelBMFont::create("...", "goldFont.fnt");
        m_ratingLabel->setScale(0.28f); // Reduced by 30% (0.4 -> 0.28)
        m_ratingLabel->setPosition({8.f, 3.f}); // Adjusted position (Moved 3px up from 0.f)
        ratingContainer->addChild(m_ratingLabel);
        
        // Fetch Rating
        std::string username = "Unknown";
        if (auto gm = GameManager::sharedState()) username = gm->m_playerName;
        
        std::string thumbnailId = "";
        if (m_currentIndex >= 0 && m_currentIndex < m_thumbnails.size()) {
            thumbnailId = m_thumbnails[m_currentIndex].id;
        }

        this->retain();
        ThumbnailAPI::get().getRating(m_levelID, username, thumbnailId, [this](bool success, float average, int count, int userVote) {
             if (success) {
                log::info("[ThumbnailViewPopup] Rating found for level {}: {:.1f} ({})", m_levelID, average, count);
                if (m_ratingLabel) {
                    m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
                    if (count == 0) {
                        m_ratingLabel->setColor({255, 100, 100}); // Red-ish if 0
                    } else {
                        m_ratingLabel->setColor({255, 255, 255});
                    }
                }
                m_userVote = userVote;
                m_initialUserVote = userVote;
            } else {
                 log::warn("[ThumbnailViewPopup] Failed to get rating for level {}", m_levelID);
            }
            this->release();
        });
    }

    void onRate(CCObject* sender) {
        std::string thumbnailId = "";
        if (m_currentIndex >= 0 && m_currentIndex < m_thumbnails.size()) {
            thumbnailId = m_thumbnails[m_currentIndex].id;
        }
        auto popup = RatePopup::create(m_levelID, thumbnailId);
        popup->m_onRateCallback = [this]() {
            this->setupRating();
        };
        popup->show();
    }

    bool setup(std::pair<int32_t, bool> data) override {
        m_levelID = data.first;
        m_canAcceptUpload = data.second;
        
        this->setTitle("");

        bool openedFromReport = false;
        int verificationCategory = -1; // -1 = not a verification entry, 0=Verify, 1=Update, 2=Report
        try {
            openedFromReport = Mod::get()->getSavedValue<bool>("from-report-popup", false);
            verificationCategory = Mod::get()->getSavedValue<int>("verification-category", -1);
            if (openedFromReport) Mod::get()->setSavedValue("from-report-popup", false);
            if (verificationCategory >= 0) Mod::get()->setSavedValue("verification-category", -1);
        } catch (...) {}
        
        if (m_bgSprite) {
            m_bgSprite->setVisible(false);
        }
        
        auto content = this->m_mainLayer->getContentSize();
        
        // area to the thumbnail
        float maxWidth = content.width - 40.f;
        // Reduced height by 10px (was -70.f, now -80.f)
        float maxHeight = content.height - 80.f;

        if (m_closeBtn) {
             // Position close button relative to top-left of the thumbnail area
             float topY = (content.height / 2 + 5.f) + (maxHeight / 2);
             float leftX = (content.width - maxWidth) / 2;
             m_closeBtn->setPosition({leftX - 3.f, topY + 3.f});
        }
        
        log::info("[ThumbnailViewPopup] Content size: {}x{}, Max area: {}x{}", 
            content.width, content.height, maxWidth, maxHeight);
        
        // Create a clipping container so the thumbnail doesn't spill out
#ifdef GEODE_IS_ANDROID
        // On Android, avoid CCClippingNode for stability
        m_clippingNode = nullptr;
        // Use a simple container node
        auto container = CCNode::create();
        container->setContentSize({maxWidth, maxHeight});
        container->setAnchorPoint({0.5f, 0.5f});
        container->setPosition({content.width / 2, content.height / 2 + 5.f});
        this->m_mainLayer->addChild(container, 1);
        // Assign to m_clippingNode so the rest of the code works (even if it's a plain CCNode)
        m_clippingNode = static_cast<CCClippingNode*>(container); 
#else
        // Use CCScale9Sprite as stencil for better compatibility
        auto stencil = CCScale9Sprite::create("square02_001.png");
        if (!stencil) {
            stencil = CCScale9Sprite::create();
        }
        stencil->setContentSize({maxWidth, maxHeight});
        // Center the stencil in the clipping node
        stencil->setPosition({maxWidth / 2, maxHeight / 2});
        
        m_clippingNode = CCClippingNode::create(stencil);
        m_clippingNode->setAlphaThreshold(0.1f);
        m_clippingNode->setContentSize({maxWidth, maxHeight});
        m_clippingNode->setAnchorPoint({0.5f, 0.5f});
        m_clippingNode->setPosition({content.width / 2, content.height / 2 + 5.f});
        this->m_mainLayer->addChild(m_clippingNode, 1);
#endif
        
        // Background for the thumbnail area (black with 10% opacity)
        // This ensures that when panning, the empty space is dark instead of showing the underlying popup
        auto clippingBg = CCLayerColor::create({0, 0, 0, 255}); // Full black temporarily to test visibility, adjust opacity later if needed
        clippingBg->setOpacity(25); // 10% opacity (approx 25/255)
        clippingBg->setContentSize({maxWidth, maxHeight});
        // CCLayerColor anchors at 0,0, so we just add it. Since m_clippingNode anchors at 0.5, 0.5, we need to center it or adjust
        // Wait, m_clippingNode children coords are relative to its 0,0 anchor point? No, usually relative to bottom-left if ignoreAnchorPointForPosition is false.
        // Let's just center it.
        clippingBg->ignoreAnchorPointForPosition(false);
        clippingBg->setAnchorPoint({0.5f, 0.5f});
        clippingBg->setPosition({maxWidth / 2, maxHeight / 2});
        
        if (m_clippingNode) {
            m_clippingNode->addChild(clippingBg, -1);
        }

        // Frame/border to show the thumbnail area
        auto border = CCScale9Sprite::create("GJ_square07.png");
        border->setContentSize({maxWidth + 4.f, maxHeight + 4.f});
        border->setPosition({content.width / 2, content.height / 2 + 5.f});
        this->m_mainLayer->addChild(border, 2);

        // Gallery Arrows
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        this->m_mainLayer->addChild(menu, 10);

        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        m_leftArrow = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(LocalThumbnailViewPopup::onPrev));
        m_leftArrow->setPosition({25.f, content.height / 2 + 5.f});
        m_leftArrow->setVisible(false);
        menu->addChild(m_leftArrow);

        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        nextSpr->setFlipX(true);
        m_rightArrow = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(LocalThumbnailViewPopup::onNext));
        m_rightArrow->setPosition({content.width - 25.f, content.height / 2 + 5.f});
        m_rightArrow->setVisible(false);
        menu->addChild(m_rightArrow);

        
        // Enable touches for zoom/pan
        this->setTouchEnabled(true);
        // CCTouchDispatcher::get()->addTargetedDelegate(this, -128, true);
        
        // Enable mouse wheel zoom
#ifndef GEODE_IS_ANDROID
        this->setMouseEnabled(true);
        this->setKeypadEnabled(true);
#endif
        
        // Load thumbnail directly with multiple sources
        log::info("[ThumbnailViewPopup] === INICIANDO CARGA DE THUMBNAIL ===");
        log::info("[ThumbnailViewPopup] Level ID: {}", m_levelID);
        log::info("[ThumbnailViewPopup] Verification Category: {}", verificationCategory);
        this->retain();
        
        // Store the verification category
        m_verificationCategory = verificationCategory;
        
        // If coming from the verification queue, load from the corresponding API
        if (verificationCategory >= 0) {
            this->loadFromVerificationQueue(static_cast<PendingCategory>(verificationCategory), maxWidth, maxHeight, content, openedFromReport);
        } else {
            // Try loading from multiple sources in priority order
            this->tryLoadFromMultipleSources(maxWidth, maxHeight, content, openedFromReport);

            // Fetch gallery list
            this->retain();
            ThumbnailAPI::get().getThumbnails(m_levelID, [this](bool success, const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbs) {
                if (success && !thumbs.empty()) {
                    m_thumbnails = thumbs;
                    if (m_thumbnails.size() > 1) {
                        if (m_leftArrow) m_leftArrow->setVisible(true);
                        if (m_rightArrow) m_rightArrow->setVisible(true);
                    }
                    // Refresh rating now that we have the thumbnails
                    this->setupRating();
                }
                this->release();
            });
        }
        
        setupRating();
        return true;
    }
    
    void loadFromVerificationQueue(PendingCategory category, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Cargando desde cola de verificación - Categoría: {}", static_cast<int>(category));
        
        LocalThumbnailViewPopup* popupPtr = this;
        
        // Use the appropriate method based on category
        if (category == PendingCategory::Verify) {
            // Verification: download from /suggestions
            ThumbnailAPI::get().downloadSuggestion(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
                if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar suggestion");
                    return;
                }
                
                if (success && tex) {
                    log::info("[ThumbnailViewPopup] ✓ Suggestion cargada");
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar suggestion");
                    popupPtr->showNoThumbnail(content);
                }
                popupPtr->release();
            });
        } else if (category == PendingCategory::Update) {
            // Update: download from /updates
            ThumbnailAPI::get().downloadUpdate(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
                if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar update");
                    return;
                }
                
                if (success && tex) {
                    log::info("[ThumbnailViewPopup] ✓ Update cargada");
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar update");
                    popupPtr->showNoThumbnail(content);
                }
                popupPtr->release();
            });
        } else if (category == PendingCategory::Report) {
            // Report: download the reported thumbnail (the current official)
            ThumbnailAPI::get().downloadReported(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, CCTexture2D* tex) {
                if (!popupPtr || !popupPtr->getParent() || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup destruido antes de cargar reported");
                    return;
                }
                
                if (success && tex) {
                    log::info("[ThumbnailViewPopup] ✓ Reported cargada");
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ No se pudo cargar reported");
                    popupPtr->showNoThumbnail(content);
                }
                popupPtr->release();
            });
        } else {
            log::error("[ThumbnailViewPopup] Categoría de verificación desconocida: {}", static_cast<int>(category));
            this->showNoThumbnail(content);
            this->release();
        }
    }
    
    void tryLoadFromMultipleSources(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        // Source 1: LocalThumbs (locally captured thumbnails)
        if (LocalThumbs::get().has(m_levelID)) {
            log::info("[ThumbnailViewPopup] ✓ Fuente 1: LocalThumbs ENCONTRADO");
            auto tex = LocalThumbs::get().loadTexture(m_levelID);
            if (tex) {
                log::info("[ThumbnailViewPopup] ✓ Textura cargada desde LocalThumbs");
                this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                this->release();
                return;
            }
            log::warn("[ThumbnailViewPopup] ✗ LocalThumbs falló al cargar textura");
        } else {
            log::info("[ThumbnailViewPopup] ✗ Fuente 1: LocalThumbs - NO disponible");
        }
        
        // Source 2: Try loading directly from the mod cache
        if (tryLoadFromCache(maxWidth, maxHeight, content, openedFromReport)) {
            return;
        }
        
        // Source 3: ThumbnailLoader with download
        loadFromThumbnailLoader(maxWidth, maxHeight, content, openedFromReport);
    }

    bool tryLoadFromCache(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Intentando Fuente 2: Cache directo");
        auto cachePath = geode::Mod::get()->getSaveDir() / "thumbnails" / fmt::format("{}.webp", m_levelID);
        if (std::filesystem::exists(cachePath)) {
            log::info("[ThumbnailViewPopup] ✓ Encontrado en cache: {}", cachePath.generic_string());
            auto tex = CCTextureCache::sharedTextureCache()->addImage(cachePath.generic_string().c_str(), false);
            if (tex) {
                log::info("[ThumbnailViewPopup] ✓ Textura cargada desde cache ({}x{})", 
                    tex->getPixelsWide(), tex->getPixelsHigh());
                this->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                this->release();
                return true;
            }
            log::warn("[ThumbnailViewPopup] ✗ Error al cargar textura desde cache");
        } else {
            log::info("[ThumbnailViewPopup] ✗ No existe en cache: {}", cachePath.generic_string());
        }
        return false;
    }

    void loadFromThumbnailLoader(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Intentando Fuente 3: ThumbnailLoader + Descarga");
        std::string fileName = fmt::format("{}.png", m_levelID);
        
        // Capture a raw pointer for safer verification
        LocalThumbnailViewPopup* popupPtr = this;
        this->retain(); // Retain to ensure lifetime during callback
        
        ThumbnailLoader::get().requestLoad(m_levelID, fileName, [popupPtr, maxWidth, maxHeight, content, openedFromReport](CCTexture2D* tex, bool) {
            log::info("[ThumbnailViewPopup] === CALLBACK THUMBNAILLOADER ===");
            
            // Critical verification: ensure the popup still exists as a valid CCNode
            try {
                if (!popupPtr) {
                    log::warn("[ThumbnailViewPopup] popupPtr es null");
                    return;
                }
                
                // Ensure the popup is still valid before using it
                auto parent = popupPtr->getParent();
                if (!parent || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup ya no tiene parent o mainLayer válido - objeto destruido");
                    // Release retained reference and exit
                    popupPtr->release();
                    return;
                }
                
                // Popup is valid; process the texture
                if (tex) {
                    log::info("[ThumbnailViewPopup] ✓ Textura recibida ({}x{})", 
                        tex->getPixelsWide(), tex->getPixelsHigh());
                    popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                } else {
                    log::warn("[ThumbnailViewPopup] ✗ ThumbnailLoader no devolvió textura");
                    log::info("[ThumbnailViewPopup] === TODAS LAS FUENTES FALLARON ===");
                    popupPtr->showNoThumbnail(content);
                }
                
                // Release retained reference
                popupPtr->release();
            } catch (const std::exception& e) {
                log::error("[ThumbnailViewPopup] Exception en callback: {}", e.what());
                // Attempt release if safe
                if (popupPtr) popupPtr->release();
            } catch (...) {
                log::error("[ThumbnailViewPopup] Exception desconocida en callback");
                if (popupPtr) popupPtr->release();
            }
        }, 10); // Highest priority
    }
    
    void tryDirectServerDownload(float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] Intentando Fuente 3: Descarga directa del servidor");
        this->retain();
        
        // Capture a raw pointer for safer verification
        LocalThumbnailViewPopup* popupPtr = this;
        
        HttpClient::get().downloadThumbnail(m_levelID, [popupPtr, maxWidth, maxHeight, content, openedFromReport](bool success, const std::vector<uint8_t>& data, int w, int h) {
            // Critical verification: ensure the popup still exists
            try {
                if (!popupPtr) {
                    log::warn("[ThumbnailViewPopup] popupPtr es null (descarga servidor)");
                    return;
                }
                
                auto parent = popupPtr->getParent();
                if (!parent || !popupPtr->m_mainLayer) {
                    log::warn("[ThumbnailViewPopup] Popup ya no tiene parent válido (descarga servidor) - no hacer release");
                    // Do NOT release if the object was already destroyed
                    return;
                }
            } catch (...) {
                log::error("[ThumbnailViewPopup] Exception al validar popup (descarga servidor)");
                return;
            }
            
            if (success && !data.empty()) {
                log::info("[ThumbnailViewPopup] ✓ Datos descargados del servidor ({} bytes)", data.size());
                
                // Create a texture from the downloaded data
                auto image = new CCImage();
                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                    auto tex = new CCTexture2D();
                    if (tex->initWithImage(image)) {
                        log::info("[ThumbnailViewPopup] ✓ Textura creada desde servidor ({}x{})",
                            tex->getPixelsWide(), tex->getPixelsHigh());
                        // displayThumbnail will retain the texture
                        popupPtr->displayThumbnail(tex, maxWidth, maxHeight, content, openedFromReport);
                        // Release local references
                        tex->release();
                        image->release();
                        popupPtr->release();
                        return;
                    }
                    tex->release();
                }
                image->release();
                log::error("[ThumbnailViewPopup] ✗ Error creando textura desde datos del servidor");
            } else {
                log::warn("[ThumbnailViewPopup] ✗ Descarga del servidor falló");
            }
            
            // If everything failed, show message
            log::info("[ThumbnailViewPopup] === TODAS LAS FUENTES FALLARON ===");
            popupPtr->showNoThumbnail(content);
            popupPtr->release();
        });
    }
    
    void displayThumbnail(CCTexture2D* tex, float maxWidth, float maxHeight, CCSize content, bool openedFromReport) {
        log::info("[ThumbnailViewPopup] === MOSTRANDO THUMBNAIL ===");
        log::info("[ThumbnailViewPopup] Textura: {}x{}", tex->getPixelsWide(), tex->getPixelsHigh());
        
        // Critical safety check before continuing
        if (!this->getParent() || !m_mainLayer) {
            log::error("[ThumbnailViewPopup] Popup destruido antes de displayThumbnail!");
            return;
        }

        // CLEANUP: Remove existing sprite
        if (m_thumbnailSprite) {
            m_thumbnailSprite->removeFromParent();
            m_thumbnailSprite = nullptr;
        }
        
        if (!m_mainLayer) {
            log::error("[ThumbnailViewPopup] m_mainLayer es null!");
            return;
        }

        // Cleanup previous state
        if (m_thumbnailTexture) {
            m_thumbnailTexture->release();
            m_thumbnailTexture = nullptr;
        }
        if (m_thumbnailSprite) {
            m_thumbnailSprite->removeFromParent();
            m_thumbnailSprite = nullptr;
        }
        if (m_buttonMenu) {
            m_buttonMenu->removeFromParent();
            m_buttonMenu = nullptr;
        }
        if (m_leftArrow) {
            m_leftArrow->removeFromParent();
            m_leftArrow = nullptr;
        }
        if (m_rightArrow) {
            m_rightArrow->removeFromParent();
            m_rightArrow = nullptr;
        }
        if (m_counterLabel) {
            m_counterLabel->removeFromParent();
            m_counterLabel = nullptr;
        }
        
        m_thumbnailTexture = tex;
        tex->retain(); // Retain the texture to prevent it from being freed
        
        CCSprite* sprite = nullptr;
        
        // Default to static texture
        sprite = CCSprite::createWithTexture(tex);
        
        // Check for GIF
        if (ThumbnailLoader::get().hasGIFData(m_levelID)) {
             auto path = ThumbnailLoader::get().getCachePath(m_levelID);
             this->retain();
             AnimatedGIFSprite::createAsync(path.generic_string(), [this, maxWidth, maxHeight](AnimatedGIFSprite* anim) {
                 if (anim && this->m_thumbnailSprite) {
                     auto oldSprite = this->m_thumbnailSprite;
                     auto parent = oldSprite->getParent();
                     if (parent) {
                         CCPoint pos = oldSprite->getPosition();
                         oldSprite->removeFromParent();
                         
                         anim->setAnchorPoint({0.5f, 0.5f});
                         float scaleX = maxWidth / anim->getContentWidth();
                         float scaleY = maxHeight / anim->getContentHeight();
                         float scale = std::min(scaleX, scaleY);
                         scale = std::min(scale, 1.0f);
                         anim->setScale(scale);
                         anim->setPosition(pos);
                         
                         parent->addChild(anim, 10);
                         this->m_thumbnailSprite = anim;
                     }
                 }
                 this->release();
             });
        }
        
        if (!sprite) {
            log::error("[ThumbnailViewPopup] No se pudo crear sprite con textura");
            return;
        }
        
        log::info("[ThumbnailViewPopup] Sprite creado correctamente");
        sprite->setAnchorPoint({0.5f, 0.5f});
        
        // Compute initial scale so it fits in the area
        float scaleX = maxWidth / sprite->getContentWidth();
        float scaleY = maxHeight / sprite->getContentHeight();
        float scale = std::min(scaleX, scaleY);
        scale = std::min(scale, 1.0f); // Max 1x to avoid pixelation
        
        sprite->setScale(scale);
        m_initialScale = scale;
        
        // Position at the popup center
        float centerX = content.width * 0.5f;
        float centerY = content.height * 0.5f + 5.f;
        sprite->setPosition({centerX, centerY});
        sprite->setID("thumbnail");
        
        // Improve texture quality
        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        tex->setTexParameters(&params);
        
        // Add to clipping node so it doesn't overflow the area
        if (m_clippingNode) {
            // Convert world position to clipping-node local space
            m_clippingNode->addChild(sprite, 10);
            // Adjust position relative to the clipping node (centered)
            sprite->setPosition({maxWidth / 2, maxHeight / 2});
        } else {
            // Fallback: add directly to mainLayer
            this->m_mainLayer->addChild(sprite, 10);
            sprite->setPosition({centerX, centerY});
        }
        m_thumbnailSprite = sprite;
        
        // Force visual refresh
        sprite->setVisible(true);
        sprite->setOpacity(255);
        
        log::info("[ThumbnailViewPopup] ✓ Thumbnail agregado a mainLayer");
        log::info("[ThumbnailViewPopup] Posición: ({},{}), Scale: {}, Tamaño final: {}x{}", 
            centerX, centerY, scale, sprite->getContentWidth() * scale, sprite->getContentHeight() * scale);
        log::info("[ThumbnailViewPopup] Parent: {}, Visible: {}, Opacity: {}, Z-Order: {}", 
            (void*)sprite->getParent(), sprite->isVisible(), sprite->getOpacity(), sprite->getZOrder());

        // Navigation Arrows and Counter
        if (!m_suggestions.empty()) {
            auto menu = CCMenu::create();
            menu->setPosition({0, 0});
            this->m_mainLayer->addChild(menu, 20);

            auto leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            m_leftArrow = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(LocalThumbnailViewPopup::onPrevSuggestion));
            m_leftArrow->setPosition({centerX - maxWidth/2 - 20.f, centerY});
            menu->addChild(m_leftArrow);

            auto rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            rightSpr->setFlipX(true);
            m_rightArrow = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(LocalThumbnailViewPopup::onNextSuggestion));
            m_rightArrow->setPosition({centerX + maxWidth/2 + 20.f, centerY});
            menu->addChild(m_rightArrow);

            m_counterLabel = CCLabelBMFont::create(fmt::format("{}/{}", m_currentIndex + 1, m_suggestions.size()).c_str(), "bigFont.fnt");
            m_counterLabel->setScale(0.5f);
            m_counterLabel->setPosition({centerX, centerY - maxHeight/2 - 15.f});
            this->m_mainLayer->addChild(m_counterLabel, 20);

            // Update visibility based on count
            m_leftArrow->setVisible(m_suggestions.size() > 1);
            m_rightArrow->setVisible(m_suggestions.size() > 1);
        }
        
        // Button menu
        m_buttonMenu = CCMenu::create();
        auto buttonMenu = m_buttonMenu;
        
        // Download button (bottom-right corner) via the Assets override system
        auto downloadSprite = Assets::loadButtonSprite(
            "popup-download",
            "frame:GJ_downloadBtn_001.png",
            [](){
                if (auto spr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png")) return spr;
                if (auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) return spr;
                return CCSprite::createWithSpriteFrameName("GJ_button_01.png");
            }
        );
        downloadSprite->setScale(0.7f);
        auto downloadBtn = CCMenuItemSpriteExtra::create(downloadSprite, this, menu_selector(LocalThumbnailViewPopup::onDownloadBtn));
        // downloadBtn->setPosition({content.width - 65.f, 20.f});
        // buttonMenu->addChild(downloadBtn);

        // Center button: Accept (verification queue), Delete (report queue), or Report (normal)
        CCMenuItemSpriteExtra* centerBtn = nullptr;
        
        // If opened from the verification queue (Verify/Update), show the Accept button
        if (m_verificationCategory >= 0 && m_verificationCategory != 2) { // 0=Verify, 1=Update, 2=Report
            auto acceptSpr = ButtonSprite::create(Localization::get().getString("level.accept_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.5f);
            acceptSpr->setScale(0.6f);
            centerBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
        } else if (openedFromReport) {
            auto delSpr = ButtonSprite::create(Localization::get().getString("level.delete_button").c_str(), 90, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.5f);
            delSpr->setScale(0.6f);
            centerBtn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(LocalThumbnailViewPopup::onDeleteReportedThumb));
        } else {
            auto reportSpr = ButtonSprite::create(Localization::get().getString("level.report_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.5f);
            reportSpr->setScale(0.6f);
            centerBtn = CCMenuItemSpriteExtra::create(reportSpr, this, menu_selector(LocalThumbnailViewPopup::onReportBtn));
        }
        // centerBtn->setPosition({content.width / 2, 20.f});
        // buttonMenu->addChild(centerBtn);
        
        // Extra accept button (only if m_canAcceptUpload is enabled and not from verification)
        CCMenuItemSpriteExtra* acceptBtn = nullptr;
        if (m_canAcceptUpload && m_verificationCategory < 0) {
            auto acceptSpr = ButtonSprite::create(Localization::get().getString("level.accept_button").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.5f);
            acceptSpr->setScale(0.6f);
            acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(LocalThumbnailViewPopup::onAcceptThumbBtn));
            // acceptBtn->setPosition({content.width / 2 - 60.f, 20.f});
            // buttonMenu->addChild(acceptBtn);
            
            // Reposition the center button to the right
            // centerBtn->setPosition({content.width / 2 + 60.f, 20.f});
        }

        // Add buttons to the menu in layout order
        if (acceptBtn) buttonMenu->addChild(acceptBtn);
        if (centerBtn) buttonMenu->addChild(centerBtn);
        
        // Rate button (next to report button)
        auto rateSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        rateSpr->setScale(0.7f);
        auto rateBtn = CCMenuItemSpriteExtra::create(rateSpr, this, menu_selector(LocalThumbnailViewPopup::onRate));
        buttonMenu->addChild(rateBtn);
        
        buttonMenu->addChild(downloadBtn);

        // Add delete button for moderators
        auto gm = GameManager::sharedState();
        if (gm) {
            auto username = gm->m_playerName;
            auto accountID = gm->m_playerUserID;
            
            this->retain();
            ThumbnailAPI::get().checkModeratorAccount(username, accountID, [this](bool isMod, bool isAdmin) {
                if (isMod || isAdmin) {
                    auto spr = CCSprite::createWithSpriteFrameName("GJ_deleteBtn_001.png");
                    if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
                    
                    if (spr) {
                        spr->setScale(0.6f);
                        auto btn = CCMenuItemSpriteExtra::create(
                            spr,
                            this,
                            menu_selector(LocalThumbnailViewPopup::onDeleteThumbnail)
                        );
                        
                        if (m_buttonMenu) {
                            m_buttonMenu->addChild(btn);
                            m_buttonMenu->updateLayout();
                        }
                    }
                }
                this->release();
            });
        }

        // Apply a horizontal row layout
        // Ensure the menu has the correct properties for the layout system
        buttonMenu->ignoreAnchorPointForPosition(false);
        buttonMenu->setAnchorPoint({0.5f, 0.5f});
        buttonMenu->setContentSize({content.width - 40.f, 60.f});
        buttonMenu->setPosition({content.width / 2, 46.f});

        auto layout = RowLayout::create();
        layout->setGap(15.f);
        layout->setAxisAlignment(AxisAlignment::Center);
        layout->setCrossAxisAlignment(AxisAlignment::Center);
        
        buttonMenu->setLayout(layout);
        buttonMenu->updateLayout();
        
        this->m_mainLayer->addChild(buttonMenu, 10);
    }
    
    void showNoThumbnail(CCSize content) {
        float centerX = content.width * 0.5f;
        float centerY = content.height * 0.5f + 10.f;
        float bgWidth = content.width - 60.f;
        float bgHeight = content.height - 80.f;
        
        // Black background
        auto bg = CCLayerColor::create({0, 0, 0, 200});
        bg->setContentSize({bgWidth, bgHeight});
        bg->setPosition({centerX - bgWidth / 2, centerY - bgHeight / 2});
        this->m_mainLayer->addChild(bg);
        
        // Add a black border via UIBorderHelper
        UIBorderHelper::createBorder(centerX, centerY, bgWidth, bgHeight, this->m_mainLayer);
        
        // Sad-face text
        auto sadLabel = CCLabelBMFont::create(":(", "bigFont.fnt");
        sadLabel->setScale(3.0f);
        sadLabel->setOpacity(100);
        sadLabel->setPosition({centerX, centerY + 20.f});
        this->m_mainLayer->addChild(sadLabel, 2);
        
        // "No thumbnail" text
        auto noThumbLabel = CCLabelBMFont::create(Localization::get().getString("level.no_thumbnail_text").c_str(), "goldFont.fnt");
        noThumbLabel->setScale(0.6f);
        noThumbLabel->setOpacity(150);
        noThumbLabel->setPosition({centerX, centerY - 20.f});
        this->m_mainLayer->addChild(noThumbLabel, 2);
    }
    
    void onDownloadBtn(CCObject*) {
        try {
            std::string savePath;

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
            auto saveDir = Mod::get()->getSaveDir() / "saved_thumbnails";
            if (!std::filesystem::exists(saveDir)) {
                std::filesystem::create_directories(saveDir);
            }
            savePath = (saveDir / fmt::format("thumb_{}.png", m_levelID)).string();
            Notification::create(Localization::get().getString("level.saving_mod_folder").c_str(), NotificationIcon::Info)->show();
#else
            auto pathOpt = pt::saveImageFileDialog(L"miniatura.png");
            if (!pathOpt) {
                log::info("User cancelled save dialog");
                return;
            }
            savePath = pathOpt->string();
#endif
            log::debug("Save path chosen: {}", savePath);

            // Load the RGB data from LocalThumbs
            auto pathStr = LocalThumbs::get().getThumbPath(m_levelID);
            if (!pathStr) {
                log::error("Thumbnail path not found");
                Notification::create(Localization::get().getString("level.no_thumbnail").c_str(), NotificationIcon::Error)->show();
                return;
            }

            // Use ImageConverter to load the RGB and its dimensions
            std::vector<uint8_t> rgbData;
            uint32_t width, height;
            if (!ImageConverter::loadRgbFile(*pathStr, rgbData, width, height)) {
                Notification::create(Localization::get().getString("level.read_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            // Convert from RGB888 to RGBA8888
            auto rgba = ImageConverter::rgbToRgba(rgbData, width, height);

            // Build a CCImage from the RGBA data
            CCImage img;
            if (!img.initWithImageData(rgba.data(), rgba.size(), CCImage::kFmtRawData, width, height)) {
                log::error("Failed to init CCImage with raw data");
                Notification::create(Localization::get().getString("level.create_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            // Save to file (PNG)
            std::thread([img = std::move(img), savePath]() mutable {
                try {
                    if (!img.saveToFile(savePath.c_str(), false)) {
                         geode::Loader::get()->queueInMainThread([savePath]() {
                            log::error("Failed to save image to {}", savePath);
                            geode::Notification::create(Localization::get().getString("level.save_error").c_str(), geode::NotificationIcon::Error)->show();
                         });
                         return;
                    }
                    geode::Loader::get()->queueInMainThread([savePath]() {
                        log::info("Image saved successfully to {}", savePath);
                        geode::Notification::create(Localization::get().getString("level.saved").c_str(), geode::NotificationIcon::Success)->show();
                    });
                } catch(...) {
                    geode::Loader::get()->queueInMainThread([]() {
                        log::error("Unknown error in save thread");
                    });
                }
            }).detach();

        } catch (std::exception& e) {
            log::error("Exception in onDownloadBtn: {}", e.what());
            Notification::create(Localization::get().getString("level.error_prefix") + std::string(e.what()), NotificationIcon::Error)->show();
        }
    }

    void onDeleteReportedThumb(CCObject*) {
        log::info("[ThumbnailViewPopup] Borrar miniatura reportada para levelID={}", m_levelID);
        
        // Save the levelID before any async work
        int levelID = m_levelID;
        
        // Get username
        std::string username;
        try {
            auto* gm = GameManager::sharedState();
            if (gm) {
                username = gm->m_playerName;
            } else {
                log::warn("[ThumbnailViewPopup] GameManager::sharedState() es null");
                username = "Unknown";
            }
        } catch(...) {
            log::error("[ThumbnailViewPopup] Excepción al acceder a GameManager");
            username = "Unknown";
        }
        
        // Check moderator permissions before deleting
        ThumbnailAPI::get().checkModerator(username, [levelID, username](bool isMod, bool isAdmin) {
            if (!isMod && !isAdmin) {
                Notification::create(Localization::get().getString("level.delete_moderator_only").c_str(), NotificationIcon::Error)->show();
                return;
            }
            
            // Delete from the server and update local state
            Notification::create(Localization::get().getString("level.deleting_server").c_str(), NotificationIcon::Info)->show();
            ThumbnailAPI::get().deleteThumbnail(levelID, username, [levelID](bool success, const std::string& msg) {
                if (success) {
                    // Also remove it from the local reports queue
                    PendingQueue::get().accept(levelID, PendingCategory::Report);
                    Notification::create(Localization::get().getString("level.deleted_server").c_str(), NotificationIcon::Success)->show();
                    log::info("[ThumbnailViewPopup] Miniatura {} eliminada del servidor", levelID);
                } else {
                    Notification::create(Localization::get().getString("level.delete_error") + msg, NotificationIcon::Error)->show();
                    log::error("[ThumbnailViewPopup] Error al borrar miniatura: {}", msg);
                }
            });
        });
    }
    
    void onAcceptThumbBtn(CCObject*) {
        log::info("Aceptar thumbnail presionado en ThumbnailViewPopup para levelID={}", m_levelID);
        
        // If it came from the verification queue, accept it on the server
        if (m_verificationCategory >= 0) {
            log::info("Aceptando thumbnail desde cola de verificación (categoría: {})", m_verificationCategory);
            
            std::string username;
            try {
                auto* gm = GameManager::sharedState();
                if (gm) {
                    username = gm->m_playerName;
                } else {
                    log::warn("[ThumbnailViewPopup] GameManager::sharedState() es null");
                }
            } catch(...) {
                log::error("[ThumbnailViewPopup] Excepción al acceder a GameManager");
            }
            
            Notification::create(Localization::get().getString("level.accepting").c_str(), NotificationIcon::Info)->show();
            
            std::string targetFilename = "";
            if (!m_suggestions.empty() && m_currentIndex >= 0 && m_currentIndex < m_suggestions.size()) {
                targetFilename = m_suggestions[m_currentIndex].filename;
            }

            // Accept the queue item
            ThumbnailAPI::get().acceptQueueItem(
                m_levelID, 
                static_cast<PendingCategory>(m_verificationCategory), 
                username,
                [levelID = m_levelID, category = m_verificationCategory](bool success, const std::string& message) {
                    if (success) {
                        // Remove from local queue
                        PendingQueue::get().accept(levelID, static_cast<PendingCategory>(category));
                        Notification::create(Localization::get().getString("level.accepted").c_str(), NotificationIcon::Success)->show();
                        log::info("[ThumbnailViewPopup] Miniatura aceptada para nivel {}", levelID);
                    } else {
                        Notification::create(Localization::get().getString("level.accept_error") + message, NotificationIcon::Error)->show();
                        log::error("[ThumbnailViewPopup] Error aceptando miniatura: {}", message);
                    }
                },
                targetFilename
            );
            
            return;
        }
        
        // If not from verification, try uploading from LocalThumbs (original behavior)
        log::info("Intentando aceptar desde LocalThumbs");
        
        // Convert the local thumbnail to PNG via ImageConverter
        auto pathOpt = LocalThumbs::get().getThumbPath(m_levelID);
        if (!pathOpt) {
            Notification::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        std::vector<uint8_t> pngData;
        if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
            Notification::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // Log the PNG size for debugging
        size_t base64Size = ((pngData.size() + 2) / 3) * 4;
        log::info("PNG size: {} bytes ({:.2f} KB), Base64 size: ~{} bytes ({:.2f} KB)", 
                 pngData.size(), pngData.size() / 1024.0, base64Size, base64Size / 1024.0);

        // Check if the user is a verified moderator via ModeratorVerification
// [SERVER DISABLED]         bool isMod = ModeratorVerification::isVerifiedModerator();

        // Force online verification before upload
        std::string username;
        try {
            auto* gm = GameManager::sharedState();
            if (gm) {
                username = gm->m_playerName;
            } else {
                log::warn("[ThumbnailViewPopup] GameManager::sharedState() es null");
            }
        } catch(...) {
            log::error("[ThumbnailViewPopup] Excepción al acceder a GameManager");
        }
        
        // SERVER UPLOAD DISABLED
        log::warn("[ThumbnailViewPopup] Server upload disabled - thumbnail saved locally only");
        Notification::create(Localization::get().getString("level.saved_local_server_disabled").c_str(), NotificationIcon::Info)->show();
        
        /* ORIGINAL SERVER CODE - DISABLED
        Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
        ModeratorVerification::verifyOnline(username, [this, pngData, username](bool approved) {
            if (approved) {
                log::info("[ThumbnailViewPopup] User verified as moderator, uploading level {}", m_levelID);
                Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                ServerAPI::get().uploadThumbnailPNG(m_levelID, pngData.data(), static_cast<int>(pngData.size()),
                    [levelID = m_levelID](bool success, const std::string&){
                        if (success) {
                            PendingQueue::get().removeForLevel(levelID);
                            Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                        } else {
                            Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                        }
                    }
                );
            } else {
                log::info("[ThumbnailViewPopup] Non-moderator user - enqueueing as pending");
                auto mappedLocal = LocalThumbs::get().getFileName(m_levelID);
                ServerAPI::get().checkThumbnailExists(m_levelID, [levelID = m_levelID, username, mappedLocal](bool existsServer){
                    auto cat = (mappedLocal.has_value() || existsServer) ? PendingCategory::Update : PendingCategory::Verify;
                    PendingQueue::get().addOrBump(levelID, cat, username);
                    Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Info)->show();
                });
            }
        });
        */
    }

    void onReportBtn(CCObject*) {
        
        // Save the levelID before any async work
        int levelID = m_levelID;
        
        auto popup = ReportInputPopup::create(levelID, [levelID](std::string reason) {
            std::string user;
            try {
                auto* gm = GameManager::sharedState();
                if (gm) {
                    user = gm->m_playerName;
                }
            } catch(...) {}
            
            // Send the report to the server
            ThumbnailAPI::get().submitReport(levelID, user, reason, [levelID, reason](bool success, const std::string& message) {
                if (success) {
                    Notification::create(Localization::get().getString("report.sent_synced") + reason, NotificationIcon::Warning)->show();
                    log::info("[ThumbnailViewPopup] Reporte confirmado y enviado al servidor para nivel {}", levelID);
                } else {
                    Notification::create(Localization::get().getString("report.saved_local").c_str(), NotificationIcon::Info)->show();
                    log::warn("[ThumbnailViewPopup] Reporte guardado solo localmente para nivel {}", levelID);
                }
            });
        });
        
        if (popup) {
            popup->show();
        }
    }

    void onDeleteThumbnail(CCObject*) {
        int levelID = m_levelID;
        auto gm = GameManager::sharedState();
        std::string username = gm ? gm->m_playerName : "";
        
        std::string thumbnailId = "";
        if (m_currentIndex >= 0 && m_currentIndex < m_thumbnails.size()) {
            thumbnailId = m_thumbnails[m_currentIndex].id;
        }

        this->retain();
        ThumbnailAPI::get().getRating(levelID, username, thumbnailId, [this, levelID, username](bool success, float avg, int count, int userVote) {
            ThumbnailAPI::get().checkModerator(username, [this, levelID, username, count](bool isMod, bool isAdmin) {
                if (!isMod && !isAdmin) {
                     Notification::create("No tienes permisos", NotificationIcon::Error)->show();
                     this->release();
                     return;
                }
                
                if (count > 100 && !isAdmin) {
                    Notification::create("Solo administradores pueden borrar miniaturas con +100 votos", NotificationIcon::Error)->show();
                    this->release();
                    return;
                }
                
                geode::createQuickPopup(
                    "Borrar Miniatura",
                    "Estas seguro de que quieres borrar esta miniatura? Esto tambien eliminara los puntos de rating del creador.",
                    "Cancelar", "Borrar",
                    [this, levelID, username](auto, bool btn2) {
                        if (btn2) {
                            ThumbnailAPI::get().deleteThumbnail(levelID, username, [this](bool success, std::string msg) {
                                if (success) {
                                    Notification::create("Miniatura borrada", NotificationIcon::Success)->show();
                                    this->onClose(nullptr);
                                } else {
                                    Notification::create(msg.c_str(), NotificationIcon::Error)->show();
                                }
                            });
                        }
                        this->release();
                    }
                );
            });
        });
    }
    
    // Recenter the image
    void onRecenter(CCObject*) {
        if (!m_thumbnailSprite) return;
        
        m_thumbnailSprite->stopAllActions();
        
        auto content = this->m_mainLayer->getContentSize();
        float centerX = content.width * 0.5f;
        float centerY = content.height * 0.5f + 10.f;
        
        // Smooth recenter animation
        auto moveTo = CCMoveTo::create(0.3f, {centerX, centerY});
        auto scaleTo = CCScaleTo::create(0.3f, m_initialScale);
        auto easeMove = CCEaseSineOut::create(moveTo);
        auto easeScale = CCEaseSineOut::create(scaleTo);
        
        m_thumbnailSprite->runAction(easeMove);
        m_thumbnailSprite->runAction(easeScale);
        m_thumbnailSprite->setAnchorPoint({0.5f, 0.5f});
    }
    
    // Utility: clamp a value between min and max
    static float clamp(float value, float min, float max) {
        return std::max(min, std::min(value, max));
    }
    
    // Touch handlers for zoom and pan
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        if (!this->isVisible()) return false;

        // Check if the touch is inside the popup area
        auto touchPos = touch->getLocation();
        auto nodePos = m_mainLayer->convertToNodeSpace(touchPos);
        auto size = m_mainLayer->getContentSize();
        CCRect rect = {0, 0, size.width, size.height};
        
        if (!rect.containsPoint(nodePos)) {
            return false;
        }

        // Check if touch is on any menu button to allow interaction
        auto isTouchOnMenu = [](CCMenu* menu, CCTouch* touch) -> bool {
            if (!menu || !menu->isVisible()) return false;
            auto point = menu->convertTouchToNodeSpace(touch);
            
            CCObject* obj;
            CCARRAY_FOREACH(menu->getChildren(), obj) {
                auto item = dynamic_cast<CCMenuItem*>(obj);
                if (item && item->isVisible() && item->isEnabled()) {
                    if (item->boundingBox().containsPoint(point)) {
                        return true;
                    }
                }
            }
            return false;
        };

        if (isTouchOnMenu(m_buttonMenu, touch)) return false;
        if (isTouchOnMenu(m_ratingMenu, touch)) return false;

#ifdef GEODE_IS_ANDROID
        // On Android, consume the touch but do nothing (static).
        // This avoids crashes related to UI/OpenGL updates during touch handling.
        return true;
#endif

        if (m_touches.size() == 1) {
            // Segunda touch - preparar para zoom
            auto firstTouch = *m_touches.begin();
            // Avoid issues if, for some reason, the same touch gets processed twice
            if (firstTouch == touch) return true;

            auto firstLoc = firstTouch->getLocation();
            auto secondLoc = touch->getLocation();
            
            m_touchMidPoint = (firstLoc + secondLoc) / 2.0f;
            m_savedScale = m_thumbnailSprite ? m_thumbnailSprite->getScale() : m_initialScale;
            m_initialDistance = firstLoc.getDistance(secondLoc);
            
            // Adjust the anchor point to the touch midpoint
            if (m_thumbnailSprite) {
                auto oldAnchor = m_thumbnailSprite->getAnchorPoint();
                auto worldPos = m_thumbnailSprite->convertToWorldSpace({0, 0});
                auto newAnchorX = (m_touchMidPoint.x - worldPos.x) / m_thumbnailSprite->getScaledContentWidth();
                auto newAnchorY = (m_touchMidPoint.y - worldPos.y) / m_thumbnailSprite->getScaledContentHeight();
                
                m_thumbnailSprite->setAnchorPoint({clamp(newAnchorX, 0, 1), clamp(newAnchorY, 0, 1)});
                m_thumbnailSprite->setPosition({
                    m_thumbnailSprite->getPositionX() + m_thumbnailSprite->getScaledContentWidth() * -(oldAnchor.x - clamp(newAnchorX, 0, 1)),
                    m_thumbnailSprite->getPositionY() + m_thumbnailSprite->getScaledContentHeight() * -(oldAnchor.y - clamp(newAnchorY, 0, 1))
                });
            }
        }
        
        m_touches.insert(touch);
        return true;
    }
    
    void ccTouchMoved(CCTouch* touch, CCEvent* event) override {
#ifdef GEODE_IS_ANDROID
        return;
#endif
        if (!m_thumbnailSprite) return;
        
        if (m_touches.size() == 1) {
            // One-finger pan
            auto delta = touch->getDelta();
            m_thumbnailSprite->setPosition({
                m_thumbnailSprite->getPositionX() + delta.x,
                m_thumbnailSprite->getPositionY() + delta.y
            });
        } else if (m_touches.size() == 2) {
            // Two-finger pinch zoom
            m_wasZooming = true;
            
            auto it = m_touches.begin();
            auto firstTouch = *it;
            ++it;
            auto secondTouch = *it;
            
            auto firstLoc = firstTouch->getLocation();
            auto secondLoc = secondTouch->getLocation();
            auto center = (firstLoc + secondLoc) / 2.0f;
            auto distNow = firstLoc.getDistance(secondLoc);
            
            // Compute new zoom
            // Guard against division by zero
            if (m_initialDistance < 0.1f) m_initialDistance = 0.1f;
            if (distNow < 0.1f) distNow = 0.1f;

            auto mult = m_initialDistance / distNow;
            if (mult < 0.0001f) mult = 0.0001f;

            auto zoom = clamp(m_savedScale / mult, m_minScale, m_maxScale);
            m_thumbnailSprite->setScale(zoom);
            
            // Adjust position based on center movement
            auto centerDiff = m_touchMidPoint - center;
            m_thumbnailSprite->setPosition(m_thumbnailSprite->getPosition() - centerDiff);
            m_touchMidPoint = center;
        }
    }
    
    void ccTouchEnded(CCTouch* touch, CCEvent* event) override {
        m_touches.erase(touch);

#ifdef GEODE_IS_ANDROID
        return;
#endif
        
        if (!m_thumbnailSprite) return;
        
        // If we finished zooming and only one touch remains, reset state
        if (m_wasZooming && m_touches.size() == 1) {
            auto scale = m_thumbnailSprite->getScale();
            
            // Clamp scale with animation if out of range
            if (scale < m_minScale) {
                m_thumbnailSprite->runAction(
                    CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_minScale))
                );
            } else if (scale > m_maxScale) {
                m_thumbnailSprite->runAction(
                    CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_maxScale))
                );
            }
            
            m_wasZooming = false;
        }
        
        return;
    }
    
    // Soporte para scroll wheel (zoom)
    void scrollWheel(float x, float y) override {
#ifdef GEODE_IS_ANDROID
        return;
#endif
        // Robust validation: ensure the popup and sprite still exist
        if (!m_mainLayer || !m_thumbnailSprite) {
            log::warn("[ThumbnailViewPopup] scrollWheel llamado pero popup o sprite no válidos");
            return;
        }
        
        // Ensure the sprite is still part of the scene graph
        if (!m_thumbnailSprite->getParent()) {
            log::warn("[ThumbnailViewPopup] scrollWheel: sprite sin parent, posiblemente destruido");
            m_thumbnailSprite = nullptr;
            return;
        }
        
        log::info("[ThumbnailViewPopup] Scroll wheel recibido: x={}, y={}", x, y);
        
        // Use magnitude/sign to determine direction
        float zoomFactor;
        if (std::abs(y) < 0.001f) {
            // No hay scroll vertical, probar horizontal
            zoomFactor = x < 0 ? 1.15f : 0.85f;
            log::info("[ThumbnailViewPopup] Usando scroll horizontal: x={}", x);
        } else {
            // Scroll vertical normal
            zoomFactor = y < 0 ? 1.15f : 0.85f;
            log::info("[ThumbnailViewPopup] Usando scroll vertical: y={}", y);
        }
        
        float currentScale = m_thumbnailSprite->getScale();
        float newScale = currentScale * zoomFactor;
        
        // Clamp zoom
        newScale = clamp(newScale, m_minScale, m_maxScale);
        
        m_thumbnailSprite->setScale(newScale);
        
        log::info("[ThumbnailViewPopup] Factor={}, Escala: {} -> {}", zoomFactor, currentScale, newScale);
    }
    
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override {
        m_touches.erase(touch);
        m_wasZooming = false;
    }

public:
    static LocalThumbnailViewPopup* create(int32_t levelID, bool canAcceptUpload) {
        auto ret = new LocalThumbnailViewPopup();
        // Width adjusted to 400.f as requested
        if (ret && ret->initAnchored(400.f, 280.f, std::make_pair(levelID, canAcceptUpload))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// Exported function used from other files
CCNode* createThumbnailViewPopup(int32_t levelID, bool canAcceptUpload, const std::vector<Suggestion>& suggestions) {
    auto ret = LocalThumbnailViewPopup::create(levelID, canAcceptUpload);
    if (ret) {
        ret->setSuggestions(suggestions);
    }
    return ret;
}

class $modify(PaimonLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        CCMenuItemSpriteExtra* m_thumbnailButton = nullptr;
        CCNode* m_pixelBg = nullptr;
        bool m_fromThumbsList = false;
        bool m_fromReportSection = false;
        bool m_fromVerificationQueue = false;
        bool m_fromLeaderboards = false;
        LeaderboardState m_leaderboardState = LeaderboardState::Top100;
        CCMenuItemSpriteExtra* m_acceptThumbBtn = nullptr;
        CCMenuItemSpriteExtra* m_editModeBtn = nullptr;
        CCMenuItemSpriteExtra* m_uploadLocalBtn = nullptr;
        CCMenu* m_extraMenu = nullptr;
        bool m_thumbnailRequested = false; // Flag para evitar cargas duplicadas
        
        // Multi-thumbnail fields
        std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
        int m_currentThumbnailIndex = 0;
        CCMenuItemSpriteExtra* m_prevBtn = nullptr;
        CCMenuItemSpriteExtra* m_nextBtn = nullptr;
        CCMenuItemSpriteExtra* m_rateBtn = nullptr;
        bool m_cycling = true;
        float m_cycleTimer = 0.0f;
    };
    
    void applyThumbnailBackground(CCTexture2D* tex, int32_t levelID) {
        if (!tex) return;
        
        log::info("[LevelInfoLayer] Aplicando fondo del thumbnail");
        
        // Obtener estilo e intensidad
        std::string bgStyle = "pixel";
        int intensity = 5;
        try { 
            bgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style"); 
            intensity = static_cast<int>(Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity"));
        } catch(...) {}
        
        intensity = std::max(1, std::min(10, intensity));
        auto win = CCDirector::sharedDirector()->getWinSize();

        // Helper lambda to apply effects
        auto applyEffects = [this, bgStyle, intensity, win, tex](CCSprite*& sprite, bool isGIF) {
            if (!sprite) return;

            // Initial Scale & Position
            float scaleX = win.width / sprite->getContentSize().width;
            float scaleY = win.height / sprite->getContentSize().height;
            float scale = std::max(scaleX, scaleY);
            sprite->setScale(scale);
            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
            sprite->setAnchorPoint({0.5f, 0.5f});

            if (bgStyle == "normal") {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                sprite->getTexture()->setTexParameters(&params);
            } 
            else if (bgStyle == "pixel") {
                if (isGIF) {
                     auto shader = getOrCreateShader("pixelate"_spr, vertexShader, fragmentShaderPixelate);
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = dynamic_cast<AnimatedGIFSprite*>(sprite)) ags->m_intensity = intensityVal;
                         else shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                         shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                     }
                } else {
                    // Pixel mode: pixelation effect based on intensity (1-10)
                    float t = (intensity - 1) / 9.0f; 
                    float pixelFactor = 0.5f - (t * 0.47f); 
                    int renderWidth = std::max(32, static_cast<int>(win.width * pixelFactor));
                    int renderHeight = std::max(32, static_cast<int>(win.height * pixelFactor));
                    
                    auto renderTex = CCRenderTexture::create(renderWidth, renderHeight);
                    if (renderTex) {
                        float renderScaleX = static_cast<float>(renderWidth) / tex->getContentSize().width;
                        float renderScaleY = static_cast<float>(renderHeight) / tex->getContentSize().height;
                        float renderScale = std::min(renderScaleX, renderScaleY);
                        
                        sprite->setScale(renderScale);
                        sprite->setPosition({renderWidth / 2.0f, renderHeight / 2.0f});
                        
                        renderTex->begin();
                        glClearColor(0, 0, 0, 0);
                        glClear(GL_COLOR_BUFFER_BIT);
                        sprite->visit();
                        renderTex->end();
                        
                        auto pixelTexture = renderTex->getSprite()->getTexture();
                        sprite = CCSprite::createWithTexture(pixelTexture);
                        
                        if (sprite) {
                            float finalScaleX = win.width / renderWidth;
                            float finalScaleY = win.height / renderHeight;
                            float finalScale = std::max(finalScaleX, finalScaleY);
                            
                            sprite->setScale(finalScale);
                            sprite->setFlipY(true);
                            sprite->setAnchorPoint({0.5f, 0.5f});
                            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                            
                            ccTexParams params{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                            pixelTexture->setTexParameters(&params);
                        }
                    }
                }
            }
            else if (bgStyle == "blur") {
                if (isGIF) {
                     auto shader = getOrCreateShader("blur-single"_spr, vertexShader, fragmentShaderBlurSinglePass);
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = dynamic_cast<AnimatedGIFSprite*>(sprite)) ags->m_intensity = intensityVal;
                         else shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                         shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                     }
                } else {
                    float blurRadius = 0.01f + ((intensity - 1) / 9.0f * 0.14f);
                    auto rtA = CCRenderTexture::create(win.width, win.height);
                    auto rtB = CCRenderTexture::create(win.width, win.height);
                    auto blurH = getOrCreateShader("blur-horizontal"_spr, vertexShader, fragmentShaderHorizontal);
                    auto blurV = getOrCreateShader("blur-vertical"_spr, vertexShader, fragmentShaderVertical);
                    
                    if (blurH && blurV && rtA && rtB) {
                        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                        tex->setTexParameters(&params);
                        applyBlurPass(sprite, rtA, blurH, win, blurRadius);
                        applyBlurPass(rtA->getSprite(), rtB, blurV, win, blurRadius);
                        
                        sprite = rtB->getSprite();
                        sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                    }
                }
            }
            else {
                // Shader effects that work for both
                CCGLProgram* shader = nullptr;
                float val = 0.0f;
                bool useScreenSize = false;

                if (bgStyle == "grayscale") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("grayscale"_spr, vertexShader, fragmentShaderGrayscale);
                } else if (bgStyle == "sepia") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("sepia"_spr, vertexShader, fragmentShaderSepia);
                } else if (bgStyle == "vignette") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("vignette"_spr, vertexShader, fragmentShaderVignette);
                } else if (bgStyle == "scanlines") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("scanlines"_spr, vertexShader, fragmentShaderScanlines);
                    useScreenSize = true;
                } else if (bgStyle == "bloom") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("bloom"_spr, vertexShader, fragmentShaderBloom);
                    useScreenSize = true;
                } else if (bgStyle == "chromatic") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("chromatic"_spr, vertexShader, fragmentShaderChromatic);
                } else if (bgStyle == "radial-blur") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("radial-blur"_spr, vertexShader, fragmentShaderRadialBlur);
                } else if (bgStyle == "glitch") {
                    val = (intensity / 10.0f) * 2.25f;
                    shader = getOrCreateShader("glitch"_spr, vertexShader, fragmentShaderGlitch);
                } else if (bgStyle == "posterize") {
                    val = intensity / 10.0f;
                    shader = getOrCreateShader("posterize"_spr, vertexShader, fragmentShaderPosterize);
                }

                if (shader) {
                    sprite->setShaderProgram(shader);
                    shader->use();
                    shader->setUniformsForBuiltins();
                    if (auto ags = dynamic_cast<AnimatedGIFSprite*>(sprite)) {
                        ags->m_intensity = val;
                    } else {
                        shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), val);
                    }
                    if (useScreenSize) {
                        shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                    }
                }
            }
        };

        // 1. Create and apply to Static Sprite
        CCSprite* finalSprite = CCSprite::createWithTexture(tex);
        if (finalSprite) {
            applyEffects(finalSprite, false);
            
            if (m_fields->m_pixelBg) {
                m_fields->m_pixelBg->removeFromParent();
            } else if (auto old = this->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
                old->removeFromParent();
            }
            
            finalSprite->setZOrder(-1);
            finalSprite->setID("paimon-levelinfo-pixel-bg"_spr);
            this->addChild(finalSprite);
            m_fields->m_pixelBg = finalSprite;
        }

        // 2. Check for GIF and replace if exists
        if (ThumbnailLoader::get().hasGIFData(levelID)) {
             auto path = ThumbnailLoader::get().getCachePath(levelID);
             this->retain();
             AnimatedGIFSprite::createAsync(path.generic_string(), [this, applyEffects](AnimatedGIFSprite* anim) {
                 if (anim) {
                     // Remove old static bg
                     if (m_fields->m_pixelBg) {
                         m_fields->m_pixelBg->removeFromParent();
                     } else if (auto old = this->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
                         old->removeFromParent();
                     }
                     
                     // Apply effects to GIF
                     CCSprite* spritePtr = anim; // Helper expects CCSprite*&
                     applyEffects(spritePtr, true);
                     
                     anim->setZOrder(-1);
                     anim->setID("paimon-levelinfo-pixel-bg"_spr);
                     
                     this->addChild(anim);
                     m_fields->m_pixelBg = anim;
                 }
                 this->release();
             });
        }

        // Overlay
        auto overlay = CCLayerColor::create({0,0,0,70});
        overlay->setContentSize(win);
        overlay->setAnchorPoint({0,0});
        overlay->setPosition({0,0});
        overlay->setZOrder(-1);
        overlay->setID("paimon-levelinfo-pixel-overlay"_spr);
        this->addChild(overlay);
        
        log::info("[LevelInfoLayer] Fondo aplicado exitosamente (estilo: {}, intensidad: {})", bgStyle, intensity);
    }
    
    void onExit() {
        ThumbnailLoader::get().resumeQueue();
        LevelInfoLayer::onExit();
    }

    void onSetDailyWeekly(CCObject* sender) {
        if (m_level->m_levelID.value() <= 0) return;
        SetDailyWeeklyPopup::create(m_level->m_levelID.value())->show();
    }

    bool init(GJGameLevel* level, bool challenge) {
        // Detect whether we came from LeaderboardsLayer
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            if (auto layer = scene->getChildByType<LeaderboardsLayer>(0)) {
                m_fields->m_fromLeaderboards = true;
                m_fields->m_leaderboardState = layer->m_state;
            }
        }

        if (!LevelInfoLayer::init(level, challenge)) return false;
        // ThumbnailLoader::get().pauseQueue(); // Removed to allow background loading
        
        try {
            if (!level || level->m_levelID <= 0) {
                log::debug("Level ID invalid, skipping thumbnail button");
                return true;
            }

            // Consume the "opened from thumbnails list" flag
            bool fromThumbs = false;
            try {
                fromThumbs = Mod::get()->getSavedValue<bool>("open-from-thumbs", false);
                if (fromThumbs) Mod::get()->setSavedValue("open-from-thumbs", false);
            } catch(...) {}
            m_fields->m_fromThumbsList = fromThumbs;

            // Detect if we were opened from the reports tab of the Verification Center
            bool fromReport = false;
            try {
                fromReport = Mod::get()->getSavedValue<bool>("open-from-report", false);
                if (fromReport) Mod::get()->setSavedValue("open-from-report", false);
            } catch(...) {}
            m_fields->m_fromReportSection = fromReport;
            
            // Detect if we came from the verification queue
            bool fromVerificationQueue = false;
            int verificationQueueCategory = -1;
            int verificationQueueLevelID = -1;
            try {
                // Check if we are in verification queue mode by checking the level ID
                verificationQueueLevelID = Mod::get()->getSavedValue<int>("verification-queue-levelid", -1);
                
                if (verificationQueueLevelID == level->m_levelID.value()) {
                    fromVerificationQueue = true;
                    verificationQueueCategory = Mod::get()->getSavedValue<int>("verification-queue-category", -1);
                    m_fields->m_fromVerificationQueue = true;
                    
                    // We DO NOT clear the saved values here anymore, to persist across PlayLayer
                }
            } catch(...) {}
            
            // Try to show a pixelated background from the thumbnail
            bool isMainLevel = level->m_levelType == GJLevelType::Main;
            if (!isMainLevel && !m_fields->m_thumbnailRequested) {
                m_fields->m_thumbnailRequested = true;
                int32_t levelID = level->m_levelID.value();
                std::string fileName = fmt::format("{}.png", levelID);
                // Retain this layer during the async load to avoid premature destruction
                this->retain();
                auto selfPtr = this;
                ThumbnailLoader::get().requestLoad(levelID, fileName, [selfPtr, levelID](CCTexture2D* tex, bool) {
                    // Validate that the layer still exists before using it
                    try {
                        if (!selfPtr || !selfPtr->getParent()) {
                            log::warn("[LevelInfoLayer] Layer invalidated before applying pixel background");
                            // Don't release if it was already destroyed
                            return;
                        }
                    } catch (...) {
                        log::error("[LevelInfoLayer] Exception validating layer before pixel background");
                        return;
                    }
                    if (tex) {
                        static_cast<PaimonLevelInfoLayer*>(selfPtr)->applyThumbnailBackground(tex, levelID);
                    } else {
                        log::warn("[LevelInfoLayer] No texture for pixel background");
                    }
                    selfPtr->release();
                }, 5);
            }

            // Load button layouts
            ButtonLayoutManager::get().load();
            
            // Find the left-side menu
            auto leftMenu = this->getChildByID("left-side-menu");
            if (!leftMenu) {
                log::warn("Left side menu not found");
                return true;
            }

            // Save a reference to the menu for ButtonEditOverlay
            m_fields->m_extraMenu = static_cast<CCMenu*>(leftMenu);
            
            // Create the button sprite (camera icon or similar)
            CCSprite* btnSprite = nullptr;
            
            // First, try a custom resource sprite via Geode's resource system.
            // This handles paths/resolutions better than absolute paths.
            btnSprite = CCSprite::create("BotonMostrarThumbnails.png"_spr);
            
            // Fallback: camera icon
            if (!btnSprite) {
                btnSprite = CCSprite::createWithSpriteFrameName("GJ_messagesBtn_001.png");
            }
            
            // Last resort: info icon
            if (!btnSprite) {
                btnSprite = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
            }
            
            if (!btnSprite) {
                log::error("Failed to create button sprite");
                return true;
            }
            
            // Rotate 90 degrees clockwise (kept from the original code)
            btnSprite->setRotation(-90.0f);
            
            // Fix: adjust scale to avoid giant buttons on low/medium quality.
            // Force a visual size of ~40 points (standard for small GD buttons).
            // This fixes the "giant buttons" issue on some resolutions.
            float targetSize = 40.0f;
            float currentSize = std::max(btnSprite->getContentWidth(), btnSprite->getContentHeight());
            
            if (currentSize > 0) {
                float scale = targetSize / currentSize;
                btnSprite->setScale(scale);
            }
            
            auto button = CCMenuItemSpriteExtra::create(
                btnSprite,
                this,
                menu_selector(PaimonLevelInfoLayer::onThumbnailButton)
            );
            PaimonButtonHighlighter::registerButton(button);
            
            if (!button) {
                log::error("Failed to create menu button");
                return true;
            }
            
            button->setID("thumbnail-view-button"_spr);
            m_fields->m_thumbnailButton = button;

            // Fetch thumbnails for gallery
            this->retain();
            ThumbnailAPI::get().getThumbnails(level->m_levelID.value(), [this](bool success, const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbs) {
                // Check if layer is still valid? 
                // Ideally we should check if the scene still contains this layer or something.
                // But retain() ensures the object is alive.
                
                if (success) {
                    m_fields->m_thumbnails = thumbs;
                }
                
                // If empty, add default
                if (m_fields->m_thumbnails.empty()) {
                     ThumbnailAPI::ThumbnailInfo mainThumb;
                     mainThumb.id = "0";
                     mainThumb.url = ThumbnailAPI::get().getThumbnailURL(m_level->m_levelID.value());
                     m_fields->m_thumbnails.push_back(mainThumb);
                }

                if (m_fields->m_thumbnails.size() > 1) {
                    this->setupGallery();
                    this->schedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
                } else {
                    this->setupGallery();
                    if (m_fields->m_prevBtn) m_fields->m_prevBtn->setVisible(false);
                    if (m_fields->m_nextBtn) m_fields->m_nextBtn->setVisible(false);
                }
                
                this->release();
            });

            // Add first to compute the default layout, then apply any saved values
            leftMenu->addChild(button);
            leftMenu->updateLayout();

            ButtonLayout defaultLayout;
            defaultLayout.position = button->getPosition();
            defaultLayout.scale = button->getScale();
            defaultLayout.opacity = 1.0f;
            ButtonLayoutManager::get().setDefaultLayoutIfAbsent("LevelInfoLayer", "thumbnail-view-button", defaultLayout);

            // Load the layout saved to the Button
            auto savedLayout = ButtonLayoutManager::get().getLayout("LevelInfoLayer", "thumbnail-view-button");
            if (savedLayout) {
                button->setPosition(savedLayout->position);
                button->setScale(savedLayout->scale);
                button->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
                // Update the registered scale after applying the layout
                PaimonButtonHighlighter::updateButtonScale(button);
            }

            // Check for Admin status to show Set Daily/Weekly button
            if (auto gm = GameManager::sharedState()) {
                auto username = gm->m_playerName;
                auto accountID = gm->m_playerUserID;
                
                this->retain();
                HttpClient::get().checkModeratorAccount(username, accountID, [this](bool isMod, bool isAdmin) {
                    if (isAdmin) {
                        // Daily Button (Admin Set)
                        // Use generic star/time icon instead of Daily.png which is for Leaderboards
                        CCSprite* spr = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
                        
                        if (!spr) {
                            spr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
                        }
                        
                        // Scale logic
                        float targetSize = 35.0f;
                        float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
                        if (currentSize > 0) spr->setScale(targetSize / currentSize);
                        else spr->setScale(0.6f);
                        
                        auto btn = CCMenuItemSpriteExtra::create(
                            spr,
                            this,
                            menu_selector(PaimonLevelInfoLayer::onSetDailyWeekly)
                        );
                        btn->setID("set-daily-weekly-button"_spr);
                        
                        auto leftMenu = static_cast<CCMenu*>(this->getChildByID("left-side-menu"));
                        if (leftMenu) {
                            leftMenu->addChild(btn);
                            leftMenu->updateLayout();
                        }
                    }
                    this->release();
                });
            }

            // Edit mode button removed
            
            // Add Button of upload to moderator if exists thumbnail local
            /* [SERVIDOR DESACTIVADO] - ModeratorVerification removido
            bool isMod = ModeratorVerification::isVerifiedModerator();
            
            if (isMod && LocalThumbs::get().has(level->m_levelID.value())) {
                // Try a custom upload sprite first.
                // Use _spr for better resource handling.
                auto uploadSpr = CCSprite::create("Subida.png"_spr);
                
                if (!uploadSpr) {
                    uploadSpr = CCSprite::createWithSpriteFrameName("GJ_uploadBtn_001.png");
                }
                if (!uploadSpr) {
                    // Fallback sprite
                    uploadSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
                }
                if (uploadSpr) {
                    // Fix: Scale based on size
                    float targetSize = 35.0f;
                    float currentSize = std::max(uploadSpr->getContentWidth(), uploadSpr->getContentHeight());
                    
                    if (currentSize > 0) {
                        float scale = targetSize / currentSize;
                        uploadSpr->setScale(scale);
                    } else {
                        uploadSpr->setScale(0.8f);
                    }
                    uploadSpr->setRotation(-90.0f);
                    auto uploadBtn = CCMenuItemSpriteExtra::create(uploadSpr, this, menu_selector(PaimonLevelInfoLayer::onUploadLocalThumbnail));
                    PaimonButtonHighlighter::registerButton(uploadBtn);
                    uploadBtn->setID("upload-local-thumbnail-button"_spr);
                    
 // Load the saved layout for the upload button
                    savedLayout = ButtonLayoutManager::get().getLayout("LevelInfoLayer", "upload-local-thumbnail-button");
                    if (savedLayout) {
                        uploadBtn->setPosition(savedLayout->position);
                        uploadBtn->setScale(savedLayout->scale);
                        uploadBtn->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
 // Update scale after applying the layout
                        PaimonButtonHighlighter::updateButtonScale(uploadBtn);
                    }
                    
                    leftMenu->addChild(uploadBtn);
                    leftMenu->updateLayout();
                    m_fields->m_uploadLocalBtn = uploadBtn;
                    log::info("Upload local thumbnail button added to LevelInfoLayer");
                }
            }
            */
            
            log::info("Thumbnail button added successfully");
            
            // If opened from the verification queue, store the category so it loads correctly
            if (fromVerificationQueue && verificationQueueLevelID == level->m_levelID.value()) {
                log::info("Nivel abierto desde verificación (categoría: {}) - botón listo para usar", verificationQueueCategory);
                // Store the category so ThumbnailViewPopup loads correctly when clicking the button
                Mod::get()->setSavedValue("verification-category", verificationQueueCategory);
            }

            // Accept/upload buttons are now shown inside ThumbnailViewPopup
            
        } catch (std::exception& e) {
            log::error("Exception in LevelInfoLayer::init: {}", e.what());
        } catch (...) {
            log::error("Unknown exception in LevelInfoLayer::init");
        }
        
        return true;
    }
    
    void onThumbnailButton(CCObject*) {
        log::info("Thumbnail button clicked");
        
        try {
            if (!m_level) {
                log::error("Level is null");
                return;
            }
            
            int32_t levelID = m_level->m_levelID.value();
            log::info("Opening thumbnail view for level ID: {}", levelID);
            
            // Use ModeratorVerification utility
            /* [SERVIDOR DESACTIVADO] - ModeratorVerification removido
            bool isMod = ModeratorVerification::isVerifiedModerator();
            bool canAccept = isMod && m_fields->m_fromThumbsList && LocalThumbs::get().has(levelID);
            */
            bool canAccept = false; // No server functionality
            // Pass context to the popup via a temporary flag
            Mod::get()->setSavedValue("from-report-popup", m_fields->m_fromReportSection);
            auto popup = LocalThumbnailViewPopup::create(levelID, canAccept);
            if (popup) {
                popup->show();
            } else {
                log::error("Failed to create thumbnail view popup");
                Notification::create("Error al abrir miniatura", NotificationIcon::Error)->show();
            }
            
        } catch (std::exception& e) {
            log::error("Exception in onThumbnailButton: {}", e.what());
            Notification::create(Localization::get().getString("level.error_prefix") + std::string(e.what()), NotificationIcon::Error)->show();
        } catch (...) {
            log::error("Unknown exception in onThumbnailButton");
            Notification::create("Error desconocido", NotificationIcon::Error)->show();
        }
    }

    void onToggleEditMode(CCObject*) {
        if (!m_fields->m_extraMenu) return;

        // Enable the glow highlight effect when entering edit mode
        PaimonButtonHighlighter::highlightAll();

        // Create and add the edit overlay to the scene
        auto overlay = ButtonEditOverlay::create("LevelInfoLayer", m_fields->m_extraMenu);
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            scene->addChild(overlay, 1000);
        }
    }

    void onUploadLocalThumbnail(CCObject*) {
        log::info("[LevelInfoLayer] Upload local thumbnail button clicked");
        
        if (!m_level) {
            Notification::create(Localization::get().getString("level.error_prefix") + "nivel no encontrado", NotificationIcon::Error)->show();
            return;
        }
        
        // Save the level pointer before async work
        auto* level = m_level;
        int32_t levelID = level->m_levelID.value();
        
        // Check if exists a thumbnail local
        if (!LocalThumbs::get().has(levelID)) {
            Notification::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // Get username
        std::string username;
        try {
            auto* gm = GameManager::sharedState();
            if (gm) {
                username = gm->m_playerName;
            } else {
                log::warn("[LevelInfoLayer] GameManager::sharedState() es null");
                username = "Unknown";
            }
        } catch(...) {
            log::error("[LevelInfoLayer] Excepción al acceder a GameManager");
            username = "Unknown";
        }
        
        // Load the local thumbnail and convert it to PNG
        auto pathOpt = LocalThumbs::get().getThumbPath(levelID);
        if (!pathOpt) {
            Notification::create("No se pudo encontrar la miniatura", NotificationIcon::Error)->show();
            return;
        }
        
        std::vector<uint8_t> pngData;
        if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
            Notification::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // Retain self to ensure we exist during callbacks
        this->retain();

        // Check moderator status
        ThumbnailAPI::get().checkModerator(username, [this, levelID, pngData, username](bool isMod, bool isAdmin) {
            if (isMod || isAdmin) {
                auto onFinish = [this, levelID](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                        try {
                            auto path = ThumbnailLoader::get().getCachePath(levelID);
                            if (std::filesystem::exists(path)) std::filesystem::remove(path);
                        } catch(...) {}
                        ThumbnailLoader::get().invalidateLevel(levelID);
                        ThumbnailLoader::get().requestLoad(levelID, "", [this, levelID](CCTexture2D* tex, bool success) {
                            if (success && tex) {
                                if (m_fields->m_pixelBg) {
                                    m_fields->m_pixelBg->removeFromParent();
                                    m_fields->m_pixelBg = nullptr;
                                }
                                this->applyThumbnailBackground(tex, levelID);
                            }
                            this->release();
                        });
                        return;
                    } else {
                        Notification::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                    }
                    this->release();
                };

                ThumbnailAPI::get().getThumbnails(levelID, [this, levelID, pngData, username, onFinish](bool success, const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbs) {
                    if (!success) {
                        Notification::create("Error checking existing thumbnails", NotificationIcon::Error)->show();
                        this->release();
                        return;
                    }

                    if (!thumbs.empty()) {
                        auto popup = ThumbnailSelectionPopup::create(thumbs, [this, levelID, pngData, username, onFinish](const std::string& replaceId) {
                            if (replaceId == "CANCEL") {
                                this->release();
                                return;
                            }
                            std::string mode = replaceId.empty() ? "add" : "replace";
                            Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                            ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, mode, replaceId, onFinish);
                        });
                        popup->show();
                    } else {
                        Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                        ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, onFinish);
                    }
                });
            } else {
                // Regular users: check if a thumbnail already exists to pick suggestion vs update
                log::info("[LevelInfoLayer] Regular user upload for level {}", levelID);
                
                ThumbnailAPI::get().checkExists(levelID, [this, levelID, pngData, username](bool exists) {
                    if (exists) {
                        // If a thumbnail exists -> send as an update
                        log::info("[LevelInfoLayer] Uploading as update for level {}", levelID);
                        Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadUpdate(levelID, pngData, username, [this](bool success, const std::string& msg) {
                            if (success) {
                                Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                Notification::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                            this->release();
                        });
                    } else {
                        // If it doesn't exist -> send as a suggestion
                        log::info("[LevelInfoLayer] Uploading as suggestion for level {}", levelID);
                        Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadSuggestion(levelID, pngData, username, [this](bool success, const std::string& msg) {
                            if (success) {
                                Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                            } else {
                                Notification::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
                            }
                            this->release();
                        });
                    }
                });
            }
        });
    }

    void onBack(CCObject* sender) {
        if (m_fields->m_fromVerificationQueue) {
            // Clear the flags
            Mod::get()->setSavedValue("open-from-verification-queue", false);
            Mod::get()->setSavedValue("verification-queue-levelid", -1);
            Mod::get()->setSavedValue("verification-queue-category", -1);
            
            // Signal MenuLayer to reopen the popup
            Mod::get()->setSavedValue("reopen-verification-queue", true);
            
            // Go back to MenuLayer
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, MenuLayer::scene(false)));
            return;
        }

        // Check if opened from LeaderboardsLayer
        if (m_fields->m_fromLeaderboards) {
            auto scene = LeaderboardsLayer::scene(m_fields->m_leaderboardState);
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
            return;
        }

        // Removed paimon-from-daily check as per user request to remove animation
        
        LevelInfoLayer::onBack(sender);
    }

    void setupGallery() {
        // Create arrows
        auto menu = CCMenu::create();
        menu->setID("gallery-menu");
        
        /*
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        m_fields->m_prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(PaimonLevelInfoLayer::onPrevBtn));
        m_fields->m_prevBtn->setPosition({-160, 0}); // Adjust position
        menu->addChild(m_fields->m_prevBtn);
        
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        nextSpr->setFlipX(true);
        m_fields->m_nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(PaimonLevelInfoLayer::onNextBtn));
        m_fields->m_nextBtn->setPosition({160, 0}); // Adjust position
        menu->addChild(m_fields->m_nextBtn);
        */

        // Rate button - Replaced with Stars Display
        /*
        auto rateSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        rateSpr->setScale(0.6f);
        m_fields->m_rateBtn = CCMenuItemSpriteExtra::create(rateSpr, this, menu_selector(PaimonLevelInfoLayer::onRateBtn));
        m_fields->m_rateBtn->setPosition({145, 90}); // Top right corner
        menu->addChild(m_fields->m_rateBtn);
        */
        
        // Single Rate Button (like in preview popup)
        /*
        auto starMenu = CCMenu::create();
        starMenu->setPosition({145, 90}); // Top right corner
        starMenu->setScale(0.6f);
        
        auto starSpr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        auto btn = CCMenuItemSpriteExtra::create(starSpr, this, menu_selector(PaimonLevelInfoLayer::onRateBtn));
        starMenu->addChild(btn);
        
        menu->addChild(starMenu);
        */
        
        if (m_fields->m_thumbnailButton) {
            menu->setPosition(m_fields->m_thumbnailButton->getPosition());
            this->addChild(menu, 100);
        }
    }
    
    void onRateBtn(CCObject* sender) {
        // Open RatePopup with pre-selected star? Or just open it.
        // The user might want to rate directly.
        // Let's open RatePopup for now as it handles the logic.
        if (m_fields->m_currentThumbnailIndex < 0 || m_fields->m_currentThumbnailIndex >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[m_fields->m_currentThumbnailIndex];
        RatePopup::create(m_level->m_levelID.value(), thumb.id)->show();
    }
    
    void updateGallery(float dt) {
        if (!m_fields->m_cycling || m_fields->m_thumbnails.size() <= 1) return;
        
        m_fields->m_cycleTimer += dt;
        if (m_fields->m_cycleTimer >= 3.0f) {
            m_fields->m_cycleTimer = 0.0f;
            m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % m_fields->m_thumbnails.size();
            this->loadThumbnail(m_fields->m_currentThumbnailIndex);
        }
    }
    
    void onPrevBtn(CCObject*) {
        m_fields->m_cycling = false; // Stop auto-cycling on interaction
        m_fields->m_currentThumbnailIndex--;
        if (m_fields->m_currentThumbnailIndex < 0) m_fields->m_currentThumbnailIndex = m_fields->m_thumbnails.size() - 1;
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void onNextBtn(CCObject*) {
        m_fields->m_cycling = false;
        m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % m_fields->m_thumbnails.size();
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void loadThumbnail(int index) {
        if (index < 0 || index >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[index];
        // Load from URL
        ThumbnailAPI::get().downloadFromUrl(thumb.url, [this, index](bool success, CCTexture2D* tex) {
            if (success && tex) {
                // Update thumbnail button sprite
                if (m_fields->m_thumbnailButton) {
                    auto spr = (CCSprite*)m_fields->m_thumbnailButton->getNormalImage();
                    if (spr) {
                        spr->setTexture(tex);
                        spr->setTextureRect({0, 0, tex->getContentSize().width, tex->getContentSize().height});
                    }
                }
                // Update background
                int32_t levelID = (index == 0 && m_level) ? m_level->m_levelID.value() : 0;
                this->applyThumbnailBackground(tex, levelID);
            }
        });
    }
};


