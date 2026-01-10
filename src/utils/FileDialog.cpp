#include "FileDialog.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif

namespace pt {
    std::optional<std::filesystem::path> openImageFileDialog() {
    #ifdef _WIN32
        OPENFILENAMEW ofn{};
        wchar_t fileBuffer[MAX_PATH] = {0};
        wchar_t filter[] = L"Image Files (*.webp;*.gif;*.png;*.jpg;*.jpeg)\0*.webp;*.gif;*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0\0";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
        ofn.lpstrDefExt = L"png";

        if (GetOpenFileNameW(&ofn)) {
            return std::filesystem::path(fileBuffer);
        }
        return std::nullopt;
    #else
        // macOS/iOS/Android: Not implemented yet.
        log::warn("[FileDialog] Image file dialog not supported on this platform");
        return std::nullopt;
    #endif
    }

    std::optional<std::filesystem::path> saveImageFileDialog(const std::wstring& defaultName) {
    #ifdef _WIN32
        OPENFILENAMEW ofn{};
        wchar_t fileBuffer[MAX_PATH] = {0};
        // Pre-fill default name
        wcsncpy_s(fileBuffer, defaultName.c_str(), _TRUNCATE);
        wchar_t filter[] = L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0\0";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        ofn.lpstrDefExt = L"png";

        if (GetSaveFileNameW(&ofn)) {
            std::filesystem::path p(fileBuffer);
            // Ensure .png extension
            if (p.extension().wstring() != L".png") p.replace_extension(L".png");
            return p;
        }
        return std::nullopt;
    #else
        // macOS/iOS/Android: Not implemented yet.
        log::warn("[FileDialog] Save file dialog not supported on this platform");
        return std::nullopt;
    #endif
    }
    
    std::optional<std::filesystem::path> openFolderDialog() {
    #ifdef _WIN32
        // Use IFileDialog (Windows Vista+) for a modern folder picker.
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        bool needsUninit = SUCCEEDED(hr);
        
        IFileDialog* pfd = nullptr;
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
        
        if (SUCCEEDED(hr)) {
            DWORD dwOptions;
            hr = pfd->GetOptions(&dwOptions);
            if (SUCCEEDED(hr)) {
                hr = pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
            }
            
            if (SUCCEEDED(hr)) {
                hr = pfd->Show(nullptr);
                if (SUCCEEDED(hr)) {
                    IShellItem* psi;
                    hr = pfd->GetResult(&psi);
                    if (SUCCEEDED(hr)) {
                        PWSTR pszPath = nullptr;
                        hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                        if (SUCCEEDED(hr)) {
                            std::filesystem::path result(pszPath);
                            CoTaskMemFree(pszPath);
                            psi->Release();
                            pfd->Release();
                            if (needsUninit) CoUninitialize();
                            return result;
                        }
                        psi->Release();
                    }
                }
            }
            pfd->Release();
        }
        
        if (needsUninit) CoUninitialize();
        return std::nullopt;
    #else
        // macOS/iOS/Android: Not implemented yet.
        log::warn("[FileDialog] Folder dialog not supported on this platform");
        return std::nullopt;
    #endif
    }
}

