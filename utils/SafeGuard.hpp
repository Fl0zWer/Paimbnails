#pragma once
#include <Geode/Geode.hpp>

// Simple guard macros to prevent crashes from unexpected exceptions
#define PAIMON_GUARD_BEGIN try {
#define PAIMON_GUARD_END \
  } catch (const std::exception& e) { \
    geode::prelude::log::error("[Guard] Exception: {}", e.what()); \
  } catch (...) { \
    geode::prelude::log::error("[Guard] Unknown exception"); \
  }

