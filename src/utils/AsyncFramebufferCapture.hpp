#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <Geode/cocos/platform/CCGL.h>

namespace cocos2d {
    class CCTexture2D;
}

/**
 * Asynchronous framebuffer capture using PBOs (Pixel Buffer Objects).
 *
 * Notes:
 * - Avoids blocking the main thread (reduces stutter).
 * - GPU transfers data in the background.
 * - Data is typically available in 1–2 frames.
 *
 * Usage:
 * - Frame N:   requestAsyncCapture() -> starts GPU->PBO transfer
 * - Frame N+1: tryGetCapturedData()  -> retrieves data if ready
 */
class AsyncFramebufferCapture {
public:
    AsyncFramebufferCapture();
    ~AsyncFramebufferCapture();
    
    // Non-copyable.
    AsyncFramebufferCapture(const AsyncFramebufferCapture&) = delete;
    AsyncFramebufferCapture& operator=(const AsyncFramebufferCapture&) = delete;
    
    /**
     * Request an async capture of the current framebuffer.
     * Starts GPU->PBO transfer and returns immediately.
     *
     * @return true if the capture was started successfully.
     */
    bool requestAsyncCapture(int width, int height);
    
    /**
     * Try to retrieve data from the previous capture (non-blocking).
     * Returns false immediately if not ready.
     *
     * @param outPixels Output buffer (RGBA)
     * @param outWidth Output width
     * @param outHeight Output height
     * @return true if data is ready and was copied.
     */
    bool tryGetCapturedData(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight);
    
    /**
     * Returns whether a capture is pending.
     */
    bool hasPendingCapture() const { return m_captureInProgress; }
    
    /**
     * Cancel a pending capture.
     */
    void cancelPending();
    
    /**
     * Free OpenGL resources (call before destroying).
     */
    void cleanup();

private:
    // Double buffering: while one PBO is written by GPU, the other can be read by CPU.
    GLuint m_pbo[2];
    int m_currentPBO;
    bool m_pboInitialized;
    
    // Capture state.
    bool m_captureInProgress;
    int m_captureWidth;
    int m_captureHeight;
    
    // Framebuffer bound at capture time.
    GLint m_capturedFramebuffer;
    
    /**
     * Initialize PBOs for the given size.
     */
    bool initializePBOs(int width, int height);
    
    /**
     * Check whether existing PBOs match the requested size.
     */
    bool arePBOsCompatible(int width, int height) const;
    
    /**
     * Recreate PBOs if the size changed.
     */
    bool recreatePBOs(int width, int height);
};


