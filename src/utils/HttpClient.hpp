#pragma once

#include <Geode/Geode.hpp>
#include "ThumbnailTypes.hpp"
#include <string>
#include <functional>
#include <vector>
#include <memory>

class HttpClient {
public:
    using UploadCallback = std::function<void(bool success, const std::string& message)>;
    using DownloadCallback = std::function<void(bool success, const std::vector<uint8_t>& data, int width, int height)>;
    using CheckCallback = std::function<void(bool exists)>;
    using ModeratorCallback = std::function<void(bool isModerator, bool isAdmin)>;
    using GenericCallback = std::function<void(bool success, const std::string& response)>;
    using BanListCallback = std::function<void(bool success, const std::string& jsonData)>;
    using BanUserCallback = std::function<void(bool success, const std::string& message)>;
    using ModeratorsListCallback = std::function<void(bool success, const std::vector<std::string>& moderators)>;

    static HttpClient& get() {
        static HttpClient instance;
        return instance;
    }

    std::string getServerURL() const { return m_serverURL; }

    // Upload PNG thumbnail to server
    void uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    void uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, 
                         const std::string& mode, const std::string& replaceId, UploadCallback callback);

    // Upload GIF thumbnail to server (Mod/Admin only)
    void uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);
    void uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, 
                   const std::string& mode, const std::string& replaceId, UploadCallback callback);

    // Get list of thumbnails
    void getThumbnails(int levelId, GenericCallback callback);

    // Upload suggestion thumbnail (non-moderator) to /suggestions
    void uploadSuggestion(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // Upload update proposal (non-moderator) to /updates
    void uploadUpdate(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // Download suggestion thumbnail from /suggestions
    void downloadSuggestion(int levelId, DownloadCallback callback);
    // Download update thumbnail from /updates
    void downloadUpdate(int levelId, DownloadCallback callback);
    // Download reported thumbnail (current official thumbnail)
    void downloadReported(int levelId, DownloadCallback callback);

    // Upload profile image by accountID to /profiles path
    void uploadProfile(int accountID, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback);
    // Upload profile GIF by accountID (Mod/Admin/Donator only)
    void uploadProfileGIF(int accountID, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback);
    // Download profile image by accountID from /profiles path
    void downloadProfile(int accountID, const std::string& username, DownloadCallback callback);
    // Download image from arbitrary URL
    void downloadFromUrl(const std::string& url, DownloadCallback callback);
    
    // Upload profile config
    void uploadProfileConfig(int accountID, const std::string& jsonConfig, GenericCallback callback);
    // Download profile config
    void downloadProfileConfig(int accountID, GenericCallback callback);

    // Download thumbnail from server (respects mod setting 'thumbnail-priority')
    void downloadThumbnail(int levelId, DownloadCallback callback);
    void downloadThumbnail(int levelId, bool isGif, DownloadCallback callback);
    
    // Check if thumbnail exists on server
    void checkThumbnailExists(int levelId, CheckCallback callback);
    
    // Check if user is moderator
    void checkModerator(const std::string& username, ModeratorCallback callback);
    // Check if user is moderator with accountID (prefer this for security)
    void checkModeratorAccount(const std::string& username, int accountID, ModeratorCallback callback);

    // Reports
    void submitReport(int levelId, const std::string& username, const std::string& note, GenericCallback callback);

    // Moderation: list of banned users
    void getBanList(BanListCallback callback);

    // Moderation: ban a user by username (admin/moderator only)
    void banUser(const std::string& username, const std::string& reason, BanUserCallback callback);
    // Moderation: unban a user
    void unbanUser(const std::string& username, BanUserCallback callback);

    // Get list of moderators
    void getModerators(ModeratorsListCallback callback);
    
    // Rating System
    void getRating(int levelId, const std::string& username, const std::string& thumbnailId, GenericCallback callback);
    void submitVote(int levelId, int stars, const std::string& username, const std::string& thumbnailId, GenericCallback callback);
    
    // Generic GET and POST requests
    void get(const std::string& endpoint, GenericCallback callback);
    void post(const std::string& endpoint, const std::string& data, GenericCallback callback);
    
    // Set server URL
    void setServerURL(const std::string& url);

private:
    HttpClient();
    ~HttpClient() = default;
    
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    std::string m_serverURL;
    std::string m_apiKey;
    
    // Thumbnail existence cache (avoids repeat requests)
    struct ExistsCacheEntry {
        bool exists;
        time_t timestamp;
    };
    std::map<int, ExistsCacheEntry> m_existsCache;
    static constexpr int EXISTS_CACHE_DURATION = 300; // 5 minutes
    
    // Helper to perform async web requests
    void performRequest(
        const std::string& url,
        const std::string& method,
        const std::string& postData,
        const std::vector<std::string>& headers,
        std::function<void(bool, const std::string&)> callback
    );
    
    // Helper to perform file upload
    void performUpload(
        const std::string& url,
        const std::string& fieldName,
        const std::string& filename,
        const std::vector<uint8_t>& data,
        const std::vector<std::pair<std::string, std::string>>& formFields,
        const std::vector<std::string>& headers,
        std::function<void(bool, const std::string&)> callback,
        const std::string& contentType = "image/png"
    );
};

