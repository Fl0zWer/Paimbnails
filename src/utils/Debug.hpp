#pragma once
#include <Geode/Geode.hpp>

namespace PaimonDebug {
    // Defined in DebugSettings.cpp or similar, but for header-only we can use inline static
    inline bool g_debugEnabled = false;

    inline void setEnabled(bool enabled) {
        g_debugEnabled = enabled;
    }

    inline bool isEnabled() {
        return g_debugEnabled;
    }

    template<typename... Args>
    void log(fmt::format_string<Args...> format, Args&&... args) {
        if (g_debugEnabled) {
            geode::log::info(format, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void warn(fmt::format_string<Args...> format, Args&&... args) {
        if (g_debugEnabled) {
            geode::log::warn(format, std::forward<Args>(args)...);
        }
    }
    
    inline void log(std::string_view str) {
        if (g_debugEnabled) {
            geode::log::info("{}", str);
        }
    }

    inline void warn(std::string_view str) {
        if (g_debugEnabled) {
            geode::log::warn("{}", str);
        }
    }
}
