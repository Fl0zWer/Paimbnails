#pragma once

#include <Geode/Geode.hpp>
#include "GIFDecoder.hpp"
#include <vector>
#include <string>
#include <utility>
#include <list>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <set>

// Animated sprite that plays GIF frames with caching and incremental loading.
class AnimatedGIFSprite : public cocos2d::CCSprite {
public:
    static AnimatedGIFSprite* create(const std::string& filename);
    static AnimatedGIFSprite* create(const void* data, size_t size);

    static void pinGIF(const std::string& key);
    static void unpinGIF(const std::string& key);
    static bool isPinned(const std::string& key);

    struct SharedGIFData {
        std::vector<cocos2d::CCTexture2D*> textures;
        std::vector<float> delays;
        std::vector<cocos2d::CCRect> frameRects; // Stores left, top, width, height
        int width;
        int height;
    };

protected:
    struct GIFFrame {
        cocos2d::CCTexture2D* texture = nullptr;
        cocos2d::CCRect rect; // Position and size in canvas
        float delay = 0.1f; // Seconds
        
        ~GIFFrame() {
            if (texture) {
                texture->release();
                texture = nullptr;
            }
        }
    };
    
    static std::map<std::string, SharedGIFData> s_gifCache;
    static std::list<std::string> s_lruList;
    static std::set<std::string> s_pinnedGIFs;
    
    static size_t s_currentCacheSize; // Bytes
    static size_t getMaxCacheMem();

    std::vector<GIFFrame*> m_frames;
    // Dominant colors per frame: {A, B}
    std::vector<std::pair<cocos2d::ccColor3B, cocos2d::ccColor3B>> m_frameColors;
    
    unsigned int m_currentFrame = 0;
    float m_frameTimer = 0.0f;
    bool m_isPlaying = true;
    bool m_loop = true;
    std::string m_filename;
    int m_canvasWidth = 0;
    int m_canvasHeight = 0;
    
    std::vector<GIFDecoder::Frame> m_pendingFrames;
    void updateTextureLoading(float dt);

    void updateAnimation(float dt);
    
    virtual ~AnimatedGIFSprite();
    
public:
    // Shader support (used by e.g. LevelCell blur)
    float m_intensity = 0.0f;
    float m_time = 0.0f;
    float m_brightness = 1.0f;
    cocos2d::CCSize m_texSize = {0, 0};

    static void clearCache();
    static void remove(const std::string& filename);
    static bool isCached(const std::string& filename);
    
    using AsyncCallback = std::function<void(AnimatedGIFSprite*)>;
    static void createAsync(const std::string& path, AsyncCallback callback);
    static void createAsync(const std::vector<uint8_t>& data, const std::string& key, AsyncCallback callback);
    
    static AnimatedGIFSprite* createFromCache(const std::string& key);

    // Disk cache
    struct DiskCacheEntry {
        int width;
        int height;
        struct Frame {
            std::vector<uint16_t> pixels; // RGBA4444
            float delay;
            int width;
            int height;
        };
        std::vector<Frame> frames;
    };
    
    static bool loadFromDiskCache(const std::string& path, DiskCacheEntry& outEntry);
    static void saveToDiskCache(const std::string& path, const DiskCacheEntry& entry);
    static std::string getCachePath(const std::string& path);

private:
    // Worker queue
    struct GIFTask {
        std::string path;
        std::vector<uint8_t> data;
        std::string key;
        AsyncCallback callback;
        bool isData = false;
    };
    
    static std::deque<GIFTask> s_taskQueue;
    static std::mutex s_queueMutex;
    static std::condition_variable s_queueCV;
    static std::thread s_workerThread;
    static bool s_workerRunning;
    static void workerLoop();
    static void initWorker();

public:
    void play() { m_isPlaying = true; }
    void pause() { m_isPlaying = false; }
    void stop() { 
        m_isPlaying = false; 
        m_currentFrame = 0;
        if (!m_frames.empty() && m_frames[0] && m_frames[0]->texture) {
            this->setTexture(m_frames[0]->texture);
        }
    }
    
    void setLoop(bool loop) { m_loop = loop; }
    bool isPlaying() const { return m_isPlaying; }
    bool isLooping() const { return m_loop; }
    
    unsigned int getCurrentFrame() const { return m_currentFrame; }
    unsigned int getFrameCount() const { return m_frames.size(); }
    std::string getCacheKey() const { return m_filename; }
    
    void setCurrentFrame(unsigned int frame);

    // Used by the async loader to ensure frame 0 exists before layout.
    bool processNextPendingFrame();

    std::pair<cocos2d::ccColor3B, cocos2d::ccColor3B> getCurrentFrameColors() const {
        if (m_currentFrame < m_frameColors.size()) {
            return m_frameColors[m_currentFrame];
        }
        return { {0,0,0}, {0,0,0} };
    }

private:
    bool initFromCache(const std::string& cacheKey);
    
    const std::string& getFilename() const { return m_filename; }
    
    void update(float dt) override;

    // Override draw for shader support
    void draw() override;

private:
};
