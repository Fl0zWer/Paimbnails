// Original layout logic was removed; keep stubs for API compatibility.

#include "UILayout.hpp"
#include <Geode/utils/cocos.hpp>

using namespace cocos2d;
using namespace geode::prelude;

UILayoutGuard::UILayoutGuard(CCNode* container) : m_container(container) {}

CCRect UILayoutGuard::nodeRectInContainer(CCNode* node) const {
    if (!node || !node->getParent()) return CCRectZero;
    return node->boundingBox();
}

CCRect UILayoutGuard::inflateRect(const CCRect& r, float pad) const {
    return r; // no inflation
}

bool UILayoutGuard::intersectsAny(const CCRect& r) const {
    return false; // never intersects
}

bool UILayoutGuard::insideBounds(const CCRect& r, float margin) const {
    return true; // always inside
}

void UILayoutGuard::addObstacleNode(CCNode* node, float inflate) {
    // no-op
}

void UILayoutGuard::collectObstacles(const std::vector<std::string>& ignoreIDs, float inflate) {
    m_obstacles.clear();
}

CCPoint UILayoutGuard::placeNonOverlapping(CCNode* node, CCPoint preferred, float padding, float step, float maxRadius) {
    if (node) node->setPosition(preferred);
    return preferred; // pass-through
}

