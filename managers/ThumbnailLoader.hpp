#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <string>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include "../utils/GIFDecoder.hpp"

/**
 * Optimized thumbnail loader:
 * - Concurrency limit
 * - Priority queue
 * - Automatic caching
 * - Avoids lag
 */
class ThumbnailLoader {
public:
    using LoadCallback = std::function<void(cocos2d::CCTexture2D* texture, bool success)>;

    static ThumbnailLoader& get();

    // Request a thumbnail load. Higher priority value = higher priority.
    void requestLoad(int levelID, std::string fileName, LoadCallback callback, int priority = 0, bool isGif = false);
    
    // Cancel a pending load
    void cancelLoad(int levelID, bool isGif = false);
    
    // Cache
    bool isLoaded(int levelID, bool isGif = false) const;
    bool isPending(int levelID, bool isGif = false) const;
    bool isFailed(int levelID, bool isGif = false) const;
    void clearCache();
    void invalidateLevel(int levelID, bool isGif = false);
    
    // Config
    void setMaxConcurrentTasks(int max);
    void setBatchMode(bool enabled) { m_batchMode = enabled; }

    // Helpers
    static bool isTextureSane(cocos2d::CCTexture2D* tex);
    std::filesystem::path getCachePath(int levelID, bool isGif = false);
    
    // Compatibility
    void updateSessionCache(int levelID, cocos2d::CCTexture2D* texture);
    bool hasGIFData(int levelID) const;
    void cleanup();
    void clearDiskCache();
    void pauseQueue() {} // Deprecated/No-op
    void resumeQueue() {} // Deprecated/No-op
    void clearPendingQueue();

private:
    ThumbnailLoader();
    ~ThumbnailLoader();

    struct Task {
        int levelID;
        std::string fileName;
        int priority;
        std::vector<LoadCallback> callbacks;
        bool running = false;
        bool cancelled = false;
    };

    // Queue Management
    std::map<int, std::shared_ptr<Task>> m_tasks; // ID -> Task (Pending & Running)
    std::list<int> m_queueOrder; // IDs ordered by priority/FIFO
    int m_activeTaskCount = 0;
    int m_maxConcurrentTasks = 4;
    std::mutex m_queueMutex;
    
    // RAM Cache (Session)
    std::map<int, cocos2d::CCTexture2D*> m_textureCache;
    std::list<int> m_lruOrder;
    const size_t MAX_CACHE_SIZE = 60;
    
    // Disk Cache Index
    std::unordered_set<int> m_diskCache;
    std::mutex m_diskMutex;
    
    // Failed Cache
    std::unordered_set<int> m_failedCache;
    
    // GIF Cache
    std::unordered_set<int> m_gifLevels;

    bool m_batchMode = false; // Default: "smart" downloading (Disabled for speed)

    // Methods
    void processQueue();
    void startTask(std::shared_ptr<Task> task);
    void finishTask(std::shared_ptr<Task> task, cocos2d::CCTexture2D* texture, bool success);
    
    void addToCache(int levelID, cocos2d::CCTexture2D* texture);
    void initDiskCache();
    
    // Worker methods
    void workerLoadFromDisk(std::shared_ptr<Task> task);
    void workerDownload(std::shared_ptr<Task> task);
};

