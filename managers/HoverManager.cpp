#include "HoverManager.hpp"
#include <Geode/utils/cocos.hpp>

HoverManager* HoverManager::s_instance = nullptr;

HoverManager::HoverManager() {}

HoverManager::~HoverManager() {
    clearAll();
    if (m_detectorLayer) {
        m_detectorLayer->removeFromParent();
        m_detectorLayer = nullptr;
    }
    s_instance = nullptr;
}

HoverManager* HoverManager::get() {
    if (!s_instance) {
        s_instance = new HoverManager();
        if (!s_instance->init()) {
            delete s_instance;
            s_instance = nullptr;
        }
    }
    return s_instance;
}

void HoverManager::destroy() {
    if (s_instance) {
        s_instance->removeFromParent();
        s_instance = nullptr;
    }
}

bool HoverManager::init() {
    if (!CCNode::init()) {
        return false;
    }
    
    // No mouse hover on Android/iOS; disable the system.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    m_enabled = false;
    log::info("[HoverManager] Hover system disabled on mobile platform (no mouse)");
    return true;
#endif
    
    // Detector layer (mouse platforms only).
    m_detectorLayer = CCLayer::create();
    m_detectorLayer->setID("hover-detector-layer"_spr);
    m_detectorLayer->retain();
    
    // Don't schedule updates here; do it when added to the scene.
    
    return true;
}

void HoverManager::update(float dt) {
    // Mobile: no-op.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return;
#endif
    
    static bool firstCall = true;
    if (firstCall) {
        log::info("[HoverManager] First update() call! System is working.");
        firstCall = false;
    }
    
    if (!m_enabled) {
        log::debug("[HoverManager] Update called but disabled");
        return;
    }
    
    static int frameCount = 0;
    if (frameCount++ % 60 == 0) {
        log::debug("[HoverManager] Update running, tracking {} cells", m_cellTracking.size());
    }
    
    // Get current mouse position.
    CCPoint currentMousePos = {0, 0};
    bool hasMousePos = false;
    
#if defined(_WIN32)
    auto* view = CCEGLView::sharedOpenGLView();
    if (view) {
        currentMousePos = view->getMousePosition();
        // Invert Y: mouse uses Y=0 at top, Cocos2d uses Y=0 at bottom.
        currentMousePos.y = view->getFrameSize().height - currentMousePos.y;
        hasMousePos = true;
        
        static int logCounter = 0;
        if (logCounter++ % 60 == 0) {
            log::debug("[HoverManager] Mouse pos: ({}, {})", currentMousePos.x, currentMousePos.y);
        }
    }
#endif
    
    if (!hasMousePos) {
        log::warn("[HoverManager] No mouse position available");
        return;
    }
    
    // Only re-check hover when the mouse moves.
    m_mouseMoved = (currentMousePos.x != m_lastMousePos.x || 
                   currentMousePos.y != m_lastMousePos.y);
    m_lastMousePos = currentMousePos;
    
    // Remove destroyed cells.
    cleanupDestroyedCells();
    
    // Update registered cells.
    for (auto& pair : m_cellTracking) {
        CellHoverInfo& info = pair.second;
        
        // Skip missing/hidden cells.
        if (!info.cell || !info.cell->isVisible()) {
            continue;
        }
        
        // Update detector position.
        updateDetectorPosition(info);
        
        // Check hover if mouse moved.
        bool wasHovered = info.isHovered;
        if (m_mouseMoved) {
            info.isHovered = checkMouseOver(info);
            
            if (info.isHovered != wasHovered) {
                log::info("[HoverManager] Cell hover changed to: {}", info.isHovered);
            }
        }
        
        // Notify hover state change.
        if (info.isHovered != wasHovered && info.onHoverChange) {
            log::debug("[HoverManager] Cell hover changed: {}", info.isHovered);
            info.onHoverChange(info.isHovered);
        }
        
        // Smooth hover lerp.
        float target = info.isHovered ? 1.0f : 0.0f;
        float speed = 8.0f;
        info.hoverLerp += (target - info.hoverLerp) * std::min(1.0f, dt * speed);
        
        // Notify hover lerp updates.
        if (info.onHoverUpdate) {
            info.onHoverUpdate(info.hoverLerp);
        }
    }
}

void HoverManager::registerCell(
    CCNode* cell,
    std::function<void(bool)> onHoverChange,
    std::function<void(float)> onHoverUpdate
) {
    if (!cell) return;
    
    // Mobile: disabled.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return;
#endif
    
    log::info("[HoverManager] Registering cell at {}", fmt::ptr(cell));
    
    // Ensure manager exists in the active scene.
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (scene && !this->getParent()) {
        scene->addChild(this, 10000); // Alto z-order
        // Schedule updates every frame.
        this->schedule(schedule_selector(HoverManager::update), 0.0f);
        log::info("[HoverManager] Added manager to scene and scheduled updates with selector");
        log::info("[HoverManager] Manager parent: {}, isRunning: {}", fmt::ptr(this->getParent()), this->isRunning());
    } else if (this->getParent()) {
        log::debug("[HoverManager] Manager already in scene");
    }
    
    // Ensure detector layer exists in the scene.
    if (!m_detectorLayer->getParent()) {
        if (scene) {
            scene->addChild(m_detectorLayer, 1000); // Z-order alto pero no bloquea clicks
            log::info("[HoverManager] Added detector layer to scene");
        } else {
            log::warn("[HoverManager] No running scene to add detector layer!");
            return;
        }
    }
    
    // If already tracked, update callbacks.
    if (m_cellTracking.find(cell) != m_cellTracking.end()) {
        auto& info = m_cellTracking[cell];
        info.onHoverChange = onHoverChange;
        info.onHoverUpdate = onHoverUpdate;
        log::info("[HoverManager] Updated callbacks for existing cell");
        return;
    }
    
    // Create invisible detector.
    auto detector = CCSprite::create();
    detector->setContentSize(cell->getContentSize());
    detector->setOpacity(0); // Invisible
    detector->setAnchorPoint({0.5f, 0.5f});
    
    // Add to detector layer.
    m_detectorLayer->addChild(detector);
    
    // Track cell.
    CellHoverInfo info;
    info.cell = cell;
    info.detector = detector;
    info.onHoverChange = onHoverChange;
    info.onHoverUpdate = onHoverUpdate;
    info.isHovered = false;
    info.hoverLerp = 0.0f;
    
    // Initial placement.
    updateDetectorPosition(info);
    
    m_cellTracking[cell] = info;
    
    log::debug("[HoverManager] Registered cell, total: {}", m_cellTracking.size());
}

void HoverManager::unregisterCell(CCNode* cell) {
    auto it = m_cellTracking.find(cell);
    if (it != m_cellTracking.end()) {
        // Remover detector
        if (it->second.detector) {
            it->second.detector->removeFromParent();
        }
        m_cellTracking.erase(it);
        log::debug("[HoverManager] Unregistered cell, remaining: {}", m_cellTracking.size());
    }
}

bool HoverManager::isCellHovered(CCNode* cell) const {
    auto it = m_cellTracking.find(cell);
    return (it != m_cellTracking.end()) ? it->second.isHovered : false;
}

float HoverManager::getCellHoverLerp(CCNode* cell) const {
    auto it = m_cellTracking.find(cell);
    return (it != m_cellTracking.end()) ? it->second.hoverLerp : 0.0f;
}

void HoverManager::clearAll() {
    for (auto& pair : m_cellTracking) {
        if (pair.second.detector) {
            pair.second.detector->removeFromParent();
        }
    }
    m_cellTracking.clear();
    log::debug("[HoverManager] Cleared all cells");
}

void HoverManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        // Reset hover state.
        for (auto& pair : m_cellTracking) {
            if (pair.second.isHovered && pair.second.onHoverChange) {
                pair.second.onHoverChange(false);
            }
            pair.second.isHovered = false;
            pair.second.hoverLerp = 0.0f;
        }
    }
}

void HoverManager::updateDetectorPosition(CellHoverInfo& info) {
    if (!info.cell || !info.detector) return;
    
    // Compute cell center in local coordinates.
    CCSize cellSize = info.cell->getContentSize();
    CCPoint centerLocal = {
        cellSize.width * 0.5f,
        cellSize.height * 0.5f
    };
    
    // Convert to world coordinates.
    CCPoint worldPos = info.cell->convertToWorldSpace(centerLocal);
    
    // Skip tiny movements.
    float threshold = 0.5f; // pixels
    if (std::abs(worldPos.x - info.lastWorldPos.x) < threshold &&
        std::abs(worldPos.y - info.lastWorldPos.y) < threshold) {
        return;
    }
    
    info.lastWorldPos = worldPos;
    
    // Convert to detector-layer space.
    CCPoint detectorPos = m_detectorLayer->convertToNodeSpace(worldPos);
    
    // Update detector position.
    info.detector->setPosition(detectorPos);
    
    // Sync size (in case the cell scaled).
    info.detector->setContentSize(cellSize);
}

bool HoverManager::checkMouseOver(const CellHoverInfo& info) const {
    if (!info.cell) return false;
    
    // Use the cell bounding box in world space.
    CCRect worldBounds = info.cell->boundingBox();
    
    // Convert to world space.
    if (auto parent = info.cell->getParent()) {
        CCPoint worldPos = parent->convertToWorldSpace(worldBounds.origin);
        worldBounds.origin = worldPos;
    }
    
    // Check mouse position.
    return worldBounds.containsPoint(m_lastMousePos);
}

void HoverManager::cleanupDestroyedCells() {
    std::vector<CCNode*> toRemove;
    
    for (auto& pair : m_cellTracking) {
        CCNode* cell = pair.first;
        
        // Remove cells that are gone or detached.
        if (!cell || !cell->getParent() || cell->retainCount() <= 1) {
            toRemove.push_back(cell);
        }
    }
    
    for (CCNode* cell : toRemove) {
        unregisterCell(cell);
    }
}

