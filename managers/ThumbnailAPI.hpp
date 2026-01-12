#pragma once

#include <Geode/Geode.hpp>
#include "../utils/HttpClient.hpp"
#include "../utils/ThumbnailTypes.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../managers/PendingQueue.hpp"
#include "../managers/ProfileThumbs.hpp"
#include <functional>
#include <string>

/**
 * ThumbnailAPI - Singleton manager for thumbnail operations
 * Handles upload/download with server, caching, and fallback to local storage
 */
class ThumbnailAPI {
public:
    using UploadCallback = std::function<void(bool success, const std::string& message)>;
    using DownloadCallback = std::function<void(bool success, cocos2d::CCTexture2D* texture)>;
    using DownloadDataCallback = std::function<void(bool success, const std::vector<uint8_t>& data)>;
    using ExistsCallback = std::function<void(bool exists)>;
    using ModeratorCallback = std::function<void(bool isModerator, bool isAdmin)>;
    using QueueCallback = std::function<void(bool success, const std::vector<PendingItem>& items)>;
    using ActionCallback = std::function<void(bool success, const std::string& message)>;

    using ThumbnailInfo = ::ThumbnailInfo;
    using ThumbnailListCallback = std::function<void(bool success, const std::vector<ThumbnailInfo>& thumbnails)>;

    static ThumbnailAPI& get() {
        static ThumbnailAPI instance;
        return instance;
    }

    // Main API functions
    
    /**
     * Get list of thumbnails for a level
     * @param levelId Level ID
     * @param callback Callback with list of thumbnails
     */
    void getThumbnails(int levelId, ThumbnailListCallback callback);

    /**
     * Get URL for a level's thumbnail
     * @param levelId Level ID
     * @return URL string
     */
    std::string getThumbnailURL(int levelId);

    /**
     * Upload thumbnail to server
     * @param levelId Level ID
     * @param pngData PNG image data
     * @param username Username of uploader
     * @param callback Callback with success status and message
     */
    void uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    void uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, 
                         const std::string& mode, const std::string& replaceId, UploadCallback callback);

    // Upload GIF thumbnail (Mod/Admin only)
    void uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);
    void uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, 
                   const std::string& mode, const std::string& replaceId, UploadCallback callback);

    // Upload suggestion (non-moderator) to /suggestions
    void uploadSuggestion(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // Upload update proposal (non-moderator) to /updates
    void uploadUpdate(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // Upload profile image by accountID
    void uploadProfile(int accountID, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // Upload profile GIF by accountID
    void uploadProfileGIF(int accountID, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);
    // Download profile image by accountID
    void downloadProfile(int accountID, const std::string& username, DownloadCallback callback);

    // Download image from arbitrary URL
    void downloadFromUrl(const std::string& url, DownloadCallback callback);
    // Download image data from arbitrary URL
    void downloadFromUrlData(const std::string& url, DownloadDataCallback callback);


    
    // Upload profile config
    void uploadProfileConfig(int accountID, const ProfileConfig& config, ActionCallback callback);
    // Download profile config
    void downloadProfileConfig(int accountID, std::function<void(bool success, const ProfileConfig& config)> callback);

    // Download suggestion thumbnail from /suggestions
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // Download specific suggestion image by filename
    void downloadSuggestionImage(const std::string& filename, DownloadCallback callback);
    // Download update thumbnail from /updates
    void downloadUpdate(int levelId, DownloadCallback callback);
    // Download reported thumbnail (current official)
    void downloadReported(int levelId, DownloadCallback callback);


    // Rating System
    void getRating(int levelId, const std::string& username, const std::string& thumbnailId, std::function<void(bool success, float average, int count, int userVote)> callback);
    void submitVote(int levelId, int stars, const std::string& username, const std::string& thumbnailId, ActionCallback callback);

    /**
     * Download thumbnail from server (with caching)
     * @param levelId Level ID
     * @param callback Callback with success status and texture
     */
    void downloadThumbnail(int levelId, DownloadCallback callback);
    
    /**
     * Check if thumbnail exists on server
     * @param levelId Level ID
     * @param callback Callback with exists status
     */
    void checkExists(int levelId, ExistsCallback callback);
    
    /**
     * Check if user is moderator
     * @param username Username to check
     * @param callback Callback with moderator status
     */
    void checkModerator(const std::string& username, ModeratorCallback callback);
    // Secure moderator check with accountID > 0 enforcement
    void checkModeratorAccount(const std::string& username, int accountID, ModeratorCallback callback);
    
    /**
     * Check moderator status of ANY user (Public check for profiles/comments)
     * Does NOT perform security verification of the current user.
     */
    void checkUserStatus(const std::string& username, ModeratorCallback callback);

    /**
     * Get thumbnail texture (tries cache, local, then server)
     * @param levelId Level ID
     * @param callback Callback with texture (or nullptr if not found)
     */
    void getThumbnail(int levelId, DownloadCallback callback);
    
    /**
     * Sync verification queue with server
     * @param category Category to sync (Verify, Update, Report)
     * @param callback Callback with items from server
     */
    void syncVerificationQueue(PendingCategory category, QueueCallback callback);
    
    /**
     * Claim item from verification queue (mark as being reviewed)
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void claimQueueItem(int levelId, PendingCategory category, const std::string& username, ActionCallback callback);
    
    /**
     * Accept item from verification queue
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void acceptQueueItem(int levelId, PendingCategory category, const std::string& username, ActionCallback callback, const std::string& targetFilename = "");
    
    /**
     * Reject item from verification queue
     * @param levelId Level ID
     * @param category Category
     * @param username Moderator username
     * @param reason Rejection reason
     * @param callback Callback with success status
     */
    void rejectQueueItem(int levelId, PendingCategory category, const std::string& username, const std::string& reason, ActionCallback callback);
    
    /**
     * Submit report to server
     * @param levelId Level ID
     * @param username Reporter username
     * @param note Report reason
     * @param callback Callback with success status
     */
    void submitReport(int levelId, const std::string& username, const std::string& note, ActionCallback callback);
    
    /**
     * Add moderator (admin only)
     * @param username Username to add
     * @param adminUser Admin username
     * @param callback Callback with success status
     */
    void addModerator(const std::string& username, const std::string& adminUser, ActionCallback callback);
    
    /**
     * Delete thumbnail from server (moderator only)
     * @param levelId Level ID
     * @param username Moderator username
     * @param callback Callback with success status
     */
    void deleteThumbnail(int levelId, const std::string& username, ActionCallback callback);
    
    // Configuration
    void setServerEnabled(bool enabled);
    bool isServerEnabled() const { return m_serverEnabled; }
    
    // Stats
    int getUploadCount() const { return m_uploadCount; }
    int getDownloadCount() const { return m_downloadCount; }

    // Helper to convert data to CCTexture2D
    cocos2d::CCTexture2D* webpToTexture(const std::vector<uint8_t>& webpData);

private:
    ThumbnailAPI();
    ~ThumbnailAPI() = default;
    
    ThumbnailAPI(const ThumbnailAPI&) = delete;
    ThumbnailAPI& operator=(const ThumbnailAPI&) = delete;

    bool m_serverEnabled = true;
    int m_uploadCount = 0;
    int m_downloadCount = 0;
    
    // Helper to load from local storage
    cocos2d::CCTexture2D* loadFromLocal(int levelId);
};

