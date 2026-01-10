#pragma once

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <cocos2d.h>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>

// Full-screen overlay for editing button positions, scale, and opacity.
// Features:
// - Dark background with low opacity (buttons remain bright on top)
// - Accept button to save and exit
// - Reset button to restore defaults
// - Selection system (tap button to select, shows controls)
// - Scale slider for selected button
// - Opacity slider for selected button
// - Visual highlight on selected button

struct EditableButton {
    cocos2d::CCMenuItem* item = nullptr;
    std::string buttonID;
    cocos2d::CCPoint originalPos;
    float originalScale = 1.0f;
    float originalOpacity = 1.0f;
};

class ButtonEditOverlay : public cocos2d::CCLayer {
protected:
    std::string m_sceneKey;
    cocos2d::CCMenu* m_targetMenu = nullptr;
    std::vector<EditableButton> m_editableButtons;
    EditableButton* m_selectedButton = nullptr;
    bool m_isClosing = false;
    
    cocos2d::CCLayerColor* m_darkBG = nullptr;
    cocos2d::CCNode* m_selectionHighlight = nullptr;
    std::unordered_map<std::string, cocos2d::CCNode*> m_buttonHighlights;
    
    cocos2d::CCMenu* m_controlsMenu = nullptr;
    Slider* m_scaleSlider = nullptr;
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;

    // Drag state
    EditableButton* m_draggedButton = nullptr;
    cocos2d::CCPoint m_dragStartPos;
    cocos2d::CCPoint m_originalButtonPos;

    bool init(const std::string& sceneKey, cocos2d::CCMenu* menu);
    
    void collectEditableButtons();
    void createControls();
    void selectButton(EditableButton* btn);
    void updateSelectionHighlight();
    void createAllHighlights();
    void updateAllHighlights();
    void clearAllHighlights();
    void updateSliderLabels();
    void showControls(bool show);
    void update(float dt) override;
    
    // Touch handling for dragging
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    
    EditableButton* findButtonAtPoint(cocos2d::CCPoint worldPos);
    
    void onAccept(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
    void onScaleChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);

public:
    static ButtonEditOverlay* create(const std::string& sceneKey, cocos2d::CCMenu* menu);
    ~ButtonEditOverlay() override;
};

