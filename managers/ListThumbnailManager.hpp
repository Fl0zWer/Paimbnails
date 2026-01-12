#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <queue>

class ListThumbnailManager {
public:
    static ListThumbnailManager& get();

    // Callback: levelID, texture
    using ListCallback = std::function<void(int, cocos2d::CCTexture2D*)>;

    void processList(const std::vector<int>& levelIDs, ListCallback callback, std::shared_ptr<bool> callerAlive = nullptr);

private:
    ListThumbnailManager() = default;
    
    struct BatchTask {
        std::vector<int> levelIDs;
        ListCallback callback;
    };

    void workerLoop();
    void processBatch(const BatchTask& task);

    // Helper to add texture to ThumbnailLoader cache safely
    void cacheTexture(int levelID, cocos2d::CCTexture2D* texture);
};
