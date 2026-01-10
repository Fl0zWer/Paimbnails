#include <Geode/modify/LevelCell.hpp>
#include <Geode/binding/DailyLevelNode.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <cmath>
#include <unordered_set>
#include "../managers/LocalThumbs.hpp"
#include "../managers/LevelColors.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/Constants.hpp"
#include "../utils/SafeGuard.hpp"
#include "../utils/AnimatedGIFSprite.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// Shaders for LevelCell effects
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

constexpr auto fragmentShaderGrayscaleCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 result = mix(color.rgb, vec3(gray), u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderInvertCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec3 inverted = vec3(1.0) - color.rgb;
    vec3 result = mix(color.rgb, inverted, u_intensity);
    gl_FragColor = vec4(result, color.a);
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
    float blurSize = u_intensity * 2.0; 
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    
    float totalWeight = 0.0;
    
    for (float x = -2.0; x <= 2.0; x+=1.0) {
        for (float y = -2.0; y <= 2.0; y+=1.0) {
            float weight = 1.0 / (1.0 + x*x + y*y);
            vec4 sample = texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel * blurSize);
            color += sample * weight;
            totalWeight += weight;
        }
    }
    vec4 finalColor = (color / totalWeight) * v_fragmentColor;
    gl_FragColor = finalColor;
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

constexpr auto fragmentShaderGlitchCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texCoord;
    float glitchStrength = u_intensity * 0.1;
    
    float lineNoise = rand(vec2(floor(uv.y * 100.0), u_time));
    if (lineNoise > 0.95 - u_intensity * 0.05) {
        uv.x += (rand(vec2(uv.y, u_time)) - 0.5) * glitchStrength;
    }
    
    vec4 color = texture2D(u_texture, uv);
    
    if (rand(vec2(uv.y, u_time + 1.0)) > 0.98 - u_intensity * 0.02) {
        color.r = texture2D(u_texture, uv + vec2(0.01 * u_intensity, 0.0)).r;
        color.b = texture2D(u_texture, uv - vec2(0.01 * u_intensity, 0.0)).b;
    }
    
    gl_FragColor = color * v_fragmentColor;
})";

constexpr auto fragmentShaderSepiaCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 sepia = vec3(gray * 1.2, gray * 1.0, gray * 0.8);
    vec3 result = mix(color.rgb, sepia, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

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

constexpr auto fragmentShaderSharpenCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    vec4 color = texture2D(u_texture, v_texCoord);
    
    vec4 sum = vec4(0.0);
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, -1.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(-1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 0.0) * onePixel) * 5.0;
    sum += texture2D(u_texture, v_texCoord + vec2(1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 1.0) * onePixel) * -1.0;
    
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

constexpr auto fragmentShaderEdgeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    
    float kernel[9];
    kernel[0] = -1.0; kernel[1] = -1.0; kernel[2] = -1.0;
    kernel[3] = -1.0; kernel[4] = 8.0; kernel[5] = -1.0;
    kernel[6] = -1.0; kernel[7] = -1.0; kernel[8] = -1.0;
    
    vec4 sum = vec4(0.0);
    int index = 0;
    for (float y = -1.0; y <= 1.0; y++) {
        for (float x = -1.0; x <= 1.0; x++) {
            sum += texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel) * kernel[index];
            index++;
        }
    }
    
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

constexpr auto fragmentShaderVignetteCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec2 uv = v_texCoord - 0.5;
    float dist = length(uv);
    float vignette = smoothstep(0.8, 0.25 * (1.0 - u_intensity * 0.5), dist);
    gl_FragColor = vec4(color.rgb * vignette, color.a);
})";

constexpr auto fragmentShaderPixelateCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    float pixelSize = 2.0 + u_intensity * 15.0;
    vec2 coord = floor(v_texCoord * u_texSize / pixelSize) * pixelSize / u_texSize;
    gl_FragColor = texture2D(u_texture, coord) * v_fragmentColor;
})";

constexpr auto fragmentShaderPosterizeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float levels = 10.0 - (u_intensity * 8.0);
    levels = max(2.0, levels);
    vec3 result = floor(color.rgb * levels) / levels;
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderChromaticCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float amount = u_intensity * 0.02;
    float r = texture2D(u_texture, uv + vec2(amount, 0.0)).r;
    float g = texture2D(u_texture, uv).g;
    float b = texture2D(u_texture, uv - vec2(amount, 0.0)).b;
    gl_FragColor = vec4(r, g, b, texture2D(u_texture, uv).a) * v_fragmentColor;
})";

constexpr auto fragmentShaderScanlinesCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float scanline = sin(v_texCoord.y * u_texSize.y * 0.5) * 0.1 * u_intensity;
    color.rgb -= scanline;
    gl_FragColor = color;
})";

constexpr auto fragmentShaderSolarizeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float threshold = 0.5;
    vec3 solarized = abs(color.rgb - vec3(threshold)) * 2.0;
    vec3 result = mix(color.rgb, solarized, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

// Simple Hue Shift for Rainbow
constexpr auto fragmentShaderRainbowCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec3 hsv = rgb2hsv(color.rgb);
    // Shift hue based on time and intensity
    hsv.x = fract(hsv.x + u_time * 0.5 * u_intensity); 
    vec3 rgb = hsv2rgb(hsv);
    // Mix with original based on intensity (so it fades in)
    vec3 result = mix(color.rgb, rgb, u_intensity);
    gl_FragColor = vec4(result, color.a);
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
        program->release();
        return nullptr;
    }

    program->updateUniforms();
    shaderCache->addProgram(program, key);
    program->release();
    return program;
}

class PaimonShaderSprite : public CCSprite {
public:
    float m_intensity = 0.0f;
    float m_time = 0.0f;
    float m_brightness = 1.0f;
    CCSize m_texSize = {0, 0};
    
    static PaimonShaderSprite* createWithTexture(CCTexture2D* texture) {
        auto sprite = new PaimonShaderSprite();
        if (sprite && sprite->initWithTexture(texture)) {
            sprite->autorelease();
            sprite->setID("paimon-shader-sprite");
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    void draw() override {
        // Manual draw implementation to bypass potential hooks (e.g., Happy Textures) 
        // that might crash on custom shader sprites or generated textures.
        
        CC_NODE_DRAW_SETUP();

        GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
        if (intensityLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
        }
        
        GLint timeLoc = getShaderProgram()->getUniformLocationForName("u_time");
        if (timeLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(timeLoc, m_time);
        }

        GLint brightLoc = getShaderProgram()->getUniformLocationForName("u_brightness");
        if (brightLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(brightLoc, m_brightness);
        }
        
        GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
        if (sizeLoc != -1) {
            if (m_texSize.width == 0) {
                    m_texSize = getTexture()->getContentSizeInPixels();
            }
            getShaderProgram()->setUniformLocationWith2f(sizeLoc, m_texSize.width, m_texSize.height);
        }
        
        ccGLBlendFunc( m_sBlendFunc.src, m_sBlendFunc.dst );

        if (getTexture()) {
            ccGLBindTexture2D( getTexture()->getName() );
        } else {
            ccGLBindTexture2D(0);
        }
        
        // Fix: Explicitly unbind any active VBO to prevent crashes on drivers (e.g. atio6axx.dll)
        // when passing client-side pointers to glVertexAttribPointer.
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        ccGLEnableVertexAttribs( kCCVertexAttribFlag_PosColorTex );

        #define kQuadSize sizeof(m_sQuad.bl)
        uintptr_t offset = (uintptr_t)&m_sQuad;

        // vertex
        int diff = offsetof( ccV3F_C4B_T2F, vertices);
        glVertexAttribPointer(kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, kQuadSize, (void*) (offset + diff));

        // texCoods
        diff = offsetof( ccV3F_C4B_T2F, texCoords);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, kQuadSize, (void*)(offset + diff));

        // color
        diff = offsetof( ccV3F_C4B_T2F, colors);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, kQuadSize, (void*)(offset + diff));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        
        CHECK_GL_ERROR_DEBUG();
        
        CC_INCREMENT_GL_DRAWS(1);
    }
};

class PaimonShaderGradient : public CCSprite {
public:
    float m_intensity = 0.0f;
    float m_time = 0.0f;
    CCSize m_texSize = {0, 0};
    ccColor3B m_startColor = {255, 255, 255};
    ccColor3B m_endColor = {255, 255, 255};

    static PaimonShaderGradient* create(const ccColor4B& start, const ccColor4B& end) {
        auto sprite = new PaimonShaderGradient();
        
        // Create a 2x2 white texture manually to ensure it exists
        unsigned char data[] = {
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255
        };
        
        auto texture = new CCTexture2D();
        if (texture && texture->initWithData(data, kCCTexture2DPixelFormat_RGBA8888, 2, 2, {2.0f, 2.0f})) {
            if (sprite && sprite->initWithTexture(texture)) {
                texture->release(); // sprite retains it
                sprite->autorelease();
                sprite->setTextureRect({0, 0, 2, 2});
                sprite->setStartColor({start.r, start.g, start.b});
                sprite->setEndColor({end.r, end.g, end.b});
                sprite->setOpacity(start.a);
                return sprite;
            }
        }
        
        CC_SAFE_DELETE(texture);
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    void setStartColor(const ccColor3B& color) {
        m_startColor = color;
        updateGradient();
    }

    void setEndColor(const ccColor3B& color) {
        m_endColor = color;
        updateGradient();
    }
    
    void updateGradient() {
        GLubyte opacity = getOpacity();
        ccColor4B start4 = {m_startColor.r, m_startColor.g, m_startColor.b, opacity};
        ccColor4B end4 = {m_endColor.r, m_endColor.g, m_endColor.b, opacity};
        
        // Horizontal: Left=Start, Right=End
        // CCSprite Quad: bl, br, tl, tr
        m_sQuad.bl.colors = start4;
        m_sQuad.tl.colors = start4;
        m_sQuad.br.colors = end4;
        m_sQuad.tr.colors = end4;
    }
    
    void setOpacity(GLubyte opacity) override {
        CCSprite::setOpacity(opacity);
        updateGradient();
    }
    
    void setContentSize(const CCSize& size) override {
        CCSprite::setContentSize(size);
        
        // Fix: Manually update quad vertices to match the content size
        // This ensures the sprite is drawn at the correct size, stretching the 2x2 texture
        m_sQuad.bl.vertices = {0.0f, 0.0f, 0.0f};
        m_sQuad.br.vertices = {size.width, 0.0f, 0.0f};
        m_sQuad.tl.vertices = {0.0f, size.height, 0.0f};
        m_sQuad.tr.vertices = {size.width, size.height, 0.0f};
        
        updateGradient();
    }
    
    // Dummy method to match CCLayerGradient interface
    void setVector(const CCPoint& vec) {}

    void draw() override {
        // Manual draw implementation to bypass potential hooks (e.g., Happy Textures)
        // that might crash on the gradient's custom texture setup.
        
        CC_NODE_DRAW_SETUP();
        
        GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
        if (intensityLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
        }
        
        GLint timeLoc = getShaderProgram()->getUniformLocationForName("u_time");
        if (timeLoc != -1) {
            getShaderProgram()->setUniformLocationWith1f(timeLoc, m_time);
        }
        
        GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
        if (sizeLoc != -1) {
            // Always use current content size for shader effects on gradient
            CCSize size = getContentSize();
            getShaderProgram()->setUniformLocationWith2f(sizeLoc, size.width, size.height);
        }

        ccGLBlendFunc( m_sBlendFunc.src, m_sBlendFunc.dst );

        if (getTexture()) {
            ccGLBindTexture2D( getTexture()->getName() );
        } else {
            ccGLBindTexture2D(0);
        }
        
        // Fix: Explicitly unbind any active VBO to prevent crashes on drivers (e.g. atio6axx.dll)
        // when passing client-side pointers to glVertexAttribPointer.
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        ccGLEnableVertexAttribs( kCCVertexAttribFlag_PosColorTex );

        #define kQuadSize sizeof(m_sQuad.bl)
        uintptr_t offset = (uintptr_t)&m_sQuad;

        // vertex
        int diff = offsetof( ccV3F_C4B_T2F, vertices);
        glVertexAttribPointer(kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, kQuadSize, (void*) (offset + diff));

        // texCoods
        diff = offsetof( ccV3F_C4B_T2F, texCoords);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, kQuadSize, (void*)(offset + diff));

        // color
        diff = offsetof( ccV3F_C4B_T2F, colors);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, kQuadSize, (void*)(offset + diff));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        CHECK_GL_ERROR_DEBUG();
        CC_INCREMENT_GL_DRAWS(1);
    }
};

class $modify(PaimonLevelCell, LevelCell) {
    struct Fields {
        CCClippingNode* m_clippingNode = nullptr;
        CCLayerColor* m_separator = nullptr;
        CCNode* m_gradient = nullptr;
        CCParticleSystemQuad* m_mythicParticles = nullptr;
        CCLayerColor* m_darkOverlay = nullptr;
        float m_gradientTime = 0.0f;
        ccColor3B m_gradientColorA;
        ccColor3B m_gradientColorB;
        CCSprite* m_gradientLayer = nullptr;
        bool m_loaderConfigured = false;
        CCSprite* m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false;
        CCSprite* m_thumbSprite = nullptr;
        CCPoint m_thumbBasePos = {0.f, 0.f};
        CCPoint m_clipBasePos = {0.f, 0.f}; // Store clipping node base position
        CCPoint m_separatorBasePos = {0.f, 0.f}; // Store separator base position
        float m_thumbBaseScaleX = 1.0f;
        float m_thumbBaseScaleY = 1.0f;
        bool m_thumbnailRequested = false; // Flag to avoid duplicate loads
        int m_requestId = 0; // Unique request ID to invalidate late callbacks
        int m_lastRequestedLevelID = 0; // Last requested levelID to detect changes
        bool m_thumbnailApplied = false; // Flag to avoid applying the thumbnail multiple times
        bool m_wasInCenter = false; // Flag to detect state changes
        float m_centerLerp = 0.0f; // Smooth interpolation 0-1
        Ref<CCMenuItemSpriteExtra> m_viewOverlay = nullptr; // Invisible overlay for the button
        
        float m_animTime = 0.0f;
        bool m_hasGif = false;
        Ref<CCTexture2D> m_gifTexture = nullptr;
        Ref<CCTexture2D> m_staticTexture = nullptr;
        bool m_isHovering = false;
    };
    
    // Destructor to mark the cell as being destroyed
    ~PaimonLevelCell() {
        auto fields = m_fields.self();
        if (fields) {
            fields->m_isBeingDestroyed = true;
        }
        
        // Stop animations
        this->unschedule(schedule_selector(PaimonLevelCell::updateGradientAnim));
        this->unschedule(schedule_selector(PaimonLevelCell::checkCenterPosition));
        this->unschedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));
    }
    
    // Computes the thumbnail scale in a LevelCell (respects width factor, covers height)
    static void calculateLevelCellThumbScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float& outScaleX, float& outScaleY) {
        if (!sprite) return;
        
        const float contentWidth = sprite->getContentSize().width;
        const float contentHeight = sprite->getContentSize().height;
        const float desiredWidth = bgWidth * widthFactor;
        
        // Scale to cover the exact height
        outScaleY = bgHeight / contentHeight;
        
        // For width: use the desired width but never go below what's needed for aspect ratio
        float minScaleX = outScaleY; // Keep aspect ratio
        float desiredScaleX = desiredWidth / contentWidth;
        outScaleX = std::max(minScaleX, desiredScaleX);
    }
    
    // Computes the thumbnail scale for popups (full coverage with margin)
    static void calculateFullCoverageThumbScale(CCSprite* sprite, float targetWidth, float targetHeight, float& outScale) {
        if (!sprite) return;
        
        const float contentWidth = sprite->getContentSize().width;
        const float contentHeight = sprite->getContentSize().height;
        
        // Use the larger scale to fully cover, with a safety margin
        outScale = std::max(
            targetWidth / contentWidth,
            targetHeight / contentHeight
        ) * 1.15f;
    }
    
    void showLoadingSpinner() {
        auto fields = m_fields.self();
        
        // Remove existing spinner if any
        if (fields->m_loadingSpinner) {
            fields->m_loadingSpinner->removeFromParent();
            fields->m_loadingSpinner = nullptr;
        }
        
        // Create a loading spinner with a pulsing effect
        auto spinner = CCSprite::create("loadingCircle.png");
        if (!spinner) {
            // Fallback to sprite frame if file not found
            spinner = CCSprite::createWithSpriteFrameName("loadingCircle.png");
        }
        if (!spinner) {
            // Fallback: create a simple circle
            spinner = CCSprite::create();
            auto circle = CCLayerColor::create({100, 100, 100, 200});
            circle->setContentSize({40, 40});
            spinner->addChild(circle);
        }
        
        spinner->setScale(0.25f);
        spinner->setOpacity(180);
        
        // Position at the center of thumbnail area
        auto bg = m_backgroundLayer;
        if (bg) {
            auto cs = bg->getContentSize();
            spinner->setPosition({cs.width - 75.f, cs.height / 2.f});
        } else {
            spinner->setPosition({280.f, 30.f});
        }
        
        spinner->setZOrder(999);
        
        try {
            spinner->setID("paimon-loading-spinner"_spr);
        } catch (...) {}
        
        this->addChild(spinner);
        fields->m_loadingSpinner = spinner;
        
        // Animation: rotation only
        auto rotateAction = CCRepeatForever::create(
            CCRotateBy::create(0.8f, 360.0f)
        );
        
        spinner->runAction(rotateAction);
        
        // Smooth fade-in
        spinner->setOpacity(0);
        spinner->runAction(CCFadeTo::create(0.3f, 180));
    }
    
    void hideLoadingSpinner() {
        auto fields = m_fields.self();
        if (fields->m_loadingSpinner) {
            fields->m_loadingSpinner->stopAllActions();
            
            // Fade out animation
            auto fadeOut = CCFadeOut::create(0.2f);
            auto remove = CCCallFunc::create(fields->m_loadingSpinner, callfunc_selector(CCNode::removeFromParent));
            auto sequence = CCSequence::create(fadeOut, remove, nullptr);
            fields->m_loadingSpinner->runAction(sequence);
            
            fields->m_loadingSpinner = nullptr;
        }
    }

    void configureThumbnailLoader() {
        auto fields = m_fields.self();
        if (!fields->m_loaderConfigured) {
            try {
                int maxDownloads = Mod::get()->getSettingValue<int64_t>("thumbnail-concurrent-downloads");
                ThumbnailLoader::get().setMaxConcurrentTasks(maxDownloads);
                fields->m_loaderConfigured = true;
                log::debug("[LevelCell] ThumbnailLoader configured with {} max downloads", maxDownloads);
            } catch (...) {
                ThumbnailLoader::get().setMaxConcurrentTasks(3);
                fields->m_loaderConfigured = true;
            }
        }
    }

    void cleanPaimonNodes(CCNode* bg) {
        auto fields = m_fields.self();
        
        // Safe removal of tracked nodes
        auto removeNodeSafe = [](auto& node) {
            if (node) {
                if (node->getParent()) node->removeFromParent();
                node = nullptr; 
            }
        };

        removeNodeSafe(fields->m_clippingNode);
        removeNodeSafe(fields->m_separator);
        removeNodeSafe(fields->m_gradient);
        
        // m_mythicParticles is a CCParticleSystemQuad*, manual handling or cast
        if (fields->m_mythicParticles) {
            if (fields->m_mythicParticles->getParent()) fields->m_mythicParticles->removeFromParent();
            fields->m_mythicParticles = nullptr;
        }

        removeNodeSafe(fields->m_darkOverlay);
        
        // Nullify other references
        fields->m_gradientLayer = nullptr;
        fields->m_thumbSprite = nullptr;
        fields->m_loadingSpinner = nullptr; // Spinner is usually managed by show/hide, but clear here to be safe

        // Clean any stragglers in bg (by ID) which might be left over from previous versions or other calls
        if (bg) {
            if (auto children = bg->getChildren()) {
                std::vector<CCNode*> toRemove;
                for (unsigned int i = 0; i < children->count(); i++) {
                    if (auto child = typeinfo_cast<CCNode*>(children->objectAtIndex(i))) {
                        std::string childID = child->getID();
                        if (childID.find("paimon") != std::string::npos) {
                            toRemove.push_back(child);
                        }
                    }
                }
                for (auto node : toRemove) node->removeFromParent();
            }
        }

        // Clean any stragglers in this (LevelCell)
        if (auto children = this->getChildren()) {
            std::vector<CCNode*> toRemove;
            for (unsigned int i = 0; i < children->count(); i++) {
                if (auto child = typeinfo_cast<CCNode*>(children->objectAtIndex(i))) {
                    std::string childID = child->getID();
                    if (childID.find("paimon") != std::string::npos) {
                        toRemove.push_back(child);
                    }
                }
            }
            for (auto node : toRemove) node->removeFromParent();
        }
    }

    void setupDarkMode(CCNode* bg) {
        auto fields = m_fields.self();
        
        std::string bgType = "gradient";
        try { bgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type"); } catch (...) {}
        if (bgType == "thumbnail") return;

        bool darkMode = false;
        float darkIntensity = PaimonConstants::DEFAULT_DARK_INTENSITY;
        try { 
            darkMode = Mod::get()->getSettingValue<bool>("dark-mode"); 
            darkIntensity = Mod::get()->getSettingValue<float>("dark-mode-intensity");
        } catch (...) {}
        
        if (darkMode && darkIntensity > PaimonConstants::MIN_BLUR_STRENGTH) {
            auto darkOverlay = CCLayerColor::create(ccc4(0, 0, 0, static_cast<GLubyte>(darkIntensity * PaimonConstants::DARK_INTENSITY_MULTIPLIER)));
            darkOverlay->setContentSize(bg->getContentSize());
            darkOverlay->setAnchorPoint({0, 0});
            darkOverlay->setPosition({0, 0});
            darkOverlay->setZOrder(100);
            darkOverlay->setID("paimon-dark-overlay"_spr);
            fields->m_darkOverlay = darkOverlay;
            this->addChild(darkOverlay);
        }
    }

    CCSprite* createThumbnailSprite(CCTexture2D* texture) {
        CCSprite* sprite = PaimonShaderSprite::createWithTexture(texture);
        if (!sprite) return nullptr;

        int32_t levelIDForGIF = m_level ? m_level->m_levelID.value() : 0;
        
        if (levelIDForGIF > 0 && ThumbnailLoader::get().hasGIFData(levelIDForGIF)) {
            auto path = ThumbnailLoader::get().getCachePath(levelIDForGIF);
            
            this->retain();
            AnimatedGIFSprite::createAsync(path.string(), [this, levelIDForGIF](AnimatedGIFSprite* anim) {
                if (this->m_level && this->m_level->m_levelID == levelIDForGIF) {
                    if (anim && m_fields->m_thumbSprite) {
                        auto old = m_fields->m_thumbSprite;
                        auto parent = old->getParent();
                        if (parent) {
                            anim->setScaleX(old->getScaleX());
                            anim->setScaleY(old->getScaleY());
                            anim->setPosition(old->getPosition());
                            anim->setAnchorPoint(old->getAnchorPoint());
                            anim->setSkewX(old->getSkewX());
                            anim->setSkewY(old->getSkewY());
                            anim->setZOrder(old->getZOrder());
                            anim->setColor(old->getColor());
                            anim->setOpacity(old->getOpacity());
                            anim->setID("paimon-thumbnail"_spr);
                            
                            old->removeFromParent();
                            parent->addChild(anim);
                            m_fields->m_thumbSprite = anim;
                        }
                    }
                }
                this->release();
            });
        }
        
        if (sprite) {
            sprite->setID("paimon-thumbnail"_spr);
            // Ensure ID is set for shader detection if it's our custom sprite
            if (auto pss = dynamic_cast<PaimonShaderSprite*>(sprite)) {
                 pss->setID("paimon-shader-sprite");
            }
            
            std::string bgType = "gradient";
            try { bgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type"); } catch (...) {}
            
            if (bgType == "thumbnail") {
                auto shader = getOrCreateShaderCell("paimon_cell_saturation", vertexShaderCell, fragmentShaderSaturationCell);
                if (shader) {
                    // Only apply saturation/brightness boost to static images (PaimonShaderSprite)
                    if (auto pss = dynamic_cast<PaimonShaderSprite*>(sprite)) {
                        sprite->setShaderProgram(shader);
                        float saturation = 2.5f;
                        float brightness = 3.0f;
                        pss->m_intensity = saturation;
                        pss->m_brightness = brightness;
                    }
                    // AnimatedGIFSprite (GIFs) will render with default shader (no effect)
                }
            }
        }
        return sprite;
    }

    void setupClippingAndSeparator(CCNode* bg, CCSprite* sprite, bool androidSafe) {
        auto fields = m_fields.self();
        
        float kThumbWidthFactor = PaimonConstants::DEFAULT_THUMB_WIDTH_FACTOR;
        try { kThumbWidthFactor = Mod::get()->getSettingValue<float>("level-thumb-width"); } catch (...) {}
        kThumbWidthFactor = std::max(PaimonConstants::MIN_THUMB_WIDTH_FACTOR, std::min(PaimonConstants::MAX_THUMB_WIDTH_FACTOR, kThumbWidthFactor));
        
        // Force full width for Daily cells
        bool isDaily = false;
        if (m_level && m_level->m_dailyID > 0) isDaily = true;
        // if (isDaily) kThumbWidthFactor = 1.0f; // Reverted: Daily uses normal width

        const float bgWidth = bg->getContentWidth();
        const float bgHeight = bg->getContentHeight();

        float scaleX, scaleY;
        // Reverted: Always use standard calculation
        calculateLevelCellThumbScale(sprite, bgWidth, bgHeight, kThumbWidthFactor, scaleX, scaleY);

        sprite->setScaleY(scaleY);
        sprite->setScaleX(scaleX);

        float desiredWidth = bgWidth * kThumbWidthFactor;
        float angle = 18.f;
        // if (isDaily) angle = 0.f; // Reverted: Daily uses skew

        CCSize scaledSize{ desiredWidth, sprite->getContentHeight() * scaleY };
        
        CCNode* mask = nullptr;
        // Reverted: Always use slanted layer mask
        auto layer = CCLayerColor::create({255,255,255});
        layer->setContentSize(scaledSize);
        layer->setAnchorPoint({1,0});
        layer->setSkewX(angle);
        mask = layer;

        auto clippingNode = CCClippingNode::create();
        if (!clippingNode) return;
        
        // FIX: Disable touch for clipping node to prevent blocking buttons
        // CCClippingNode doesn't consume touches by default, but let's be sure
        // Actually, we can't easily disable touch on a CCNode unless we unregister it, 
        // but it shouldn't be registered.
        
        clippingNode->setStencil(mask);
        // Reverted: No alpha threshold needed for layer mask
        
        clippingNode->setContentSize(scaledSize);
        clippingNode->setAnchorPoint({1,0});
        
        // Reverted: Standard position
        clippingNode->setPosition({ bgWidth, 0.3f });
        
        clippingNode->setID("paimon-clipping-node"_spr);
        clippingNode->setZOrder(-1);

        sprite->setPosition(clippingNode->getContentSize() * 0.5f);
        clippingNode->addChild(sprite);
        
        // Revert: Add to 'this' (LevelCell) instead of 'bg' to ensure visibility
        // Adding to 'bg' caused the thumbnail to be hidden or clipped incorrectly
        this->addChild(clippingNode);

        fields->m_thumbSprite = sprite;
        fields->m_thumbBasePos = sprite->getPosition();
        fields->m_clipBasePos = clippingNode->getPosition();
        fields->m_thumbBaseScaleX = scaleX;
        fields->m_thumbBaseScaleY = scaleY;
        
        bool hoverEnabled = true;
        try { hoverEnabled = Mod::get()->getSettingValue<bool>("levelcell-hover-effects"); } catch (...) {}
        if (androidSafe) hoverEnabled = false;
        
        if (hoverEnabled) {
            this->schedule(schedule_selector(PaimonLevelCell::checkCenterPosition), 0.05f);
            this->schedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));
        }

        fields->m_clippingNode = clippingNode;
        
        // NOTE: clippingNode is added to 'this' above.
        
        bool showSeparator = true;
        try { showSeparator = Mod::get()->getSettingValue<bool>("levelcell-show-separator"); } catch (...) {}
        if (androidSafe) showSeparator = false;
        
        if (showSeparator && !isDaily) { // No separator for Daily
            float separatorXMul = m_compactView ? 0.75f : 1.0f;
            auto separator = CCLayerColor::create({0,0,0});
            separator->setZOrder(-2);
            separator->setOpacity(50);
            separator->setScaleX(0.45f);
            separator->ignoreAnchorPointForPosition(false);
            separator->setSkewX(angle * 2);
            separator->setContentSize(scaledSize);
            separator->setAnchorPoint({1,0});
            separator->setPosition({bgWidth - separator->getContentWidth()/2 - (20.f * separatorXMul), 0.3f});
            separator->setID("paimon-separator"_spr);

            fields->m_separator = separator;
            fields->m_separatorBasePos = separator->getPosition();
            this->addChild(separator);
        }
    }

    void setupGradient(CCNode* bg, int levelID, bool androidSafe, CCTexture2D* texture) {
        auto fields = m_fields.self();

        // Clean up previous background nodes
        if (auto children = bg->getChildren()) {
            std::vector<CCNode*> toRemove;
            for (unsigned int i = 0; i < children->count(); i++) {
                if (auto child = typeinfo_cast<CCNode*>(children->objectAtIndex(i))) {
                    std::string childID = child->getID();
                    if (childID.find("paimon-level-gradient") != std::string::npos ||
                        childID.find("paimon-bg-clipper") != std::string::npos ||
                        childID == "paimon-level-background") {
                        toRemove.push_back(child);
                    }
                }
            }
            for (auto node : toRemove) node->removeFromParent();
        }
        fields->m_gradientLayer = nullptr;

        std::string bgType = "gradient";
        try { bgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type"); } catch (...) {}
        
        if (bgType == "thumbnail" && texture && !androidSafe) {
             CCSprite* bgSprite = nullptr;
             bool isGIF = false;
             
             float blurIntensity = 1.0f;
             try { blurIntensity = Mod::get()->getSettingValue<float>("levelcell-background-blur"); } catch (...) {}

             if (ThumbnailLoader::get().hasGIFData(levelID)) {
                 auto path = ThumbnailLoader::get().getCachePath(levelID);
                 this->retain();
                 AnimatedGIFSprite::createAsync(path.string(), [this, levelID, blurIntensity](AnimatedGIFSprite* anim) {
                     if (this->m_level && this->m_level->m_levelID == levelID) {
                         if (anim) {
                             if (auto bg = this->m_mainLayer) {
                                 if (auto clipper = bg->getChildByID("paimon-bg-clipper"_spr)) {
                                     clipper->removeAllChildren();
                                     
                                     float targetW = clipper->getContentWidth();
                                     float targetH = clipper->getContentHeight();
                                     float scale = std::max(targetW / anim->getContentWidth(), targetH / anim->getContentHeight());
                                     anim->setScale(scale);
                                     anim->setPosition(clipper->getContentSize() / 2);
                                     
                                     // Apply blur shader to GIF
                                     auto shader = getOrCreateShaderCell("paimon_cell_blur", vertexShaderCell, fragmentShaderBlurCell);
                                     if (shader) {
                                         anim->setShaderProgram(shader);
                                         anim->m_intensity = blurIntensity;
                                         anim->m_texSize = anim->getTexture()->getContentSizeInPixels();
                                     }
                                     
                                     clipper->addChild(anim);
                                 }
                             }
                         }
                     }
                     this->release();
                 });
             }

             if (!bgSprite) {
                 // Static image - apply high quality blur (Multi-pass Gaussian)
                 // Use background size for higher quality blur (similar to LevelInfoLayer)
                 CCSize targetSize = bg->getContentSize();
                 
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
                 // tempSprite->setFlipY(true); // Removed to fix inversion
                 
                 auto rtA = CCRenderTexture::create(targetSize.width, targetSize.height);
                 auto rtB = CCRenderTexture::create(targetSize.width, targetSize.height);
                 auto blurH = getOrCreateShaderCell("blur-horizontal"_spr, vertexShaderCell, fragmentShaderHorizontal);
                 auto blurV = getOrCreateShaderCell("blur-vertical"_spr, vertexShaderCell, fragmentShaderVertical);
                 
                 if (blurH && blurV && rtA && rtB) {
                     ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                     texture->setTexParameters(&params);
                     
                     // Map intensity to radius (matching LevelInfoLayer formula)
                     // LevelInfoLayer: 0.01 + ((intensity - 1) / 9.0 * 0.14)
                     // Here blurIntensity is float, assuming range 1-10 roughly
                     float normalizedIntensity = std::clamp((blurIntensity - 1.0f) / 9.0f, 0.0f, 1.0f);
                     float radius = 0.02f + (normalizedIntensity * 0.25f); // Increased multiplier for stronger blur
                     
                     // First pass: Render scaled sprite to rtA with Horizontal Blur
                     applyBlurPass(tempSprite, rtA, blurH, targetSize, radius);
                     
                     // Second pass: Render rtA to rtB with Vertical Blur
                     applyBlurPass(rtA->getSprite(), rtB, blurV, targetSize, radius);
                     
                     bgSprite = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
                     bgSprite->setFlipY(true);
                     
                     // Ensure linear filtering on the result
                     bgSprite->getTexture()->setTexParameters(&params);
                 } else {
                     bgSprite = PaimonShaderSprite::createWithTexture(texture);
                 }
             }
             
             if (bgSprite) {
                 // Create Clipper
                 auto stencil = CCDrawNode::create();
                 CCPoint rect[4];
                 rect[0] = ccp(0, 0);
                 rect[1] = ccp(bg->getContentWidth(), 0);
                 rect[2] = ccp(bg->getContentWidth(), bg->getContentHeight());
                 rect[3] = ccp(0, bg->getContentHeight());
                 ccColor4F white = {1, 1, 1, 1};
                 stencil->drawPolygon(rect, 4, white, 0, white);
                 
                 auto clipper = CCClippingNode::create(stencil);
                 clipper->setContentSize(bg->getContentSize());
                 // No alpha threshold for geometric stencil
                 clipper->setPosition({0,0});
                 clipper->setZOrder(10); // Same Z as gradient
                 clipper->setID("paimon-bg-clipper"_spr);

                 float targetW = bg->getContentWidth();
                 float targetH = bg->getContentHeight();

                 float scale = std::max(
                     targetW / bgSprite->getContentSize().width,
                     targetH / bgSprite->getContentSize().height
                 );
                 bgSprite->setScale(scale);
                 
                 bgSprite->setPosition(bg->getContentSize() / 2);
                 
                 // Only apply shader if it's a GIF (static is already blurred)
                 if (isGIF) {
                     auto shader = getOrCreateShaderCell("paimon_cell_blur", vertexShaderCell, fragmentShaderBlurCell);
                     if (shader) {
                         bgSprite->setShaderProgram(shader);
                         
                         if (auto pss = dynamic_cast<PaimonShaderSprite*>(bgSprite)) {
                             pss->m_intensity = blurIntensity;
                             pss->m_texSize = bgSprite->getTexture()->getContentSizeInPixels();
                         } else if (auto ags = dynamic_cast<AnimatedGIFSprite*>(bgSprite)) {
                             ags->m_intensity = blurIntensity;
                             ags->m_texSize = bgSprite->getTexture()->getContentSizeInPixels();
                         }
                     }
                 }
                 
                 clipper->addChild(bgSprite);

                 float darkness = 0.6f;
                 try { darkness = Mod::get()->getSettingValue<float>("levelcell-background-darkness"); } catch (...) {}
                 GLubyte opacity = static_cast<GLubyte>(std::clamp(darkness, 0.0f, 1.0f) * 255.0f);

                 auto overlay = CCLayerColor::create({0, 0, 0, opacity});
                 overlay->setContentSize({bg->getContentWidth(), bg->getContentHeight() + 1.0f});
                 overlay->setPosition({0, 0});
                 clipper->addChild(overlay);

                 bg->addChild(clipper);
                 bg->reorderChild(clipper, 10);
                 fields->m_gradientLayer = bgSprite;
                 return;
             }
        }

        ccColor3B colorA = {0, 0, 0};
        ccColor3B colorB = {255, 0, 0};

        if (auto pair = LevelColors::get().getPair(levelID)) {
            colorA = pair->a;
            colorB = pair->b;
        }

        bool animatedGradient = true;
        try { animatedGradient = Mod::get()->getSettingValue<bool>("levelcell-animated-gradient"); } catch (...) {}
        if (androidSafe) animatedGradient = false;

        auto grad = PaimonShaderGradient::create(
            ccc4(colorA.r, colorA.g, colorA.b, 255),
            ccc4(colorB.r, colorB.g, colorB.b, 255)
        );
        grad->setContentSize({ bg->getContentWidth() + 2.f, bg->getContentHeight() + 1.f });
        grad->setAnchorPoint({0,0});
        grad->setPosition({0.0f, 0.0f}); // Reset to 0,0 relative to this if bg is at 0,0? No, bg checks its pos
        
        // Fix: Add gradient to 'this' instead of 'bg' to avoid potential BatchNode issues (Happy Textures)
        // Ensure z-order is below content but similar to bg
        int bgZ = bg->getZOrder();
        grad->setZOrder(bgZ - 1); // Behind bg
        grad->setID("paimon-level-gradient"_spr);
        
        // We need to position 'grad' same as 'bg'.
        grad->setPosition(bg->getPosition());
        // Bg anchor is usually 0,0?
        if (bg->isIgnoreAnchorPointForPosition()) {
             // If bg ignores anchor, its position is bottom-left.
             // grad should mimic.
        }
        
        // Simply hide the original background and place ours
        bg->setVisible(false);
        this->addChild(grad);
        
        // fields->m_gradient = grad; // m_gradient is CCNode* in struct
        fields->m_gradient = grad;
        // bg->addChild(grad); // REMOVED
        // bg->reorderChild(grad, 10); // REMOVED

        fields->m_gradientLayer = grad;
        fields->m_gradientColorA = colorA;
        fields->m_gradientColorB = colorB;

        if (animatedGradient) {
            this->schedule(schedule_selector(PaimonLevelCell::updateGradientAnim), 0.0f);
        }
    }

    void setupMythicParticles(CCNode* bg, int levelID, bool androidSafe) {
        auto fields = m_fields.self();
        bool enableMythic = true; 
        try { enableMythic = Mod::get()->getSettingValue<bool>("levelcell-mythic-particles"); } catch (...) {}
        if (androidSafe) enableMythic = false;
        
        if (enableMythic && m_level->m_isEpic >= 3) {
             try {
                auto brighten = [](ccColor3B c) {
                    auto clamp = [](int v){ return std::max(0, std::min(255, v)); };
                    int add = 35;
                    return ccColor3B{ (GLubyte)clamp(c.r + add), (GLubyte)clamp(c.g + add), (GLubyte)clamp(c.b + add) };
                };
                ccColor3B ca{220,220,220}, cb{255,255,255};
                if (auto pair2 = LevelColors::get().getPair(levelID)) {
                    ca = brighten(pair2->a);
                    cb = brighten(pair2->b);
                }
                
                auto ps = CCParticleSystemQuad::create();
                if (!ps) return;
                
                ps->setBlendAdditive(false);
                ps->setID("paimon-mythic-particles"_spr);
                ps->setEmitterMode(kCCParticleModeGravity);
                ps->setGravity({0.f, 0.f});
                ps->setAngle(0.f);
                ps->setAngleVar(6.f);
                
                float width = bg->getContentWidth();
                float speed = 160.f;
                float life = (0.70f * width) / speed;
                if (life < 0.4f) life = 0.4f;
                
                ps->setSpeed(speed);
                ps->setSpeedVar(20.f);
                ps->setLife(life);
                ps->setLifeVar(life * 0.15f);
                
                float height = bg->getContentHeight();
                ps->setPosition({0.f, height * 0.5f});
                ps->setPosVar({0.f, height * 0.5f});
                
                ps->setStartSize(3.0f);
                ps->setStartSizeVar(1.2f);
                ps->setEndSize(2.0f);
                ps->setEndSizeVar(1.0f);
                
                ccColor4F startColorA{ ca.r / 255.f, ca.g / 255.f, ca.b / 255.f, 0.80f };
                ccColor4F startColorB{ cb.r / 255.f, cb.g / 255.f, cb.b / 255.f, 0.80f };
                ccColor4F base{
                    (startColorA.r + startColorB.r) * 0.5f,
                    (startColorA.g + startColorB.g) * 0.5f,
                    (startColorA.b + startColorB.b) * 0.5f,
                    0.80f
                };
                ccColor4F var{
                    fabsf(startColorA.r - startColorB.r) * 0.5f,
                    fabsf(startColorA.g - startColorB.g) * 0.5f,
                    fabsf(startColorA.b - startColorB.b) * 0.5f,
                    0.05f
                };
                ps->setStartColor(base);
                ps->setStartColorVar(var);
                ccColor4F end = base; end.a = 0.f;
                ccColor4F endVar = var; endVar.a = 0.05f;
                ps->setEndColor(end);
                ps->setEndColorVar(endVar);
                
                ps->setTotalParticles(120);
                ps->setEmissionRate(120.f / life);
                ps->setDuration(-1.f);
                ps->setPositionType(kCCPositionTypeRelative);
                ps->setAutoRemoveOnFinish(false);
                
                fields->m_mythicParticles = ps;
                
                // Fix: Add particles to 'this' instead of 'bg'
                ps->setPosition(ps->getPosition() + bg->getPosition());
                this->addChild(ps, bg->getZOrder() + 1); // Above bg
                
                ps->resetSystem();
            } catch (...) {}
        }
    }

    void setupViewButton(bool androidSafe) {
        if (androidSafe) return;
        auto fields = m_fields.self();
        auto children = this->getChildren();
        if (!children) return;

        // ... (View button logic is too complex to fully extract without more context on local variables, 
        // but I will simplify the main function by keeping it here or extracting it if I had more time.
        // For now, I will keep the original logic inside addOrUpdateThumb but cleaned up, 
        // OR I can try to extract it. Let's try to extract the search logic.)
        
        // Actually, I'll just paste the original view button logic back into addOrUpdateThumb 
        // to avoid breaking it, as it relies on `this` and `fields`.
    }

    void addOrUpdateThumb(CCTexture2D* texture) {
        if (!texture) {
            log::warn("[LevelCell] addOrUpdateThumb called with null texture");
            return;
        }
        
        try {
            bool androidSafe = false;
#ifdef GEODE_IS_ANDROID
            try { androidSafe = Mod::get()->getSettingValue<bool>("android-safe-mode"); } catch (...) {}
#endif
            // Re-added parent check because adding children to 'this' when 'this' is detached might be unsafe
            // if we rely on parent for layout. But for now, let's keep it removed as per persistence fix.
            // However, if 'this' is not in scene, actions might not run.
            
            auto fields = m_fields.self();
            if (!fields || fields->m_isBeingDestroyed) {
                log::warn("[LevelCell] Fields null or destroyed in addOrUpdateThumb");
                return;
            }

            auto bg = m_backgroundLayer;
            if (!bg) {
                log::warn("[LevelCell] Background layer null in addOrUpdateThumb");
                return;
            }
            
            // If bg has no parent, it might be detached. We can still try to add children to it.
            // But cleanPaimonNodes checks parent? No, it checks fields.
            
            cleanPaimonNodes(bg);

            bg->setZOrder(-2);

            setupDarkMode(bg);

            CCSprite* sprite = createThumbnailSprite(texture);
            if (!sprite) {
                log::warn("[LevelCell] Failed to create sprite from texture");
                return;
            }

            setupClippingAndSeparator(bg, sprite, androidSafe);

            if (m_level) {
                int32_t levelID = m_level->m_levelID.value();
                setupGradient(bg, levelID, androidSafe, texture);
                setupMythicParticles(bg, levelID, androidSafe);
            }
            
            log::info("[LevelCell] Thumbnail applied successfully for level {}", m_level ? m_level->m_levelID.value() : 0);

            // View button logic (Original logic preserved for safety)
            if (!androidSafe) {
                auto children = this->getChildren();
                if (children) {
                    // ... (Original view button search logic)
                    // Since I cannot easily extract it without risking breakage due to local variables and complexity,
                    // I will re-insert the original logic here.
                    
                    // Expand the "view" button to the full size of the LevelCell
                    for (unsigned int i = 0; i < children->count(); i++) {
                        auto node = children->objectAtIndex(i);
                        auto menu = geode::cast::typeinfo_cast<CCMenu*>(node);
                        if (!menu) continue;
                        
                        auto menuChildren = menu->getChildren();
                        if (!menuChildren) continue;
                        
                        for (unsigned int j = 0; j < menuChildren->count(); j++) {
                            auto menuItem = geode::cast::typeinfo_cast<CCMenuItemSpriteExtra*>(menuChildren->objectAtIndex(j));
                            if (!menuItem) continue;
                            
                            auto sprite = menuItem->getNormalImage();
                            if (!sprite) continue;
                            
                            auto spriteChildren = sprite->getChildren();
                            
                            bool isViewButton = false;
                            if (spriteChildren) {
                                for (unsigned int k = 0; k < spriteChildren->count(); k++) {
                                    auto child = spriteChildren->objectAtIndex(k);
                                    auto label = geode::cast::typeinfo_cast<CCLabelBMFont*>(child);
                                    if (label) {
                                        std::string text = label->getString();
                                        std::string textLower = text;
                                        for (auto& c : textLower) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                                        if (textLower.find("view") != std::string::npos || textLower.find("ver") != std::string::npos || textLower.find("get it") != std::string::npos) {
                                            isViewButton = true;
                                            break;
                                        }
                                    }
                                }
                            }

                            if (!isViewButton) {
                                std::string id = menuItem->getID();
                                if (id == "view-button" || id == "paimon-view-button") {
                                    isViewButton = true;
                                }
                            }

                            if (isViewButton) {
                                log::debug("[LevelCell] Found view button (ID: {})", menuItem->getID());
                                bool showButton = false;
                                try { showButton = Mod::get()->getSettingValue<bool>("levelcell-show-view-button"); } catch (...) { showButton = false; }
                                if (showButton) break;

                                if (isDailyCell()) break;

                                auto cellSize = this->getContentSize();
                                float areaWidth = 90.f;
                                float areaHeight = cellSize.height;

                                auto makeStateSprite = [&](GLubyte alpha) {
                                    auto stateSprite = CCSprite::create();
                                    auto layer = CCLayerColor::create(ccc4(0, 0, 0, alpha));
                                    layer->setContentSize({ areaWidth, areaHeight });
                                    stateSprite->addChild(layer);
                                    stateSprite->setContentSize({ areaWidth, areaHeight });
                                    stateSprite->setAnchorPoint({0.5f, 0.5f});
                                    return stateSprite;
                                };
                                
                                if (menuItem->getID() == "paimon-view-button") {
                                    try { menuItem->setID("view-button"); } catch (...) {}
                                }
                                fields->m_viewOverlay = menuItem;
                                menuItem->m_baseScale = menuItem->getScale();
                                menuItem->setVisible(true);
                                menuItem->setEnabled(true);

                                if (!isDailyCell()) {
                                    // Always re-apply sprites to ensure correct state
                                    auto normal = makeStateSprite(0);
                                    auto selected = makeStateSprite(0);
                                    auto disabled = makeStateSprite(0);

                                    if (auto norImg = menuItem->getNormalImage()) norImg->setVisible(false);
                                    if (auto sel = menuItem->getSelectedImage()) sel->setVisible(false);
                                    if (auto dis = menuItem->getDisabledImage()) dis->setVisible(false);

                                    menuItem->setNormalImage(normal);
                                    menuItem->setSelectedImage(selected);
                                    menuItem->setDisabledImage(disabled);
                                    
                                    CCPoint overlayCenterLocal;
                                    if (fields->m_clippingNode) {
                                        CCPoint clipPos = fields->m_clippingNode->getPosition();
                                        overlayCenterLocal = cocos2d::CCPoint(clipPos.x - areaWidth / 2.f - 15.f, cellSize.height / 2.f - 1.f);
                                    } else {
                                        overlayCenterLocal = cocos2d::CCPoint(cellSize.width - areaWidth / 2.f - 15.f, cellSize.height / 2.f - 1.f);
                                    }
                                    CCPoint cellCenterWorld = this->convertToWorldSpace(overlayCenterLocal);
                                    CCNode* parentNode = menuItem->getParent();
                                    if (parentNode) {
                                        parentNode->setVisible(true);
                                        CCPoint targetPos = parentNode->convertToNodeSpace(cellCenterWorld);
                                        menuItem->setPosition(targetPos);
                                    } else {
                                        menuItem->setPosition(overlayCenterLocal);
                                    }
                                }
                                break;
                            }
                        }
                    }

                    if (!fields->m_viewOverlay) {
                        std::vector<CCNode*> stack;
                        stack.push_back(this);
                        while (!stack.empty()) {
                            CCNode* cur = stack.back();
                            stack.pop_back();
                            if (!cur) continue;

                            if (auto menuItem = geode::cast::typeinfo_cast<CCMenuItemSpriteExtra*>(cur)) {
                                bool isViewButton = false;

                                std::string id = menuItem->getID();
                                if (id == "view-button" || id == "main-button" || id == "paimon-view-button") isViewButton = true;

                                if (!isViewButton) {
                                    if (auto ni = menuItem->getNormalImage()) {
                                        if (auto ch = ni->getChildren()) {
                                            for (unsigned int k = 0; k < ch->count(); k++) {
                                                if (auto lbl = geode::cast::typeinfo_cast<CCLabelBMFont*>(ch->objectAtIndex(k))) {
                                                    std::string text = lbl->getString();
                                                    std::string tl = text; for (auto& c : tl) c = (char)tolower((unsigned char)c);
                                                    if (tl.find("view") != std::string::npos || tl.find("ver") != std::string::npos || 
                                                        tl.find("get it") != std::string::npos || tl.find("play") != std::string::npos ||
                                                        tl.find("safe") != std::string::npos) {
                                                        isViewButton = true; break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                if (!isViewButton && isDailyCell()) {
                                    auto pos = menuItem->getPosition();
                                    auto cellSize = this->getContentSize();
                                    if (pos.x > cellSize.width * 0.4f) {
                                        isViewButton = true;
                                    }
                                }

                                if (isViewButton) {
                                    bool showButton = false;
                                    try { showButton = Mod::get()->getSettingValue<bool>("levelcell-show-view-button"); } catch (...) { showButton = false; }
                                    if (showButton) break;
                                    
                                    // Allow view button setup for Daily cells too
                                    // if (isDailyCell()) break;

                                    if (menuItem->getID() == "paimon-view-button") {
                                        try { menuItem->setID("view-button"); } catch (...) {}
                                    }
                                    fields->m_viewOverlay = menuItem;
                                    menuItem->m_baseScale = menuItem->getScale();
                                    menuItem->setVisible(true);
                                    menuItem->setEnabled(true);

                                    {
                                        auto cellSize3 = this->getContentSize();
                                        float areaWidth2 = 90.f;
                                        float areaHeight2 = cellSize3.height;
                                        auto makeStateSprite2 = [&](GLubyte alpha) {
                                            auto stateSprite = CCSprite::create();
                                            auto layer = CCLayerColor::create(ccc4(0, 0, 0, alpha));
                                            layer->setContentSize({ areaWidth2, areaHeight2 });
                                            stateSprite->addChild(layer);
                                            stateSprite->setContentSize({ areaWidth2, areaHeight2 });
                                            stateSprite->setAnchorPoint({0.5f, 0.5f});
                                            return stateSprite;
                                        };

                                        if (!isDailyCell()) {
                                            // Always re-apply sprites
                                            auto normal2 = makeStateSprite2(0);
                                            auto selected2 = makeStateSprite2(0);
                                            auto disabled2 = makeStateSprite2(0);

                                            auto curNormal = menuItem->getNormalImage();
                                            auto curSelected = menuItem->getSelectedImage();
                                            auto curDisabled = menuItem->getDisabledImage();
                                            
                                            if (curNormal) curNormal->setVisible(false);
                                            if (curSelected) curSelected->setVisible(false);
                                            if (curDisabled) curDisabled->setVisible(false);
                                            
                                            menuItem->setNormalImage(normal2);
                                            menuItem->setSelectedImage(selected2);
                                            menuItem->setDisabledImage(disabled2);

                                            auto setNodeOpacity2 = [](CCNode* n, GLubyte a) {
                                                if (!n) return;
                                                if (auto sp = geode::cast::typeinfo_cast<CCSprite*>(n)) sp->setOpacity(a);
                                                else if (auto lc = geode::cast::typeinfo_cast<CCLayerColor*>(n)) lc->setOpacity(a);
                                            };
                                            setNodeOpacity2(menuItem->getNormalImage(), 0);
                                            setNodeOpacity2(menuItem->getSelectedImage(), 0);
                                            setNodeOpacity2(menuItem->getDisabledImage(), 0);
                                            
                                            CCPoint centerLocal;
                                            if (fields->m_clippingNode) {
                                                CCPoint clipPos = fields->m_clippingNode->getPosition();
                                                centerLocal = cocos2d::CCPoint(clipPos.x - areaWidth2 / 2.f - 15.f, cellSize3.height / 2.f - 1.f);
                                            } else {
                                                centerLocal = cocos2d::CCPoint(cellSize3.width - areaWidth2 / 2.f - 15.f, cellSize3.height / 2.f - 1.f);
                                            }
                                            CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
                                            if (auto parentNode = menuItem->getParent()) {
                                                parentNode->setVisible(true);
                                                CCPoint targetPos = parentNode->convertToNodeSpace(centerWorld);
                                                menuItem->setPosition(targetPos);
                                            } else {
                                                menuItem->setPosition(centerLocal);
                                            }
                                        }
                                    }
                                    break;
                                }
                            }

                            if (auto arr = cur->getChildren()) {
                                for (unsigned int i2 = 0; i2 < arr->count(); i2++) {
                                    if (auto n2 = geode::cast::typeinfo_cast<CCNode*>(arr->objectAtIndex(i2))) {
                                        stack.push_back(n2);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Update gradient colors
            if (m_level && fields && !fields->m_isBeingDestroyed && fields->m_gradientLayer) {
                if (!fields->m_gradientLayer->getParent()) {
                    fields->m_gradientLayer = nullptr;
                } else {
                    int32_t levelID = m_level->m_levelID.value();
                    if (auto pair = LevelColors::get().getPair(levelID)) {
                        fields->m_gradientColorA = pair->a;
                        fields->m_gradientColorB = pair->b;
                        if (auto grad = dynamic_cast<PaimonShaderGradient*>(fields->m_gradientLayer)) {
                            try {
                                grad->setStartColor(pair->a);
                                grad->setEndColor(pair->b);
                            } catch (...) {
                                fields->m_gradientLayer = nullptr;
                            }
                        }
                    }
                }
            }

            if (isDailyCell()) {
                // fixDailyCell(); // Removed as per user request to move logic to DailyLevelNode
            }

            // Trigger flash animation when thumbnail loads - REMOVED to prevent annoyance when returning to list
            // log::info("[LevelCell] Thumbnail loaded successfully, triggering flash animation");
            // auto delayAction = CCDelayTime::create(0.15f);
            // auto flashAction = CCCallFunc::create(this, callfunc_selector(PaimonLevelCell::playTapFlashAnimation));
            // auto sequence = CCSequence::create(delayAction, flashAction, nullptr);
            // this->runAction(sequence);

        } catch (std::exception& e) {
            log::error("[addOrUpdateThumb] Exception: {}", e.what());
        } catch (...) {
            log::error("[addOrUpdateThumb] Unknown exception");
        }
    }

    bool checkMenuCollision(CCNode* node, CCPoint worldPoint, CCNode* ignoreNode) {
        if (!node || !node->isVisible()) return false;
        
        // If it's a CCMenu, check its items
        if (auto menu = dynamic_cast<CCMenu*>(node)) {
            if (!menu->isEnabled()) return false;
            
            auto children = menu->getChildren();
            if (!children) return false;
            
            for (int i = 0; i < children->count(); ++i) {
                auto child = static_cast<CCNode*>(children->objectAtIndex(i));
                if (!child->isVisible()) continue;
                
                // Skip the ignored node (m_viewOverlay)
                if (child == ignoreNode) continue;
                
                auto item = dynamic_cast<CCMenuItem*>(child);
                if (item && item->isEnabled()) {
                    // Check collision
                    CCPoint local = item->getParent()->convertToNodeSpace(worldPoint);
                    CCRect r = item->boundingBox();
                    if (r.containsPoint(local)) {
                        return true;
                    }
                }
            }
        }
        
        // Recurse
        auto children = node->getChildren();
        if (children) {
            for (int i = 0; i < children->count(); ++i) {
                auto child = static_cast<CCNode*>(children->objectAtIndex(i));
                if (checkMenuCollision(child, worldPoint, ignoreNode)) return true;
            }
        }
        return false;
    }

    bool isTouchOnMenu(CCTouch* touch) {
        auto fields = m_fields.self();
        CCPoint worldPoint = touch->getLocation();
        // Pass nullptr as ignoreNode so m_viewOverlay is INCLUDED in collision check
        // This ensures ccTouchBegan returns false if m_viewOverlay is hit, letting CCMenu handle it
        return checkMenuCollision(this, worldPoint, nullptr);
    }

    // Register a non-swallowing touch delegate to detect tap vs drag
    // REMOVED to prevent crashes in dispatchScrollMSG
    /*
    $override void onEnter() {
        LevelCell::onEnter();
        auto fields = m_fields.self();
        if (!fields) return;
        if (!fields->m_touchRegistered) {
            try {
                CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, 0, false);
                fields->m_touchRegistered = true;
                log::debug("[LevelCell][Touch] Delegate registered (priority 0, swallow=false)");
            } catch (...) {
                log::debug("[LevelCell][Touch] Failed to register delegate at priority 0; retrying 10");
                try {
                    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, 10, false);
                    fields->m_touchRegistered = true;
                    log::debug("[LevelCell][Touch] Delegate registered (priority 10 fallback)");
                } catch (...) {
                    log::debug("[LevelCell][Touch] Final failure registering delegate");
                }
            }
        }
    }

    $override void onExit() {
        if (m_level) {
            ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value());
        }
        try {
            CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
        } catch (...) {}
        auto fields = m_fields.self();
        if (fields) fields->m_touchRegistered = false;
        LevelCell::onExit();
    }

    $override bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return false;
        
        CCPoint world = touch->getLocation();
        CCPoint local = this->convertToNodeSpace(world);
        CCSize cs = this->getContentSize();
        CCRect bounds = CCRect(0.f, 0.f, cs.width, cs.height);
        if (bounds.containsPoint(local)) {
            if (isTouchOnMenu(touch)) {
                 return false;
            }

            fields->m_trackingTap = true;
            fields->m_touchStart = world;
            fields->m_touchMaxDist = 0.f;
            return true;
        }
        return false;
    }

    $override void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        auto fields = m_fields.self();
        if (!fields || !fields->m_trackingTap) return;
        CCPoint cur = touch->getLocation();
        float dx = cur.x - fields->m_touchStart.x;
        float dy = cur.y - fields->m_touchStart.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist > fields->m_touchMaxDist) fields->m_touchMaxDist = dist;
    }
    */
    
    // Minimal valid onExit to cancel load
    $override void onExit() {
        if (m_level) {
            ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value());
        }
        LevelCell::onExit();
    }

    void playTapFlashAnimation() {
        auto fields = m_fields.self();
        if (!fields) return;

        CCSize cellSize = this->getContentSize();
        if (cellSize.width < 4.f || cellSize.height < 4.f) {
            if (auto p = this->getParent()) cellSize = p->getContentSize();
        }
        log::debug("[LevelCell][Flash] Trigger requested; resolved cellSize=({}, {})", cellSize.width, cellSize.height);

        // Prefer m_mainMenu/m_button parent if available for higher stacking
        CCNode* flashParent = this;
        if (m_mainMenu) flashParent = m_mainMenu;
        else if (m_button && m_button->getParent()) flashParent = m_button->getParent();

        auto flash = CCLayerColor::create(ccc4(255,255,255,0));
        if (!flash) return;
        flash->setContentSize(cellSize);
        flash->ignoreAnchorPointForPosition(false);
        flash->setAnchorPoint({0.5f,0.5f});
        
        // Fix: Calculate position in flashParent's coordinate space to ensure centering
        // Using world space conversion handles cases where flashParent (e.g. m_mainMenu) 
        // has a different position/anchor than the cell itself.
        CCPoint centerLocal = CCPoint(cellSize.width / 2.0f, cellSize.height / 2.0f);
        CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
        CCPoint centerInParent = flashParent->convertToNodeSpace(centerWorld);
        flash->setPosition(centerInParent);

        flash->setZOrder(99999);
        try { flash->setID("paimon-tap-flash"_spr); } catch (...) {}
        flashParent->addChild(flash);
        flashParent->reorderChild(flash, 99999);
        log::debug("[LevelCell][Flash] Layer added parent={} (ptr={}, z={})", (void*)flashParent, (void*)flash, flash->getZOrder());

        ccBlendFunc blend { GL_SRC_ALPHA, GL_ONE };
        flash->setBlendFunc(blend);
        log::debug("[LevelCell][Flash] BlendFunc set (src=GL_SRC_ALPHA, dst=GL_ONE)");

        auto fadeIn  = CCFadeTo::create(0.05f, 230);
        auto hold    = CCDelayTime::create(0.02f);
        auto fadeOut = CCFadeTo::create(0.30f, 0);
        auto remove  = CCCallFunc::create(flash, callfunc_selector(CCNode::removeFromParent));
        auto scaleUp = CCScaleTo::create(0.07f, 1.05f);
        auto scaleDown = CCScaleTo::create(0.25f, 1.0f);
        auto pulse = CCSequence::create(scaleUp, scaleDown, nullptr);

        auto easeIn = CCEaseOut::create(static_cast<CCActionInterval*>(fadeIn->copy()->autorelease()), 2.6f);
        auto easeOut = CCEaseIn::create(static_cast<CCActionInterval*>(fadeOut->copy()->autorelease()), 1.4f);
        auto flashSeq = CCSequence::create(easeIn, hold, easeOut, remove, nullptr);
        flash->runAction(flashSeq);
        flash->runAction(pulse);
        log::debug("[LevelCell][Flash] Actions started");

        if (m_backgroundLayer) {
            auto originalColor = m_backgroundLayer->getColor();
            m_backgroundLayer->setColor({255,255,255});
            auto delayBG = CCDelayTime::create(0.03f);
            auto tintBack = CCTintTo::create(0.22f, originalColor.r, originalColor.g, originalColor.b);
            m_backgroundLayer->runAction(CCSequence::create(delayBG, tintBack, nullptr));
            log::debug("[LevelCell][Flash] Background tint pulse applied");
        }

        if (fields->m_thumbSprite) {
            auto ts = fields->m_thumbSprite;
            ts->setOpacity(255);
            auto thumbPulseUp = CCScaleTo::create(0.07f, ts->getScale() * 1.02f);
            auto thumbPulseDown = CCScaleTo::create(0.22f, ts->getScale());
            ts->runAction(CCSequence::create(thumbPulseUp, thumbPulseDown, nullptr));
            log::debug("[LevelCell][Flash] Thumbnail pulse applied (spritePtr={})", (void*)ts);
        }
    }

    // Fallback: flash on button click (original click behavior preserved)
    $override void onClick(CCObject* sender) {
        log::debug("[LevelCell][Click] onClick invoked (levelID={}, ptr={})", m_level ? m_level->m_levelID.value() : -1, (void*)this);
        playTapFlashAnimation();
        LevelCell::onClick(sender);
    }
/*
    $override void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        auto fields = m_fields.self();
    // Register a non-swallowing touch delegate to detect tap vs drag
    // REMOVED to prevent crashes in dispatchScrollMSG
*/
    // REMOVED to prevent crashes in dispatchScrollMSG
    /*
    $override void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        // ... Removed impl ...
    }

    $override void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        // ... Removed impl ...
    }
    */
    
    // Helper: brighten color by amount [0..255]
    static inline ccColor3B brightenColor(const ccColor3B& c, int add) {
        auto clamp = [](int v){ return std::max(0, std::min(255, v)); };
        return ccColor3B{
            (GLubyte)clamp(c.r + add),
            (GLubyte)clamp(c.g + add),
            (GLubyte)clamp(c.b + add)
        };
    }

    void updateGradientAnim(float dt) {
        PAIMON_GUARD_BEGIN
            auto fields = m_fields.self();
            if (!fields || fields->m_isBeingDestroyed || !fields->m_gradientLayer) return;
            
            auto grad = dynamic_cast<PaimonShaderGradient*>(fields->m_gradientLayer);
            if (!grad) return;

            // Dynamic GIF gradient support
            if (fields->m_thumbSprite) {
                // GifSprite does not support getCurrentFrameColors yet
            }

            fields->m_gradientTime += dt;
            float t = (sinf(fields->m_gradientTime * 1.2f) + 1.0f) / 2.0f;
            
            // Base gradient colors (animated wave)
            ccColor3B left = {
                (GLubyte)((1-t)*fields->m_gradientColorA.r + t*fields->m_gradientColorB.r),
                (GLubyte)((1-t)*fields->m_gradientColorA.g + t*fields->m_gradientColorB.g),
                (GLubyte)((1-t)*fields->m_gradientColorA.b + t*fields->m_gradientColorB.b)
            };
            ccColor3B right = {
                (GLubyte)((1-t)*fields->m_gradientColorB.r + t*fields->m_gradientColorA.r),
                (GLubyte)((1-t)*fields->m_gradientColorB.g + t*fields->m_gradientColorA.g),
                (GLubyte)((1-t)*fields->m_gradientColorB.b + t*fields->m_gradientColorA.b)
            };
            
            // Apply brightness based on centerLerp
            auto clamp = [](int v) { return std::max(0, std::min(255, v)); };
            int brightAmount = static_cast<int>(60.0f * fields->m_centerLerp);
            
            left.r = (GLubyte)clamp(left.r + brightAmount);
            left.g = (GLubyte)clamp(left.g + brightAmount);
            left.b = (GLubyte)clamp(left.b + brightAmount);
            
            right.r = (GLubyte)clamp(right.r + brightAmount);
            right.g = (GLubyte)clamp(right.g + brightAmount);
            right.b = (GLubyte)clamp(right.b + brightAmount);
            
            grad->setStartColor(left);
            grad->setEndColor(right);
        PAIMON_GUARD_END
    }

    void checkCenterPosition(float dt) {
        PAIMON_GUARD_BEGIN
            auto fields = m_fields.self();
            if (!fields || fields->m_isBeingDestroyed) return;
            
            // Check whether hover effects are enabled
            bool hoverEnabled = true;
            try {
                hoverEnabled = Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
            } catch (...) {}
            
            if (!hoverEnabled) {
                if (fields->m_centerLerp > 0.0f) {
                    fields->m_centerLerp = 0.0f;
                }
                return;
            }
            
            // Ensure the cell has a valid parent
            if (!this->getParent()) {
                return;
            }
            
            // Get the cell position in window coordinates
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            CCPoint worldPos = this->convertToWorldSpace(CCPointZero);
            
            // Compute the cell center
            float cellCenterY = worldPos.y + this->getContentSize().height / 2.0f;
            float screenCenterY = winSize.height / 2.0f;
            
            // Smaller center zone (±45px around the screen center)
            float centerZone = 45.0f;

            // Check compact mode
            bool compactMode = false;
            try { compactMode = Mod::get()->getSettingValue<bool>("compact-list-mode"); } catch(...) {}
            
            if (compactMode) {
                centerZone *= 0.55f; // Reduce by 45% (45px -> 24.75px)
            }

            float distanceFromCenter = std::abs(cellCenterY - screenCenterY);
            bool isInCenter = distanceFromCenter < centerZone;
            
            // Ensure the cell is visible on-screen
            bool isVisible = cellCenterY > 0 && cellCenterY < winSize.height;
            
            if (!isVisible) {
                fields->m_wasInCenter = false;
                return;
            }
            
            // Update state flag
            fields->m_wasInCenter = isInCenter;
        PAIMON_GUARD_END
    }

    void updateCenterAnimation(float dt) {
        PAIMON_GUARD_BEGIN
            auto fields = m_fields.self();
            if (!fields || fields->m_isBeingDestroyed) return;
            
            fields->m_animTime += dt;

            // Read settings
            std::string animType = "zoom-slide";
            float speedMult = 1.0f;
            std::string animEffect = "none";
            try {
                animType = Mod::get()->getSettingValue<std::string>("levelcell-anim-type");
                speedMult = Mod::get()->getSettingValue<float>("levelcell-anim-speed");
                animEffect = Mod::get()->getSettingValue<std::string>("levelcell-anim-effect");
            } catch (...) {}

            if (animType == "none") {
                 fields->m_centerLerp = 0.0f;
            } else {
                // Compute target lerp (0 or 1)
                float target = fields->m_wasInCenter ? 1.0f : 0.0f;
                
                // Smooth interpolation
                float baseSpeed = 6.0f; 
                float speed = baseSpeed * speedMult;
                
                fields->m_centerLerp += (target - fields->m_centerLerp) * std::min(1.0f, dt * speed);
            }
            
            float lerp = fields->m_centerLerp;
            
            // Calculate transforms based on type
            float offsetX = 0.0f;
            float zoomFactor = 1.0f;
            float rotation = 0.0f;
            float spriteRotation = 0.0f;
            float spriteOffsetX = 0.0f;

            if (animType == "zoom-slide") {
                offsetX = -10.f * lerp;
                zoomFactor = 1.0f + (0.12f * lerp);
            } else if (animType == "zoom") {
                offsetX = 0.0f;
                zoomFactor = 1.0f + (0.15f * lerp);
            } else if (animType == "slide") {
                // Pan inside instead of moving clipping node
                offsetX = 0.0f;
                zoomFactor = 1.0f + (0.1f * lerp);
                spriteOffsetX = -15.f * lerp;
            } else if (animType == "bounce") {
                 zoomFactor = 1.0f + (0.20f * lerp);
            } else if (animType == "rotate") {
                 // Reduced intensity to prevent overlapping
                 zoomFactor = 1.0f + (0.05f * lerp);
                 rotation = sinf(fields->m_animTime * 3.0f) * 1.5f * lerp;
            } else if (animType == "rotate-content") {
                 // Rotates only the sprite inside, safe from overlapping
                 zoomFactor = 1.0f + (0.15f * lerp);
                 spriteRotation = sinf(fields->m_animTime * 4.0f) * 3.0f * lerp;
            } else if (animType == "shake") {
                 zoomFactor = 1.0f + (0.05f * lerp);
                 offsetX = sinf(fields->m_animTime * 20.0f) * 3.0f * lerp;
            } else if (animType == "pulse") {
                 // Heartbeat effect
                 float pulse = (sinf(fields->m_animTime * 10.0f) + 1.0f) * 0.5f; // 0 to 1
                 zoomFactor = 1.0f + (0.05f * lerp) + (pulse * 0.05f * lerp);
            } else if (animType == "swing") {
                 // Pendulum swing
                 zoomFactor = 1.0f + (0.05f * lerp);
                 rotation = sinf(fields->m_animTime * 4.0f) * 3.0f * lerp;
            }

            // Apply compact mode reduction
            bool compactMode = false;
            try { compactMode = Mod::get()->getSettingValue<bool>("compact-list-mode"); } catch(...) {}
            
            if (compactMode) {
                // Reduce intensity of all transformations by 45%
                float excessZoom = zoomFactor - 1.0f;
                zoomFactor = 1.0f + (excessZoom * 0.55f);
                
                offsetX *= 0.55f;
                spriteOffsetX *= 0.55f;
                rotation *= 0.55f;
                spriteRotation *= 0.55f;
            }

            // Apply effects based on lerp
            if (fields->m_clippingNode) {
                // Only scale in X so it doesn't exceed the height
                // Adjust position to compensate for growth from anchorPoint (1,0)
                float posAdjustment = 0.0f;
                if (animType == "zoom-slide" || animType == "zoom" || animType == "bounce" || animType == "rotate" || animType == "shake" || animType == "rotate-content" || animType == "pulse" || animType == "swing") {
                     posAdjustment = (zoomFactor - 1.0f) * fields->m_clippingNode->getContentSize().width;
                }
                
                CCPoint newPos = CCPoint(fields->m_clipBasePos.x + offsetX + posAdjustment, fields->m_clipBasePos.y);
                fields->m_clippingNode->setPosition(newPos);
                fields->m_clippingNode->setScaleX(zoomFactor);
                fields->m_clippingNode->setRotation(rotation);
            }
            
            // Move the separator along with the clipping node
            if (fields->m_separator) {
                CCPoint newSepPos = CCPoint(fields->m_separatorBasePos.x + offsetX, fields->m_separatorBasePos.y);
                fields->m_separator->setPosition(newSepPos);
                fields->m_separator->setRotation(rotation);
            }
            
            // Zoom effect on the image (uniform scale)
            if (fields->m_thumbSprite) {
                // Zoom: scale from 1.0 to 1.20 (20% larger when centered)
                fields->m_thumbSprite->setScale(fields->m_thumbBaseScaleX * zoomFactor);
                fields->m_thumbSprite->setRotation(spriteRotation);
                fields->m_thumbSprite->setPosition(fields->m_thumbBasePos + CCPoint(spriteOffsetX, 0.0f));
                
                // Reset opacity first
                fields->m_thumbSprite->setOpacity(255);
            }

            // Apply Effects to Thumbnail and Gradient
            bool effectOnGradient = false;
            try { effectOnGradient = Mod::get()->getSettingValue<bool>("levelcell-effect-on-gradient"); } catch (...) {}

            std::string bgType = "gradient";
            try { bgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type"); } catch (...) {}

            std::vector<CCSprite*> targets;
            if (fields->m_thumbSprite) targets.push_back(fields->m_thumbSprite);
            
            // Only apply effect to gradient layer if it's NOT a thumbnail background
            // Because thumbnail background uses m_intensity for blur, which would be overwritten
            if (effectOnGradient && fields->m_gradientLayer && bgType != "thumbnail") {
                targets.push_back(fields->m_gradientLayer);
            }

            for (auto target : targets) {
                bool usingShader = false;
                
                // Check for PaimonShaderSprite
                PaimonShaderSprite* pss = dynamic_cast<PaimonShaderSprite*>(target);
                // Check for PaimonShaderGradient
                PaimonShaderGradient* psg = dynamic_cast<PaimonShaderGradient*>(target);
                // GifSprite does not support shader properties yet
                
                auto setIntensity = [&](float i) {
                    if (pss) pss->m_intensity = i;
                    if (psg) psg->m_intensity = i;
                };
                
                auto setTime = [&](float t) {
                    if (pss) pss->m_time = t;
                    if (psg) psg->m_time = t;
                };
                
                auto setTexSize = [&]() {
                    if (pss) pss->m_texSize = target->getTexture()->getContentSizeInPixels();
                    if (psg) psg->m_texSize = target->getContentSize(); // Gradient uses content size
                };

                // Apply Effects
                if (animEffect == "brightness") {
                    // Brighten by starting darker and going to white
                    float brightness = 180.0f + (75.0f * lerp);
                    target->setColor({(GLubyte)brightness, (GLubyte)brightness, (GLubyte)brightness});
                } else if (animEffect == "darken") {
                    // Darken when hovered
                    float brightness = 255.0f - (100.0f * lerp);
                    target->setColor({(GLubyte)brightness, (GLubyte)brightness, (GLubyte)brightness});
                } else if (animEffect == "sepia") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_sepia", vertexShaderCell, fragmentShaderSepiaCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "sharpen") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_sharpen", vertexShaderCell, fragmentShaderSharpenCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                        setTexSize();
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "edge-detection") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_edge", vertexShaderCell, fragmentShaderEdgeCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                        setTexSize();
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "vignette") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_vignette", vertexShaderCell, fragmentShaderVignetteCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "pixelate") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_pixelate", vertexShaderCell, fragmentShaderPixelateCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                        setTexSize();
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "posterize") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_posterize", vertexShaderCell, fragmentShaderPosterizeCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "chromatic") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_chromatic", vertexShaderCell, fragmentShaderChromaticCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "scanlines") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_scanlines", vertexShaderCell, fragmentShaderScanlinesCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                        setTexSize();
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "solarize") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_solarize", vertexShaderCell, fragmentShaderSolarizeCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "rainbow") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_rainbow", vertexShaderCell, fragmentShaderRainbowCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                        setTime(fields->m_animTime);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "red") {
                     float g = 255.0f - (100.0f * lerp);
                     float b = 255.0f - (100.0f * lerp);
                     target->setColor({255, (GLubyte)g, (GLubyte)b});
                } else if (animEffect == "blue") {
                     float r = 255.0f - (100.0f * lerp);
                     float g = 255.0f - (100.0f * lerp);
                     target->setColor({(GLubyte)r, (GLubyte)g, 255});
                } else if (animEffect == "gold") {
                     // Interpolate from white (255,255,255) to Gold (255, 215, 0)
                     float g = 255.0f - (40.0f * lerp);
                     float b_val = 255.0f - (255.0f * lerp);
                     target->setColor({255, (GLubyte)g, (GLubyte)b_val});
                } else if (animEffect == "fade") {
                     target->setColor({255, 255, 255});
                     target->setOpacity((GLubyte)(255.0f - (100.0f * lerp)));
                } else if (animEffect == "grayscale") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_grayscale", vertexShaderCell, fragmentShaderGrayscaleCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "invert") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_invert", vertexShaderCell, fragmentShaderInvertCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "blur") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_blur", vertexShaderCell, fragmentShaderBlurCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                        setTexSize();
                    }
                    target->setColor({255, 255, 255});
                } else if (animEffect == "glitch") {
                    usingShader = true;
                    auto shader = getOrCreateShaderCell("paimon_cell_glitch", vertexShaderCell, fragmentShaderGlitchCell);
                    if (shader) {
                        target->setShaderProgram(shader);
                        setIntensity(lerp);
                        setTime(fields->m_animTime);
                    }
                    target->setColor({255, 255, 255});
                } else {
                    // Reset color if no effect
                    if (psg) {
                        // Gradient handles its own color via updateGradientAnim, so we shouldn't mess with it unless an effect is active
                        // But here we are in the loop.
                        // If animEffect is "none", we do nothing.
                        // If animEffect is something else but not handled above, we do nothing.
                    } else {
                        target->setColor({255, 255, 255});
                    }
                }

                if (!usingShader) {
                    // Reset to default shader if it was changed
                    target->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
                }
            }

            // Keep the "view" overlay docked as a 90px strip at the thumbnail edge (parent coordinates)
            if (fields->m_viewOverlay) {
                auto cs2 = this->getContentSize();
                float areaWidth3 = 90.f;                                  // constant 90px strip
                float areaHeight3 = cs2.height;                           // same height as the cell
                CCPoint centerLocal;
                if (fields->m_clippingNode) {
                    CCPoint clipPos = fields->m_clippingNode->getPosition();
                    centerLocal = cocos2d::CCPoint(clipPos.x - areaWidth3 / 2.f - 15.f, cs2.height / 2.f - 1.f);
                } else {
                    centerLocal = cocos2d::CCPoint(cs2.width - areaWidth3 / 2.f - 15.f, cs2.height / 2.f - 1.f);
                }
                CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
                if (auto parentNode = fields->m_viewOverlay->getParent()) {
                    CCPoint targetPos = parentNode->convertToNodeSpace(centerWorld);
                    fields->m_viewOverlay->setPosition(targetPos);
                } else {
                    fields->m_viewOverlay->setPosition(centerLocal);
                }

                // Update overlay image sizes to match the thumbnail
                auto adjustState = [&](CCNode* n){
                    if (auto sp = geode::cast::typeinfo_cast<CCSprite*>(n)) {
                        sp->setContentSize({ areaWidth3, areaHeight3 });
                        if (auto ch = sp->getChildren()) {
                            for (unsigned int i = 0; i < ch->count(); i++) {
                                if (auto lc = geode::cast::typeinfo_cast<CCLayerColor*>(ch->objectAtIndex(i))) {
                                    lc->setContentSize({ areaWidth3, areaHeight3 });
                                }
                            }
                        }
                    }
                };
                adjustState(fields->m_viewOverlay->getNormalImage());
                adjustState(fields->m_viewOverlay->getSelectedImage());
                adjustState(fields->m_viewOverlay->getDisabledImage());
            }
        PAIMON_GUARD_END
    }

    void animateToCenter() {
        // Deprecated - now using lerp system
    }

    void animateFromCenter() {
        // Deprecated - now using lerp system
    }

    // Detect whether this cell is inside a DailyLevelNode/DailyLevelPage
    bool isDailyCell() {
        CCNode* parent = this->getParent();
        int depth = 0;
        while (parent && depth < 10) {
            // Check by class name or node ID
            const char* className = typeid(*parent).name();
            std::string classStr(className);
            
            // Look for DailyLevelNode / DailyLevelPage / CCScale9Sprite (used in dailies)
            if (classStr.find("DailyLevelNode") != std::string::npos ||
                classStr.find("DailyLevelPage") != std::string::npos) {
                log::debug("[LevelCell] Detected as Daily cell (class: {})", classStr);
                return true;
            }
            
            // Also check via typeinfo_cast
            if (typeinfo_cast<DailyLevelNode*>(parent)) {
                log::debug("[LevelCell] Detected as Daily cell via typeinfo_cast");
                return true;
            }
            
            parent = parent->getParent();
            depth++;
        }
        return false;
    }
    
    // Adjust thumbnail for daily cells (larger scale and adjusted position)
    void fixDailyCell() {
        // Logic moved to DailyLevelNode hook
    }

    // Removed onPaimonDailyPlay as per user request to remove animation
    
    // Removed onLevelInfo hook as it's not available in binding
    
    void tryLoadThumbnail() {
        try {
            configureThumbnailLoader();

            if (!m_level) return;
            
            int dailyID = m_level->m_dailyID.value();
            bool isDaily = dailyID > 0;
            if (isDaily) return;
            
            int32_t levelID = m_level->m_levelID.value();
            if (levelID <= 0) return;
            
            auto fields = m_fields.self();
            
            if (fields->m_lastRequestedLevelID != levelID) {
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
                fields->m_lastRequestedLevelID = levelID;
                fields->m_hasGif = false;
                fields->m_gifTexture = nullptr;
                fields->m_staticTexture = nullptr;
            }
            
            if (fields->m_thumbnailRequested) return;
            
            fields->m_requestId++;
            int currentRequestId = fields->m_requestId;
            fields->m_thumbnailRequested = true;
            fields->m_lastRequestedLevelID = levelID;
            
            std::string fileName = fmt::format("{}.png", levelID);
            
            bool enableSpinners = true;
            // try { enableSpinners = Mod::get()->getSettingValue<bool>("enable-loading-spinners"); } catch (...) {}
            
            if (enableSpinners) showLoadingSpinner();
            
            this->retain();
            LevelCell* cellPtr = this;
            
            // 1. Request Static Thumbnail
            ThumbnailLoader::get().requestLoad(levelID, fileName, [cellPtr, levelID, enableSpinners, currentRequestId](CCTexture2D* tex, bool fromServer) {
                if (!cellPtr) return;
                auto* cell = static_cast<PaimonLevelCell*>(cellPtr);
                auto fields = cell->m_fields.self();
                
                // Robustness check: Ensure cell is still valid and pointing to the same level
                if (!fields || fields->m_isBeingDestroyed || fields->m_requestId != currentRequestId) {
                    cellPtr->release();
                    return;
                }

                // Extra check: Ensure the level object in the cell matches the requested ID
                if (!cell->m_level || cell->m_level->m_levelID != levelID) {
                    cellPtr->release();
                    return;
                }
                
                if (fields->m_thumbnailApplied) {
                    cellPtr->release();
                    return;
                }
                
                if (enableSpinners) cell->hideLoadingSpinner();
                
                if (tex) {
                    fields->m_thumbnailApplied = true;
                    fields->m_staticTexture = tex; // Store static texture
                    cell->addOrUpdateThumb(tex);
                    
                    if (fromServer && fields->m_thumbSprite) {
                        // Flash effect
                        auto flash = CCLayerColor::create({255, 255, 255, 255});
                        flash->setContentSize(fields->m_thumbSprite->getContentSize());
                        flash->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
                        fields->m_thumbSprite->addChild(flash, 100);
                        flash->runAction(CCSequence::create(CCFadeOut::create(0.5f), CCRemoveSelf::create(), nullptr));
                    }
                } else {
                    // If texture failed to load and it's a main level (ID <= 100 approx), show black background
                    // For main levels without thumbnails, show a black placeholder.
                    if (levelID > 0 && levelID <= 100) {
                        // Create a 1x1 black texture
                        auto blackImage = new CCImage();
                        uint8_t blackPixel[4] = {0, 0, 0, 255};
                        if (blackImage->initWithImageData(blackPixel, 4)) {
                            auto blackTex = new CCTexture2D();
                            if (blackTex->initWithImage(blackImage)) {
                                blackTex->autorelease();
                                cell->addOrUpdateThumb(blackTex);
                            }
                            // blackTex is autoreleased, do not release manually
                        }
                        blackImage->release();
                    }
                }
                cellPtr->release();
            });

            // 2. Request GIF thumbnail (hover animation). Background blur uses the static thumbnail.
            
            this->retain(); // Retain for GIF callback
            ThumbnailLoader::get().requestLoad(levelID, fileName, [cellPtr, levelID, currentRequestId](CCTexture2D* tex, bool fromServer) {
                if (!cellPtr) return;
                auto* cell = static_cast<PaimonLevelCell*>(cellPtr);
                auto fields = cell->m_fields.self();
                
                if (!fields || fields->m_isBeingDestroyed || fields->m_requestId != currentRequestId) {
                    cellPtr->release();
                    return;
                }

                // Extra check: Ensure the level object in the cell matches the requested ID
                if (!cell->m_level || cell->m_level->m_levelID != levelID) {
                    cellPtr->release();
                    return;
                }
                
                if (tex) {
                    fields->m_hasGif = true;
                    fields->m_gifTexture = tex;
                }
                cellPtr->release();
            }, 0, true); // isGif = true

        } catch (...) {}
    }

    $override void update(float dt) {
        LevelCell::update(dt);
        
        auto fields = m_fields.self();
        if (!fields) return;
        
        if (fields->m_hasGif && fields->m_gifTexture && fields->m_thumbSprite) {
            // Check hover
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto mousePos = cocos2d::CCDirector::sharedDirector()->getOpenGLView()->getMousePosition();
            // In cocos2d-x 2.2.3 (GD), getMousePosition returns y from top.
            mousePos.y = winSize.height - mousePos.y;
            
            auto nodePos = fields->m_thumbSprite->getParent()->convertToNodeSpace(mousePos);
            auto box = fields->m_thumbSprite->boundingBox();
            
            bool hovering = box.containsPoint(nodePos);
            
            if (hovering && !fields->m_isHovering) {
                fields->m_isHovering = true;
                fields->m_thumbSprite->setTexture(fields->m_gifTexture);
                fields->m_thumbSprite->setTextureRect({0, 0, fields->m_gifTexture->getContentSize().width, fields->m_gifTexture->getContentSize().height});
            } else if (!hovering && fields->m_isHovering) {
                fields->m_isHovering = false;
                if (fields->m_staticTexture) {
                    fields->m_thumbSprite->setTexture(fields->m_staticTexture);
                    fields->m_thumbSprite->setTextureRect({0, 0, fields->m_staticTexture->getContentSize().width, fields->m_staticTexture->getContentSize().height});
                }
            }
        }
    }

    $override void loadCustomLevelCell() {
        LevelCell::loadCustomLevelCell();
        tryLoadThumbnail();
    }

    $override void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);
        tryLoadThumbnail();
    }
};




