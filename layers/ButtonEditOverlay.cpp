#include "ButtonEditOverlay.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include "../utils/Localization.hpp"
#include <Geode/loader/Log.hpp>

using namespace cocos2d;
using namespace geode::prelude;

ButtonEditOverlay* ButtonEditOverlay::create(const std::string& sceneKey, CCMenu* menu) {
    auto ret = new ButtonEditOverlay();
    if (ret && ret->init(sceneKey, menu)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ButtonEditOverlay::init(const std::string& sceneKey, CCMenu* menu) {
    if (!CCLayer::init()) return false;
    
    m_sceneKey = sceneKey;
    m_targetMenu = menu;
    if (m_targetMenu) m_targetMenu->retain();
    m_selectedButton = nullptr;
    m_draggedButton = nullptr;
    
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    m_darkBG = CCLayerColor::create(ccc4(0, 0, 0, 120));
    m_darkBG->setContentSize(winSize);
    m_darkBG->setZOrder(-1);
    this->addChild(m_darkBG);
    
    collectEditableButtons();
    
    for (auto& btn : m_editableButtons) {
        if (btn.item && btn.item->getParent()) {
            btn.item->setZOrder(1000);
        }
    }
    
    createControls();
    showControls(false);
    
    m_selectionHighlight = CCScale9Sprite::create("square02b_001.png");
    if (auto rgba = geode::cast::typeinfo_cast<CCRGBAProtocol*>(m_selectionHighlight)) {
        rgba->setColor(ccColor3B{100, 255, 100});
        rgba->setOpacity(150);
    }
    m_selectionHighlight->setVisible(false);
    m_selectionHighlight->setZOrder(999);
    
    if (m_targetMenu && m_targetMenu->getParent()) {
        m_targetMenu->getParent()->addChild(m_selectionHighlight, 999);
    }

    createAllHighlights();
    
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-500);
    this->scheduleUpdate();
    
    return true;
}

ButtonEditOverlay::~ButtonEditOverlay() {
    // Safety: ensure highlight is detached and retained menu released
    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    clearAllHighlights();
    if (m_targetMenu) {
        m_targetMenu->release();
        m_targetMenu = nullptr;
    }
}

void ButtonEditOverlay::collectEditableButtons() {
    m_editableButtons.clear();
    
    if (!m_targetMenu) return;

    auto children = m_targetMenu->getChildren();
    if (!children) return;

    CCObject* child = nullptr;
    CCARRAY_FOREACH(children, child) {
        auto item = dynamic_cast<CCMenuItem*>(child);
        if (!item) continue;

        std::string buttonID = item->getID();
        if (buttonID.empty()) continue;

        EditableButton editable;
        editable.item = item;
        editable.buttonID = buttonID;
        editable.originalPos = item->getPosition();
        editable.originalScale = item->getScale();
        editable.originalOpacity = item->getOpacity() / 255.0f;

        m_editableButtons.push_back(editable);
    }
    
    log::info("[ButtonEditOverlay] Collected {} editable buttons", m_editableButtons.size());
}

void ButtonEditOverlay::createControls() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    m_controlsMenu = CCMenu::create();
    m_controlsMenu->setPosition({0, 0});
    m_controlsMenu->setZOrder(1001);
    this->addChild(m_controlsMenu);

    // Fresh, consistent layout
    // Bottom margin from screen edge
    const float bottomMargin = 60.f;
    const float leftX = 90.f;        // left column for labels
    const float rowGap = 40.f;       // vertical distance between rows
    const float row1Y = bottomMargin + 60.f; // scale row Y
    const float row2Y = bottomMargin + 20.f; // opacity row Y
    const float rightMargin = 110.f; // margin from right screen edge for action buttons
    const float actionGapY = 40.f;   // vertical gap between Accept & Reset
    const float actionX = winSize.width - rightMargin;
    const float valueX = actionX - 70.f; // numeric value just left of actions
    const float sliderX = valueX - 180.f; // slider center a bit left of value
    
    // Scale controls
    auto scaleText = CCLabelBMFont::create(Localization::get().getString("edit.scale").c_str(), "bigFont.fnt");
    scaleText->setScale(0.4f);
    scaleText->setPosition({leftX + 30.f, row1Y});
    this->addChild(scaleText);
    
    m_scaleLabel = CCLabelBMFont::create("1.00x", "goldFont.fnt");
    m_scaleLabel->setScale(0.45f);
    m_scaleLabel->setPosition({valueX, row1Y});
    this->addChild(m_scaleLabel);

    m_scaleSlider = Slider::create(this, menu_selector(ButtonEditOverlay::onScaleChanged));
    m_scaleSlider->setPosition({sliderX, row1Y - 50.f});
    m_scaleSlider->setScale(0.7f);
    m_scaleSlider->setValue(0.5f);
    this->addChild(m_scaleSlider);

    // Opacity controls
    auto opacityText = CCLabelBMFont::create(Localization::get().getString("edit.opacity").c_str(), "bigFont.fnt");
    opacityText->setScale(0.4f);
    opacityText->setPosition({leftX + 30.f, row2Y});
    this->addChild(opacityText);

    m_opacityLabel = CCLabelBMFont::create("100%", "goldFont.fnt");
    m_opacityLabel->setScale(0.45f);
    m_opacityLabel->setPosition({valueX, row2Y});
    this->addChild(m_opacityLabel);

    m_opacitySlider = Slider::create(this, menu_selector(ButtonEditOverlay::onOpacityChanged));
    m_opacitySlider->setPosition({sliderX, row2Y - 50.f});
    m_opacitySlider->setScale(0.7f);
    m_opacitySlider->setValue(1.0f);
    this->addChild(m_opacitySlider);

    // Accept and Reset buttons
    auto acceptSpr = ButtonSprite::create(Localization::get().getString("edit.accept").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.8f);
    auto acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(ButtonEditOverlay::onAccept));
        PaimonButtonHighlighter::registerButton(acceptBtn);
    float acceptY = row1Y + 10.f; // near first row
    acceptBtn->setPosition({actionX + 30.f, acceptY - 20.f});

    auto resetSpr = ButtonSprite::create(Localization::get().getString("edit.reset").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.8f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ButtonEditOverlay::onReset));
        PaimonButtonHighlighter::registerButton(resetBtn);
    resetBtn->setPosition({actionX + 30.f, acceptY - actionGapY - 20.f});

    m_controlsMenu->addChild(acceptBtn);
    m_controlsMenu->addChild(resetBtn);
}

void ButtonEditOverlay::showControls(bool show) {
    if (m_scaleSlider) m_scaleSlider->setVisible(show);
    if (m_opacitySlider) m_opacitySlider->setVisible(show);
    if (m_scaleLabel) m_scaleLabel->setVisible(show);
    if (m_opacityLabel) m_opacityLabel->setVisible(show);
}

void ButtonEditOverlay::update(float) {
    // If the menu or its parent is gone (scene changed), close safely
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Target menu no longer valid; closing editor to avoid crash");
        m_isClosing = true;
        // Disable controls immediately to avoid late clicks
        if (m_controlsMenu) m_controlsMenu->setTouchEnabled(false);
        this->setTouchEnabled(false);
        // Remove selection highlight if still attached
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        clearAllHighlights();
        // Remove overlay
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    // Keep all per-button highlights in sync with their items
    updateAllHighlights();
}

void ButtonEditOverlay::selectButton(EditableButton* btn) {
    m_selectedButton = btn;
    
    if (!btn) {
        showControls(false);
        m_selectionHighlight->setVisible(false);
        return;
    }

    showControls(true);
    
    // Set slider values from current button state
    float currentScale = btn->item->getScale();
    float currentOpacity = btn->item->getOpacity() / 255.0f;
    
    // Map scale [0.3, 2.0] to slider [0, 1]
    float scaleNorm = (currentScale - 0.3f) / 1.7f;
    m_scaleSlider->setValue(std::max(0.f, std::min(1.f, scaleNorm)));
    
    m_opacitySlider->setValue(currentOpacity);
    
    updateSliderLabels();
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::updateSelectionHighlight() {
    if (!m_selectedButton || !m_selectionHighlight) return;
    
    auto item = m_selectedButton->item;
    auto contentSize = item->getContentSize();
    float scale = item->getScale();
    
    m_selectionHighlight->setContentSize({
        contentSize.width * scale + 10.f,
        contentSize.height * scale + 10.f
    });
    
    auto worldPos = item->getParent()->convertToWorldSpace(item->getPosition());
    m_selectionHighlight->setPosition(worldPos);
    m_selectionHighlight->setVisible(true);
}

void ButtonEditOverlay::updateSliderLabels() {
    if (!m_selectedButton) return;
    
    float scale = m_selectedButton->item->getScale();
    float opacity = m_selectedButton->item->getOpacity() / 255.0f * 100.0f;
    
    m_scaleLabel->setString(fmt::format("{:.2f}x", scale).c_str());
    m_opacityLabel->setString(fmt::format("{:.0f}%", opacity).c_str());
}

EditableButton* ButtonEditOverlay::findButtonAtPoint(CCPoint worldPos) {
    for (auto& btn : m_editableButtons) {
        if (!btn.item) continue;
        
        auto parent = btn.item->getParent();
        if (!parent) continue;
        
        auto localPos = parent->convertToNodeSpace(worldPos);
        auto bbox = btn.item->boundingBox();
        
        if (bbox.containsPoint(localPos)) {
            return &btn;
        }
    }
    return nullptr;
}

bool ButtonEditOverlay::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto touchPos = touch->getLocation();
    auto foundBtn = findButtonAtPoint(touchPos);
    
    if (foundBtn) {
        m_draggedButton = foundBtn;
        m_dragStartPos = touchPos;
        m_originalButtonPos = foundBtn->item->getPosition();
        
        // Select the button
        selectButton(foundBtn);
        
        return true;
    }
    
    // If no button touched, deselect
    selectButton(nullptr);
    return true;
}

void ButtonEditOverlay::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_draggedButton || !m_draggedButton->item) return;
    
    auto touchPos = touch->getLocation();
    auto delta = ccpSub(touchPos, m_dragStartPos);
    auto newPos = ccpAdd(m_originalButtonPos, delta);
    
    m_draggedButton->item->setPosition(newPos);
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_draggedButton = nullptr;
}

void ButtonEditOverlay::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_draggedButton = nullptr;
}

void ButtonEditOverlay::onScaleChanged(CCObject*) {
    if (!m_selectedButton || !m_selectedButton->item) return;
    
    float sliderValue = m_scaleSlider->getValue();
    float scale = 0.3f + sliderValue * 1.7f; // Map [0,1] to [0.3, 2.0]
    
    m_selectedButton->item->setScale(scale);
    updateSliderLabels();
    updateSelectionHighlight();
}

void ButtonEditOverlay::onOpacityChanged(CCObject*) {
    if (!m_selectedButton || !m_selectedButton->item) return;
    
    float opacity = m_opacitySlider->getValue();
    m_selectedButton->item->setOpacity(static_cast<GLubyte>(opacity * 255));
    updateSliderLabels();
}

void ButtonEditOverlay::onAccept(CCObject*) {
    if (m_isClosing) {
        // Already closing; ignore action
        return;
    }
    // If context is gone (navigated away), just close safely
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Accept pressed after leaving page; closing without saving");
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->removeFromParent();
        return;
    }

    // Save all button layouts from the current menu children by ID (avoid stale pointers)
    if (auto children = m_targetMenu->getChildren()) {
        cocos2d::CCObject* obj = nullptr;
        CCARRAY_FOREACH(children, obj) {
            auto node = dynamic_cast<cocos2d::CCNode*>(obj);
            auto item = dynamic_cast<cocos2d::CCMenuItem*>(node);
            if (!item) continue;
            std::string id = item->getID();
            if (id.empty()) continue;

            ButtonLayout layout;
            layout.position = item->getPosition();
            layout.scale = item->getScale();
            layout.opacity = item->getOpacity() / 255.0f;
            ButtonLayoutManager::get().setLayout(m_sceneKey, id, layout);
        }
        ButtonLayoutManager::get().save();
    }
    
    // Remove selection highlight
    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    
    // Remove overlay
    m_isClosing = true;
    this->removeFromParent();
}

void ButtonEditOverlay::onReset(CCObject*) {
    if (m_isClosing) {
        return;
    }
    // If context is gone, just clear saved layouts and close
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        ButtonLayoutManager::get().resetScene(m_sceneKey);
        ButtonLayoutManager::get().save();
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->removeFromParent();
        return;
    }

    // Restore buttons to persisted defaults when available, otherwise to captured originals
    for (auto& btn : m_editableButtons) {
        if (btn.buttonID.empty()) continue;
        auto node = m_targetMenu->getChildByID(btn.buttonID);
        auto item = dynamic_cast<cocos2d::CCMenuItem*>(node);
        if (!item) continue;

        auto def = ButtonLayoutManager::get().getDefaultLayout(m_sceneKey, btn.buttonID);
        if (def) {
            item->setPosition(def->position);
            item->setScale(def->scale);
            item->setOpacity(static_cast<GLubyte>(def->opacity * 255));
        } else {
            item->setPosition(btn.originalPos);
            item->setScale(btn.originalScale);
            item->setOpacity(static_cast<GLubyte>(btn.originalOpacity * 255));
        }
    }

    // Clear saved layouts for this scene and persist
    ButtonLayoutManager::get().resetScene(m_sceneKey);
    ButtonLayoutManager::get().save();

    // Deselect
    selectButton(nullptr);
}

void ButtonEditOverlay::createAllHighlights() {
    clearAllHighlights();
    if (!m_targetMenu || !m_targetMenu->getParent()) return;

    auto parent = m_targetMenu->getParent();
    for (auto& btn : m_editableButtons) {
        if (!btn.item || btn.buttonID.empty()) continue;
        auto spr = CCScale9Sprite::create("square02b_001.png");
        if (!spr) continue;
        if (auto rgba = geode::cast::typeinfo_cast<CCRGBAProtocol*>(spr)) {
            // Different color from selection (e.g., cyan-ish)
            rgba->setColor(ccColor3B{80, 180, 255});
            rgba->setOpacity(120);
        }
        spr->setZOrder(998);
        parent->addChild(spr, 998);
        m_buttonHighlights[btn.buttonID] = spr;
    }
    updateAllHighlights();
}

void ButtonEditOverlay::updateAllHighlights() {
    if (!m_targetMenu) return;
    for (auto& btn : m_editableButtons) {
        if (!btn.item || btn.buttonID.empty()) continue;
        auto it = m_buttonHighlights.find(btn.buttonID);
        if (it == m_buttonHighlights.end()) continue;
        auto node = it->second;
        if (!node) continue;

        auto contentSize = btn.item->getContentSize();
        float scale = btn.item->getScale();
        node->setContentSize({ contentSize.width * scale + 10.f, contentSize.height * scale + 10.f });

        if (auto parent = btn.item->getParent()) {
            auto worldPos = parent->convertToWorldSpace(btn.item->getPosition());
            node->setPosition(worldPos);
            node->setVisible(true);
        } else {
            node->setVisible(false);
        }
    }
}

void ButtonEditOverlay::clearAllHighlights() {
    for (auto it = m_buttonHighlights.begin(); it != m_buttonHighlights.end(); ++it) {
        auto node = it->second;
        if (node && node->getParent()) {
            node->removeFromParent();
        }
    }
    m_buttonHighlights.clear();
}

