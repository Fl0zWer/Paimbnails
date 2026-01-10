#include "ListThumbnailCarousel.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../managers/ListThumbnailManager.hpp"
#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS
#include <excpt.h>
#endif

using namespace geode::prelude;

ListThumbnailCarousel::~ListThumbnailCarousel() {
    if (m_alive) *m_alive = false;
    
    // Cancel pending downloads for this list to free up the queue.
    for (int id : m_levelIDs) {
        ThumbnailLoader::get().cancelLoad(id);
    }

    this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
    this->unschedule(schedule_selector(ListThumbnailCarousel::updatePan));
}

ListThumbnailCarousel* ListThumbnailCarousel::create(const std::vector<int>& levelIDs, CCSize size) {
    auto ret = new ListThumbnailCarousel();
    if (ret && ret->init(levelIDs, size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ListThumbnailCarousel::init(const std::vector<int>& levelIDs, CCSize size) {
    if (!CCNode::init()) return false;
    
    m_alive = std::make_shared<bool>(true);
    m_levelIDs = levelIDs;
    m_size = size;
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});
    
    // CCClippingNode was removed to avoid scrolling visibility issues.
    // We use setTextureRect on sprites instead.
    
    m_loadingCircle = CCSprite::create("loadingCircle.png");
    if (m_loadingCircle) {
        this->addChild(m_loadingCircle);
        // Place it near the "View" button area (right side), with a bit of padding
        // so it doesn't overlap the button.
        
        m_loadingCircle->setPosition({size.width - 85.0f, size.height / 2});
        m_loadingCircle->setScale(0.4f); // Slightly smaller
        m_loadingCircle->runAction(CCRepeatForever::create(CCRotateBy::create(1.0f, 360.0f)));
    }

    return true;
}

void ListThumbnailCarousel::startCarousel() {
    if (m_levelIDs.empty()) return;
    
    // Optional: pre-process the list in the background (disabled for now).
    // m_alive allows callers to cancel work if this node is destroyed.
    // ListThumbnailManager::get().processList(m_levelIDs, nullptr, m_alive);

    // Load the first image to display.
    tryShowNextImage();
}

void ListThumbnailCarousel::updateCarousel(float dt) {
    tryShowNextImage();
}

void ListThumbnailCarousel::updatePan(float dt) {
    if (!m_currentSprite) return;
    
    m_panElapsed += dt;
    float duration = 5.0f; // Slow pan
    
    float t = m_panElapsed / duration;
    if (t > 1.0f) t = 1.0f;
    
    // Sine in-out easing for smooth start/end.
    float easeT = 0.5f * (1.0f - std::cos(t * M_PI));
    
    float currentX = m_panStartRect.origin.x + (m_panEndRect.origin.x - m_panStartRect.origin.x) * easeT;
    float currentY = m_panStartRect.origin.y + (m_panEndRect.origin.y - m_panStartRect.origin.y) * easeT;
    
    CCRect currentRect = m_panStartRect;
    currentRect.origin.x = currentX;
    currentRect.origin.y = currentY;
    
    m_currentSprite->setTextureRect(currentRect);
}

void ListThumbnailCarousel::tryShowNextImage() {
    if (m_levelIDs.empty()) return;
    
    int foundIndex = -1;
    size_t listSize = m_levelIDs.size();
    int triggeredDownloads = 0;

    // Scan the list starting from the current index.
    for (size_t i = 0; i < listSize; i++) {
        int idx = (m_currentIndex + i) % listSize;
        int levelID = m_levelIDs[idx];

        // If it failed before, skip it.
        if (ThumbnailLoader::get().isFailed(levelID)) {
            continue;
        }

        // If it's already loaded, we found our candidate.
        if (ThumbnailLoader::get().isLoaded(levelID)) {
            foundIndex = idx;
            break;
        } else {
            // Not loaded and not failed -> queue a download so it may be ready next time.
            
            // Only auto-download the first 3 items in the list.
            // For the rest, we only show them if they're already cached.
            if (idx < 3) {
                // Limit to 3 requests per cycle to avoid spamming the queue.
                if (triggeredDownloads < 3) {
                    if (!ThumbnailLoader::get().isPending(levelID)) {
                        std::string fileName = fmt::format("{}.png", levelID);
                        ThumbnailLoader::get().requestLoad(levelID, fileName, [](CCTexture2D*, bool){}, 1);
                        triggeredDownloads++;
                    }
                }
            }
        }
    }

    if (foundIndex != -1) {
        // Found a valid image to show.
        int levelID = m_levelIDs[foundIndex];
        
        // Use a shared alive-flag so callbacks can bail out safely.
        auto alive = m_alive;
        auto* self = this;
        std::string fileName = fmt::format("{}.png", levelID);
        
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, alive, levelID](CCTexture2D* tex, bool) {
            if (!alive || !*alive) return;
            // Double-check parent just in case.
            if (!self->getParent()) return;

            if (self->m_loadingCircle) {
                self->m_loadingCircle->runAction(CCSequence::create(
                    CCFadeOut::create(0.2f),
                    CCCallFunc::create(self->m_loadingCircle, callfunc_selector(CCNode::removeFromParent)),
                    nullptr
                ));
                self->m_loadingCircle = nullptr;
            }
            if (tex) self->onImageLoaded(tex, levelID);
        }, 0);
        
        // Next time, start from the item after this one (rotates through the list).
        m_currentIndex = (foundIndex + 1) % listSize;
        
        // Schedule next rotation.
        this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
        this->schedule(schedule_selector(ListThumbnailCarousel::updateCarousel), 3.0f);
    } else {
        // No images ready yet; check again soon.
        this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
        this->schedule(schedule_selector(ListThumbnailCarousel::updateCarousel), 0.5f);
    }
    
    // Background: make sure the next item is queued with priority.
    int nextID = m_levelIDs[m_currentIndex];
    if (!ThumbnailLoader::get().isLoaded(nextID) && 
        !ThumbnailLoader::get().isFailed(nextID) && 
        !ThumbnailLoader::get().isPending(nextID)) {
         std::string fileName = fmt::format("{}.png", nextID);
         // Priority 1 to bump it up in the queue.
         ThumbnailLoader::get().requestLoad(nextID, fileName, [](CCTexture2D*, bool){}, 1);
    }
}

void ListThumbnailCarousel::onImageLoaded(CCTexture2D* texture, int index) {
    if (!texture) {
        return;
    }

    // If this node is no longer attached, avoid creating sprites/actions.
    if (!this->getParent()) {
        return;
    }

    // Safety check for texture validity.
    if (!ThumbnailLoader::isTextureSane(texture)) {
        return;
    }
    
    CCSprite* sprite = nullptr;
    
    #ifdef GEODE_IS_WINDOWS
    __try {
        sprite = CCSprite::createWithTexture(texture);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    #else
    try {
        sprite = CCSprite::createWithTexture(texture);
    } catch(...) {
        return;
    }
    #endif

    if (!sprite) return;
    
    // 1) Compute an aspect-fit visible rect.
    float targetAspect = m_size.width / m_size.height;
    float texWidth = texture->getContentSize().width;
    float texHeight = texture->getContentSize().height;
    
    float maxW = texWidth;
    float maxH = texWidth / targetAspect;
    
    if (maxH > texHeight) {
        maxH = texHeight;
        maxW = texHeight * targetAspect;
    }
    
    // 2) Apply a small zoom so the pan has some room.
    float zoom = 1.06f;
    float visibleW = maxW / zoom;
    float visibleH = maxH / zoom;
    
    // 3) Compute the available slack.
    float totalSlackW = texWidth - visibleW;
    
    // 4) Determine the pan range.
    // Limit movement to 10% of width to avoid fast jumps on wide images.
    float maxPan = visibleW * 0.10f;
    float travelX = std::min(totalSlackW, maxPan);
    
    // Center the travel range within the available slack.
    float unusedSlackX = totalSlackW - travelX;
    float offsetX = unusedSlackX / 2.0f;
    
    // 5) Pick a pan direction (random).
    bool panRight = (rand() % 2) == 0;
    
    float startX = panRight ? offsetX : (offsetX + travelX);
    float endX = panRight ? (offsetX + travelX) : offsetX;
    
    // Center Y (vertical).
    float startY = (texHeight - visibleH) / 2.0f;
    float endY = startY;
    
    m_panStartRect = CCRect(startX, startY, visibleW, visibleH);
    m_panEndRect = CCRect(endX, endY, visibleW, visibleH);
    m_panElapsed = 0.0f;
    
    sprite->setTextureRect(m_panStartRect);
    
    // Scale sprite to match the target size.
    float scale = m_size.width / visibleW;
    sprite->setScale(scale);
    
    // Center in the node.
    sprite->setPosition(m_size / 2);
    sprite->setOpacity(0);
    
    // Ensure a shader program is set (workaround for some mod interactions).
    if (!sprite->getShaderProgram()) {
        sprite->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
    }

    this->addChild(sprite);
    
    // Fade in the new sprite.
    sprite->runAction(CCFadeTo::create(0.5f, m_opacity));
    
    // Fade out and remove the previous sprite.
    if (m_currentSprite) {
        m_currentSprite->runAction(CCSequence::create(
            CCFadeOut::create(0.5f),
            CCCallFunc::create(m_currentSprite, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }
    
    m_currentSprite = sprite;
    
    // Ensure updatePan is scheduled.
    this->unschedule(schedule_selector(ListThumbnailCarousel::updatePan));
    this->schedule(schedule_selector(ListThumbnailCarousel::updatePan));
}

void ListThumbnailCarousel::setOpacity(GLubyte opacity) {
    m_opacity = opacity;
    if (m_currentSprite) {
        m_currentSprite->setOpacity(opacity);
    }
}

void ListThumbnailCarousel::visit() {
#ifdef GEODE_IS_WINDOWS
    __try {
        CCNode::visit();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Swallow SEH exceptions to avoid crashing during rendering.
    }
#else
    CCNode::visit();
#endif
}
