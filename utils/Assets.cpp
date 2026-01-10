#include "Assets.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <filesystem>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
std::string trim(std::string s) {
    auto isspace2 = [](unsigned char c){ return std::isspace(c) != 0; };
    while (!s.empty() && isspace2(s.front())) s.erase(s.begin());
    while (!s.empty() && isspace2(s.back())) s.pop_back();
    return s;
}

std::filesystem::path cfgPathFor(std::string const& key) {
    auto base = Mod::get()->getSaveDir() / "assets" / "buttons";
    std::error_code ec; std::filesystem::create_directories(base, ec);
    return base / (key + ".txt");
}

void limitSpriteSize(CCSprite* spr, float maxDim = 45.0f) {
    if (!spr) return;
    float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
    if (currentSize > maxDim) {
        spr->setScale(maxDim / currentSize);
    }
}
}

namespace Assets {

CCSprite* loadButtonSprite(
    std::string const& key,
    std::string const& defaultContent,
    std::function<CCSprite*()> fallback
) {
    auto path = cfgPathFor(key);
    if (!std::filesystem::exists(path)) {
        // Write a helpful default file
        std::stringstream ss;
        ss << "# Button: " << key << "\n";
        ss << "# Supported formats (first non-empty line):\n";
        ss << "#   frame:FrameName.png\n";
        ss << "#   file:C:/path/to/my_button.png\n";
        ss << "#   C:/path/to/my_button.png\n";
        ss << "# Leave empty to use the default icon.\n";
        if (!defaultContent.empty()) {
            ss << defaultContent << "\n";
        }
        (void)file::writeString(path, ss.str());
    }

    auto txt = file::readString(path).unwrapOr("");
    std::stringstream s(txt);
    std::string line;
    std::string directive;
    while (std::getline(s, line)) {
        auto t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        directive = t; break;
    }

    // Try custom override from .txt file
    if (!directive.empty()) {
        // frame:Name
        constexpr std::string_view framePrefix = "frame:";
        constexpr std::string_view filePrefix = "file:";
        if (directive.rfind(framePrefix.data(), 0) == 0) {
            auto name = directive.substr(framePrefix.size());
            name = trim(name);
            if (!name.empty()) {
                if (auto spr = CCSprite::createWithSpriteFrameName(name.c_str())) {
                    return spr;
                }
            }
        } else {
            // file:Path or plain Path
            std::string pathStr = directive;
            if (directive.rfind(filePrefix.data(), 0) == 0) {
                pathStr = directive.substr(filePrefix.size());
                pathStr = trim(pathStr);
            }

            if (!pathStr.empty()) {
                // Relative paths resolve from the config file directory
                std::filesystem::path p = pathStr;
                if (!p.is_absolute()) p = cfgPathFor(key).parent_path() / p;
                if (std::filesystem::exists(p)) {
                    if (auto spr = CCSprite::create(geode::utils::string::pathToString(p).c_str())) {
                        limitSpriteSize(spr);
                        return spr;
                    }
                }
            }
        }
    }

    // Try default mod resources path: resources/buttons/{key}.png then resources/{key}.png
    auto modResourcePath = Mod::get()->getResourcesDir() / "buttons" / (key + ".png");
    if (std::filesystem::exists(modResourcePath)) {
        if (auto spr = CCSprite::create(geode::utils::string::pathToString(modResourcePath).c_str())) {
            limitSpriteSize(spr);
            return spr;
        }
    }
    auto modResourcePath2 = Mod::get()->getResourcesDir() / (key + ".png");
    if (std::filesystem::exists(modResourcePath2)) {
        if (auto spr = CCSprite::create(geode::utils::string::pathToString(modResourcePath2).c_str())) {
            limitSpriteSize(spr);
            return spr;
        }
    }

    // Fallback last
    return fallback();
}

} // namespace Assets

