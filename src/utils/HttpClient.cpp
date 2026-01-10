#include "HttpClient.hpp"
#include "Debug.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <ctime>
#include <map>

using namespace geode::prelude;

namespace {
constexpr int kUnboundAccountID = 0; // Geode doesn't expose accountID in public bindings

void handleResponse(web::WebResponse* response, std::function<void(bool, const std::string&)> callback) {
    if (response->ok()) {
        auto data = response->data();
        std::string responseStr(data.begin(), data.end());
        Loader::get()->queueInMainThread([callback, responseStr]() {
            callback(true, responseStr);
        });
    } else {
        std::string errorMsg = response->string().unwrapOr("Unknown error");
        std::string error = "HTTP " + std::to_string(response->code()) + ": " + errorMsg;
        Loader::get()->queueInMainThread([callback, error]() {
            callback(false, error);
        });
    }
}
}

HttpClient::HttpClient() {
    // Hardcoded configuration
    m_serverURL = "https://paimon-thumbnails-server.paimonalcuadrado.workers.dev";
    m_apiKey = "074b91c9-6631-4670-a6f08a2ce970-0183-471b";
    
    PaimonDebug::log("[HttpClient] Initialized with server: {}", m_serverURL);
}



void HttpClient::uploadProfile(int accountID, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile for account {} ({} bytes)", accountID, pngData.size());

    std::string url = m_serverURL + "/mod/upload";
    std::string filename = std::to_string(accountID) + ".webp"; // server stores under .webp key

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/profiles"},
        {"levelId", std::to_string(accountID)}, // server expects 'levelId' field name
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        pngData,
        formFields,
        headers,
        [callback, accountID](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile upload successful for account {}", accountID);
                callback(true, "Profile upload successful");
            } else {
                log::error("[HttpClient] Profile upload failed for account {}: {}", accountID, response);
                callback(false, "Profile upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadProfileGIF(int accountID, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile GIF for account {} ({} bytes)", accountID, gifData.size());

    std::string url = m_serverURL + "/mod/upload-gif";
    std::string filename = std::to_string(accountID) + ".gif";

    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/profiles"},
        {"levelId", std::to_string(accountID)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };

    performUpload(
        url,
        "image",
        filename,
        gifData,
        formFields,
        headers,
        [callback, accountID](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Profile GIF upload successful for account {}", accountID);
                callback(true, "Profile GIF upload successful");
            } else {
                log::error("[HttpClient] Profile GIF upload failed for account {}: {}", accountID, response);
                callback(false, "Profile GIF upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::uploadProfileConfig(int accountID, const std::string& jsonConfig, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading profile config for account {}", accountID);
    
    std::string url = m_serverURL + "/api/profiles/config/upload";
    
    std::string boundary = "---------------------------" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string body;
    
    // accountID
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"accountID\"\r\n\r\n";
    body += std::to_string(accountID) + "\r\n";
    
    // config
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"config\"\r\n\r\n";
    body += jsonConfig + "\r\n";
    
    body += "--" + boundary + "--\r\n";
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: multipart/form-data; boundary=" + boundary
    };
    
    performRequest(url, "POST", body, headers, [callback](bool success, const std::string& response) {
        callback(success, response);
    });
}

void HttpClient::downloadProfileConfig(int accountID, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading profile config for account {}", accountID);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::string url = m_serverURL + "/api/profiles/config/" + std::to_string(accountID) + ".json?_ts=" + std::to_string(ts);
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& response) {
        callback(success, response);
    });
}

void HttpClient::downloadProfile(int accountID, const std::string& username, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading profile for account {} (user: {})", accountID, username);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    // Intentar descargar WebP primero
    // Add timestamp to bypass cache
    // Remove extension to allow server to serve GIF or WebP based on availability
    std::string url = m_serverURL + "/profiles/" + std::to_string(accountID) + "?_ts=" + std::to_string(ts);
    if (!username.empty()) {
        url += "&username=" + username;
    }
    
    performRequest(url, "GET", "", headers, [callback, accountID](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
            PaimonDebug::log("[HttpClient] Profile downloaded for account {}: {} bytes", accountID, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No profile found for account {}", accountID);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::setServerURL(const std::string& url) {
    m_serverURL = url;
    PaimonDebug::log("[HttpClient] Server URL updated to: {}", url);
}

void HttpClient::setAPIKey(const std::string& key) {
    m_apiKey = key;
    PaimonDebug::log("[HttpClient] API key updated");
}

void HttpClient::uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading thumbnail as WebP for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/mod/upload";
    std::string filename = std::to_string(levelId) + ".webp"; // Cambio: .webp en lugar de .png
    
    int accountID = kUnboundAccountID;
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback, levelId](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Upload successful for level {}", levelId);
                callback(true, "Upload successful");
            } else {
                log::error("[HttpClient] Upload failed for level {}: {}", levelId, response);
                callback(false, "Upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadThumbnail(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, 
                                 const std::string& mode, const std::string& replaceId, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading thumbnail (mode={}) for level {}", mode, levelId);
    
    std::string url = m_serverURL + "/mod/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = kUnboundAccountID;
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)},
        {"mode", mode}
    };
    if (!replaceId.empty()) {
        formFields.push_back({"replaceId", replaceId});
    }
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback, levelId](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Upload successful for level {}", levelId);
                callback(true, "Upload successful");
            } else {
                log::error("[HttpClient] Upload failed for level {}: {}", levelId, response);
                callback(false, "Upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading GIF for level {}, size: {} bytes", levelId, gifData.size());
    
    std::string url = m_serverURL + "/mod/upload-gif";
    std::string filename = std::to_string(levelId) + ".gif";
    
    int accountID = kUnboundAccountID;
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, gifData, formFields, headers, 
        [callback, levelId](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] GIF upload successful for level {}", levelId);
                callback(true, "GIF Upload successful");
            } else {
                log::error("[HttpClient] GIF upload failed for level {}: {}", levelId, response);
                callback(false, "GIF Upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::uploadGIF(int levelId, const std::vector<uint8_t>& gifData, const std::string& username, 
                           const std::string& mode, const std::string& replaceId, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading GIF (mode={}) for level {}", mode, levelId);
    
    std::string url = m_serverURL + "/mod/upload-gif";
    std::string filename = std::to_string(levelId) + ".gif";
    
    int accountID = kUnboundAccountID;
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/thumbnails"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)},
        {"mode", mode}
    };
    if (!replaceId.empty()) {
        formFields.push_back({"replaceId", replaceId});
    }
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, gifData, formFields, headers, 
        [callback, levelId](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] GIF upload successful for level {}", levelId);
                callback(true, "GIF Upload successful");
            } else {
                log::error("[HttpClient] GIF upload failed for level {}: {}", levelId, response);
                callback(false, "GIF Upload failed: " + response);
            }
        },
        "image/gif"
    );
}

void HttpClient::getThumbnails(int levelId, GenericCallback callback) {
    std::string url = m_serverURL + "/api/thumbnails/list?levelId=" + std::to_string(levelId);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& response) {
        callback(success, response);
    });
}

void HttpClient::uploadSuggestion(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading suggestion for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/suggestions/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = kUnboundAccountID;
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/suggestions"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback, levelId](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Suggestion upload successful for level {}", levelId);
                callback(true, "Suggestion uploaded successfully");
            } else {
                log::error("[HttpClient] Suggestion upload failed for level {}: {}", levelId, response);
                callback(false, "Suggestion upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::uploadUpdate(int levelId, const std::vector<uint8_t>& pngData, const std::string& username, UploadCallback callback) {
    PaimonDebug::log("[HttpClient] Uploading update for level {}, size: {} bytes", levelId, pngData.size());
    
    std::string url = m_serverURL + "/api/updates/upload";
    std::string filename = std::to_string(levelId) + ".webp";
    
    int accountID = kUnboundAccountID;
    std::vector<std::pair<std::string, std::string>> formFields = {
        {"path", "/updates"},
        {"levelId", std::to_string(levelId)},
        {"username", username},
        {"accountID", std::to_string(accountID)}
    };
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performUpload(url, "image", filename, pngData, formFields, headers, 
        [callback, levelId](bool success, const std::string& response) {
            if (success) {
                PaimonDebug::log("[HttpClient] Update upload successful for level {}", levelId);
                callback(true, "Update uploaded successfully");
            } else {
                log::error("[HttpClient] Update upload failed for level {}: {}", levelId, response);
                callback(false, "Update upload failed: " + response);
            }
        },
        "image/png"
    );
}

void HttpClient::downloadThumbnail(int levelId, bool isGif, DownloadCallback callback) {
    if (!isGif) {
        downloadThumbnail(levelId, callback);
        return;
    }
    
    std::string url = m_serverURL + "/t/" + std::to_string(levelId) + ".gif";
    downloadFromUrl(url, callback);
}

void HttpClient::downloadThumbnail(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] downloadThumbnail llamado para level {} (priorizando WebP/GIF)", levelId);
    
    // Priority: GIF (animated) > WebP > PNG
    bool preferGif = false;
    try { preferGif = Mod::get()->getSettingValue<bool>("prefer-gif"); } catch(...) {}
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    auto makeHeaders = [this]() {
        return std::vector<std::string>{
            "X-API-Key: " + m_apiKey,
            "Connection: keep-alive"
        };
    };

    auto headers = makeHeaders();
    
    // Optimized URLs (GIF/WebP/PNG)
    // REMOVED: timestamp (_ts) to allow Cloudflare CDN caching
    std::string gifURL = m_serverURL + "/t/" + std::to_string(levelId) + ".gif";
    std::string webpURL = m_serverURL + "/t/" + std::to_string(levelId) + ".webp";
    std::string pngURL = m_serverURL + "/t/" + std::to_string(levelId) + ".png";
    
    PaimonDebug::log("[HttpClient] Prioridad: {} -> WebP -> PNG (fallback)", preferGif ? "GIF" : "WebP");

    auto tryOnce = [this, headers, callback, levelId](const std::string& url){
        PaimonDebug::log("[HttpClient] Intentando descargar desde: {}", url);
        performRequest(url, "GET", "", headers, [callback, levelId, url](bool success, const std::string& resp){
            PaimonDebug::log("[HttpClient] Respuesta de {}: success={}, size={}", url, success, resp.size());
            if (!success) {
                PaimonDebug::warn("[HttpClient] DESCARGA FALLIDA para level {} desde {}", levelId, url);
            }
            if (success && resp.empty()) {
                PaimonDebug::warn("[HttpClient] RESPUESTA VACIA para level {} desde {}", levelId, url);
            }
            if (success && !resp.empty()) {
                std::vector<uint8_t> data(resp.begin(), resp.end());
                PaimonDebug::log("[HttpClient] ✓ THUMBNAIL ENCONTRADO - Level {} ({} bytes)", levelId, data.size());
                callback(true, data, 0, 0);
            } else {
                PaimonDebug::warn("[HttpClient] ✗ Thumbnail NO disponible para level {}", levelId);
                callback(false, {}, 0, 0);
            }
        });
    };

    if (preferGif) {
        PaimonDebug::log("[HttpClient] Intentando GIF primero, luego WebP, luego PNG");
        PaimonDebug::log("[HttpClient] Intentando descargar GIF desde: {}", gifURL);
        performRequest(gifURL, "GET", "", headers, [=, this](bool ok, const std::string& resp){
            if (ok && !resp.empty()) {
                std::vector<uint8_t> data(resp.begin(), resp.end());
                callback(true, data, 0, 0);
            } else {
                // Fallback a WebP
                PaimonDebug::log("[HttpClient] GIF falló, intentando WebP desde: {}", webpURL);
                performRequest(webpURL, "GET", "", headers, [=, this](bool ok2, const std::string& resp2){
                    if (ok2 && !resp2.empty()) {
                        std::vector<uint8_t> data(resp2.begin(), resp2.end());
                        callback(true, data, 0, 0);
                    } else {
                        // Final fallback: PNG
                        tryOnce(pngURL);
                    }
                });
            }
        });
    } else {
        PaimonDebug::log("[HttpClient] Intentando WebP primero, luego GIF, luego PNG");
        PaimonDebug::log("[HttpClient] Intentando descargar WebP desde: {}", webpURL);
        performRequest(webpURL, "GET", "", headers, [=, this](bool ok, const std::string& resp){
            if (ok && !resp.empty()) {
                std::vector<uint8_t> data(resp.begin(), resp.end());
                callback(true, data, 0, 0);
            } else {
                // Fallback a GIF
                PaimonDebug::log("[HttpClient] WebP falló, intentando GIF desde: {}", gifURL);
                performRequest(gifURL, "GET", "", headers, [=, this](bool ok2, const std::string& resp2){
                    if (ok2 && !resp2.empty()) {
                        std::vector<uint8_t> data(resp2.begin(), resp2.end());
                        callback(true, data, 0, 0);
                    } else {
                        // Final fallback: PNG
                        tryOnce(pngURL);
                    }
                });
            }
        });
    }
}

void HttpClient::downloadSuggestion(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading suggestion for level {}", levelId);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    std::string url = m_serverURL + "/suggestions/" + std::to_string(levelId) + ".webp?_ts=" + std::to_string(ts);
    
    performRequest(url, "GET", "", headers, [callback, levelId](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
            PaimonDebug::log("[HttpClient] Suggestion downloaded for level {}: {} bytes", levelId, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No suggestion found for level {}", levelId);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadUpdate(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading update for level {}", levelId);
    
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Cache-Control: no-cache"
    };
    
    std::string url = m_serverURL + "/updates/" + std::to_string(levelId) + ".webp?_ts=" + std::to_string(ts);
    
    performRequest(url, "GET", "", headers, [callback, levelId](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
            PaimonDebug::log("[HttpClient] Update downloaded for level {}: {} bytes", levelId, data.size());
            callback(true, data, 0, 0);
        } else {
            PaimonDebug::warn("[HttpClient] No update found for level {}", levelId);
            callback(false, {}, 0, 0);
        }
    });
}

void HttpClient::downloadReported(int levelId, DownloadCallback callback) {
    PaimonDebug::log("[HttpClient] Downloading reported thumbnail for level {}", levelId);
    
    // Reports: use current official thumbnail
    downloadThumbnail(levelId, callback);
}

void HttpClient::checkThumbnailExists(int levelId, CheckCallback callback) {
    // Check cache first
    time_t now = std::time(nullptr);
    auto cacheIt = m_existsCache.find(levelId);
    if (cacheIt != m_existsCache.end()) {
        if (now - cacheIt->second.timestamp < EXISTS_CACHE_DURATION) {
            // log::debug("[HttpClient] Using cached exists result for level {}: {}", levelId, cacheIt->second.exists);
            callback(cacheIt->second.exists);
            return;
        } else {
            // Expired; remove entry
            m_existsCache.erase(cacheIt);
        }
    }
    
    std::string url = m_serverURL + "/api/exists?levelId=" + std::to_string(levelId) + "&path=thumbnails";
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey
    };
    
    performRequest(url, "GET", "", headers,
        [this, callback, levelId, now](bool success, const std::string& response) {
            if (success) {
                // Parse JSON response: {"exists": true/false}
                bool exists = response.find("\"exists\":true") != std::string::npos ||
                             response.find("\"exists\": true") != std::string::npos;
                
                // Cache result
                m_existsCache[levelId] = {exists, now};
                
                PaimonDebug::log("[HttpClient] Thumbnail exists check for level {}: {} (cached)", levelId, exists);
                callback(exists);
            } else {
                PaimonDebug::warn("[HttpClient] Failed to check thumbnail exists for level {}", levelId);
                callback(false);
            }
        }
    );
}

void HttpClient::checkModerator(const std::string& username, ModeratorCallback callback) {
    PaimonDebug::log("[HttpClient] Checking moderator status for user: {}", username);

    std::string url = m_serverURL + "/api/moderator/check?username=" + username;

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };

    performRequest(url, "GET", "", headers,
        [callback, username](bool success, const std::string& response) {
            if (success) {
                bool isMod = false;
                bool isAdmin = false;
                try {
                    auto jsonRes = matjson::parse(response);
                    if (jsonRes) {
                        auto json = jsonRes.unwrap();
                        if (json.contains("isModerator")) {
                            isMod = json["isModerator"].asBool().unwrapOr(false);
                        }
                        if (json.contains("isAdmin")) {
                            isAdmin = json["isAdmin"].asBool().unwrapOr(false);
                        }
                    }
                } catch (...) {
                    // Fallback to manual check if parsing fails
                    isMod = response.find("\"isModerator\":true") != std::string::npos ||
                            response.find("\"isModerator\": true") != std::string::npos;
                    isAdmin = response.find("\"isAdmin\":true") != std::string::npos ||
                              response.find("\"isAdmin\": true") != std::string::npos;
                }
                
                PaimonDebug::log("[HttpClient] User {} => moderator: {}, admin: {}", username, isMod, isAdmin);
                callback(isMod, isAdmin);
            } else {
                log::error("[HttpClient] Failed to check moderator status for {}: {}", username, response);
                callback(false, false);
            }
        }
    );
}

void HttpClient::checkModeratorAccount(const std::string& username, int accountID, ModeratorCallback callback) {
    PaimonDebug::log("[HttpClient] Checking moderator status (secure) for user: {} id:{}", username, accountID);
    std::string url = m_serverURL + "/api/moderator/check?username=" + username + "&accountID=" + std::to_string(accountID);
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers,
        [callback, username, accountID](bool success, const std::string& response) {
            if (success) {
                bool isMod = false;
                bool isAdmin = false;
                try {
                    auto jsonRes = matjson::parse(response);
                    if (jsonRes) {
                        auto json = jsonRes.unwrap();
                        if (json.contains("isModerator")) {
                            isMod = json["isModerator"].asBool().unwrapOr(false);
                        }
                        if (json.contains("isAdmin")) {
                            isAdmin = json["isAdmin"].asBool().unwrapOr(false);
                        }
                    }
                } catch (...) {
                    // Fallback to manual check
                    isMod = response.find("\"isModerator\":true") != std::string::npos ||
                            response.find("\"isModerator\": true") != std::string::npos;
                    isAdmin = response.find("\"isAdmin\":true") != std::string::npos ||
                              response.find("\"isAdmin\": true") != std::string::npos;
                }

                // Server validates by username (Geode doesn't expose the real ID)
                PaimonDebug::log("[HttpClient] (secure) User {}#{} => moderator: {}, admin: {}", username, accountID, isMod, isAdmin);
                callback(isMod, isAdmin);
            } else {
                log::error("[HttpClient] Failed secure moderator check for {}#{}: {}", username, accountID, response);
                callback(false, false);
            }
        }
    );
}

void HttpClient::performRequest(
    const std::string& url,
    const std::string& method,
    const std::string& postData,
    const std::vector<std::string>& headers,
    std::function<void(bool, const std::string&)> callback
) {
    auto req = web::WebRequest();
    
    // Longer timeout for slow/cold servers
    req.timeout(std::chrono::seconds(30));  // 30 segundos para dar tiempo a que el servidor "despierte"
    
    // Add headers
    for (const auto& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }
    
    // Dispatch by method (POST attaches body)
    if (method == "POST") {
        if (!postData.empty()) {
            std::vector<uint8_t> body(postData.begin(), postData.end());
            req.body(body);
        }
        auto asyncReq = req.post(url);
        asyncReq.listen([callback](web::WebResponse* response) {
            handleResponse(response, callback);
        });
        return;
    }

    auto asyncReq = req.get(url);
    asyncReq.listen([callback](web::WebResponse* response) {
        handleResponse(response, callback);
    });
}

void HttpClient::performUpload(
    const std::string& url,
    const std::string& fieldName,
    const std::string& filename,
    const std::vector<uint8_t>& data,
    const std::vector<std::pair<std::string, std::string>>& formFields,
    const std::vector<std::string>& headers,
    std::function<void(bool, const std::string&)> callback,
    const std::string& fileContentType
) {
    // Create multipart form data
    std::string boundary = "----WebKitFormBoundary" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string contentType = "multipart/form-data; boundary=" + boundary;
    
    std::vector<uint8_t> body;
    
    // Add form fields
    for (const auto& field : formFields) {
        std::string fieldPart = "--" + boundary + "\r\n";
        fieldPart += "Content-Disposition: form-data; name=\"" + field.first + "\"\r\n\r\n";
        fieldPart += field.second + "\r\n";
        body.insert(body.end(), fieldPart.begin(), fieldPart.end());
    }
    
    // Add file data
    std::string filePart = "--" + boundary + "\r\n";
    filePart += "Content-Disposition: form-data; name=\"" + fieldName + "\"; filename=\"" + filename + "\"\r\n";
    filePart += "Content-Type: " + fileContentType + "\r\n\r\n";
    body.insert(body.end(), filePart.begin(), filePart.end());
    body.insert(body.end(), data.begin(), data.end());
    
    // Add closing boundary
    std::string closing = "\r\n--" + boundary + "--\r\n";
    body.insert(body.end(), closing.begin(), closing.end());
    
    // Perform POST request
    auto req = web::WebRequest();
    req.header("Content-Type", contentType);
    for (const auto& header : headers) {
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t"));
            req.header(key, value);
        }
    }
    
    req.body(body);
    
    auto asyncReq = req.post(url);
    
    asyncReq.listen([callback](web::WebResponse* response) {
        handleResponse(response, callback);
    });
}

void HttpClient::get(const std::string& endpoint, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::post(const std::string& endpoint, const std::string& data, GenericCallback callback) {
    std::string url = m_serverURL + endpoint;
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    
    performRequest(url, "POST", data, headers, callback);
}

void HttpClient::getUserProgress(const std::string& username, UserProgressCallback callback) {
    PaimonDebug::log("[HttpClient] Getting user progress for: {}", username);
    
    std::string url = m_serverURL + "/api/paimon/user/" + username;
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    
    performRequest(url, "GET", "", headers, callback);
}



void HttpClient::getBanList(BanListCallback callback) {
    PaimonDebug::log("[HttpClient] Getting ban list");

    std::string url = m_serverURL + "/api/admin/banlist";

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };

    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::banUser(const std::string& username, const std::string& reason, BanUserCallback callback) {
    PaimonDebug::log("[HttpClient] Ban user request: {}", username);

    std::string url = m_serverURL + "/api/admin/ban";
    std::string adminUser = GJAccountManager::sharedState()->m_username;

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"reason", reason},
        {"admin", adminUser}
    });

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };

    performRequest(url, "POST", json.dump(), headers, [callback](bool success, const std::string& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::unbanUser(const std::string& username, BanUserCallback callback) {
    PaimonDebug::log("[HttpClient] Unban user request: {}", username);

    std::string url = m_serverURL + "/api/admin/unban";

    matjson::Value json = matjson::makeObject({
        {"username", username}
    });

    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };

    performRequest(url, "POST", json.dump(), headers, [callback](bool success, const std::string& resp) {
        if (callback) callback(success, resp);
    });
}

void HttpClient::getModerators(ModeratorsListCallback callback) {
    std::string url = m_serverURL + "/api/moderators";
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& response) {
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
            std::vector<std::string> moderators;
            
            if (json.contains("moderators") && json["moderators"].isArray()) {
                for (const auto& item : json["moderators"].asArray().unwrap()) {
                    if (item.contains("username")) {
                        moderators.push_back(item["username"].asString().unwrapOr(""));
                    }
                }
            }
            callback(true, moderators);
        } catch (...) {
            callback(false, {});
        }
    });
}

void HttpClient::submitReport(int levelId, const std::string& username, const std::string& note, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Submitting report for level {} by user {}", levelId, username);
    
    std::string url = m_serverURL + "/api/report/submit";
    
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"username", username},
        {"note", note}
    });
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    
    performRequest(url, "POST", json.dump(), headers, callback);
}

void HttpClient::getRating(int levelId, const std::string& username, const std::string& thumbnailId, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Getting rating for level {}", levelId);
    std::string url = m_serverURL + "/api/v2/ratings/" + std::to_string(levelId) + "?username=" + username;
    if (!thumbnailId.empty()) url += "&thumbnailId=" + thumbnailId;
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Accept: application/json"
    };
    performRequest(url, "GET", "", headers, callback);
}

void HttpClient::submitVote(int levelId, int stars, const std::string& username, const std::string& thumbnailId, GenericCallback callback) {
    PaimonDebug::log("[HttpClient] Submitting vote {} stars for level {} by {}", stars, levelId, username);
    std::string url = m_serverURL + "/api/v2/ratings/vote";
    matjson::Value json = matjson::makeObject({
        {"levelID", levelId},
        {"stars", stars},
        {"username", username}
    });
    if (!thumbnailId.empty()) json["thumbnailId"] = thumbnailId;
    
    std::vector<std::string> headers = {
        "X-API-Key: " + m_apiKey,
        "Content-Type: application/json",
        "Accept: application/json"
    };
    performRequest(url, "POST", json.dump(), headers, callback);
}



void HttpClient::downloadFromUrl(const std::string& url, DownloadCallback callback) {
    std::vector<std::string> headers = { "X-API-Key: " + m_apiKey };
    
    performRequest(url, "GET", "", headers, [callback](bool success, const std::string& resp) {
        if (success && !resp.empty()) {
            std::vector<uint8_t> data(resp.begin(), resp.end());
            callback(true, data, 0, 0);
        } else {
            callback(false, {}, 0, 0);
        }
    });
}


