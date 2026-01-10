#include "ThumbnailLoader.hpp"
#include "LocalThumbs.hpp"
#include "LevelColors.hpp"
#include "../utils/Constants.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/DominantColors.hpp"
#include "../utils/GIFDecoder.hpp"
#include "../utils/Debug.hpp"
#include "../utils/stb_image.h"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <cmath>
#include <algorithm>

// Define for SEH if not available
#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER 1
#endif

using namespace geode::prelude;

ThumbnailLoader& ThumbnailLoader::get() {
    static ThumbnailLoader instance;
    return instance;
}

ThumbnailLoader::ThumbnailLoader() {
    m_maxConcurrentTasks = 5; // Default to 5 as requested
    initDiskCache();
}

ThumbnailLoader::~ThumbnailLoader() {
    clearCache();
}

void ThumbnailLoader::initDiskCache() {
    std::thread([this]() {
        std::lock_guard<std::mutex> lock(m_diskMutex);
        auto path = Mod::get()->getSaveDir() / "cache";
        PaimonDebug::log("[ThumbnailLoader] Initializing disk cache from: {}", path.string());
        
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
            PaimonDebug::log("[ThumbnailLoader] Created cache directory");
            return;
        }
        
        // Clear cache on startup
        // Modified: Main levels (1-22) have NO expiration (infinite retention)
        // Others: 15 days retention
        
        int deletedCount = 0;
        int keptCount = 0;
        
        auto now = std::filesystem::file_time_type::clock::now();
        auto defaultRetention = std::chrono::hours(24 * 15); // 15 days

        try {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    try {
                        auto stem = entry.path().stem().string();
                        // Check if it's a valid ID
                        int id = 0;
                        bool isGif = false;
                        if (stem.find("_anim") != std::string::npos) {
                            std::string idStr = stem.substr(0, stem.find("_anim"));
                            id = std::stoi(idStr);
                            isGif = true;
                        } else {
                            id = std::stoi(stem);
                        }
                        
                        // Check main level
                        int realID = std::abs(id);
                        bool isMain = (realID >= 1 && realID <= 22);

                        // If it's a main level, we NEVER delete it based on time.
                        if (!isMain) {
                            // Per user request: Clear cache for non-main levels on session start
                            // This ensures they are only cached during the active session.
                            std::filesystem::remove(entry.path());
                            deletedCount++;
                            continue;
                        }
                        
                        m_diskCache.insert(isGif ? -id : id);
                        keptCount++;
                    } catch(...) {
                        // If filename is not an ID or error checking time, delete it
                        std::filesystem::remove(entry.path());
                    }
                }
            }
        } catch(const std::exception& e) {
            log::error("[ThumbnailLoader] Error cleaning cache directory: {}", e.what());
        }
        
        PaimonDebug::log("[ThumbnailLoader] Disk cache initialized. Deleted: {}, Kept: {}", deletedCount, keptCount);
    }).detach();
}

void ThumbnailLoader::setMaxConcurrentTasks(int max) {
    // Allow up to 100 concurrent downloads for maximum speed
    m_maxConcurrentTasks = std::max(1, std::min(100, max));
}

bool ThumbnailLoader::isLoaded(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    return m_textureCache.find(key) != m_textureCache.end();
}

bool ThumbnailLoader::isPending(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    return m_tasks.find(key) != m_tasks.end();
}

bool ThumbnailLoader::isFailed(int levelID, bool isGif) const {
    int key = isGif ? -levelID : levelID;
    return m_failedCache.find(key) != m_failedCache.end();
}

bool ThumbnailLoader::hasGIFData(int levelID) const {
    return m_gifLevels.find(levelID) != m_gifLevels.end();
}

std::filesystem::path ThumbnailLoader::getCachePath(int levelID, bool isGif) {
    if (isGif) {
        return Mod::get()->getSaveDir() / "cache" / fmt::format("{}_anim.gif", levelID);
    }
    return Mod::get()->getSaveDir() / "cache" / fmt::format("{}.png", levelID);
}

void ThumbnailLoader::requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority, bool isGif) {
    int key = isGif ? -levelID : levelID;

    // 1. Check RAM Cache
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) {
        // Update LRU
        m_lruOrder.remove(key);
        m_lruOrder.push_back(key);
        
        // Force async callback to ensure UI is ready
        auto tex = it->second;
        tex->retain();
        Loader::get()->queueInMainThread([callback, tex]() {
            if (callback) callback(tex, true);
            tex->release();
        });
        return;
    }

    // 2. Check Failed Cache
    if (m_failedCache.count(key)) {
        if (callback) callback(nullptr, false);
        return;
    }

    // 3. Check Pending Tasks
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto taskIt = m_tasks.find(key);
    if (taskIt != m_tasks.end()) {
        // Add callback to existing task
        if (callback) taskIt->second->callbacks.push_back(callback);
        // Update priority if higher
        if (priority > taskIt->second->priority) {
            taskIt->second->priority = priority;
            // Re-sort queue logic handled in processQueue
        }
        return;
    }

    // 4. Create New Task
    auto task = std::make_shared<Task>();
    task->levelID = key;
    task->fileName = fileName;
    task->priority = priority;
    if (callback) task->callbacks.push_back(callback);

    m_tasks[key] = task;
    m_queueOrder.push_back(key);
    
    processQueue();
}

void ThumbnailLoader::cancelLoad(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto it = m_tasks.find(key);
    if (it != m_tasks.end()) {
        it->second->cancelled = true;
        // If running, we can't stop it easily, but we will ignore result
        // If pending, we could remove it, but simpler to just mark cancelled
    }
}

void ThumbnailLoader::processQueue() {
    // Must be called with m_queueMutex locked or handle locking inside
    // But processQueue calls startTask which might unlock? No.
    // Let's handle locking carefully.
    
    // Batch Mode: If enabled, do not start new tasks if ANY task is currently running.
    // This ensures we finish the current batch before starting the next one.
    if (m_batchMode && m_activeTaskCount > 0) {
        return;
    }

    // We need to find the best candidate to run
    while (m_activeTaskCount < m_maxConcurrentTasks && !m_queueOrder.empty()) {
        // Find highest priority task
        auto bestIt = m_queueOrder.end();
        int maxPrio = -9999;

        for (auto it = m_queueOrder.begin(); it != m_queueOrder.end(); ++it) {
            // Safety check: ensure task exists in map
            if (m_tasks.find(*it) == m_tasks.end()) continue;
            
            auto task = m_tasks[*it];
            if (!task) continue;

            if (task->cancelled) continue; // Skip cancelled
            
            if (task->priority > maxPrio) {
                maxPrio = task->priority;
                bestIt = it;
            }
        }

        if (bestIt == m_queueOrder.end()) {
            // All remaining are cancelled
            m_queueOrder.clear();
            // Cleanup cancelled tasks from map
            for (auto it = m_tasks.begin(); it != m_tasks.end();) {
                if (it->second->cancelled && !it->second->running) {
                    it = m_tasks.erase(it);
                } else {
                    ++it;
                }
            }
            break;
        }

        int levelID = *bestIt;
        m_queueOrder.erase(bestIt);
        
        auto task = m_tasks[levelID];
        if (task->cancelled) continue;

        startTask(task);
    }
}

void ThumbnailLoader::startTask(std::shared_ptr<Task> task) {
    task->running = true;
    m_activeTaskCount++;

    // Always try disk first to avoid race conditions with initDiskCache
    // workerLoadFromDisk will check filesystem and fallback to download if missing
    std::thread([this, task]() {
        workerLoadFromDisk(task);
    }).detach();
}

void ThumbnailLoader::workerLoadFromDisk(std::shared_ptr<Task> task) {
    if (task->cancelled) {
        Loader::get()->queueInMainThread([this, task]() {
            finishTask(task, nullptr, false);
        });
        return;
    }

    bool isGif = task->levelID < 0;
    int realID = std::abs(task->levelID);
    auto path = getCachePath(realID, isGif);
    std::vector<uint8_t> data;
    bool success = false;

    try {
        if (std::filesystem::exists(path)) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                size_t size = file.tellg();
                file.seekg(0, std::ios::beg);
                data.resize(size);
                file.read(reinterpret_cast<char*>(data.data()), size);
                success = true;
                PaimonDebug::log("[ThumbnailLoader] Loaded {} bytes from disk for level {}{}", size, realID, isGif ? " (gif)" : "");
            } else {
                PaimonDebug::warn("[ThumbnailLoader] Failed to open file for level {}: {}", realID, path.string());
            }
        } else {
            // Not found is normal for first load, don't warn
            // log::warn("[ThumbnailLoader] File not found for level {}: {}", task->levelID, path.string());
        }
    } catch(const std::exception& e) {
        log::error("[ThumbnailLoader] Exception loading from disk for level {}: {}", realID, e.what());
    } catch(...) {
        log::error("[ThumbnailLoader] Unknown exception loading from disk for level {}", realID);
    }

    if (success && !data.empty()) {
        // Optimized: Decode and process in WORKER thread
        bool isNativeGif = GIFDecoder::isGIF(data.data(), data.size());
        
        if (isNativeGif) {
            // GIF processing (Main Thread for safety with sprites/frames for now)
            Loader::get()->queueInMainThread([this, task, data, realID]() {
                if (task->cancelled) { finishTask(task, nullptr, false); return; }
                
                m_gifLevels.insert(realID);
                auto image = new CCImage();
                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                    auto tex = new CCTexture2D();
                    if (tex->initWithImage(image)) {
                        image->release();
                        tex->autorelease();
                        finishTask(task, tex, true);
                    } else {
                        image->release();
                        tex->release();
                        workerDownload(task);
                    }
                } else {
                    image->release();
                    workerDownload(task);
                }
            });
        } 
        else {
            // Static Image (PNG/JPG/WEBP)
            // Use stb_image to decode OFF the main thread
            int w = 0, h = 0, ch = 0;
            unsigned char* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &ch, 4); // Force RGBA
            
            if (pixels) {
                // Determine colors in WORKER thread
                if (!LevelColors::get().getPair(realID)) {
                    LevelColors::get().extractFromRawData(realID, pixels, w, h, true);
                }
                
                // Copy data to pass to main thread (avoid pointer issues)
                // Using shared_ptr or vector. Vector is fine.
                std::vector<uint8_t> rawData(pixels, pixels + (w * h * 4));
                stbi_image_free(pixels);
                
                Loader::get()->queueInMainThread([this, task, rawData, w, h, realID]() {
                    if (task->cancelled) { finishTask(task, nullptr, false); return; }
                    
                    auto tex = new CCTexture2D();
                    if (tex->initWithData(rawData.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize((float)w, (float)h))) {
                        tex->autorelease();
                        finishTask(task, tex, true);
                    } else {
                        tex->release();
                        // If creating texture fails, retry download might be needed, but usually it's a format issue
                         PaimonDebug::warn("[ThumbnailLoader] Texture creation failed for level {}", realID);
                        workerDownload(task);
                    }
                });
            } else {
                 PaimonDebug::warn("[ThumbnailLoader] STB decode failed for level {}", realID);
                 workerDownload(task);
            }
        }
    } else {
        // Failed to read, try redownloading
        workerDownload(task);
    }
}

void ThumbnailLoader::workerDownload(std::shared_ptr<Task> task) {
    if (task->cancelled) {
        Loader::get()->queueInMainThread([this, task]() {
            finishTask(task, nullptr, false);
        });
        return;
    }

    int realID = std::abs(task->levelID);
    bool isGif = task->levelID < 0;

    Loader::get()->queueInMainThread([this, task, realID, isGif]() {
        HttpClient::get().downloadThumbnail(realID, isGif, 
            [this, task, isGif, realID](bool success, const std::vector<uint8_t>& data, int w, int h) {
                if (task->cancelled) {
                    finishTask(task, nullptr, false);
                    return;
                }

                if (success && !data.empty()) {
                    // Start processing in worker thread
                    std::thread([this, task, data, isGif, realID]() {
                        // 1. Save to disk
                        try {
                            auto path = getCachePath(realID, isGif);
                            std::ofstream file(path, std::ios::binary);
                            file.write(reinterpret_cast<const char*>(data.data()), data.size());
                            file.close();
                            
                            std::lock_guard<std::mutex> lock(m_diskMutex);
                            m_diskCache.insert(task->levelID);
                        } catch(...) {}
                        
                        // 2. Decode & Extract Colors (Background)
                        if (GIFDecoder::isGIF(data.data(), data.size())) {
                            // GIF Logic (Queue to Main)
                            Loader::get()->queueInMainThread([this, task, data, realID]() {
                                m_gifLevels.insert(realID);
                                auto image = new CCImage();
                                if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                                    auto tex = new CCTexture2D();
                                    if (tex->initWithImage(image)) {
                                        image->release();
                                        tex->autorelease();
                                        finishTask(task, tex, true);
                                    } else {
                                        image->release();
                                        tex->release();
                                        finishTask(task, nullptr, false);
                                    }
                                } else {
                                    image->release();
                                    finishTask(task, nullptr, false);
                                }
                            });
                        } else {
                            // Static Image Optimized
                            int sw = 0, sh = 0, ch = 0;
                            unsigned char* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &sw, &sh, &ch, 4);
                             
                            if (pixels) {
                                if (!LevelColors::get().getPair(realID)) {
                                    LevelColors::get().extractFromRawData(realID, pixels, sw, sh, true);
                                }
                                
                                std::vector<uint8_t> rawData(pixels, pixels + (sw * sh * 4));
                                stbi_image_free(pixels);
                                
                                Loader::get()->queueInMainThread([this, task, rawData, sw, sh]() {
                                    if (task->cancelled) { finishTask(task, nullptr, false); return; }
                                    
                                    auto tex = new CCTexture2D();
                                    if (tex->initWithData(rawData.data(), kCCTexture2DPixelFormat_RGBA8888, sw, sh, CCSize((float)sw, (float)sh))) {
                                        tex->autorelease();
                                        finishTask(task, tex, true);
                                    } else {
                                        tex->release();
                                        finishTask(task, nullptr, false);
                                    }
                                });
                            } else {
                                // Fallback
                                Loader::get()->queueInMainThread([this, task]() {
                                     finishTask(task, nullptr, false);
                                });
                            }
                        }
                    }).detach();
                } else {
                    finishTask(task, nullptr, false);
                }
            }
        );
    });
}

void ThumbnailLoader::finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success) {
    // Main Thread
    
    if (success && texture) {
        addToCache(task->levelID, texture);
    } else {
        if (!task->cancelled) {
            m_failedCache.insert(task->levelID);
        }
    }

    if (!task->cancelled) {
        for (auto& cb : task->callbacks) {
            if (cb) cb(texture, success);
        }
    }

    // Cleanup
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_tasks.erase(task->levelID);
    m_activeTaskCount--;
    
    // Process next
    processQueue();
}

void ThumbnailLoader::addToCache(int levelID, cocos2d::CCTexture2D* texture) {
    if (!texture) return;
    
    if (m_textureCache.count(levelID)) {
        m_textureCache[levelID]->release();
    }
    
    texture->retain();
    m_textureCache[levelID] = texture;
    
    m_lruOrder.remove(levelID);
    m_lruOrder.push_back(levelID);
    
    // Trim
    while (m_lruOrder.size() > MAX_CACHE_SIZE) {
        int removeID = m_lruOrder.front();
        m_lruOrder.pop_front();
        
        auto it = m_textureCache.find(removeID);
        if (it != m_textureCache.end()) {
            it->second->release();
            m_textureCache.erase(it);
        }
    }
}

void ThumbnailLoader::clearCache() {
    for (auto& [id, tex] : m_textureCache) {
        tex->release();
    }
    m_textureCache.clear();
    m_lruOrder.clear();
    m_failedCache.clear();
}

void ThumbnailLoader::invalidateLevel(int levelID, bool isGif) {
    int key = isGif ? -levelID : levelID;
    // Remove from RAM
    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end()) {
        it->second->release();
        m_textureCache.erase(it);
        m_lruOrder.remove(key);
    }
    
    // Remove from Failed
    m_failedCache.erase(key);
    
    // Remove from Disk
    std::thread([this, levelID, isGif]() {
        try {
            std::filesystem::remove(getCachePath(levelID, isGif));
            std::lock_guard<std::mutex> lock(m_diskMutex);
            m_diskCache.erase(isGif ? -levelID : levelID);
        } catch(...) {}
    }).detach();
}

void ThumbnailLoader::clearDiskCache() {
    std::thread([this]() {
        try {
            std::filesystem::remove_all(Mod::get()->getSaveDir() / "cache");
            std::lock_guard<std::mutex> lock(m_diskMutex);
            m_diskCache.clear();
            initDiskCache(); // Re-create folder
        } catch(...) {}
    }).detach();
}

void ThumbnailLoader::clearPendingQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& [id, task] : m_tasks) {
        task->cancelled = true;
    }
    // We can't easily clear the map because tasks might be running
    // But we marked them cancelled.
}

void ThumbnailLoader::updateSessionCache(int levelID, cocos2d::CCTexture2D* texture) {
    addToCache(levelID, texture);
}

void ThumbnailLoader::cleanup() {
    clearPendingQueue();
    clearCache();
}

bool ThumbnailLoader::isTextureSane(cocos2d::CCTexture2D* tex) {
    if (!tex) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(tex);
    if (addr < 0x10000) return false; // Null or low pointer
    
#ifdef GEODE_IS_WINDOWS
    __try {
        auto sz = tex->getContentSize();
        if (std::isnan(sz.width) || std::isnan(sz.height)) return false;
        return sz.width > 0.f && sz.height > 0.f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return true;
#endif
}


