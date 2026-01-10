#include "GifManager.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../utils/stb_image.h"

using namespace geode::prelude;

GifData::~GifData() {
    if (rawData) {
        stbi_image_free(rawData);
        rawData = nullptr;
    }
}

GifManager* GifManager::get() {
    static GifManager instance;
    return &instance;
}

GifManager::GifManager() : m_running(true) {
    // Determine optimal thread count
    unsigned int threads = std::thread::hardware_concurrency();
    // Clamp between 2 and 8 to be safe (leave 1 for main thread)
    // If hardware_concurrency returns 0 (error), default to 2.
    if (threads == 0) threads = 2;
    else if (threads > 8) threads = 8;
    
    // Ensure we don't use ALL threads if the CPU has few cores
    if (threads > 2) threads -= 1; 

    log::info("GifManager: Starting thread pool with {} workers.", threads);

    for (unsigned int i = 0; i < threads; ++i) {
        m_workers.emplace_back(&GifManager::workerLoop, this);
    }
}

GifManager::~GifManager() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_running = false;
    }
    m_condition.notify_all(); // Wake up all workers
    
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void GifManager::loadGif(const std::string& path, std::function<void(std::shared_ptr<GifData>)> callback) {
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(path) != m_cache.end()) {
            auto data = m_cache[path];
            Loader::get()->queueInMainThread([callback, data]() {
                callback(data);
            });
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push({path, path, {}, callback});
    }
    m_condition.notify_one();
}

void GifManager::loadGifFromUrl(const std::string& url, std::function<void(std::shared_ptr<GifData>)> callback) {
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_cache.find(url) != m_cache.end()) {
            auto data = m_cache[url];
            Loader::get()->queueInMainThread([callback, data]() {
                callback(data);
            });
            return;
        }
    }

    // Download first (on main thread or async web thread)
    web::WebRequest()
        .get(url)
        .listen([this, url, callback](web::WebResponse* res) {
            if (res && res->ok()) {
                auto data = res->data();
                // Push to worker thread for decoding
                {
                    std::lock_guard<std::mutex> lock(m_queueMutex);
                    m_taskQueue.push({url, "", std::move(data), callback});
                }
                m_condition.notify_one();
            } else {
                log::error("Failed to download GIF: {}", res ? res->code() : -1);
                callback(nullptr);
            }
        });
}

void GifManager::workerLoop() {
    while (true) {
        GifTask task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this] { return !m_taskQueue.empty() || !m_running; });

            if (!m_running && m_taskQueue.empty()) {
                return;
            }

            task = m_taskQueue.front();
            m_taskQueue.pop();
        }

        // Check cache again in case it was loaded while waiting
        std::shared_ptr<GifData> data;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_cache.find(task.key) != m_cache.end()) {
                data = m_cache[task.key];
            }
        }

        if (!data) {
            if (!task.path.empty()) {
                data = processGif(task.path);
            } else if (!task.data.empty()) {
                data = processGifData(task.data.data(), task.data.size(), task.key);
            }
            
            if (data) {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_cache[task.key] = data;
            }
        }

        Loader::get()->queueInMainThread([callback = task.callback, data]() {
            callback(data);
        });
    }
}

std::shared_ptr<GifData> GifManager::processGif(const std::string& path) {
    // Read file content
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        log::error("Failed to open GIF file: {}", path);
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(size);
    if (!file.read((char*)buffer.data(), size)) {
        log::error("Failed to read GIF file: {}", path);
        return nullptr;
    }

    return processGifData(buffer.data(), (size_t)size, path);
}

std::shared_ptr<GifData> GifManager::processGifData(const unsigned char* bufferData, size_t size, const std::string& key) {
    int width, height, frames, channels;
    int* delays = nullptr;

    // Force 4 channels (RGBA)
    stbi_uc* data = stbi_load_gif_from_memory(
        bufferData, 
        (int)size, 
        &delays, 
        &width, 
        &height, 
        &frames, 
        &channels, 
        4
    );

    if (!data) {
        log::error("Failed to decode GIF: {}", key);
        return nullptr;
    }

    auto result = std::make_shared<GifData>();
    result->width = width;
    result->height = height;
    result->totalFrames = frames;
    result->rawData = data;

    int frameSize = width * height * 4;

    for (int i = 0; i < frames; i++) {
        GifFrame frame;
        frame.pixels = data + (i * frameSize);
        frame.delay = delays[i];
        result->frames.push_back(frame);
    }

    STBI_FREE(delays);

    return result;
}
