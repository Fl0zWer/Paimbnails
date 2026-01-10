#pragma once

#include <Geode/DefaultInclude.hpp>
#include <string>
#include <unordered_map>
#include <cocos2d.h>

// Manages custom positions, scale, and opacity for mod buttons per scene and button ID.
// Positions are stored and loaded from a text file in the mod save directory.

struct ButtonLayout {
    cocos2d::CCPoint position;
    float scale = 1.0f;
    float opacity = 1.0f; // 0.0 to 1.0
};

class ButtonLayoutManager {
public:
    static ButtonLayoutManager& get();

    // Load saved layouts from file
    void load();
    // Save current layouts to file
    void save();
    // Load/save default layouts from/to file
    void loadDefaults();
    void saveDefaults();

    // Get saved layout for a button; returns nullopt if not customized
    std::optional<ButtonLayout> getLayout(const std::string& sceneKey, const std::string& buttonID) const;

    // Set a custom layout for a button
    void setLayout(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout);

    // Remove custom layout for a button (revert to default)
    void removeLayout(const std::string& sceneKey, const std::string& buttonID);

    // Check if a scene+button has a custom layout
    bool hasCustomLayout(const std::string& sceneKey, const std::string& buttonID) const;

    // Reset all layouts for a specific scene
    void resetScene(const std::string& sceneKey);

    // Defaults API: persisted baseline positions independent of user edits
    std::optional<ButtonLayout> getDefaultLayout(const std::string& sceneKey, const std::string& buttonID) const;
    // Set default only if absent; avoids overwriting once captured
    void setDefaultLayoutIfAbsent(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout);
    // Overwrite default for a button (used for migrations/tuning)
    void setDefaultLayout(const std::string& sceneKey, const std::string& buttonID, const ButtonLayout& layout);

private:
    ButtonLayoutManager() = default;
    // Map: sceneKey -> (buttonID -> ButtonLayout)
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_layouts;
    std::unordered_map<std::string, std::unordered_map<std::string, ButtonLayout>> m_defaults;
};

