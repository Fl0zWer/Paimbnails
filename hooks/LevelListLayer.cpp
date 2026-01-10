#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "../managers/ThumbnailLoader.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListLayer, LevelListLayer) {
    bool init(GJLevelList* list) {
        // Save list ID for LevelInfoLayer to use
        if (list) {
            Mod::get()->setSavedValue("current-list-id", list->m_listID);
            log::info("Entered List: {}", list->m_listID);
        } else {
            Mod::get()->setSavedValue("current-list-id", 0);
        }

        // Ensure queue is resumed when entering a list view
        ThumbnailLoader::get().resumeQueue();
        return LevelListLayer::init(list);
    }

    void onEnter() {
        LevelListLayer::onEnter();
        ThumbnailLoader::get().resumeQueue();
    }

    void onExit() {
        LevelListLayer::onExit();
    }
};

class $modify(ContextTrackingBrowser, LevelBrowserLayer) {
    bool init(GJSearchObject* p0) {
        // Clear list ID when entering a normal browser (search, etc)
        // LevelListLayer::init calls LevelBrowserLayer::init, so this runs first,
        // then LevelListLayer::init sets the ID again.
        Mod::get()->setSavedValue("current-list-id", 0);
        return LevelBrowserLayer::init(p0);
    }
};

class $modify(ContextTrackingMenu, MenuLayer) {
    bool init() {
        Mod::get()->setSavedValue("current-list-id", 0);
        return MenuLayer::init();
    }
};
