#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <functional>
#include <vector>
#include <string>

// A layer that enables dragging CCMenuItems within a menu.
// When edit mode is enabled, intercepts touch events and allows users to drag buttons.
// Positions are saved via ButtonLayoutManager when the user releases the button.

class ButtonEditLayer : public cocos2d::CCLayer {
public:
    static ButtonEditLayer* create(const std::string& sceneKey, cocos2d::CCMenu* menu);
    bool init(const std::string& sceneKey, cocos2d::CCMenu* menu);

    // Enable/disable edit mode
    void setEditMode(bool enabled);
    bool isEditMode() const { return m_editMode; }

    // Callback when edit mode changes (for UI feedback)
    std::function<void(bool)> onEditModeChanged;

private:
    // Touch handling
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    // Find the topmost CCMenuItem under a touch point
    cocos2d::CCMenuItem* findButtonAtPoint(cocos2d::CCPoint worldPos);

    std::string m_sceneKey;
    cocos2d::CCMenu* m_menu = nullptr;
    bool m_editMode = false;

    // Currently dragged button
    cocos2d::CCMenuItem* m_draggedButton = nullptr;
    cocos2d::CCPoint m_dragStartPos;
    cocos2d::CCPoint m_originalButtonPos;
};

