#pragma once

#include <Geode/DefaultInclude.hpp>
#include <vector>

// Minimal helper to reserve positions without relying on the original layout.

class UILayoutGuard {
public:
    explicit UILayoutGuard(cocos2d::CCNode* container);

    // Register a node as an obstacle (container-space bounds)
    void addObstacleNode(cocos2d::CCNode* node, float inflate = 0.f);

    // Use container children as obstacles (optionally ignore some IDs)
    void collectObstacles(const std::vector<std::string>& ignoreIDs = {}, float inflate = 0.f);

    // Place near preferred position without overlaps
    cocos2d::CCPoint placeNonOverlapping(
        cocos2d::CCNode* node,
        cocos2d::CCPoint preferred,
        float padding = 4.f,
        float step = 8.f,
        float maxRadius = 160.f
    );

private:
    cocos2d::CCRect nodeRectInContainer(cocos2d::CCNode* node) const;
    cocos2d::CCRect inflateRect(const cocos2d::CCRect& r, float pad) const;
    bool intersectsAny(const cocos2d::CCRect& r) const;
    bool insideBounds(const cocos2d::CCRect& r, float margin = 2.f) const;

    cocos2d::CCNode* m_container = nullptr;
    std::vector<cocos2d::CCRect> m_obstacles;
};

