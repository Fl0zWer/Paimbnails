#include "ListThumbnailManager.hpp"
#include "ThumbnailLoader.hpp"
#include <fstream>

using namespace geode::prelude;

ListThumbnailManager& ListThumbnailManager::get() {
    static ListThumbnailManager instance;
    return instance;
}

void ListThumbnailManager::processList(const std::vector<int>& levelIDs, ListCallback callback, std::shared_ptr<bool> callerAlive) {
    for (int id : levelIDs) {
        if (callerAlive && !*callerAlive) return;
        
        std::string fileName = fmt::format("{}.png", id);
        ThumbnailLoader::get().requestLoad(id, fileName, [id, callback, callerAlive](cocos2d::CCTexture2D* tex, bool success) {
            if (callerAlive && !*callerAlive) return;
            if (success && callback) {
                callback(id, tex);
            }
        }, 0);
    }
}

void ListThumbnailManager::workerLoop() {}
void ListThumbnailManager::processBatch(const BatchTask& task) {}
void ListThumbnailManager::cacheTexture(int levelID, cocos2d::CCTexture2D* texture) {}
