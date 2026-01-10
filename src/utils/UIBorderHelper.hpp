#pragma once

#include <cocos2d.h>
#include "Constants.hpp"

/**
 * Utility class for creating consistent UI borders.
 * Eliminates duplicate border creation code.
 */
class UIBorderHelper {
public:
    /**
     * Create a complete border around a rectangular area.
     * @param centerX Center X position
     * @param centerY Center Y position
     * @param width Frame width
     * @param height Frame height
     * @param thickness Border thickness (default: BORDER_THICKNESS)
     * @param color Border color (default: black with alpha 128)
     * @param parent Parent node to add borders to
     * @param zOrder Z-order for the borders (default: 1)
     */
    static void createBorder(
        float centerX, 
        float centerY, 
        float width, 
        float height,
        cocos2d::CCNode* parent,
        float thickness = PaimonConstants::BORDER_THICKNESS,
        cocos2d::ccColor4B color = {0, 0, 0, PaimonConstants::DARK_OVERLAY_ALPHA},
        int zOrder = 1
    );
    
    /**
     * Create individual border segments.
     */
    static cocos2d::CCLayerColor* createTopBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
    static cocos2d::CCLayerColor* createBottomBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
    static cocos2d::CCLayerColor* createLeftBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
    static cocos2d::CCLayerColor* createRightBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
};

