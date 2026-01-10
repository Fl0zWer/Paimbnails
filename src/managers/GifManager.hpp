#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <memory>

struct GifFrame {
    unsigned char* pixels;
    int delay; // in milliseconds
};

struct GifData {
    std::vector<GifFrame> frames;
    int width;
    int height;
    int totalFrames;
    unsigned char* rawData; // Pointer to the raw data allocated by stbi

    ~GifData();
};

class GifManager {
public:
    static GifManager* get();

    // Callback receives the GifData pointer (or nullptr on failure).
    // The callback is executed on the main thread.
    void loadGif(const std::string& path, std::function<void(std::shared_ptr<GifData>)> callback);

    // Downloads a GIF from a URL, decodes it in the background, and returns the data.
    void loadGifFromUrl(const std::string& url, std::function<void(std::shared_ptr<GifData>)> callback);

private:
    GifManager();
    ~GifManager();

    void workerLoop();
    std::shared_ptr<GifData> processGif(const std::string& path);
    std::shared_ptr<GifData> processGifData(const unsigned char* data, size_t size, const std::string& key);

    std::vector<std::thread> m_workers;
    // Task can now be a file path OR raw data. 
    // If string is not empty, it's a path. If vector is not empty, it's raw data.
    struct GifTask {
        std::string key; // File path or URL (for cache key)
        std::string path; // File path (optional)
        std::vector<unsigned char> data; // Raw data (optional)
        std::function<void(std::shared_ptr<GifData>)> callback;
    };

    std::queue<GifTask> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    bool m_running;

    std::map<std::string, std::shared_ptr<GifData>> m_cache;
    std::mutex m_cacheMutex;
};
