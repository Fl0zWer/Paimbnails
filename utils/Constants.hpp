#pragma once

#include <cstdint>

/**
 * Centralized constants to eliminate magic numbers throughout the codebase.
 */
namespace PaimonConstants {
    // UI Layout Constants
    constexpr float BORDER_THICKNESS = 6.0f;
    constexpr float DEFAULT_THUMB_WIDTH_FACTOR = 0.65f;
    constexpr float MIN_THUMB_WIDTH_FACTOR = 0.2f;
    constexpr float MAX_THUMB_WIDTH_FACTOR = 0.95f;
    
    // Thumbnail Generation Constants
    constexpr int MIN_BLUR_TEXTURE_SIZE = 24;
    constexpr int DEFAULT_BLUR_TARGET_WIDTH = 96;
    constexpr int MIN_TARGET_WIDTH = 160;
    constexpr int MAX_TARGET_WIDTH = 480;
    constexpr int BASE_TARGET_WIDTH = 480;
    constexpr float MIN_BLUR_STRENGTH = 0.01f;
    constexpr float MAX_BLUR_STRENGTH = 2.0f;
    constexpr float BLUR_SCALE_FACTOR = 1.0f;
    
    // Cache Constants
    constexpr int DEFAULT_CACHE_MAX_AGE_HOURS = 24;
    constexpr int CACHE_CLEANUP_AGE_HOURS = 168; // 7 days
    constexpr size_t MAX_MEMORY_CACHE_SIZE = 50;
    
    // Moderator Verification Constants
    constexpr int MODERATOR_VERIFICATION_EXPIRY_DAYS = 30;
    constexpr int MODERATOR_CACHE_DURATION_MINUTES = 10;
    
    // Network Constants
    constexpr int DEFAULT_MAX_CONCURRENT_DOWNLOADS = 4;  // Increased for better parallel loading
    constexpr int MIN_CONCURRENT_DOWNLOADS = 1;
    constexpr int MAX_CONCURRENT_DOWNLOADS = 8;  // Increased limit
    
    // Animation Constants
    constexpr float THUMBNAIL_FADE_IN_DURATION = 0.25f;
    constexpr float LOADING_SPINNER_ROTATION_SPEED = 360.0f;
    constexpr uint8_t THUMBNAIL_FADE_START_OPACITY = 0;
    constexpr uint8_t THUMBNAIL_FADE_END_OPACITY = 255;
    
    // Color Constants
    constexpr uint8_t DARK_OVERLAY_ALPHA = 128;
    constexpr uint8_t FULLY_OPAQUE = 255;
    constexpr float DEFAULT_DARK_INTENSITY = 0.5f;
    constexpr float DARK_INTENSITY_MULTIPLIER = 180.0f;
    
    // Image Processing Constants
    constexpr int BASE64_DECODE_TABLE_SIZE = 256;
    constexpr int COLOR_QUANTIZE_DEFAULT = 8;
    constexpr uint8_t UI_BLACK_THRESHOLD = 10;
    constexpr uint8_t UI_WHITE_THRESHOLD = 245;
    
    // File Size Limits
    constexpr size_t MAX_ERROR_BODY_SIZE = 1000;
    constexpr size_t RGB_BYTES_PER_PIXEL = 3;
    constexpr size_t RGBA_BYTES_PER_PIXEL = 4;
}

