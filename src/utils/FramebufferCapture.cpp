#include "FramebufferCapture.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <cocos2d.h>

using namespace geode::prelude;
using namespace cocos2d;

// Static member initialization
FramebufferCapture::CaptureRequest FramebufferCapture::s_request;
std::vector<FramebufferCapture::DeferredCallback> FramebufferCapture::s_deferredCallbacks;

void FramebufferCapture::requestCapture(
    int levelID,
    std::function<void(bool, CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback,
    CCNode* nodeToCapture
) {
    log::info("[FramebufferCapture] Capture requested for level {}", levelID);
    
    if (s_request.active) {
        log::warn("[FramebufferCapture] A capture is already pending, replacing it");
    }
    
    s_request.levelID = levelID;
    s_request.callback = callback;
    s_request.nodeToCapture = nodeToCapture;
    s_request.active = true;
}

void FramebufferCapture::cancelPending() {
    if (s_request.active) {
        log::info("[FramebufferCapture] Pending capture cancelled");
        s_request.active = false;
        s_request.callback = nullptr;
    }
}

bool FramebufferCapture::hasPendingCapture() {
    return s_request.active;
}

void FramebufferCapture::executeIfPending() {
    if (!s_request.active) {
        return;
    }
    
    log::info("[FramebufferCapture] Executing pending capture");
    
    // If a specific node was requested, capture that.
    if (s_request.nodeToCapture) {
        doCaptureNode(s_request.nodeToCapture);
        s_request.active = false;
        return;
    }
    
    // Get current viewport size.
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    int vpW = viewport[2];
    int vpH = viewport[3];

    // Check current framebuffer
    GLint currentFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

    // Read capture settings.
    std::string mode = "auto";
    double maxMP = 8.5;
    int targetW = 1920;
    try {
        mode = Mod::get()->getSettingValue<std::string>("capture-mode");
    } catch(...) {}
    try {
        maxMP = static_cast<double>(Mod::get()->getSettingValue<float>("capture-max-megapixels"));
    } catch(...) {}
    try {
        targetW = static_cast<int>(Mod::get()->getSettingValue<int64_t>("capture-target-width"));
    } catch(...) {}

    if (vpW <= 0 || vpH <= 0) {
        // Direct fallback.
        doCapture();
    } else {
        double mp = (static_cast<double>(vpW) * static_cast<double>(vpH)) / 1'000'000.0;
        bool useRerender = false;
        
        if (mode == "render") {
            useRerender = true;
        } else if (mode == "direct") {
            useRerender = false;
        } else {
            // Auto Mode: Smart Quality
            
            // 1. If screen is smaller than target (e.g. 1920), upscale using Rerender
            if (vpW < targetW) {
                useRerender = true;
            }
            // 2. If screen is huge (above maxMP), downscale using Rerender to save resources
            else if (mp > maxMP) {
                useRerender = true;
            }
            // 3. Otherwise (Screen is >= Target and <= MaxMP), use Direct for best performance and native quality
            else {
                useRerender = false;
            }
        }

        if (useRerender) {
            // Ensure we don't upscale ridiculously if targetW is huge, but targetW is usually 1920 or 2560.
            // Also ensure we don't downscale below a reasonable quality if maxMP triggered it.
            
            // If triggered by maxMP, we want to downscale to something safe.
            // If triggered by vpW < targetW, we want to upscale to targetW.
            
            int renderWidth = targetW;
            
            log::info("[FramebufferCapture] Using rerender mode (Screen: {}x{}, Target: {})", vpW, vpH, renderWidth);
            doCaptureRerender(std::max(1280, std::min(7680, renderWidth)), vpW, vpH);
        } else {
            doCapture();
        }
    }
    s_request.active = false;
}

void FramebufferCapture::processDeferredCallbacks() {
    if (s_deferredCallbacks.empty()) {
        return;
    }
    
    log::debug("[FramebufferCapture] Processing {} deferred callbacks", s_deferredCallbacks.size());
    
    for (auto& deferred : s_deferredCallbacks) {
        try {
            if (deferred.callback) {
                // Validate texture before passing it.
                if (deferred.texture) {
                    // Ensure the texture is still valid.
                    try {
                        deferred.texture->getContentSize(); // Quick sanity check
                    } catch (...) {
                        log::error("[FramebufferCapture] Invalid texture detected in deferred callback");
                        deferred.texture = nullptr;
                        deferred.success = false;
                    }
                }
                
                deferred.callback(deferred.success, deferred.texture, deferred.rgbData, deferred.width, deferred.height);
            }
            
            // Release the texture after the callback; retain() if needed.
            if (deferred.texture) {
                try {
                    deferred.texture->release();
                } catch (...) {
                    log::error("[FramebufferCapture] Failed to release texture in deferred callback");
                }
            }
        } catch (const std::exception& e) {
            log::error("[FramebufferCapture] Exception processing deferred callback: {}", e.what());
        } catch (...) {
            log::error("[FramebufferCapture] Unknown exception processing deferred callback");
        }
    }
    
    s_deferredCallbacks.clear();
}

void FramebufferCapture::doCapture() {
    try {
        log::info("[FramebufferCapture] Starting capture (SwapBuffers - final output)");
        
        // Read viewport.
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        int width = viewport[2];
        int height = viewport[3];

        // Read from the back buffer.
        
        std::vector<uint8_t> pixels(width * height * 4); // RGBA
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        
        // Leer RGBA
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        
        glPixelStorei(GL_PACK_ALIGNMENT, 4); // Restaurar

        // Check GL error.
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            log::error("[FramebufferCapture] OpenGL error in glReadPixels: 0x{:X}", error);
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        // Make output opaque.
        for (int i = 0; i < width * height; ++i) {
            pixels[i * 4 + 3] = 255;
        }

        // Flip vertically.
        flipVertical(pixels, width, height, 4);

        // Create preview texture.
        auto texture = new CCTexture2D();
        if (texture && texture->initWithData(pixels.data(), kCCTexture2DPixelFormat_RGBA8888, width, height, CCSize(width, height))) {
            texture->setAntiAliasTexParameters();
            texture->retain();

            // Create RGB buffer for saving.
            size_t rgbSize = width * height * 3;
            std::shared_ptr<uint8_t> rgbBuffer(new uint8_t[rgbSize], std::default_delete<uint8_t[]>());
            
            for (int i = 0; i < width * height; ++i) {
                rgbBuffer.get()[i * 3 + 0] = pixels[i * 4 + 0];
                rgbBuffer.get()[i * 3 + 1] = pixels[i * 4 + 1];
                rgbBuffer.get()[i * 3 + 2] = pixels[i * 4 + 2];
            }

            // Defer callback to avoid releasing on the same stack.
            if (s_request.callback) {
                s_deferredCallbacks.push_back({
                    s_request.callback,
                    true,
                    texture,
                    rgbBuffer,
                    width,
                    height
                });
            }
        } else {
            if (texture) texture->release();
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
        }

    } catch (std::exception const& e) {
        log::error("[FramebufferCapture] Exception: {}", e.what());
        if (s_request.callback) {
            s_request.callback(false, nullptr, nullptr, 0, 0);
        }
    }
}

void FramebufferCapture::doCaptureRerender(int targetWidth, int viewportW, int viewportH) {
    try {
        auto* director = CCDirector::sharedDirector();
        if (!director) {
            log::error("[FramebufferCapture] CCDirector is null during rerender");
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }

        auto* scene = director->getRunningScene();
        if (!scene) {
            log::error("[FramebufferCapture] Scene is null during rerender");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        // Preserve original viewport aspect.
        double aspect = viewportH > 0 ? (static_cast<double>(viewportW) / static_cast<double>(viewportH)) : 16.0 / 9.0;
        int outW = std::max(1, targetWidth);
        int outH = std::max(1, static_cast<int>(std::round(static_cast<double>(outW) / aspect)));

        log::info("[FramebufferCapture] Rerender a {}x{} (aspect {:.4f})", outW, outH, aspect);

        // Render the full scene into a render texture.
        auto* rt = CCRenderTexture::create(outW, outH);
        if (!rt) {
            log::error("[FramebufferCapture] Failed to create CCRenderTexture");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        rt->begin();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        scene->visit();
        rt->end();

        // Read image from render texture (RGBA), already flipped if requested.
        CCImage* img = rt->newCCImage(true);
        if (!img) {
            log::error("[FramebufferCapture] newImage() returned null");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        int W = img->getWidth();
        int H = img->getHeight();
        unsigned char* data = img->getData();
        if (!data || W <= 0 || H <= 0) {
            log::error("[FramebufferCapture] Invalid image data during rerender");
            delete img;
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        // Force alpha to 255
        for (int i = 0; i < W * H; ++i) {
            data[i * 4 + 3] = 255;
        }

        // Create texture for preview.
        auto* texture = new CCTexture2D();
        if (!texture) {
            log::error("[FramebufferCapture] Failed to create CCTexture2D (rerender)");
            delete img;
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        if (!texture->initWithData(
            data,
            kCCTexture2DPixelFormat_RGBA8888,
            W,
            H,
            CCSize(W, H)
        )) {
            log::error("[FramebufferCapture] Failed to initialize texture (rerender)");
            texture->release();
            delete img;
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }
        texture->setAntiAliasTexParameters();
        texture->retain();

        // Crear buffer RGB compartido
        size_t rgbSize = static_cast<size_t>(W) * static_cast<size_t>(H) * 3;
        std::shared_ptr<uint8_t> rgbBuffer(new uint8_t[rgbSize], std::default_delete<uint8_t[]>());
        for (size_t i = 0; i < static_cast<size_t>(W) * static_cast<size_t>(H); ++i) {
            rgbBuffer.get()[i * 3 + 0] = data[i * 4 + 0];
            rgbBuffer.get()[i * 3 + 1] = data[i * 4 + 1];
            rgbBuffer.get()[i * 3 + 2] = data[i * 4 + 2];
        }

        delete img;

        if (s_request.callback) {
            s_deferredCallbacks.push_back({ s_request.callback, true, texture, rgbBuffer, W, H });
        }

        log::info("[FramebufferCapture] Rerender completed successfully {}x{}", W, H);
    } catch (std::exception const& e) {
        log::error("[FramebufferCapture] Exception during rerender: {}", e.what());
        if (s_request.callback) {
            s_deferredCallbacks.push_back({ s_request.callback, false, nullptr, nullptr, 0, 0 });
        }
    }
}

void FramebufferCapture::doCaptureNode(CCNode* node) {
    try {
        log::info("[FramebufferCapture] Capturing specific node");
        
        if (!node) {
            log::error("[FramebufferCapture] Node is null");
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }
        
        // Compute output size.
        auto contentSize = node->getContentSize();
        float scale = node->getScale();
        
        int width = static_cast<int>(contentSize.width * scale);
        int height = static_cast<int>(contentSize.height * scale);
        
        if (width <= 0 || height <= 0) {
            log::error("[FramebufferCapture] Invalid node size: {}x{}", width, height);
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }
        
        log::info("[FramebufferCapture] Capturing node: {}x{}", width, height);
        
        // Create render texture.
        auto* renderTexture = CCRenderTexture::create(width, height);
        if (!renderTexture) {
            log::error("[FramebufferCapture] Failed to create CCRenderTexture");
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }
        
        // Render node into the render texture.
        renderTexture->begin();
        glClearColor(0, 0, 0, 1); // Opaque black background
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Save/restore original transform.
        auto originalPos = node->getPosition();
        auto originalAnchor = node->getAnchorPoint();
        
        // Temporarily center node in the render texture.
        node->setPosition(ccp(width / 2.0f, height / 2.0f));
        node->setAnchorPoint(ccp(0.5f, 0.5f));
        
        node->visit();
        
        node->setPosition(originalPos);
        node->setAnchorPoint(originalAnchor);
        
        renderTexture->end();
        
        // Read image.
        CCImage* img = renderTexture->newCCImage(true); // true = flip vertical
        if (!img) {
            log::error("[FramebufferCapture] newCCImage() returned null");
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }
        
        int W = img->getWidth();
        int H = img->getHeight();
        unsigned char* data = img->getData();
        
        if (!data || W <= 0 || H <= 0) {
            log::error("[FramebufferCapture] Invalid image data");
            delete img;
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }
        
        // Create texture for preview.
        auto* texture = new CCTexture2D();
        if (!texture) {
            log::error("[FramebufferCapture] Failed to create CCTexture2D");
            delete img;
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }
        
        if (!texture->initWithData(
            data,
            kCCTexture2DPixelFormat_RGBA8888,
            W,
            H,
            CCSize(W, H)
        )) {
            log::error("[FramebufferCapture] Failed to initialize texture");
            texture->release();
            delete img;
            if (s_request.callback) {
                s_request.callback(false, nullptr, nullptr, 0, 0);
            }
            return;
        }
        
        texture->setAntiAliasTexParameters();
        texture->retain();
        
        // Create shared RGB buffer.
        size_t rgbSize = static_cast<size_t>(W) * static_cast<size_t>(H) * 3;
        std::shared_ptr<uint8_t> rgbBuffer(new uint8_t[rgbSize], std::default_delete<uint8_t[]>());
        for (size_t i = 0; i < static_cast<size_t>(W) * static_cast<size_t>(H); ++i) {
            rgbBuffer.get()[i * 3 + 0] = data[i * 4 + 0]; // R
            rgbBuffer.get()[i * 3 + 1] = data[i * 4 + 1]; // G
            rgbBuffer.get()[i * 3 + 2] = data[i * 4 + 2]; // B
        }
        
        delete img;
        
        // Defer callback.
        if (s_request.callback) {
            s_deferredCallbacks.push_back({
                s_request.callback,
                true,
                texture,
                rgbBuffer,
                W,
                H
            });
        }
        
        log::info("[FramebufferCapture] Node capture completed successfully: {}x{}", W, H);
        
    } catch (std::exception const& e) {
        log::error("[FramebufferCapture] Exception while capturing node: {}", e.what());
        if (s_request.callback) {
            s_deferredCallbacks.push_back({
                s_request.callback,
                false,
                nullptr,
                nullptr,
                0,
                0
            });
        }
    }
}

void FramebufferCapture::flipVertical(std::vector<uint8_t>& pixels, int width, int height, int channels) {
    int rowSize = width * channels;
    std::vector<uint8_t> temp(rowSize);
    
    for (int y = 0; y < height / 2; ++y) {
        int topRow = y;
        int bottomRow = height - 1 - y;
        
        uint8_t* topPtr = pixels.data() + topRow * rowSize;
        uint8_t* bottomPtr = pixels.data() + bottomRow * rowSize;
        
        // Swap rows
        memcpy(temp.data(), topPtr, rowSize);
        memcpy(topPtr, bottomPtr, rowSize);
        memcpy(bottomPtr, temp.data(), rowSize);
    }
}

std::vector<uint8_t> FramebufferCapture::rgbaToRgb(const std::vector<uint8_t>& rgba, int width, int height) {
    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> rgb(pixelCount * 3);
    
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0]; // R
        rgb[i * 3 + 1] = rgba[i * 4 + 1]; // G
        rgb[i * 3 + 2] = rgba[i * 4 + 2]; // B
    }
    
    return rgb;
}

