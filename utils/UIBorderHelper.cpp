#include "UIBorderHelper.hpp"

using namespace cocos2d;

void UIBorderHelper::createBorder(
    float centerX, 
    float centerY, 
    float width, 
    float height,
    CCNode* parent,
    float thickness,
    ccColor4B color,
    int zOrder
) {
    if (!parent) return;
    
    // Top border
    auto topBorder = createTopBorder(centerX, centerY, width, height, thickness, color);
    parent->addChild(topBorder, zOrder);
    
    // Bottom border
    auto bottomBorder = createBottomBorder(centerX, centerY, width, height, thickness, color);
    parent->addChild(bottomBorder, zOrder);
    
    // Left border
    auto leftBorder = createLeftBorder(centerX, centerY, width, height, thickness, color);
    parent->addChild(leftBorder, zOrder);
    
    // Right border
    auto rightBorder = createRightBorder(centerX, centerY, width, height, thickness, color);
    parent->addChild(rightBorder, zOrder);
}

CCLayerColor* UIBorderHelper::createTopBorder(float centerX, float centerY, float width, float height, float thickness, ccColor4B color) {
    auto border = CCLayerColor::create(color);
    border->setContentSize({width, thickness});
    border->setPosition({
        centerX - width / 2,
        centerY + height / 2
    });
    return border;
}

CCLayerColor* UIBorderHelper::createBottomBorder(float centerX, float centerY, float width, float height, float thickness, ccColor4B color) {
    auto border = CCLayerColor::create(color);
    border->setContentSize({width, thickness});
    border->setPosition({
        centerX - width / 2,
        centerY - height / 2 - thickness
    });
    return border;
}

CCLayerColor* UIBorderHelper::createLeftBorder(float centerX, float centerY, float width, float height, float thickness, ccColor4B color) {
    auto border = CCLayerColor::create(color);
    border->setContentSize({thickness, height + thickness * 2});
    border->setPosition({
        centerX - width / 2 - thickness,
        centerY - height / 2 - thickness
    });
    return border;
}

CCLayerColor* UIBorderHelper::createRightBorder(float centerX, float centerY, float width, float height, float thickness, ccColor4B color) {
    auto border = CCLayerColor::create(color);
    border->setContentSize({thickness, height + thickness * 2});
    border->setPosition({
        centerX + width / 2,
        centerY - height / 2 - thickness
    });
    return border;
}

