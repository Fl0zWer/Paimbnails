#pragma once

#include <Geode/DefaultInclude.hpp>
#include <functional>
#include <string>

namespace Assets {

// Load a button sprite from an override text file in the mod save dir.
// - key: logical button name (creates assets/buttons/<key>.txt if missing)
// - defaultContent: example content to write when file is created
// - fallback: factory used when no valid override is provided
//
// Override file format (first non-empty line is used):
//   frame:FrameName.png         -> loads from sprite frame atlas
//   file:C:\\path\\img.png     -> loads the PNG from absolute/relative path
//   C:\\path\\img.png           -> same as file:, path to PNG
//   (empty)                      -> use fallback
// Returns an autoreleased CCSprite* (or a valid empty sprite if fallback also fails)
cocos2d::CCSprite* loadButtonSprite(
    std::string const& key,
    std::string const& defaultContent,
    std::function<cocos2d::CCSprite*()> fallback
);

} // namespace Assets

