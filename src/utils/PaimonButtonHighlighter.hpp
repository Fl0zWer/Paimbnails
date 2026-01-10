#pragma once
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <cocos2d.h>
#include <vector>
#include <unordered_set>
#include <unordered_map>

// Helper to mark mod buttons
class PaimonButtonHighlighter {
public:
    // Register a mod button
    static void registerButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        
        // Keep original ID (prefix it)
        std::string currentID = btn->getID();
        // Avoid double-prefixing
        if (currentID.find("paimon-mod-btn") != 0) {
            std::string newID = currentID.empty() ? "paimon-mod-btn" : ("paimon-mod-btn-" + currentID);
            btn->setID(newID);
        }
    }
    
    // Check if a button is registered
    static bool isRegisteredButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        // ID-based so it survives remove/re-add.
        std::string id = btn->getID();
        return id.find("paimon-mod-btn") == 0;
    }
    
    // Legacy no-ops kept for API compatibility
    
    static void updateButtonScale(CCMenuItemSpriteExtra* btn) {}
    
    static void unregisterButton(CCMenuItemSpriteExtra* btn) {}
    
    static void ensureScaleCaptured(CCMenuItemSpriteExtra* btn) {}
    
    static float getBaseScale(CCMenuItemSpriteExtra* btn) { 
        return btn ? btn->getScale() : 1.0f; 
    }
    
    static void highlightAll() {}
    static void restoreAll() {}
};
