#pragma once
#include <optional>
#include <string>
#include <filesystem>

namespace pt {
    // Open a native file picker dialog to select an image file (png/jpg/jpeg)
    // Windows-only implementation; on other platforms returns std::nullopt.
    std::optional<std::filesystem::path> openImageFileDialog();

    // Save dialog for images (defaults to PNG). Returns chosen path or nullopt on cancel.
    std::optional<std::filesystem::path> saveImageFileDialog(const std::wstring& defaultName);
    
    // Open a folder picker dialog. Returns chosen folder path or nullopt on cancel.
    std::optional<std::filesystem::path> openFolderDialog();
}

