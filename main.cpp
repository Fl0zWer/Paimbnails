#include <Geode/Geode.hpp>
#include <geode.custom-keybinds/include/OptionalAPI.hpp>
#include "managers/ProfileThumbs.hpp"
#include "managers/LevelColors.hpp"
#include "utils/Localization.hpp"
#include <thread>
#include <chrono>
#include <filesystem>

using namespace geode::prelude;

#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

// Hooks removed (only used for PaimonCoinManager).


#include "managers/ThumbnailLoader.hpp"

// No reliable "$on_mod(Unloaded)" here; cache cleanup runs on startup instead.

$on_mod(Loaded) {
    bool optimizer = true;
    try {
        optimizer = Mod::get()->getSettingValue<bool>("optimizer");
    } catch(...) {}
    
    if (optimizer) {
        Mod::get()->setLoggingEnabled(false);
    }

    // Register keybinds
    try {
        using namespace keybinds;
        auto def = KeybindV2::create(KEY_T, ModifierV2::None);
        std::vector<geode::Ref<Bind>> defs; if (def.isOk()) defs.push_back(def.unwrap());
        auto cat = CategoryV2::create("Global").unwrapOr(nullptr);
        auto action = BindableActionV2::create(
            "paimon.level_thumbnails/capture",
            Localization::get().getString("capture.action_name"),
            Localization::get().getString("capture.action_desc"),
            defs, cat, false, Mod::get()
        );
        if (action.isOk()) {
             auto _ = BindManagerV2::registerBindable(action.unwrap());
             log::info("[PaimonThumbnails] Keybind 'capture' registered successfully.");
        }
    } catch (...) {
        log::warn("[PaimonThumbnails] Failed to register keybinds (Custom Keybinds mod might be missing or older version).");
    }

    log::info("[PaimonThumbnails][Init] Loaded event start");
    bool safeMode = false;
#ifdef GEODE_IS_ANDROID
    safeMode = Mod::get()->getSettingValue<bool>("android-safe-mode");
    log::info("[PaimonThumbnails][Init] Android detected. SafeMode={} ", safeMode ? "true" : "false");

    if (safeMode) {
        ThumbnailLoader::get().setMaxConcurrentTasks(2);
    }
#endif

    if (!safeMode) {
        // Clear cache on startup (requested behavior).
        ThumbnailLoader::get().clearDiskCache();

        log::info("[PaimonThumbnails] Queueing main level thumbnails...");
        for (int i = 1; i <= 22; i++) {
            std::string fileName = fmt::format("{}.png", i);
            ThumbnailLoader::get().requestLoad(i, fileName, nullptr, 0);
        }
    } else {
        log::info("[PaimonThumbnails][Init] Skipping cache wipe & prefetch due to SafeMode");
    }

    try {
        std::string langStr = Mod::get()->getSettingValue<std::string>("language");
        log::info("[PaimonThumbnails][Init] Language setting='{}'", langStr);
        if (langStr == "english") Localization::get().setLanguage(Localization::Language::ENGLISH);
        else Localization::get().setLanguage(Localization::Language::SPANISH);
    } catch (...) { log::warn("[PaimonThumbnails][Init] Failed to apply language"); }

    log::info("[PaimonThumbnails][Init] Applying startup tasks");

    // Clear profile cache on startup.
    auto profileDir = Mod::get()->getSaveDir() / "thumbnails" / "profiles";
    if (std::filesystem::exists(profileDir)) {
        std::error_code ec;
        std::filesystem::remove_all(profileDir, ec);
        if (ec) {
            log::warn("[PaimonThumbnails] Failed to delete profiles directory: {}", ec.message());
        } else {
            log::info("[PaimonThumbnails] Deleted profiles directory for update synchronization");
        }
    }

    // Bulk color extraction (skipped in Android SafeMode).
    if (!safeMode) {
        log::info("[PaimonThumbnails][Init] Scheduling color extraction thread");
        geode::Loader::get()->queueInMainThread([]() {
            std::thread([]() {
                try {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    LevelColors::get().extractColorsFromCache();
                    log::info("[PaimonThumbnails][Init] Color extraction finished");
                } catch (const std::exception& e) {
                    log::error("[PaimonThumbnails] Error extracting colors: {}", e.what());
                } catch (...) {
                    log::error("[PaimonThumbnails] Unknown error extracting colors");
                }
            }).detach();
        });
    } else {
        log::info("[PaimonThumbnails][Init] Color extraction skipped due to SafeMode");
    }

    log::info("[PaimonThumbnails][Init] Startup tasks complete");
}

