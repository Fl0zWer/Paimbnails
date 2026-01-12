#pragma once

#include <Geode/Geode.hpp>
#include <unordered_map>

// Forward declaration
class LevelCell;

// Per-cell hover tracking info.
struct CellHoverInfo {
    cocos2d::CCNode* cell = nullptr;              // Level cell node
    cocos2d::CCSprite* detector = nullptr;         // Sprite detector invisible
    cocos2d::CCPoint lastWorldPos = {0, 0};       // Last known position (optimization)
    bool isHovered = false;              // Estado actual de hover
    float hoverLerp = 0.0f;              // Smooth lerp (0.0 to 1.0)
    
    // Callbacks for hover state updates.
    std::function<void(bool)> onHoverChange = nullptr;
    std::function<void(float)> onHoverUpdate = nullptr;
};

/**
 * HoverManager - centralized hover detection for cells.
 * Keeps invisible detectors in a separate layer and syncs them to cells.
 */
class HoverManager : public cocos2d::CCNode {
private:
    static HoverManager* s_instance;
    
    cocos2d::CCLayer* m_detectorLayer = nullptr;
    std::unordered_map<cocos2d::CCNode*, CellHoverInfo> m_cellTracking;
    cocos2d::CCPoint m_lastMousePos = {0, 0};
    bool m_enabled = true;
    
    // Only re-check hover when the mouse moves.
    bool m_mouseMoved = false;
    
    HoverManager();
    
public:
    ~HoverManager();
    
    static HoverManager* get();
    static void destroy();
    
    bool init() override;
    void update(float dt) override;
    
    /**
        * Register a cell for hover tracking.
     */
    void registerCell(
        cocos2d::CCNode* cell,
        std::function<void(bool)> onHoverChange = nullptr,
        std::function<void(float)> onHoverUpdate = nullptr
    );
    
    /**
        * Unregister a cell.
     */
    void unregisterCell(cocos2d::CCNode* cell);
    
    /**
        * Returns whether a cell is hovered.
     */
    bool isCellHovered(cocos2d::CCNode* cell) const;
    
    /**
        * Returns the hover lerp value (0.0 to 1.0).
     */
    float getCellHoverLerp(cocos2d::CCNode* cell) const;
    
    /**
        * Remove all detectors.
     */
    void clearAll();
    
    /**
        * Enable/disable the system.
     */
    void setEnabled(bool enabled);
    
private:
    /**
     * Update a detector position to match its cell.
     */
    void updateDetectorPosition(CellHoverInfo& info);
    
    /**
     * Check whether the mouse is over the detector.
     */
    bool checkMouseOver(const CellHoverInfo& info) const;
    
    /**
     * Clean up detectors for destroyed cells.
     */
    void cleanupDestroyedCells();
};

