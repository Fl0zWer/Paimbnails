#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <memory>

namespace cocos2d {
    class CCTexture2D;
}

/**
 * Captures the main OpenGL framebuffer.
 * Captures what is visible on-screen (including rotations/effects).
 */
class FramebufferCapture {
public:
    // Request a capture on the next frame.
    // Callback receives: (success, texture, rgbData, width, height)
    static void requestCapture(
        int levelID, 
        std::function<void(bool success, cocos2d::CCTexture2D* texture, std::shared_ptr<uint8_t> rgbData, int width, int height)> callback,
        cocos2d::CCNode* nodeToCapture = nullptr
    );
    
    // Cancel a pending capture.
    static void cancelPending();
    
    // Called from the CCDirector hook to perform the capture.
    static void executeIfPending();
    
    // Returns whether a capture is pending.
    static bool hasPendingCapture();
    
    // Process deferred callbacks (call after the full frame).
    static void processDeferredCallbacks();
    
private:
    struct CaptureRequest {
        int levelID;
        std::function<void(bool, cocos2d::CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback;
        cocos2d::CCNode* nodeToCapture = nullptr;
        bool active = false;
    };
    
    struct DeferredCallback {
        std::function<void(bool, cocos2d::CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback;
        bool success;
        cocos2d::CCTexture2D* texture;
        std::shared_ptr<uint8_t> rgbData;
        int width;
        int height;
    };
    
    static CaptureRequest s_request;
    static std::vector<DeferredCallback> s_deferredCallbacks;
    // Perform the actual framebuffer capture.
    static void doCapture();
    // Capture a specific node into a CCRenderTexture.
    static void doCaptureNode(cocos2d::CCNode* node);
    // Re-render the scene into a render texture at a target size.
    static void doCaptureRerender(int targetWidth, int viewportW, int viewportH);
    
    // Vertical flip (OpenGL origin is bottom-left).
    static void flipVertical(std::vector<uint8_t>& pixels, int width, int height, int channels);
    
    // Convert RGBA to RGB.
    static std::vector<uint8_t> rgbaToRgb(const std::vector<uint8_t>& rgba, int width, int height);
};

