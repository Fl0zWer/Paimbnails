#include "AsyncFramebufferCapture.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <cstring>

using namespace geode::prelude;

AsyncFramebufferCapture::AsyncFramebufferCapture()
    : m_pbo{0, 0}
    , m_currentPBO(0)
    , m_pboInitialized(false)
    , m_captureInProgress(false)
    , m_captureWidth(0)
    , m_captureHeight(0)
    , m_capturedFramebuffer(0)
{
}

AsyncFramebufferCapture::~AsyncFramebufferCapture() {
    cleanup();
}

void AsyncFramebufferCapture::cleanup() {
    if (m_pboInitialized) {
        log::debug("[AsyncCapture] Cleaning up PBOs");
        glDeleteBuffers(2, m_pbo);
        m_pbo[0] = m_pbo[1] = 0;
        m_pboInitialized = false;
    }
    m_captureInProgress = false;
}

bool AsyncFramebufferCapture::initializePBOs(int width, int height) {
    if (width <= 0 || height <= 0) {
        log::error("[AsyncCapture] Invalid dimensions: {}x{}", width, height);
        return false;
    }
    
    // Remove previous PBOs if any.
    if (m_pboInitialized) {
        glDeleteBuffers(2, m_pbo);
    }
    
    // Create new PBOs.
    glGenBuffers(2, m_pbo);
    
    size_t bufferSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4; // RGBA
    
    for (int i = 0; i < 2; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[i]);
        
        // GL_STREAM_READ: data written by GPU, read by CPU.
        glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
        
        // Check errors.
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            log::error("[AsyncCapture] Failed to create PBO {}: 0x{:X}", i, err);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glDeleteBuffers(2, m_pbo);
            m_pbo[0] = m_pbo[1] = 0;
            return false;
        }
    }
    
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    
    m_pboInitialized = true;
    m_captureWidth = width;
    m_captureHeight = height;
    
    log::info("[AsyncCapture] PBOs initialized: {}x{} ({:.2f} MB each)", 
             width, height, bufferSize / (1024.0 * 1024.0));
    
    return true;
}

bool AsyncFramebufferCapture::arePBOsCompatible(int width, int height) const {
    return m_pboInitialized && 
           m_captureWidth == width && 
           m_captureHeight == height;
}

bool AsyncFramebufferCapture::recreatePBOs(int width, int height) {
    log::info("[AsyncCapture] Recreating PBOs for new size: {}x{}", width, height);
    cleanup();
    return initializePBOs(width, height);
}

bool AsyncFramebufferCapture::requestAsyncCapture(int width, int height) {
    // Basic OpenGL sanity checks.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        log::error("[AsyncCapture] Invalid viewport: {}x{}", viewport[2], viewport[3]);
        return false;
    }
    
    // Don't start a new capture if one is already in progress.
    if (m_captureInProgress) {
        log::warn("[AsyncCapture] Capture already in progress, ignoring new request");
        return false;
    }
    
    // Initialize/recreate PBOs if needed.
    if (!arePBOsCompatible(width, height)) {
        if (!recreatePBOs(width, height)) {
            return false;
        }
    }
    
    // Save currently bound framebuffer.
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_capturedFramebuffer);
    
    // Ensure we read from the correct buffer.
    if (m_capturedFramebuffer == 0) {
        // Default framebuffer: read from back buffer.
        glReadBuffer(GL_BACK);
    } else {
        // Custom FBO: read from color attachment 0.
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    
    // Bind current PBO.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[m_currentPBO]);
    
    // Starts GPU->PBO transfer and returns immediately.
    // Last parameter 0 means the output goes to the bound PBO (not CPU memory).
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    
    // Check errors.
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        log::error("[AsyncCapture] glReadPixels failed: 0x{:X}", err);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        return false;
    }
    
    // Unbind PBO.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    
    m_captureInProgress = true;
    
    log::debug("[AsyncCapture] Async capture requested: {}x{} -> PBO[{}]", 
              width, height, m_currentPBO);
    
    return true;
}

bool AsyncFramebufferCapture::tryGetCapturedData(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight) {
    if (!m_captureInProgress) {
        return false;
    }
    
    if (!m_pboInitialized) {
        log::error("[AsyncCapture] PBOs not initialized");
        m_captureInProgress = false;
        return false;
    }
    
    // Bind the PBO that contains the capture data.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[m_currentPBO]);
    
    // Validate buffer size.
    GLint bufferSize = 0;
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    
    size_t expectedSize = static_cast<size_t>(m_captureWidth) * 
                         static_cast<size_t>(m_captureHeight) * 4;
    
    if (bufferSize != static_cast<GLint>(expectedSize)) {
        log::error("[AsyncCapture] Buffer size mismatch: {} vs {}", bufferSize, expectedSize);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        m_captureInProgress = false;
        return false;
    }
    
    // Map buffer for read-only access.
    void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    
    if (!ptr) {
        GLenum err = glGetError();
        log::error("[AsyncCapture] Failed to map PBO: 0x{:X}", err);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        m_captureInProgress = false;
        return false;
    }
    
    // Copy data from PBO into output.
    outPixels.resize(expectedSize);
    std::memcpy(outPixels.data(), ptr, expectedSize);
    
    // Unmap buffer
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    
    // Output dimensions.
    outWidth = m_captureWidth;
    outHeight = m_captureHeight;
    
    // Swap PBO for next frame.
    m_currentPBO = (m_currentPBO + 1) % 2;
    
    m_captureInProgress = false;
    
    log::debug("[AsyncCapture] Capture data retrieved successfully: {}x{} ({:.2f} KB)", 
              outWidth, outHeight, expectedSize / 1024.0);
    
    return true;
}

void AsyncFramebufferCapture::cancelPending() {
    if (m_captureInProgress) {
        log::info("[AsyncCapture] Cancelling pending capture");
        m_captureInProgress = false;
    }
}
