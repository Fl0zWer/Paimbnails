#include "ThumbnailAPI.hpp"
#include "ThumbnailLoader.hpp"
#include "ProfileThumbs.hpp"
#include "../utils/ImageConverter.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <fstream>
#include <chrono>

using namespace geode::prelude;

ThumbnailAPI::ThumbnailAPI() {
    m_serverEnabled = true;
    log::info("[ThumbnailAPI] Initialized - Server enabled: {}", m_serverEnabled);
}

void ThumbnailAPI::setServerEnabled(bool enabled) {
    m_serverEnabled = enabled;
    log::info("[ThumbnailAPI] Server mode set to: {}", enabled);
}

void ThumbnailAPI::uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, upload skipped for level {}", levelId);
        callback(false, "Server functionality disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Uploading thumbnail for level {} ({} bytes)", levelId, pngData.size());
    
    HttpClient::get().uploadThumbnail(levelId, pngData, username, [this, callback, levelId](bool success, const std::string& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] Upload successful - Total uploads: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, 
                                   const std::string& mode, const std::string& replaceId, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, upload skipped for level {}", levelId);
        callback(false, "Server functionality disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Uploading thumbnail (mode={}) for level {}", mode, levelId);
    
    HttpClient::get().uploadThumbnail(levelId, pngData, username, mode, replaceId, [this, callback, levelId](bool success, const std::string& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] Upload successful - Total uploads: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, GIF upload skipped for level {}", levelId);
        callback(false, "Server functionality disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Uploading GIF for level {} ({} bytes)", levelId, gifData.size());
    
    HttpClient::get().uploadGIF(levelId, gifData, username, [this, callback, levelId](bool success, const std::string& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] GIF Upload successful - Total uploads: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, 
                             const std::string& mode, const std::string& replaceId, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, GIF upload skipped for level {}", levelId);
        callback(false, "Server functionality disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Uploading GIF (mode={}) for level {}", mode, levelId);
    
    HttpClient::get().uploadGIF(levelId, gifData, username, mode, replaceId, [this, callback, levelId](bool success, const std::string& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] GIF Upload successful - Total uploads: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::getThumbnails(int levelId, ThumbnailListCallback callback) {
    if (!m_serverEnabled) {
        callback(false, {});
        return;
    }
    
    HttpClient::get().getThumbnails(levelId, [callback](bool success, const std::string& response) {
        if (!success) {
            callback(false, {});
            return;
        }
        
        try {
            auto res = matjson::parse(response);
            if (!res.isOk()) {
                callback(false, {});
                return;
            }
            auto json = res.unwrap();
            std::vector<ThumbnailInfo> thumbnails;
            
            if (json.contains("thumbnails") && json["thumbnails"].isArray()) {
                for (const auto& item : json["thumbnails"].asArray().unwrap()) {
                    ThumbnailInfo info;
                    info.id = item["id"].asString().unwrapOr("");
                    info.url = item["url"].asString().unwrapOr("");
                    info.type = item["type"].asString().unwrapOr("");
                    info.format = item["format"].asString().unwrapOr("");
                    thumbnails.push_back(info);
                }
            }
            callback(true, thumbnails);
        } catch (...) {
            callback(false, {});
        }
    });
}

std::string ThumbnailAPI::getThumbnailURL(int levelId) {
    return HttpClient::get().getServerURL() + "/thumbnails/" + std::to_string(levelId) + ".png";
}

void ThumbnailAPI::uploadSuggestion(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, suggestion upload skipped for level {}", levelId);
        callback(false, "Server functionality disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Uploading suggestion for level {} ({} bytes)", levelId, pngData.size());
    
    HttpClient::get().uploadSuggestion(levelId, pngData, username, [this, callback](bool success, const std::string& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] Suggestion upload successful - Total uploads: {}", m_uploadCount);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadUpdate(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, update upload skipped for level {}", levelId);
        callback(false, "Server functionality disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Uploading update for level {} ({} bytes)", levelId, pngData.size());
    
    HttpClient::get().uploadUpdate(levelId, pngData, username, [this, callback, levelId](bool success, const std::string& message) {
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] Update upload successful - Total uploads: {}", m_uploadCount);
            ThumbnailLoader::get().invalidateLevel(levelId);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadProfile(int accountID, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, profile upload skipped for account {}", accountID);
        callback(false, "Server functionality disabled");
        return;
    }
    log::info("[ThumbnailAPI] Uploading profile for account {} ({} bytes)", accountID, pngData.size());
    HttpClient::get().uploadProfile(accountID, pngData, username, [this, callback, accountID](bool success, const std::string& message){
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] Profile upload successful - Total uploads: {}", m_uploadCount);
            ProfileThumbs::get().deleteProfile(accountID);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::uploadProfileGIF(int accountID, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback) {
    if (GJAccountManager::sharedState()->m_accountID <= 0) {
        log::warn("[ThumbnailAPI] User not logged in, upload denied.");
        callback(false, "You must be logged in to upload thumbnails.");
        return;
    }
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, profile GIF upload skipped for account {}", accountID);
        callback(false, "Server functionality disabled");
        return;
    }
    log::info("[ThumbnailAPI] Uploading profile GIF for account {} ({} bytes)", accountID, gifData.size());
    HttpClient::get().uploadProfileGIF(accountID, gifData, username, [this, callback, accountID](bool success, const std::string& message){
        if (success) {
            m_uploadCount++;
            log::info("[ThumbnailAPI] Profile GIF upload successful - Total uploads: {}", m_uploadCount);
            ProfileThumbs::get().deleteProfile(accountID);
        }
        callback(success, message);
    });
}

void ThumbnailAPI::downloadProfile(int accountID, const std::string& username, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, profile download skipped for account {}", accountID);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] Downloading profile for account {} (user: {})", accountID, username);
    
    HttpClient::get().downloadProfile(accountID, username, [this, accountID, callback](bool success, const std::vector<uint8_t>& data, int width, int height) {
        if (!success || data.empty()) {
            log::warn("[ThumbnailAPI] Profile download failed for account {}", accountID);
            callback(false, nullptr);
            return;
        }
        
        // Convert to a texture
        auto texture = webpToTexture(data);
        if (texture) {
            m_downloadCount++;
            log::info("[ThumbnailAPI] Profile download successful for account {}", accountID);
            callback(true, texture);
        } else {
            log::error("[ThumbnailAPI] Failed to create texture from profile data for account {}", accountID);
            callback(false, nullptr);
        }
    });
}



void ThumbnailAPI::uploadProfileConfig(int accountID, const ProfileConfig& config, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "Server disabled");
        return;
    }

    matjson::Value json;
    json["backgroundType"] = config.backgroundType;
    json["blurIntensity"] = config.blurIntensity;
    json["darkness"] = config.darkness;
    json["useGradient"] = config.useGradient;
    
    matjson::Value colorA;
    colorA["r"] = (int)config.colorA.r;
    colorA["g"] = (int)config.colorA.g;
    colorA["b"] = (int)config.colorA.b;
    json["colorA"] = colorA;
    
    matjson::Value colorB;
    colorB["r"] = (int)config.colorB.r;
    colorB["g"] = (int)config.colorB.g;
    colorB["b"] = (int)config.colorB.b;
    json["colorB"] = colorB;

    matjson::Value sepColor;
    sepColor["r"] = (int)config.separatorColor.r;
    sepColor["g"] = (int)config.separatorColor.g;
    sepColor["b"] = (int)config.separatorColor.b;
    json["separatorColor"] = sepColor;
    
    json["separatorOpacity"] = config.separatorOpacity;
    json["widthFactor"] = config.widthFactor; // Serialize widthFactor
    
    std::string jsonStr = json.dump(matjson::NO_INDENTATION);
    log::info("[ThumbnailAPI] Uploading config JSON: {}", jsonStr);
    
    HttpClient::get().uploadProfileConfig(accountID, jsonStr, [callback, accountID](bool success, const std::string& msg) {
        if (success) {
            ProfileThumbs::get().deleteProfile(accountID);
        }
        callback(success, msg);
    });
}

void ThumbnailAPI::downloadProfileConfig(int accountID, std::function<void(bool success, const ProfileConfig& config)> callback) {
    if (!m_serverEnabled) {
        callback(false, ProfileConfig());
        return;
    }
    
    HttpClient::get().downloadProfileConfig(accountID, [callback](bool success, const std::string& response) {
        if (!success || response.empty()) {
            callback(false, ProfileConfig());
            return;
        }
        
        try {
            auto res = matjson::parse(response);
            if (!res) {
                callback(false, ProfileConfig());
                return;
            }
            auto json = res.unwrap();
            
            ProfileConfig config;
            config.hasConfig = true;
            
            if (json.contains("backgroundType")) config.backgroundType = json["backgroundType"].asString().unwrapOr("gradient");
            if (json.contains("blurIntensity")) config.blurIntensity = (float)json["blurIntensity"].asDouble().unwrapOr(3.0);
            if (json.contains("darkness")) config.darkness = (float)json["darkness"].asDouble().unwrapOr(0.2);
            if (json.contains("useGradient")) config.useGradient = json["useGradient"].asBool().unwrapOr(false);
            
            if (json.contains("colorA")) {
                auto c = json["colorA"];
                config.colorA.r = (GLubyte)c["r"].asInt().unwrapOr(255);
                config.colorA.g = (GLubyte)c["g"].asInt().unwrapOr(255);
                config.colorA.b = (GLubyte)c["b"].asInt().unwrapOr(255);
            }
            
            if (json.contains("colorB")) {
                auto c = json["colorB"];
                config.colorB.r = (GLubyte)c["r"].asInt().unwrapOr(255);
                config.colorB.g = (GLubyte)c["g"].asInt().unwrapOr(255);
                config.colorB.b = (GLubyte)c["b"].asInt().unwrapOr(255);
            }

            if (json.contains("separatorColor")) {
                auto c = json["separatorColor"];
                config.separatorColor.r = (GLubyte)c["r"].asInt().unwrapOr(0);
                config.separatorColor.g = (GLubyte)c["g"].asInt().unwrapOr(0);
                config.separatorColor.b = (GLubyte)c["b"].asInt().unwrapOr(0);
            }
            if (json.contains("separatorOpacity")) config.separatorOpacity = json["separatorOpacity"].asInt().unwrapOr(50);
            if (json.contains("widthFactor")) config.widthFactor = (float)json["widthFactor"].asDouble().unwrapOr(0.60);
            
            callback(true, config);
        } catch (...) {
            callback(false, ProfileConfig());
        }
    });
}

void ThumbnailAPI::downloadSuggestion(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, suggestion download skipped for level {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] Downloading suggestion for level {}", levelId);
    
    HttpClient::get().downloadSuggestion(levelId, [this, levelId, callback](bool success, const std::vector<uint8_t>& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] Suggestion download failed for level {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        m_downloadCount++;
        log::info("[ThumbnailAPI] Downloaded suggestion {} bytes for level {}", data.size(), levelId);
        
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::downloadSuggestionImage(const std::string& filename, DownloadCallback callback) {
    if (!m_serverEnabled) {
        callback(false, nullptr);
        return;
    }
    
    std::string url = HttpClient::get().getServerURL() + "/" + filename;
    log::info("[ThumbnailAPI] Downloading suggestion image: {}", url);
    
    HttpClient::get().downloadFromUrl(url, [this, callback](bool success, const std::vector<uint8_t>& data, int w, int h) {
        if (!success || data.empty()) {
            callback(false, nullptr);
            return;
        }
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::downloadUpdate(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, update download skipped for level {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] Downloading update for level {}", levelId);
    
    HttpClient::get().downloadUpdate(levelId, [this, levelId, callback](bool success, const std::vector<uint8_t>& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] Update download failed for level {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        m_downloadCount++;
        log::info("[ThumbnailAPI] Downloaded update {} bytes for level {}", data.size(), levelId);
        
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::downloadReported(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, reported download skipped for level {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] Downloading reported thumbnail for level {}", levelId);
    
    HttpClient::get().downloadReported(levelId, [this, levelId, callback](bool success, const std::vector<uint8_t>& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] Reported thumbnail download failed for level {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        m_downloadCount++;
        log::info("[ThumbnailAPI] Downloaded reported thumbnail {} bytes for level {}", data.size(), levelId);
        
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}



void ThumbnailAPI::getRating(int levelId, const std::string& username, const std::string& thumbnailId, std::function<void(bool success, float average, int count, int userVote)> callback) {
    if (!m_serverEnabled) {
        callback(false, 0, 0, 0);
        return;
    }
    HttpClient::get().getRating(levelId, username, thumbnailId, [callback](bool success, const std::string& response) {
        if (!success) {
            callback(false, 0, 0, 0);
            return;
        }
        try {
            auto jsonRes = matjson::parse(response);
            if (!jsonRes) {
                 callback(false, 0, 0, 0);
                 return;
            }
            auto json = jsonRes.unwrap();
            float average = (float)json["average"].asDouble().unwrapOr(0.0);
            int count = (int)json["count"].asInt().unwrapOr(0);
            int userVote = (int)json["userVote"].asInt().unwrapOr(0);
            callback(true, average, count, userVote);
        } catch (...) {
            callback(false, 0, 0, 0);
        }
    });
}

void ThumbnailAPI::submitVote(int levelId, int stars, const std::string& username, const std::string& thumbnailId, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "Server disabled");
        return;
    }
    HttpClient::get().submitVote(levelId, stars, username, thumbnailId, callback);
}

void ThumbnailAPI::downloadThumbnail(int levelId, DownloadCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, download skipped for level {}", levelId);
        callback(false, nullptr);
        return;
    }
    
    log::info("[ThumbnailAPI] Downloading thumbnail for level {}", levelId);
    
    HttpClient::get().downloadThumbnail(levelId, [this, levelId, callback](bool success, const std::vector<uint8_t>& data, int width, int height) {
        if (!success || data.empty()) {
            log::error("[ThumbnailAPI] Download failed for level {}", levelId);
            callback(false, nullptr);
            return;
        }
        
        m_downloadCount++;
        log::info("[ThumbnailAPI] Downloaded {} bytes for level {} - Total downloads: {}", data.size(), levelId, m_downloadCount);
        
        // Convert data to texture directly (no cache)
        CCTexture2D* texture = webpToTexture(data);
        callback(success, texture);
    });
}

void ThumbnailAPI::checkExists(int levelId, ExistsCallback callback) {
    if (!m_serverEnabled) {
        callback(false);
        return;
    }
    
    HttpClient::get().checkThumbnailExists(levelId, callback);
}

#include <Geode/utils/web.hpp>

void ThumbnailAPI::checkModerator(const std::string& username, ModeratorCallback callback) {
    if (!m_serverEnabled) {
        callback(false, false);
        return;
    }

    // SECURITY: Verify account ownership via GDBrowser
    int currentAccountID = GJAccountManager::sharedState()->m_accountID;
    
    if (currentAccountID <= 0) {
        log::warn("[ThumbnailAPI] Security: User '{}' is not logged in. Moderator check denied.", username);
        callback(false, false);
        return;
    }

    std::string url = fmt::format("https://gdbrowser.com/api/profile/{}", username);

    auto req = web::WebRequest();
    req.get(url).listen([this, username, currentAccountID, callback](web::WebResponse* response) {
        if (!response->ok()) {
            log::warn("[ThumbnailAPI] Security: GDBrowser check failed for '{}'", username);
            callback(false, false);
            return;
        }

        auto data = response->data();
        std::string respStr(data.begin(), data.end());

        try {
            auto jsonRes = matjson::parse(respStr);
            if (!jsonRes.isOk()) {
                 log::warn("[ThumbnailAPI] Security: Invalid JSON from GDBrowser for '{}'", username);
                 callback(false, false);
                 return;
            }
            auto json = jsonRes.unwrap();
            
            if (!json.contains("accountID")) {
                log::warn("[ThumbnailAPI] Security: No accountID found for '{}'", username);
                callback(false, false);
                return;
            }

            std::string accIdStr = json["accountID"].asString().unwrapOr("0");
            int fetchedID = geode::utils::numFromString<int>(accIdStr).unwrapOr(0);

            if (fetchedID != currentAccountID) {
                log::warn("[ThumbnailAPI] Security: Spoof attempt? User '{}' (ID: {}) != Logged in ID: {}", 
                    username, fetchedID, currentAccountID);
                callback(false, false);
                return;
            }

            // Proceed with original check
            HttpClient::get().checkModerator(username, [callback, username](bool isMod, bool isAdmin) {
                try {
                    if (isAdmin) {
                        Mod::get()->setSavedValue<bool>("is-verified-admin", true);
                        auto path = Mod::get()->getSaveDir() / "admin_verification.dat";
                        std::ofstream f(path, std::ios::binary | std::ios::trunc);
                        if (f) {
                            time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                            f.write(reinterpret_cast<const char*>(&now), sizeof(now));
                            f.close();
                        }
                    }
                    if (isMod) {
                        Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
                        auto path = Mod::get()->getSaveDir() / "moderator_verification.dat";
                        std::ofstream f(path, std::ios::binary | std::ios::trunc);
                        if (f) {
                            time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                            f.write(reinterpret_cast<const char*>(&now), sizeof(now));
                            f.close();
                        }
                    }
                } catch (...) {}
                callback(isMod, isAdmin);
            });

        } catch (std::exception& e) {
            log::error("[ThumbnailAPI] Security check error: {}", e.what());
            callback(false, false);
        }
    });
}

void ThumbnailAPI::checkUserStatus(const std::string& username, ModeratorCallback callback) {
    if (!m_serverEnabled) {
        callback(false, false);
        return;
    }
    // This function is for display purposes (badges) and does not require security checks
    // or local side-effects (like writing admin_verification.dat).
    HttpClient::get().checkModerator(username, callback);
}

void ThumbnailAPI::checkModeratorAccount(const std::string& username, int accountID, ModeratorCallback callback) {
    if (!m_serverEnabled) {
        callback(false, false);
        return;
    }
    
    // SECURITY: Use trusted GJAccountManager ID + GDBrowser verification
    int currentAccountID = GJAccountManager::sharedState()->m_accountID;
    
    if (currentAccountID <= 0) {
        log::warn("[ThumbnailAPI] Security: User '{}' is not logged in. Moderator check denied.", username);
        callback(false, false);
        return;
    }

    std::string url = fmt::format("https://gdbrowser.com/api/profile/{}", username);

    auto req = web::WebRequest();
    req.get(url).listen([this, username, currentAccountID, callback](web::WebResponse* response) {
        if (!response->ok()) {
            callback(false, false);
            return;
        }

        auto data = response->data();
        std::string respStr(data.begin(), data.end());

        try {
             auto jsonRes = matjson::parse(respStr);
             if (!jsonRes.isOk()) {
                 callback(false, false);
                 return;
             }
             auto json = jsonRes.unwrap();
             
             if (!json.contains("accountID")) {
                callback(false, false);
                return;
             }

             std::string accIdStr = json["accountID"].asString().unwrapOr("0");
             int fetchedID = std::stoi(accIdStr);

             if (fetchedID != currentAccountID) {
                 log::warn("[ThumbnailAPI] Security: Spoof attempt? User '{}' (ID: {}) != Logged in ID: {}", 
                    username, fetchedID, currentAccountID);
                 callback(false, false);
                 return;
             }

             // Passed. 
             HttpClient::get().checkModeratorAccount(username, currentAccountID, [callback](bool isMod, bool isAdmin){
                try {
                    if (isAdmin) {
                        Mod::get()->setSavedValue<bool>("is-verified-admin", true);
                    }
                    if (isMod) {
                        Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
                    }
                } catch(...) {}
                callback(isMod, isAdmin);
            });

        } catch (...) {
            callback(false, false);
        }
    });
}

void ThumbnailAPI::getThumbnail(int levelId, DownloadCallback callback) {
    log::info("[ThumbnailAPI] Getting thumbnail for level {} (trying local/server)", levelId);
    
    // 1. Try local storage first
    CCTexture2D* localTex = loadFromLocal(levelId);
    if (localTex) {
        log::info("[ThumbnailAPI] Loaded from local storage for level {}", levelId);
        callback(true, localTex);
        return;
    }
    
    // 2. Try downloading from server
    if (m_serverEnabled) {
        log::info("[ThumbnailAPI] Not in local, downloading from server for level {}", levelId);
        downloadThumbnail(levelId, callback);
    } else {
        log::warn("[ThumbnailAPI] Thumbnail not found for level {} and server is disabled", levelId);
        callback(false, nullptr);
    }
}

CCTexture2D* ThumbnailAPI::webpToTexture(const std::vector<uint8_t>& webpData) {
    if (webpData.empty()) return nullptr;

    CCImage* img = new CCImage();
    if (!img) return nullptr;

    try {
        // Treat the data as PNG since our server returns PNG
        if (!img->initWithImageData(const_cast<uint8_t*>(webpData.data()), webpData.size())) {
            log::error("[ThumbnailAPI] Failed to init CCImage from data");
            img->release();
            return nullptr;
        }
        
        auto* tex = new CCTexture2D();
        if (!tex->initWithImage(img)) {
            tex->release();
            img->release();
            log::error("[ThumbnailAPI] Failed to create texture from image");
            return nullptr;
        }
        
        img->release();
        tex->autorelease();
        return tex;
    } catch (std::exception& e) {
        log::error("[ThumbnailAPI] Exception converting data to texture: {}", e.what());
        // Try to release img if it wasn't released yet
        // This is a bit unsafe if img was already released, but in this flow it's linear.
        // If exception happens during initWithImageData or initWithImage, img is still valid.
        // If exception happens after img->release(), we might double free.
        // But initWithImage shouldn't throw C++ exceptions usually.
        return nullptr;
    }
}

CCTexture2D* ThumbnailAPI::loadFromLocal(int levelId) {
    if (!LocalThumbs::get().has(levelId)) {
        return nullptr;
    }
    
    return LocalThumbs::get().loadTexture(levelId);
}

void ThumbnailAPI::syncVerificationQueue(PendingCategory category, QueueCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, using local queue only");
        callback(true, PendingQueue::get().list(category));
        return;
    }
    
    log::info("[ThumbnailAPI] Syncing verification queue for category: {}", (int)category);
    
    std::string endpoint = "/api/queue/";
    switch (category) {
        case PendingCategory::Verify: endpoint += "verify"; break;
        case PendingCategory::Update: endpoint += "update"; break;
        case PendingCategory::Report: endpoint += "report"; break;
    }
    
    HttpClient::get().get(endpoint, [callback, category](bool success, const std::string& response) {
        if (!success) {
            log::error("[ThumbnailAPI] Failed to sync queue from server");
            // Fallback to local queue
            callback(true, PendingQueue::get().list(category));
            return;
        }
        
        // Parse JSON response using matjson
        std::vector<PendingItem> items;
        try {
            auto jsonRes = matjson::parse(response);
            if (!jsonRes) {
                log::warn("[ThumbnailAPI] Failed to parse queue response: invalid JSON");
                callback(true, PendingQueue::get().list(category));
                return;
            }
            auto json = jsonRes.unwrap();

            if (!json.contains("items") || !json["items"].isArray()) {
                log::warn("[ThumbnailAPI] Queue sync: no items array found");
                callback(true, PendingQueue::get().list(category));
                return;
            }

            auto itemsRes = json["items"].asArray();
            if (!itemsRes) {
                log::warn("[ThumbnailAPI] Failed to get items array");
                callback(true, PendingQueue::get().list(category));
                return;
            }

            for (const auto& item : itemsRes.unwrap()) {
                PendingItem it{};
                // levelId field name in server JSON is camelCase
                if (item.contains("levelId")) {
                    if (item["levelId"].isString()) {
                        it.levelID = std::atoi(item["levelId"].asString().unwrapOr("0").c_str());
                    } else if (item["levelId"].isNumber()) {
                        it.levelID = item["levelId"].asInt().unwrapOr(0);
                    }
                }
                
                it.category = category;
                
                // Server timestamp is ms; convert to seconds
                if (item.contains("timestamp")) {
                    long long ms = 0;
                    if (item["timestamp"].isString()) {
                        ms = geode::utils::numFromString<long long>(item["timestamp"].asString().unwrapOr("0")).unwrapOr(0);
                    } else if (item["timestamp"].isNumber()) {
                        ms = (long long)item["timestamp"].asDouble().unwrapOr(0.0);
                    }
                    it.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                }
                
                if (item.contains("submittedBy") && item["submittedBy"].isString()) {
                    it.submittedBy = item["submittedBy"].asString().unwrapOr("");
                }
                
                if (item.contains("note") && item["note"].isString()) {
                    it.note = item["note"].asString().unwrapOr("");
                }

                if (item.contains("claimedBy") && item["claimedBy"].isString()) {
                    it.claimedBy = item["claimedBy"].asString().unwrapOr("");
                }
                
                it.status = PendingStatus::Open;
                it.isCreator = false; // Server doesn't currently send this, default to false
                
                // Parse suggestions array if present (for multi-suggestion support)
                if (item.contains("suggestions") && item["suggestions"].isArray()) {
                    for (const auto& sug : item["suggestions"].asArray().unwrap()) {
                        Suggestion s;
                        if (sug.contains("filename") && sug["filename"].isString()) {
                            s.filename = sug["filename"].asString().unwrapOr("");
                        }
                        if (sug.contains("submittedBy") && sug["submittedBy"].isString()) {
                            s.submittedBy = sug["submittedBy"].asString().unwrapOr("");
                        }
                        if (sug.contains("timestamp")) {
                            long long ms = 0;
                            if (sug["timestamp"].isNumber()) {
                                ms = (long long)sug["timestamp"].asDouble().unwrapOr(0.0);
                            }
                            s.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                        }
                        if (sug.contains("accountID") && sug["accountID"].isNumber()) {
                            s.accountID = sug["accountID"].asInt().unwrapOr(0);
                        }
                        it.suggestions.push_back(s);
                    }
                } else if (it.category == PendingCategory::Verify) {
                    // Legacy support: create a single suggestion from the item itself
                    Suggestion s;
                    s.filename = fmt::format("suggestions/{}.webp", it.levelID);
                    s.submittedBy = it.submittedBy;
                    s.timestamp = it.timestamp;
                    // accountID might be missing in legacy item root, but that's fine
                    it.suggestions.push_back(s);
                }

                if (it.levelID != 0) items.push_back(std::move(it));
            }
        } catch (std::exception& e) {
            log::warn("[ThumbnailAPI] Failed to parse queue response: {}; using local list", e.what());
            callback(true, PendingQueue::get().list(category));
            return;
        } catch (...) {
            log::warn("[ThumbnailAPI] Failed to parse queue response (unknown error); using local list");
            callback(true, PendingQueue::get().list(category));
            return;
        }
        callback(true, items);
    });
}

void ThumbnailAPI::claimQueueItem(int levelId, PendingCategory category, const std::string& username, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, cannot claim");
        callback(false, "Server disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Claiming queue item {} in category {} by {}", levelId, (int)category, username);
    
    std::string endpoint = fmt::format("/api/queue/claim/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username}
    });
    std::string postData = json.dump();
    
    HttpClient::get().post(endpoint, postData, [callback, levelId, username](bool success, const std::string& response) {
        if (success) {
            log::info("[ThumbnailAPI] Queue item claimed on server by {}: {}", username, levelId);
        } else {
            log::error("[ThumbnailAPI] Failed to claim on server: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::acceptQueueItem(int levelId, PendingCategory category, const std::string& username, ActionCallback callback, const std::string& targetFilename) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, accepting locally only");
        PendingQueue::get().accept(levelId, category);
        callback(true, "Accepted locally");
        return;
    }
    
    log::info("[ThumbnailAPI] Accepting queue item {} in category {}", levelId, (int)category);
    
    std::string endpoint = fmt::format("/api/queue/accept/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username}
    });
    if (!targetFilename.empty()) {
        json["targetFilename"] = targetFilename;
    }
    std::string postData = json.dump();
    
    HttpClient::get().post(endpoint, postData, [callback, levelId, category](bool success, const std::string& response) {
        if (success) {
            PendingQueue::get().accept(levelId, category);
            log::info("[ThumbnailAPI] Queue item accepted on server: {}", levelId);
        } else {
            log::error("[ThumbnailAPI] Failed to accept on server: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::rejectQueueItem(int levelId, PendingCategory category, const std::string& username, const std::string& reason, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, rejecting locally only");
        PendingQueue::get().reject(levelId, category, reason);
        callback(true, "Rejected locally");
        return;
    }
    
    log::info("[ThumbnailAPI] Rejecting queue item {} in category {}", levelId, (int)category);
    
    std::string endpoint = fmt::format("/api/queue/reject/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"reason", reason}
    });
    std::string postData = json.dump();
    
    HttpClient::get().post(endpoint, postData, [callback, levelId, category, reason](bool success, const std::string& response) {
        if (success) {
            PendingQueue::get().reject(levelId, category, reason);
            log::info("[ThumbnailAPI] Queue item rejected on server: {}", levelId);
        } else {
            log::error("[ThumbnailAPI] Failed to reject on server: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::submitReport(int levelId, const std::string& username, const std::string& note, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, submitting report locally only");
        PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
        callback(true, "Reported locally");
        return;
    }
    
    log::info("[ThumbnailAPI] Submitting report for level {}", levelId);
    
    // Use the newer dedicated HttpClient method
    HttpClient::get().submitReport(levelId, username, note, [callback, levelId, username, note](bool success, const std::string& response) {
        if (success) {
            PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
            log::info("[ThumbnailAPI] Report submitted to server: {}", levelId);
        } else {
            log::error("[ThumbnailAPI] Failed to submit report: {}", response);
        }
        callback(success, response);
    });
}

void ThumbnailAPI::addModerator(const std::string& username, const std::string& adminUser, ActionCallback callback) {
    if (!m_serverEnabled) {
        callback(false, "Server disabled");
        return;
    }

    std::string endpoint = "/api/admin/add-moderator";
    
    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser}
    });
    std::string payload = json.dump();

    HttpClient::get().post(endpoint, payload, [callback](bool success, const std::string& response) {
        if (success) {
            callback(true, "Moderator added successfully");
        } else {
            callback(false, response);
        }
    });
}

void ThumbnailAPI::deleteThumbnail(int levelId, const std::string& username, ActionCallback callback) {
    if (!m_serverEnabled) {
        log::warn("[ThumbnailAPI] Server disabled, cannot delete from server");
        callback(false, "Server disabled");
        return;
    }
    
    log::info("[ThumbnailAPI] Deleting thumbnail {} from server", levelId);
    
    std::string endpoint = fmt::format("/api/thumbnails/delete/{}", levelId);
    
    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"levelId", levelId}
    });
    std::string postData = json.dump();
    
    HttpClient::get().post(endpoint, postData, [callback, levelId](bool success, const std::string& response) {
        if (success) {
            log::info("[ThumbnailAPI] Thumbnail deleted from server: {}", levelId);
            ThumbnailLoader::get().invalidateLevel(levelId);
            callback(true, "Thumbnail deleted successfully");
        } else {
            log::error("[ThumbnailAPI] Failed to delete thumbnail from server: {}", response);
            callback(false, response);
        }
    });
}



void ThumbnailAPI::downloadFromUrl(const std::string& url, DownloadCallback callback) {
    HttpClient::get().downloadFromUrl(url, [this, callback](bool success, const std::vector<uint8_t>& data, int w, int h) {
        if (success) {
            auto texture = webpToTexture(data);
            callback(success, texture);
        } else {
            callback(false, nullptr);
        }
    });
}

void ThumbnailAPI::downloadFromUrlData(const std::string& url, DownloadDataCallback callback) {
    HttpClient::get().downloadFromUrl(url, [callback](bool success, const std::vector<uint8_t>& data, int w, int h) {
        callback(success, data);
    });
}

 
