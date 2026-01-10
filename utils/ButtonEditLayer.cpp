#include "ButtonEditLayer.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/touch_dispatcher/CCTouchDispatcher.h>

using namespace cocos2d;
using namespace geode::prelude;

ButtonEditLayer* ButtonEditLayer::create(const std::string& sceneKey, CCMenu* menu) {
    auto ret = new ButtonEditLayer();
    if (ret && ret->init(sceneKey, menu)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ButtonEditLayer::init(const std::string& sceneKey, CCMenu* menu) {
    if (!CCLayer::init()) return false;

    m_sceneKey = sceneKey;
    m_menu = menu;
    m_editMode = false;
    m_draggedButton = nullptr;

    // Enable touch handling
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-500); // Higher priority to intercept touch

    return true;
}

void ButtonEditLayer::setEditMode(bool enabled) {
    if (m_editMode == enabled) return;
    m_editMode = enabled;

    if (enabled) {
        log::info("[ButtonEditLayer] Edit mode enabled for scene '{}'", m_sceneKey);
    } else {
        log::info("[ButtonEditLayer] Edit mode disabled for scene '{}'", m_sceneKey);
        // Reset drag state
        m_draggedButton = nullptr;
    }

    if (onEditModeChanged) {
        onEditModeChanged(m_editMode);
    }
}

CCMenuItem* ButtonEditLayer::findButtonAtPoint(CCPoint worldPos) {
    if (!m_menu) return nullptr;

    // Convert world position to menu's local coordinate space
    auto localPos = m_menu->convertToNodeSpace(worldPos);

    // Iterate children in reverse (topmost first)
    auto children = m_menu->getChildren();
    if (!children) return nullptr;

    for (int i = children->count() - 1; i >= 0; --i) {
        auto* node = static_cast<CCNode*>(children->objectAtIndex(i));
        auto* menuItem = dynamic_cast<CCMenuItem*>(node);
        if (!menuItem || !menuItem->isVisible() || !menuItem->isEnabled()) continue;

        // Check if the touch is within the button's bounding box
        auto bb = menuItem->boundingBox();
        if (bb.containsPoint(localPos)) {
            return menuItem;
        }
    }

    return nullptr;
}

bool ButtonEditLayer::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!m_editMode) return false;

    auto worldPos = touch->getLocation();
    auto* button = findButtonAtPoint(worldPos);

    if (button) {
        m_draggedButton = button;
        m_dragStartPos = worldPos;
        m_originalButtonPos = button->getPosition();
        log::debug("[ButtonEditLayer] Began dragging button at ({}, {})", m_originalButtonPos.x, m_originalButtonPos.y);
        return true; // Consume the touch
    }

    return false;
}

void ButtonEditLayer::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_editMode || !m_draggedButton) return;

    auto currentPos = touch->getLocation();
    auto delta = currentPos - m_dragStartPos;

    // Convert delta to menu's coordinate space
    auto menuLocalDelta = m_menu->convertToNodeSpace(m_menu->convertToWorldSpace(CCPointZero) + delta) 
                           - m_menu->convertToNodeSpace(m_menu->convertToWorldSpace(CCPointZero));

    auto newPos = m_originalButtonPos + menuLocalDelta;
    m_draggedButton->setPosition(newPos);
}

void ButtonEditLayer::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (!m_editMode || !m_draggedButton) return;

    auto finalPos = m_draggedButton->getPosition();
    auto buttonID = m_draggedButton->getID();

    log::info("[ButtonEditLayer] Drag ended for '{}' at ({}, {})", buttonID, finalPos.x, finalPos.y);

    // Save the new position (keeping existing scale/opacity if any)
    if (!buttonID.empty()) {
        ButtonLayout layout;
        layout.position = finalPos;
        
        // Try to preserve existing scale/opacity
        auto existing = ButtonLayoutManager::get().getLayout(m_sceneKey, buttonID);
        if (existing) {
            layout.scale = existing->scale;
            layout.opacity = existing->opacity;
        } else {
            layout.scale = m_draggedButton->getScale();
            layout.opacity = m_draggedButton->getOpacity() / 255.0f;
        }
        
        ButtonLayoutManager::get().setLayout(m_sceneKey, buttonID, layout);
    } else {
        log::warn("[ButtonEditLayer] Dragged button has no ID; cannot save position");
    }

    m_draggedButton = nullptr;
}

void ButtonEditLayer::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (!m_editMode || !m_draggedButton) return;

    // Revert to original position on cancel
    m_draggedButton->setPosition(m_originalButtonPos);
    log::debug("[ButtonEditLayer] Drag cancelled; reverted button position");
    m_draggedButton = nullptr;
}

