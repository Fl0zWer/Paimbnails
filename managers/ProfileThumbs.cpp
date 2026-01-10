#include "ProfileThumbs.hpp"
#include "ThumbnailAPI.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include <Geode/utils/file.hpp>
#include <filesystem>
#include <Geode/loader/Mod.hpp>
#include <fstream>
#include <algorithm>
#include <deque>

using namespace geode::prelude;
using namespace cocos2d;

// Shaders reutilizados del pipeline de blur utilizado en este mod (ver LevelCell.cpp)
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
    // Simple single-pass blur for real-time usage (Same as BannerConfigPopup)
    constexpr auto fragmentShaderFastBlur = R"(
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
                vec2 offset = vec2(x, y) * texOffset * 2.0; // Spread out
                color += texture2D(u_texture, v_texCoord + offset);
                total += 1.0;
            }
        }
        
        gl_FragColor = (color / total) * v_fragmentColor;
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

namespace {
    struct Header { int32_t w; int32_t h; int32_t fmt; };
}

ProfileThumbs& ProfileThumbs::get() {
    static ProfileThumbs inst; 
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        // Clear disk cache on startup
        auto dir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
        if (std::filesystem::exists(dir)) {
            try {
                std::filesystem::remove_all(dir);
                log::info("[ProfileThumbs] Cleared profile disk cache on startup");
            } catch (const std::exception& e) {
                log::error("[ProfileThumbs] Failed to clear profile disk cache: {}", e.what());
            }
        }
    }
    return inst;
}

std::string ProfileThumbs::makePath(int accountID) const {
    auto dir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
    (void)file::createDirectoryAll(dir);
    return (dir / fmt::format("{}.rgb", accountID)).string();
}

bool ProfileThumbs::saveRGB(int accountID, const uint8_t* rgb, int width, int height) {
    // Do not save to disk if we want cache to be session-only
    // But we MUST update the memory cache so the new image is used immediately.
    
    if (!rgb || width <= 0 || height <= 0) return false;

    // Convert RGB to RGBA
    std::vector<uint8_t> rgbaBuf(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        rgbaBuf[i * 4 + 0] = rgb[i * 3 + 0];
        rgbaBuf[i * 4 + 1] = rgb[i * 3 + 1];
        rgbaBuf[i * 4 + 2] = rgb[i * 3 + 2];
        rgbaBuf[i * 4 + 3] = 255;
    }
    
    auto* tex = new CCTexture2D();
    if (tex->initWithData(rgbaBuf.data(), kCCTexture2DPixelFormat_RGBA8888, width, height, { (float)width, (float)height })) {
        tex->autorelease();
        
        // Save to disk (safe because we clear on startup)
        auto path = makePath(accountID);
        std::ofstream out(path, std::ios::binary);
        if (out) {
            Header h{width, height, 24};
            out.write(reinterpret_cast<const char*>(&h), sizeof(h));
            out.write(reinterpret_cast<const char*>(rgb), width * height * 3);
            log::debug("[ProfileThumbs] Saved profile to disk for account {}", accountID);
        }

        // Update cache
        // Use default colors/width for now, or preserve existing config
        // We need to get existing config first to not overwrite it with defaults
        ccColor3B cA = {255,255,255};
        ccColor3B cB = {255,255,255};
        float wF = 0.6f;
        
        auto it = m_profileCache.find(accountID);
        if (it != m_profileCache.end()) {
            cA = it->second.colorA;
            cB = it->second.colorB;
            wF = it->second.widthFactor;
        }
        
        this->cacheProfile(accountID, tex, cA, cB, wF);
        log::info("[ProfileThumbs] Updated memory cache for account {}", accountID);
    } else {
        tex->release();
        return false;
    }
    
    return true; 
}

bool ProfileThumbs::has(int accountID) const {
    return std::filesystem::exists(makePath(accountID));
}

void ProfileThumbs::deleteProfile(int accountID) {
    clearCache(accountID);
    auto path = makePath(accountID);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        log::debug("[ProfileThumbs] Deleted profile thumbnail for account {}", accountID);
    }
}

CCTexture2D* ProfileThumbs::loadTexture(int accountID) {
    auto path = makePath(accountID);
    log::debug("[ProfileThumbs] Loading profile thumbnail for account {}: {}", accountID, path);
    
    if (!std::filesystem::exists(path)) {
        log::debug("[ProfileThumbs] Profile thumbnail not found for account {}", accountID);
        return nullptr;
    }
    
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log::error("[ProfileThumbs] Failed to open file for reading: {}", path);
        return nullptr;
    }
    
    Header h{}; 
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    
    if (h.fmt != 24 || h.w <= 0 || h.h <= 0) {
        log::error("[ProfileThumbs] Invalid header: fmt={}, w={}, h={}", h.fmt, h.w, h.h);
        return nullptr;
    }
    
    log::debug("[ProfileThumbs] Loading {}x{} profile thumbnail", h.w, h.h);
    
    std::vector<uint8_t> buf(h.w * h.h * 3);
    in.read(reinterpret_cast<char*>(buf.data()), buf.size());
    
    if (in.gcount() != static_cast<std::streamsize>(buf.size())) {
        log::error("[ProfileThumbs] File truncated: expected {} bytes, got {}", buf.size(), in.gcount());
        return nullptr;
    }

    // Convert RGB to RGBA for better compatibility (and to avoid potential driver/mod issues with RGB888)
    std::vector<uint8_t> rgbaBuf(h.w * h.h * 4);
    for (int i = 0; i < h.w * h.h; ++i) {
        rgbaBuf[i * 4 + 0] = buf[i * 3 + 0]; // R
        rgbaBuf[i * 4 + 1] = buf[i * 3 + 1]; // G
        rgbaBuf[i * 4 + 2] = buf[i * 3 + 2]; // B
        rgbaBuf[i * 4 + 3] = 255;            // A
    }
    
    auto* tex = new CCTexture2D();
    if (!tex->initWithData(rgbaBuf.data(), kCCTexture2DPixelFormat_RGBA8888, h.w, h.h, { (float)h.w, (float)h.h })) {
        log::error("[ProfileThumbs] Failed to create texture");
        tex->release();
        return nullptr;
    }
    tex->autorelease();
    
    log::info("[ProfileThumbs] Successfully loaded profile thumbnail for account {}", accountID);
    return tex;
}

bool ProfileThumbs::loadRGB(int accountID, std::vector<uint8_t>& out, int& w, int& h) {
    auto path = makePath(accountID);
    if (!std::filesystem::exists(path)) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    Header head{}; in.read(reinterpret_cast<char*>(&head), sizeof(head));
    if (head.fmt != 24 || head.w <= 0 || head.h <= 0) return false;
    out.resize(head.w * head.h * 3);
    in.read(reinterpret_cast<char*>(out.data()), out.size());
    w = head.w; h = head.h; return static_cast<bool>(in);
}

void ProfileThumbs::cacheProfile(int accountID, CCTexture2D* texture, 
                                 ccColor3B colorA, ccColor3B colorB, float widthFactor) {
    if (!texture) return;
    
    clearOldCache(); // Trim before inserting
    
    log::debug("[ProfileThumbs] Caching profile for account {} with colors RGB({},{},{}) -> RGB({},{},{}), width: {}", 
               accountID, colorA.r, colorA.g, colorA.b, colorB.r, colorB.g, colorB.b, widthFactor);
    
    // Preserve existing config if present
    ProfileConfig existingConfig;
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        existingConfig = it->second.config;
    }

    m_profileCache[accountID] = ProfileCacheEntry(texture, colorA, colorB, widthFactor);
    m_profileCache[accountID].config = existingConfig;
}

void ProfileThumbs::cacheProfileGIF(int accountID, const std::string& gifKey, 
                                    cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor) {
    clearOldCache();
    
    log::debug("[ProfileThumbs] Caching GIF profile for account {} with key {}", accountID, gifKey);
    
    AnimatedGIFSprite::pinGIF(gifKey);

    // Preserve existing config if present
    ProfileConfig existingConfig;
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        existingConfig = it->second.config;
    }

    // We don't have a texture easily available here unless we extract it from the GIF sprite.
    // But for now, we can store nullptr for texture, or try to get it if the sprite exists.
    // However, queueLoad expects a texture.
    // If we don't provide a texture, queueLoad might fail or return null.
    // But createProfileNode will handle the GIF key.
    
    m_profileCache[accountID] = ProfileCacheEntry(gifKey, colorA, colorB, widthFactor);
    m_profileCache[accountID].config = existingConfig;
}

void ProfileThumbs::cacheProfileConfig(int accountID, const ProfileConfig& config) {
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        it->second.config = config;
    } else {
        // Create entry without texture if it doesn't exist
        ProfileCacheEntry entry;
        entry.config = config;
        m_profileCache[accountID] = std::move(entry);
    }
}

ProfileConfig ProfileThumbs::getProfileConfig(int accountID) {
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        ProfileConfig config = it->second.config;
        // Inject GIF key if present in cache entry
        if (!it->second.gifKey.empty()) {
            config.gifKey = it->second.gifKey;
        }
        return config;
    }
    return ProfileConfig();
}

ProfileCacheEntry* ProfileThumbs::getCachedProfile(int accountID) {
    auto it = m_profileCache.find(accountID);
    if (it == m_profileCache.end()) {
        return nullptr;
    }
    
    // Check cache expiry
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.timestamp > CACHE_DURATION) {
        log::debug("[ProfileThumbs] Cache expired for account {}", accountID);
        m_profileCache.erase(it);
        return nullptr;
    }
    
    log::debug("[ProfileThumbs] Cache hit for account {}", accountID);
    return &it->second;
}

void ProfileThumbs::clearCache(int accountID) {
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        log::debug("[ProfileThumbs] Clearing cache for account {}", accountID);
        m_profileCache.erase(it);
    }
    removeFromNoProfileCache(accountID);
}

void ProfileThumbs::clearOldCache() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = m_profileCache.begin(); it != m_profileCache.end();) {
        if (now - it->second.timestamp > CACHE_DURATION) {
            log::debug("[ProfileThumbs] Removing expired cache for account {}", it->first);
            if (!it->second.gifKey.empty()) {
                AnimatedGIFSprite::unpinGIF(it->second.gifKey);
            }
            it = m_profileCache.erase(it);
        } else {
            ++it;
        }
    }
}

void ProfileThumbs::clearAllCache() {
    log::info("[ProfileThumbs] Clearing all profile cache ({} entries)", m_profileCache.size());
    for (const auto& [id, entry] : m_profileCache) {
        if (!entry.gifKey.empty()) {
            AnimatedGIFSprite::unpinGIF(entry.gifKey);
        }
    }
    m_profileCache.clear();
    m_noProfileCache.clear();
}

void ProfileThumbs::markNoProfile(int accountID) {
    m_noProfileCache.insert(accountID);
}

void ProfileThumbs::removeFromNoProfileCache(int accountID) {
    m_noProfileCache.erase(accountID);
}

bool ProfileThumbs::isNoProfile(int accountID) const {
    return m_noProfileCache.find(accountID) != m_noProfileCache.end();
}

void ProfileThumbs::clearNoProfileCache() {
    m_noProfileCache.clear();
}

CCNode* ProfileThumbs::createProfileNode(CCTexture2D* texture, const ProfileConfig& config, CCSize cs, bool onlyBackground) {
    // If we have a GIF key, try to create an AnimatedGIFSprite
    AnimatedGIFSprite* gifSprite = nullptr;
    if (!config.gifKey.empty()) {
        // Try to create from cache
        if (AnimatedGIFSprite::isCached(config.gifKey)) {
            gifSprite = AnimatedGIFSprite::createFromCache(config.gifKey);
        }
    }

    if (!texture && !gifSprite) return nullptr;

    // Create container
    auto container = CCNode::create();
    container->setContentSize(cs);

    // --- Background Logic ---
    CCNode* bg = nullptr;

    // Determine effective background type
    std::string bgType = config.backgroundType;
    
    // Force thumbnail mode if we have a texture/gif and:
    // 1. We are in banner mode (onlyBackground=true)
    // 2. OR config is default "gradient"
    if ((onlyBackground || bgType == "gradient") && (texture || !config.gifKey.empty())) {
        bgType = "thumbnail";
    }

    if (bgType == "thumbnail") {
        if (gifSprite) {
            // --- GIF Background Logic (Real-time Blur) ---
            auto bgSprite = AnimatedGIFSprite::createFromCache(config.gifKey);
            if (bgSprite) {
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);
                
                float scaleX = targetSize.width / gifSprite->getContentSize().width;
                float scaleY = targetSize.height / gifSprite->getContentSize().height;
                float scale = std::max(scaleX, scaleY);
                
                bgSprite->setScale(scale);
                bgSprite->setPosition(targetSize * 0.5f);
                
                // Apply blur shader
                auto shader = getOrCreateShaderCell("fast-blur", vertexShaderCell, fragmentShaderFastBlur);
                if (shader) {
                    bgSprite->setShaderProgram(shader);
                    // AnimatedGIFSprite::draw() handles uniforms
                }
                
                // Create clipper
                auto stencil = CCDrawNode::create();
                CCPoint rect[4];
                rect[0] = ccp(0, 0);
                rect[1] = ccp(cs.width, 0);
                rect[2] = ccp(cs.width, cs.height);
                rect[3] = ccp(0, cs.height);
                ccColor4F white = {1, 1, 1, 1};
                stencil->drawPolygon(rect, 4, white, 0, white);
                
                auto clipper = CCClippingNode::create(stencil);
                clipper->setAlphaThreshold(0.05f);
                clipper->setContentSize(cs);
                clipper->setPosition({0,0});
                clipper->setZOrder(-10);
                
                bgSprite->setPosition(cs / 2);
                clipper->addChild(bgSprite);
                bg = clipper;
                
                if (config.darkness > 0.0f) {
                    auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(config.darkness * 255)});
                    overlay->setContentSize(cs);
                    overlay->setPosition({0, 0});
                    overlay->setZOrder(-5); 
                    container->addChild(overlay);
                }
            }
        } else if (texture) {
            // --- Static Texture Background Logic (Old Logic) ---
            // Create blurred background
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
            
            CCSprite* bgSprite = nullptr;

            if (blurH && blurV && rtA && rtB) {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                texture->setTexParameters(&params);
                
                float normalizedIntensity = std::clamp((config.blurIntensity - 1.0f) / 9.0f, 0.0f, 1.0f);
                float radius = 0.02f + (normalizedIntensity * 0.25f);
                
                applyBlurPass(tempSprite, rtA, blurH, targetSize, radius);
                applyBlurPass(rtA->getSprite(), rtB, blurV, targetSize, radius);
                
                bgSprite = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
                bgSprite->setFlipY(true);
                bgSprite->getTexture()->setTexParameters(&params);
            } else {
                bgSprite = CCSprite::createWithTexture(texture);
            }

            if (bgSprite) {
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
                clipper->setAlphaThreshold(0.05f);
                clipper->setContentSize(cs);
                clipper->setPosition({0,0});
                clipper->setZOrder(-10); // Behind everything
                
                float targetW = cs.width;
                float targetH = cs.height;
                float finalScale = std::max(
                    targetW / bgSprite->getContentSize().width,
                    targetH / bgSprite->getContentSize().height
                );
                bgSprite->setScale(finalScale);
                bgSprite->setPosition(cs / 2);
                
                clipper->addChild(bgSprite);
                bg = clipper;

                // Add darkness overlay
                if (config.darkness > 0.0f) {
                    auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(config.darkness * 255)});
                    overlay->setContentSize(cs);
                    overlay->setPosition({0, 0});
                    overlay->setZOrder(-5); 
                    container->addChild(overlay);
                }
            }
        }
    } else if (bgType != "none") {
        // Gradient or Solid
        if (config.useGradient) {
            auto grad = CCLayerGradient::create(
                ccc4(config.colorA.r, config.colorA.g, config.colorA.b, 255),
                ccc4(config.colorB.r, config.colorB.g, config.colorB.b, 255)
            );
            grad->setContentSize(cs);
            grad->setAnchorPoint({0,0});
            grad->setPosition({0,0});
            grad->setVector({1.f, 0.f}); // horizontal
            grad->setZOrder(-10);
            bg = grad;
        } else {
            auto solid = CCLayerColor::create(ccc4(config.colorA.r, config.colorA.g, config.colorA.b, 255));
            solid->setContentSize(cs);
            solid->setAnchorPoint({0,0});
            solid->setPosition({0,0});
            solid->setZOrder(-10);
            bg = solid;
        }
    }

    if (bg) {
        container->addChild(bg);
    }

    if (onlyBackground) {
        return container;
    }

    // --- Profile Sprite ---
    CCNode* mainSprite = nullptr;
    float contentW = 0, contentH = 0;

    if (gifSprite) {
        mainSprite = gifSprite;
        contentW = gifSprite->getContentSize().width;
        contentH = gifSprite->getContentSize().height;
        // Ensure animation runs
        gifSprite->scheduleUpdate();
    } else if (texture) {
        auto s = CCSprite::createWithTexture(texture);
        mainSprite = s;
        contentW = s->getContentWidth();
        contentH = s->getContentHeight();
    }

    if (mainSprite && contentW > 0 && contentH > 0) {
        float factor = 0.60f;
        if (config.hasConfig) {
            factor = config.widthFactor;
        } else {
            try { factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f); } catch (...) {}
        }
        factor = std::max(0.30f, std::min(0.95f, factor));
        float desiredWidth = cs.width * factor;

        float scaleY = cs.height / contentH;
        float scaleX = desiredWidth / contentW;

        mainSprite->setScaleY(scaleY);
        mainSprite->setScaleX(scaleX);
        
        // Skewed clipping
        constexpr float angle = 18.f;
        CCSize scaledSize{ desiredWidth, contentH * scaleY };
        auto mask = CCLayerColor::create({255,255,255});
        mask->setContentSize(scaledSize);
        mask->setAnchorPoint({1,0});
        mask->setSkewX(angle);

        auto clip = CCClippingNode::create();
        clip->setStencil(mask);
        clip->setAlphaThreshold(0.5f);
        clip->setContentSize(scaledSize);
        clip->setAnchorPoint({1,0});
        
        // Place the clip on the right
        clip->setPosition({cs.width, 0});
        clip->setZOrder(10); // Ensure on top
        
        // Adjust the sprite position inside the clip
        mainSprite->setAnchorPoint({1,0});
        mainSprite->setPosition({scaledSize.width, 0});
        
        clip->addChild(mainSprite);
        container->addChild(clip);
        
        // Separator line
        auto separator = CCLayerColor::create({
            config.separatorColor.r, 
            config.separatorColor.g, 
            config.separatorColor.b, 
            (GLubyte)std::clamp(config.separatorOpacity, 0, 255)
        });
        separator->setContentSize({2.0f, cs.height * 1.2f}); // Taller to cover skew
        separator->setAnchorPoint({0.5f, 0});
        separator->setSkewX(angle);
        separator->setPosition({cs.width - desiredWidth, 0});
        separator->setZOrder(15); // Above clip
        container->addChild(separator);
    }

    return container;
}

void ProfileThumbs::queueLoad(int accountID, const std::string& username, std::function<void(bool, cocos2d::CCTexture2D*)> callback) {
    // 0. Check negative cache (if it failed before, don't try again this session)
    if (isNoProfile(accountID)) {
        if (callback) callback(false, nullptr);
        return;
    }

    // 1. Check cache first
    auto cached = getCachedProfile(accountID);
    if (cached && cached->texture) {
        if (callback) callback(true, cached->texture);
        return;
    }

    // 2. Check if already pending
    if (m_pendingCallbacks.find(accountID) != m_pendingCallbacks.end()) {
        m_pendingCallbacks[accountID].push_back(callback);
        return;
    }

    // 3. Add to queue (FIFO - Push Back)
    // We use FIFO by default so initial list loads top-to-bottom.
    // Visibility priority will handle scrolling optimization.
    m_downloadQueue.push_back(accountID);
    m_pendingCallbacks[accountID].push_back(callback);
    
    // Store username for this request
    m_usernameMap[accountID] = username;

    // 4. Trigger processing
    processQueue();
}

void ProfileThumbs::notifyVisible(int accountID) {
    m_visibilityMap[accountID] = std::chrono::steady_clock::now();
}

void ProfileThumbs::processQueue() {
    while (m_activeDownloads < MAX_CONCURRENT_DOWNLOADS && !m_downloadQueue.empty()) {
        m_activeDownloads++;

        // Find best candidate
        // Strategy: Find the *first* (oldest request) item that is currently visible.
        // This ensures FIFO order for the visible items (top to bottom usually).
        auto now = std::chrono::steady_clock::now();
        auto bestIt = m_downloadQueue.end();
        
        for (auto it = m_downloadQueue.begin(); it != m_downloadQueue.end(); ++it) {
            int id = *it;
            if (m_visibilityMap.count(id)) {
                auto lastSeen = m_visibilityMap[id];
                if (now - lastSeen < std::chrono::milliseconds(200)) { // Visible in last 200ms
                    bestIt = it;
                    break; // Found the first visible item, stop searching
                }
            }
        }

        // If no visible item found, fallback to LIFO (newest request)
        // This helps when the user scrolls to a new area and draw hasn't been called yet,
        // or for just clearing the queue.
        if (bestIt == m_downloadQueue.end()) {
            // Use the last element (newest)
            bestIt = m_downloadQueue.end() - 1;
        }

        int accountID = *bestIt;
        m_downloadQueue.erase(bestIt);

        log::debug("[ProfileThumbs] Processing queue: AccountID {}", accountID);
        
        std::string username = "";
        if (m_usernameMap.find(accountID) != m_usernameMap.end()) {
            username = m_usernameMap[accountID];
            m_usernameMap.erase(accountID);
        }

        ThumbnailAPI::get().downloadProfile(accountID, username, [this, accountID](bool success, CCTexture2D* texture) {
            
            // Retain texture to keep it alive during the next async call
            if (texture) texture->retain();

            // Chain config download to ensure we have both image and settings
            ThumbnailAPI::get().downloadProfileConfig(accountID, [this, accountID, success, texture](bool configSuccess, const ProfileConfig& config) {
                
                if (success && texture) {
                    // Cache the profile with config (or defaults if config failed)
                    // If configSuccess is false, config will contain defaults
                    this->cacheProfile(accountID, texture, config.colorA, config.colorB, config.widthFactor);
                }

                if (configSuccess) {
                    this->cacheProfileConfig(accountID, config);
                }

                if (!success && !configSuccess) {
                    markNoProfile(accountID);
                }

                // Handle callbacks
                auto it = m_pendingCallbacks.find(accountID);
                if (it != m_pendingCallbacks.end()) {
                    for (const auto& cb : it->second) {
                        if (cb) cb(success, texture);
                    }
                    m_pendingCallbacks.erase(it);
                }
                
                // Release texture now that we are done
                if (texture) texture->release();

                // Continue queue
                m_activeDownloads--;
                
                // Schedule next process on next frame to avoid stack overflow recursion
                Loader::get()->queueInMainThread([this]() {
                    processQueue();
                });
            });
        });
    }
}
