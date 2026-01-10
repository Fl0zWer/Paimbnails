cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(PaimonThumbnails VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ==========================================
# ARQUITECTURAS MULTIPLATAFORMA
# ==========================================

# iOS y macOS: configurar arquitecturas correctas
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    message(STATUS "Building for iOS: arm64")
elseif (APPLE)
    # macOS: ambas arquitecturas (Intel y Apple Silicon)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    message(STATUS "Building for macOS: arm64 + x86_64")
endif()

# Optional WebP support: define HAVE_WEBP and link libwebp on your system or via CPM.
option(USE_WEBP "Enable WebP encode/decode (requires libwebp)" OFF)
if (USE_WEBP)
    add_compile_definitions(HAVE_WEBP=1)
    # If you have libwebp installed, uncomment the following and adjust as needed:
    # find_package(WebP REQUIRED)
    # target_link_libraries(${PROJECT_NAME} WebP::webp WebP::webpdemux)
endif()

# ==========================================
# CONFIGURACIONES POR PLATAFORMA
# ==========================================

if (WIN32)
    # Windows: WinHTTP y definiciones
    target_link_libraries(${PROJECT_NAME} winhttp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE GEODE_IS_WINDOWS)
    if (MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /FS)
    endif()
    message(STATUS "Building for Windows")
endif()

if (ANDROID)
    # Android: configuraciones específicas
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
    target_compile_definitions(${PROJECT_NAME} PRIVATE GEODE_IS_ANDROID)
    target_link_libraries(${PROJECT_NAME} log android)
    message(STATUS "Building for Android")
endif()

if (APPLE)
    # macOS/iOS: Frameworks del sistema
    target_compile_definitions(${PROJECT_NAME} PRIVATE GEODE_IS_APPLE)
    
    if (IOS OR "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
        target_compile_definitions(${PROJECT_NAME} PRIVATE GEODE_IS_IOS)
        message(STATUS "Building for iOS")
    else()
        target_compile_definitions(${PROJECT_NAME} PRIVATE GEODE_IS_MACOS)
        message(STATUS "Building for macOS")
    endif()
    
    # CoreGraphics e ImageIO para manipulación de imágenes
    find_library(CORE_GRAPHICS CoreGraphics REQUIRED)
    find_library(IMAGE_IO ImageIO REQUIRED)
    find_library(FOUNDATION Foundation REQUIRED)
    target_link_libraries(${PROJECT_NAME} ${CORE_GRAPHICS} ${IMAGE_IO} ${FOUNDATION})
endif()

setup_geode_mod(${PROJECT_NAME} EXTERNALS geode.node-ids:1.7.1)
# Refreshed for PaimonBannerPopup rename
