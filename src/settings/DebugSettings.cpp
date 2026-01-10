#include <Geode/Geode.hpp>
#include "../utils/Debug.hpp"

using namespace geode::prelude;

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

void toggleConsole(bool enable) {
    PaimonDebug::setEnabled(enable);
    
#ifdef GEODE_IS_WINDOWS
    if (enable) {
        if (GetConsoleWindow()) return; // Already open
        
        if (AllocConsole()) {
            FILE* fDummy;
            freopen_s(&fDummy, "CONOUT$", "w", stdout);
            freopen_s(&fDummy, "CONOUT$", "w", stderr);
            freopen_s(&fDummy, "CONIN$", "r", stdin);
            
            std::cout.clear();
            std::cerr.clear();
            std::clog.clear();
            std::cin.clear();
            
            // Print a welcome message
            std::cout << "Paimbnails Debug Console Enabled" << std::endl;
            PaimonDebug::log("Console enabled via Paimbnails debug mode.");
        }
    }
#endif
}

$execute {
    // Initialize default state
    bool initial = Mod::get()->getSettingValue<bool>("show-console");
    PaimonDebug::setEnabled(initial);

    listenForSettingChanges("show-console", +[](bool value) {
        if (value) {
            auto password = Mod::get()->getSettingValue<std::string>("debug-password");
            if (password == "Paimon285") {
                toggleConsole(true);
                Notification::create("Debug Console Enabled", NotificationIcon::Success)->show();
            } else {
                Notification::create("Incorrect Password", NotificationIcon::Error)->show();
                PaimonDebug::setEnabled(false);
                
                // Revert the setting to false
                Loader::get()->queueInMainThread([]{
                    Mod::get()->setSettingValue("show-console", false);
                });
            }
        } else {
            PaimonDebug::setEnabled(false);
        }
    });
}
