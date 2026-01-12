#include "BulkUploadPopup.hpp"
#include "../utils/FileDialog.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../managers/ProfilePrefs.hpp"
#include <regex>
#include <fstream>
#include "../utils/Localization.hpp"

using namespace geode::prelude;

BulkUploadPopup* BulkUploadPopup::create() {
    auto ret = new BulkUploadPopup();
    if (ret && ret->initAnchored(400.f, 280.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BulkUploadPopup::setup() {
    auto winSize = m_mainLayer->getContentSize();

    auto title = CCLabelBMFont::create(Localization::get().getString("bulk.title").c_str(), "goldFont.fnt");
    title->setScale(0.8f);
    title->setPosition(winSize.width / 2, winSize.height - 25);
    m_mainLayer->addChild(title);

    m_statusLabel = CCLabelBMFont::create(Localization::get().getString("bulk.select_folder_label").c_str(), "chatFont.fnt");
    m_statusLabel->setScale(0.5f);
    m_statusLabel->setPosition(winSize.width / 2, winSize.height - 60);
    m_statusLabel->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
    m_mainLayer->addChild(m_statusLabel);

    m_progressLabel = CCLabelBMFont::create(Localization::get().getString("bulk.progress_label").c_str(), "chatFont.fnt");
    m_progressLabel->setScale(0.45f);
    m_progressLabel->setPosition(winSize.width / 2, winSize.height - 85);
    m_mainLayer->addChild(m_progressLabel);

    auto infoText = CCLabelBMFont::create(
        Localization::get().getString("bulk.info_text").c_str(),
        "chatFont.fnt"
    );
    infoText->setScale(0.4f);
    infoText->setPosition(winSize.width / 2, winSize.height / 2 + 10);
    infoText->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
    m_mainLayer->addChild(infoText);

    m_buttonMenu = CCMenu::create();

    auto selectSprite = ButtonSprite::create(Localization::get().getString("bulk.select_folder").c_str(), "goldFont.fnt", "GJ_button_01.png", 0.8f);
    selectSprite->setScale(0.7f);
    auto selectBtn = CCMenuItemSpriteExtra::create(
        selectSprite,
        this,
        menu_selector(BulkUploadPopup::onSelectFolder)
    );
    selectBtn->setPosition(-100, -winSize.height / 2 + 50);
    m_buttonMenu->addChild(selectBtn);

    // Start upload button (disabled until folder is scanned)
    auto uploadSprite = ButtonSprite::create(Localization::get().getString("bulk.start_upload").c_str(), "goldFont.fnt", "GJ_button_01.png", 0.8f);
    uploadSprite->setScale(0.7f);
    auto uploadBtn = CCMenuItemSpriteExtra::create(
        uploadSprite,
        this,
        menu_selector(BulkUploadPopup::onStartUpload)
    );
    uploadBtn->setPosition(100, -winSize.height / 2 + 50);
    uploadBtn->setEnabled(false);
    uploadBtn->setTag(1); // Find later
    m_buttonMenu->addChild(uploadBtn);
    
    m_buttonMenu->setPosition(winSize.width / 2, winSize.height / 2);
    m_mainLayer->addChild(m_buttonMenu);
    
    return true;
}

void BulkUploadPopup::onSelectFolder(CCObject*) {
    if (m_isUploading) {
        FLAlertLayer::create(Localization::get().getString("general.error").c_str(), Localization::get().getString("bulk.upload_in_progress").c_str(), Localization::get().getString("general.ok").c_str())->show();
        return;
    }
    
    // Use Geode's file picker (cross-platform).
    geode::utils::file::pick(
        geode::utils::file::PickMode::OpenFolder,
        {
            Mod::get()->getSaveDir() / "cache",
            {} // No filters for folder
        }
    ).listen([this](geode::Result<std::filesystem::path>* result) {
        if (!result || result->isErr()) {
            return; // User cancelled or error.
        }
        
        auto folderPath = result->unwrap();
        this->updateStatus(Localization::get().getString("bulk.scanning"));

        // Scan in a background thread to avoid blocking the UI.
        std::thread([this, folder = folderPath]() {
            scanFolder(folder);

            // Update UI on the main thread.
            Loader::get()->queueInMainThread([this]() {
                if (m_thumbnailFiles.empty()) {
                    updateStatus(Localization::get().getString("bulk.no_thumbnails"));
                    m_progressLabel->setString(Localization::get().getString("bulk.progress_label").c_str());
                } else {
                    updateStatus(fmt::format(fmt::runtime(Localization::get().getString("bulk.complete_msg")), 0, m_thumbnailFiles.size()));
                    m_progressLabel->setString(fmt::format("0 / {}", m_thumbnailFiles.size()).c_str());

                    // Enable the upload button.
                    if (auto uploadBtn = static_cast<CCMenuItemSpriteExtra*>(m_buttonMenu->getChildByTag(1))) {
                        uploadBtn->setEnabled(true);
                    }
                }
            });
        }).detach();
    });
}

void BulkUploadPopup::scanFolder(const std::filesystem::path& folder) {
    m_thumbnailFiles.clear();
    m_currentIndex = 0;
    m_successCount = 0;
    m_failCount = 0;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            
            auto ext = geode::utils::string::pathToString(entry.path().extension());
            // Normalize to lowercase.
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                // Expect filenames like levelID.png.
                auto filename = geode::utils::string::pathToString(entry.path().stem());
                int levelId = extractLevelIdFromFilename(filename);
                
                if (levelId > 0) {
                    m_thumbnailFiles.push_back(entry.path());
                }
            }
        }
    } catch (const std::exception& e) {
        log::error("Failed to scan folder: {}", e.what());
    }
}

int BulkUploadPopup::extractLevelIdFromFilename(const std::string& filename) {
    // Extract the first number from the filename.
    // Supported formats: "12345.png", "level_12345.png", "12345_thumb.png".
    std::regex numberRegex(R"((\d+))");
    std::smatch match;
    
    if (std::regex_search(filename, match, numberRegex)) {
        auto result = geode::utils::numFromString<int>(match[1].str());
        if (!result.isOk()) {
            return -1;
        }
        return result.unwrap();
    }
    
    return -1;
}

void BulkUploadPopup::onStartUpload(CCObject*) {
    if (m_isUploading) {
        return;
    }
    
    if (m_thumbnailFiles.empty()) {
        FLAlertLayer::create(Localization::get().getString("general.error").c_str(), Localization::get().getString("bulk.no_thumbnails").c_str(), Localization::get().getString("general.ok").c_str())->show();
        return;
    }
    
    // Get username from GameManager.
    std::string username;
    try {
        if (auto gm = GameManager::sharedState()) {
            username = gm->m_playerName;
        }
    } catch (...) {
        log::error("[BulkUpload] Failed to read username");
    }
    
    if (username.empty()) {
        FLAlertLayer::create(
            Localization::get().getString("general.error").c_str(),
            Localization::get().getString("profile.username_error").c_str(),
            Localization::get().getString("general.ok").c_str()
        )->show();
        return;
    }
    
    m_isUploading = true;
    m_currentIndex = 0;
    m_successCount = 0;
    m_failCount = 0;
    
    // Disable buttons during upload.
    if (auto uploadBtn = static_cast<CCMenuItemSpriteExtra*>(m_buttonMenu->getChildByTag(1))) {
        uploadBtn->setEnabled(false);
    }
    
    updateStatus(Localization::get().getString("bulk.start_upload"));
    uploadNext();
}

void BulkUploadPopup::uploadNext() {
    if (m_currentIndex >= m_thumbnailFiles.size()) {
        // Done.
        m_isUploading = false;
        updateStatus(fmt::format("{}: {} | {}: {}",
            Localization::get().getString("bulk.complete_title"),
            m_successCount,
            Localization::get().getString("general.error"),
            m_failCount
        ));
        
        // Keep upload disabled (requires a new scan).
        if (auto uploadBtn = static_cast<CCMenuItemSpriteExtra*>(m_buttonMenu->getChildByTag(1))) {
            uploadBtn->setEnabled(false);
        }
        return;
    }
    
    auto& filePath = m_thumbnailFiles[m_currentIndex];
    auto filename = filePath.stem().string();
    int levelId = extractLevelIdFromFilename(filename);
    
    updateProgress();
    updateStatus(fmt::format("{} {}...", Localization::get().getString("bulk.start_upload"), levelId));
    
    // Read PNG file.
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        log::error("Failed to open file: {}", filePath.generic_string());
        m_failCount++;
        m_currentIndex++;
        uploadNext();
        return;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> pngData(size);
    if (!file.read(reinterpret_cast<char*>(pngData.data()), size)) {
        log::error("Failed reading file: {}", filePath.generic_string());
        m_failCount++;
        m_currentIndex++;
        uploadNext();
        return;
    }
    
    auto gm = GameManager::sharedState();
    auto username = gm ? gm->m_playerName : "";
    
    // Upload thumbnail.
    ThumbnailAPI::get().uploadThumbnail(levelId, pngData, username,
        [this, levelId](bool success, const std::string& message) {
            if (success) {
                m_successCount++;
                log::info("Thumbnail uploaded successfully: level {}", levelId);
            } else {
                m_failCount++;
                log::error("Failed uploading thumbnail level {}: {}", levelId, message);
            }
            
            m_currentIndex++;
            
            // Small delay between uploads.
            this->runAction(CCSequence::create(
                CCDelayTime::create(0.5f),
                CCCallFunc::create(this, callfunc_selector(BulkUploadPopup::uploadNext)),
                nullptr
            ));
        }
    );
}

void BulkUploadPopup::updateStatus(const std::string& message) {
    if (m_statusLabel) {
        m_statusLabel->setString(message.c_str());
        m_statusLabel->limitLabelWidth(360.f, 0.5f, 0.1f);
    }
}

void BulkUploadPopup::updateProgress() {
    if (m_progressLabel) {
        m_progressLabel->setString(
            fmt::format("{} / {} (OK: {} | Error: {})",
                m_currentIndex + 1,
                m_thumbnailFiles.size(),
                m_successCount,
                m_failCount
            ).c_str()
        );
    }
}

void BulkUploadPopup::onClose(CCObject* sender) {
    if (m_isUploading) {
        geode::createQuickPopup(
            "Warning",
            "An upload is in progress. <cy>Are you sure you want to close?</c>",
            "No", "Yes",
            [this](auto, bool btn2) {
                if (btn2) {
                    Popup::onClose(nullptr);
                }
            }
        );
    } else {
        Popup::onClose(sender);
    }
}

