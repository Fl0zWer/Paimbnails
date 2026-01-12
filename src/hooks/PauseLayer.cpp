#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <geode.custom-keybinds/include/OptionalAPI.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "../managers/LocalThumbs.hpp"
#include "../layers/CapturePreviewPopup.hpp"
#include "../managers/ThumbsRegistry.hpp"
#include "../utils/FramebufferCapture.hpp"
#include "../utils/DominantColors.hpp"
#include "../managers/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../managers/PendingQueue.hpp"
#include "../utils/Assets.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/GIFDecoder.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace keybinds;

// Helper: check if user is a moderator (sync).
static bool isUserModerator() {
    try {
        auto modDataPath = Mod::get()->getSaveDir() / "moderator_verification.dat";
        if (std::filesystem::exists(modDataPath)) {
            std::ifstream modFile(modDataPath, std::ios::binary);
            if (modFile) {
                time_t timestamp{};
                modFile.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
                modFile.close();
                auto now = std::chrono::system_clock::now();
                auto fileTime = std::chrono::system_clock::from_time_t(timestamp);
                auto daysDiff = std::chrono::duration_cast<std::chrono::hours>(now - fileTime).count() / 24;
                if (daysDiff < 30) {
                    return true;
                }
            }
        }
        return Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
    } catch (...) {
        return false;
    }
}

static CCSprite* tryCreateIcon() {
    // Try packed asset Capturadora.png first.
    if (auto spr = CCSprite::create("Capturadora.png"_spr)) {
        float targetSize = 35.0f;
        float currentSize = std::max(spr->getContentSize().width, spr->getContentSize().height);
        
        if (currentSize > 0) {
            float scale = targetSize / currentSize;
            spr->setScale(scale);
        }
        spr->setRotation(-90.0f);  // Rotate 90 degrees clockwise
        return spr;
    }
    // Fallback to a sprite frame.
    auto frameSpr = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (!frameSpr) frameSpr = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
    if (frameSpr) {
        float targetSize = 35.0f;
        float currentSize = std::max(frameSpr->getContentSize().width, frameSpr->getContentSize().height);
        
        if (currentSize > 0) {
            float scale = targetSize / currentSize;
            frameSpr->setScale(scale);
        } else {
            frameSpr->setScale(1.0f); // Reset scale just in case
        }
        frameSpr->setRotation(-90.0f);  // Rotate 90 degrees clockwise
    }
    log::info("[PauseLayer] Select-file button added");
    return frameSpr;
}

class $modify(PaimonPauseLayer, PauseLayer) {
    struct Fields {
    };
            // Rewire the game's native capture button to use the same handler as the T keybind.
            // Search both button menus for any item whose ID suggests "camera" or "screenshot".
    void customSetup() {
        PauseLayer::customSetup();
        
        log::info("PauseLayer customSetup called");

        if (!Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            log::info("Thumbnail taking disabled in settings");
            return;
        }

        auto playLayer = PlayLayer::get();
        if (!playLayer) {
                        // If the ID isn't helpful, fall back to normalImage type name.
            return;
        }
        
        if (!playLayer->m_level) {
            log::warn("Level not available in PlayLayer");
            return;
        }
        
        if (playLayer->m_level->m_levelID <= 0) {
            log::info("Level ID is {} (not saving thumbnails for this level)", playLayer->m_level->m_levelID.value());
            return;
        }

        auto rightMenu = this->getChildByID("right-button-menu");
        if (!rightMenu) {
            log::error("Right button menu not found in PauseLayer");
            return;
        }

        try {
            auto spr = tryCreateIcon();
            if (!spr) {
                log::error("Failed to create button sprite");
                return;
            }
            
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonPauseLayer::onScreenshot));
            if (!btn) {
                log::error("Failed to create menu button");
                return;
            }
            
            btn->setID("thumbnail-capture-button");
            rightMenu->addChild(btn);
            rightMenu->updateLayout();
            
            // Keep capture button position so other buttons can stack below.
            CCPoint capturePos = btn->getPosition();
            
            // Add moderator upload button.
            if (isUserModerator()) {
                int levelID = playLayer->m_level->m_levelID;
                
                // Only show if a local thumbnail exists.
                if (LocalThumbs::get().has(levelID)) {
                    log::info("[PauseLayer] User is moderator and local thumbnail exists; adding upload button");
                    
                    // Upload button sprite: prefer packed resource, else fallback.
                    auto uploadSpr = CCSprite::create("Subida.png"_spr);
                    
                    if (!uploadSpr) {
                        uploadSpr = Assets::loadButtonSprite(
                            "pause-upload",
                            "frame:GJ_downloadBtn_001.png",
                            []() {
                                if (auto spr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png")) return spr;
                                return CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
                            }
                        );
                    }
                    if (uploadSpr) {
                        float targetSize = 30.0f;
                        float currentSize = std::max(uploadSpr->getContentSize().width, uploadSpr->getContentSize().height);
                        
                        if (currentSize > 0) {
                            float scale = targetSize / currentSize;
                            uploadSpr->setScale(scale);
                        }
                        uploadSpr->setRotation(-90.0f); // Rotate 90 degrees.
                        
                        auto uploadBtn = CCMenuItemSpriteExtra::create(
                            uploadSpr, 
                            this, 
                            menu_selector(PaimonPauseLayer::onUploadThumbnail)
                        );
                        if (uploadBtn) {
                            uploadBtn->setID("thumbnail-upload-button");
                            rightMenu->addChild(uploadBtn);
                            rightMenu->updateLayout();
                            
                            log::info("[PauseLayer] Upload button added");
                        }
                    }
                }
            }
            
            // Add button to select a PNG/GIF from disk.
            {
                CCSprite* selectSpr = nullptr;
                
                // Try Subida.png first.
                selectSpr = CCSprite::create("Subida.png"_spr);
                
                if (!selectSpr) {
                    selectSpr = Assets::loadButtonSprite(
                        "pause-select-file",
                        "frame:GJ_folderBtn_001.png",
                        []() {
                            if (auto spr = CCSprite::createWithSpriteFrameName("GJ_folderBtn_001.png")) return spr;
                            return CCSprite::createWithSpriteFrameName("GJ_button_01.png");
                        }
                    );
                }

                if (selectSpr) {
                    float targetSize = 30.0f;
                    float currentSize = std::max(selectSpr->getContentSize().width, selectSpr->getContentSize().height);
                    
                    if (currentSize > 0) {
                        float scale = targetSize / currentSize;
                        selectSpr->setScale(scale);
                    }
                    selectSpr->setRotation(-90.0f);
                    
                    auto selectBtn = CCMenuItemSpriteExtra::create(
                        selectSpr,
                        this,
                        menu_selector(PaimonPauseLayer::onSelectPNGFile)
                    );
                    if (selectBtn) {
                        selectBtn->setID("thumbnail-select-button");
                        rightMenu->addChild(selectBtn);
                        rightMenu->updateLayout();
                        
                        log::info("[PauseLayer] Select-file button added");
                    }
                }
            }

            // Rewire the game's native capture button to use the same handler as the T keybind.
            // Search both button menus for any item whose ID suggests "camera" or "screenshot".
            auto rewireScreenshotInMenu = [this](CCNode* menu){
                if (!menu) return;
                CCArray* arr = menu->getChildren();
                if (!arr) return;
                CCObject* obj = nullptr;
                CCARRAY_FOREACH(arr, obj) {
                    auto* node = typeinfo_cast<CCNode*>(obj);
                    if (!node) continue;
                    std::string id = node->getID();
                    std::string idL = id; for (auto& c : idL) c = (char)tolower(c);
                    bool looksLikeCamera = (!idL.empty() && (idL.find("camera") != std::string::npos || idL.find("screenshot") != std::string::npos));
                    if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
                        // Si el ID no ayuda, intentamos heur�stica por nombre de clase del normalImage
                        if (!looksLikeCamera) {
                            if (auto* normal = item->getNormalImage()) {
                                auto cls = std::string(typeid(*normal).name());
                                auto clsL = cls; for (auto& c : clsL) c = (char)tolower(c);
                                if (clsL.find("camera") != std::string::npos || clsL.find("screenshot") != std::string::npos) {
                                    looksLikeCamera = true;
                                }
                            }
                        }

                        if (looksLikeCamera) {
                            log::info("[PauseLayer] Rewiring native capture button '{}' to onScreenshot", id);
                            item->setTarget(this, menu_selector(PaimonPauseLayer::onScreenshot));
                        }
                    }
                }
            };

            // Try both button menus.
            rewireScreenshotInMenu(this->getChildByID("right-button-menu"));
            rewireScreenshotInMenu(this->getChildByID("left-button-menu"));

            // Don't call updateLayout here; it would overwrite manual positions.
            log::info("Thumbnail capture + extra buttons added successfully");
        } catch (std::exception& e) {
            log::error("Exception while adding thumbnail button: {}", e.what());
        } catch (...) {
            log::error("Unknown exception while adding thumbnail button");
        }
    }

    void onScreenshot(CCObject*) {
        log::info("[PauseLayer] Capture button pressed; hiding pause menu");
        
        auto pl = PlayLayer::get();
        if (!pl) {
            log::error("[PauseLayer] PlayLayer not available");
            Notification::create(Localization::get().getString("pause.playlayer_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // Hide pause menu temporarily.
        this->setVisible(false);
        
        // Schedule capture and menu restore.
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->scheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore),
            this,
            0.05f,
            0,
            0.0f,
            false
        );
    }
    
    void performCaptureAndRestore(float dt) {
        try {
            log::info("[PauseLayer] Performing capture");
            InvokeBindEventV2("paimon.level_thumbnails/capture", true).post();
            
            // Schedule pause menu restore.
            auto scheduler = CCDirector::sharedDirector()->getScheduler();
            scheduler->scheduleSelector(
                schedule_selector(PaimonPauseLayer::restorePauseMenu),
                this,
                0.1f,
                0,
                0.0f,
                false
            );
        } catch (std::exception const& e) {
            log::error("[PauseLayer] Failed to invoke capture: {}", e.what());
            Notification::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
            this->setVisible(true);
        }
    }
    
    void restorePauseMenu(float dt) {
        this->setVisible(true);
        log::info("[PauseLayer] Pause menu restored");
    }

    void onUploadThumbnail(CCObject*) {
        log::info("[PauseLayer] Upload button pressed");
        
        try {
            auto pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available");
                return;
            }
            
            int levelID = pl->m_level->m_levelID;
            
            // Check that exists the thumbnail local
            if (!LocalThumbs::get().has(levelID)) {
                log::warn("[PauseLayer] No local thumbnail for level {}", levelID);
                Notification::create(Localization::get().getString("pause.no_local_thumb").c_str(), NotificationIcon::Warning)->show();
                return;
            }
            
            // Check with the server whether the user is a moderator before uploading.
            std::string username;
            try {
                auto* gm = GameManager::sharedState();
                if (gm) {
                    username = gm->m_playerName;
                    log::info("[PauseLayer] Username: '{}'", username);
                } else {
                    log::warn("[PauseLayer] GameManager::sharedState() is null");
                }
            } catch(...) {
                log::error("[PauseLayer] Exception accessing GameManager");
            }
            
            if (username.empty()) {
                log::error("[PauseLayer] Username is empty; cannot verify moderator status");
                Notification::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                return;
            }
            
            // Don't capture level pointer - it can become invalid during async operations
            Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();

            // Note: accountID validation removed - not available in current Geode bindings
            ThumbnailAPI::get().checkModeratorAccount(username, 0, [levelID, username](bool approved, bool isAdmin) {
                bool allowModeratorFlow = approved;
                if (allowModeratorFlow) {
                    log::info("[PauseLayer] User verified as moderator; uploading level {}", levelID);
                    auto thumbPathOpt = LocalThumbs::get().getThumbPath(levelID);
                    if (!thumbPathOpt.has_value()) {
                        log::error("[PauseLayer] Could not get local thumbnail path");
                        Notification::create(Localization::get().getString("pause.access_error").c_str(), NotificationIcon::Error)->show();
                        return;
                    }
                    
                    // Load RGB file and convert to PNG (fuera del bloque para ambos casos)
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::loadRgbFileToPng(*thumbPathOpt, pngData)) {
                        log::error("[PauseLayer] Failed to convert thumbnail to PNG");
                        Notification::create(Localization::get().getString("pause.process_thumbnail_error").c_str(), NotificationIcon::Error)->show();
                        return;
                    }
                    
                    if (allowModeratorFlow) {
                        log::info("[PauseLayer] Uploading thumbnail ({} bytes) for level {}", pngData.size(), levelID);
                        Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username,
                            [levelID](bool success, const std::string& message) {
                                if (success) {
                                    Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                    PendingQueue::get().removeForLevel(levelID);
                                    log::info("[PauseLayer] Upload successful for level {}", levelID);
                                } else {
                                    Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                    log::error("[PauseLayer] Upload failed for level {}: {}", levelID, message);
                                }
                            }
                        );
                    } else {
                        log::info("[PauseLayer] User is not moderator; uploading suggestion and enqueueing");
                        Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                        
                        ThumbnailAPI::get().uploadSuggestion(levelID, pngData, username, [levelID, username](bool success, const std::string& msg) {
                            if (success) {
                                log::info("[PauseLayer] Suggestion uploaded successfully");
                                ThumbnailAPI::get().checkExists(levelID, [levelID, username](bool exists) {
                                    auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                    // Can't safely check if creator when level may be destroyed - default to false
                                    bool isCreator = false;
                                    PendingQueue::get().addOrBump(levelID, cat, username, {}, isCreator);
                                    Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                });
                            } else {
                                log::error("[PauseLayer] Failed to upload suggestion: {}", msg);
                                Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                            }
                        });
                    }
                }
            });
            
        } catch (std::exception const& e) {
            log::error("[PauseLayer] Exception in onUploadThumbnail: {}", e.what());
            Notification::create((Localization::get().getString("level.error_prefix") + e.what()).c_str(), NotificationIcon::Error)->show();
        }
    }
    

    
    void onSelectPNGFile(CCObject*) {
        log::info("[PauseLayer] Select file button pressed");
        
        try {
            auto pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available");
                return;
            }
            
            int levelID = pl->m_level->m_levelID;
            auto level = pl->m_level; // Retain level pointer for lambda
            
            // Open file picker.
            geode::utils::file::pick(
                geode::utils::file::PickMode::OpenFile,
                {
                    std::filesystem::path(),  // Default path
                    { geode::utils::file::FilePickOptions::Filter {
                        "Images (PNG/GIF)",
                        {"*.png", "*.gif"}
                    } }
                }
            ).listen([levelID, level](geode::Result<std::filesystem::path>* result) {
                if (!result || result->isErr()) {
                    log::warn("[PauseLayer] User cancelled file picker or an error occurred");
                    return;
                }
                
                std::filesystem::path selectedPath = result->unwrap();
                log::info("[PauseLayer] Selected file: {}", selectedPath.string());
                
                // Decide by extension whether it's PNG or GIF.
                std::string ext = selectedPath.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".gif") {
#if !defined(GEODE_IS_WINDOWS) && !defined(_WIN32)
                    Notification::create(Localization::get().getString("pause.gif_not_supported").c_str(), NotificationIcon::Warning)->show();
                    return;
#else
                    // Preview the GIF and allow upload.
                    try {
                        std::ifstream gifFile(selectedPath, std::ios::binary | std::ios::ate);
                        if (!gifFile) {
                            log::error("[PauseLayer] Could not open GIF file");
                            Notification::create(Localization::get().getString("pause.gif_open_error").c_str(), NotificationIcon::Error)->show();
                            return;
                        }
                        size_t size = static_cast<size_t>(gifFile.tellg());
                        gifFile.seekg(0, std::ios::beg);
                        std::vector<uint8_t> gifData(size);
                        gifFile.read(reinterpret_cast<char*>(gifData.data()), size);
                        gifFile.close();

                        // Usar CCImage directamente desde memoria
                        CCImage* image = new CCImage();
                        bool loaded = image->initWithImageData(
                            const_cast<void*>(static_cast<const void*>(gifData.data())),
                            gifData.size()
                        );
                        
                        if (!loaded) {
                            image->release();
                            Notification::create(Localization::get().getString("pause.gif_read_error").c_str(), NotificationIcon::Error)->show();
                            return;
                        }
                        
                        int width = image->getWidth();
                        int height = image->getHeight();
                        
                        // Crear textura desde CCImage
                        CCTexture2D* texture = new CCTexture2D();
                        bool ok = texture->initWithImage(image);
                        image->release();
                        
                        if (!ok) {
                            delete texture;
                            Notification::create(Localization::get().getString("pause.gif_texture_error").c_str(), NotificationIcon::Error)->show();
                            return;
                        }
                        texture->setAntiAliasTexParameters();
                        texture->retain();
                        
                        // Obtener datos of pixels usando CCRenderTexture
                        auto renderTex = CCRenderTexture::create(width, height, kCCTexture2DPixelFormat_RGBA8888);
                        if (!renderTex) {
                            texture->release();
                            Notification::create("Failed to create render texture", NotificationIcon::Error)->show();
                            return;
                        }
                        
                        renderTex->begin();
                        auto sprite = CCSprite::createWithTexture(texture);
                        sprite->setPosition(ccp(width/2, height/2));
                        sprite->visit();
                        renderTex->end();
                        
                        // Read RGBA data.
                        auto renderedImage = renderTex->newCCImage(false);
                        if (!renderedImage) {
                            texture->release();
                            Notification::create("Failed to read rendered image", NotificationIcon::Error)->show();
                            return;
                        }
                        
                        auto imageData = renderedImage->getData();
                        size_t rgbSize = static_cast<size_t>(width) * height * 3;
                        std::shared_ptr<uint8_t> rgbData(new uint8_t[rgbSize], std::default_delete<uint8_t[]>());
                        
                        // Convert RGBA -> RGB.
                        for (int i = 0; i < width * height; ++i) {
                            rgbData.get()[i*3 + 0] = imageData[i*4 + 0];
                            rgbData.get()[i*3 + 1] = imageData[i*4 + 1];
                            rgbData.get()[i*3 + 2] = imageData[i*4 + 2];
                        }
                        
                        renderedImage->release();

                        // Show preview; if accepted, upload the full GIF.
                        auto popup = CapturePreviewPopup::create(
                            texture,
                            levelID,
                            rgbData,
                            width,
                            height,
                            [levelID, gifData = std::move(gifData)](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) mutable {
                                if (!accepted) {
                                    log::info("[PauseLayer] User cancelled GIF preview");
                                    return;
                                }

                                // Extract and store dominant colors from the first frame.
                                auto pair = DominantColors::extract(buf.get(), w, h);
                                ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                                ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                                LevelColors::get().set(lvlID, A, B);

                                LocalThumbs::get().saveRGB(lvlID, buf.get(), w, h);
                                ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                                // Get username and check moderator status before upload.
                                std::string username;
                                try {
                                    if (auto* gm = GameManager::sharedState()) username = gm->m_playerName;
                                } catch(...) {}
                                if (username.empty()) {
                                    Notification::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
                                // Note: accountID validation removed - not available in current Geode bindings
                                ThumbnailAPI::get().checkModeratorAccount(username, 0, [lvlID, username, gifData = std::move(gifData), mode, replaceId](bool approved, bool isAdmin) mutable {
                                    bool allowModeratorFlow = approved;
                                    if (allowModeratorFlow) {
                                        Notification::create(Localization::get().getString("pause.gif_uploading").c_str(), NotificationIcon::Loading)->show();
                                        ThumbnailAPI::get().uploadGIF(lvlID, gifData, username, mode, replaceId, [lvlID](bool ok, const std::string& msg){
                                            if (ok) {
                                                PendingQueue::get().removeForLevel(lvlID);
                                                Notification::create(Localization::get().getString("pause.gif_uploaded").c_str(), NotificationIcon::Success)->show();
                                            } else {
                                                Notification::create(Localization::get().getString("pause.gif_upload_error").c_str(), NotificationIcon::Error)->show();
                                            }
                                        });
                                    } else {
                                        ThumbnailAPI::get().checkExists(lvlID, [lvlID, username](bool exists){
                                            auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                            // Can't check creator status - level may be destroyed
                                            bool isCreator = false;
                                            PendingQueue::get().addOrBump(lvlID, cat, username, {}, isCreator);
                                            Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Info)->show();
                                        });
                                    }
                                });
                            }
                        );

                        if (popup) {
                            popup->show();
                        } else {
                            log::error("[PauseLayer] Failed to create GIF preview popup");
                            delete texture;
                        }

                    } catch (std::exception const& e) {
                        log::error("[PauseLayer] Error processing GIF: {}", e.what());
                        Notification::create(Localization::get().getString("pause.gif_process_error").c_str(), NotificationIcon::Error)->show();
                    }
#endif  // GEODE_IS_WINDOWS
                    return; // No continuar con flujo PNG
                }
                
                // Read the full PNG into memory.
                std::ifstream pngFile(selectedPath, std::ios::binary | std::ios::ate);
                if (!pngFile) {
                    log::error("[PauseLayer] Could not open PNG file");
                    Notification::create(Localization::get().getString("pause.file_open_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                
                size_t fileSize = (size_t)pngFile.tellg();
                pngFile.seekg(0, std::ios::beg);
                std::vector<uint8_t> pngData(fileSize);
                pngFile.read(reinterpret_cast<char*>(pngData.data()), fileSize);
                pngFile.close();
                
                log::info("[PauseLayer] PNG file read ({} bytes)", fileSize);
                
                // Load PNG into CCImage from memory.
                CCImage* img = new CCImage();
                if (!img->initWithImageData(pngData.data(), fileSize, CCImage::kFmtPng)) {
                    log::error("[PauseLayer] Failed to decode selected PNG file");
                    Notification::create(Localization::get().getString("pause.png_invalid").c_str(), NotificationIcon::Error)->show();
                    delete img;
                    return;
                }
                
                int width = img->getWidth();
                int height = img->getHeight();
                unsigned char* imgData = img->getData();
                
                if (!imgData) {
                    log::error("[PauseLayer] Failed to get image pixel data");
                    Notification::create(Localization::get().getString("pause.process_image_error").c_str(), NotificationIcon::Error)->show();
                    delete img;
                    return;
                }
                
                // Read image metadata.
                int bpp = img->getBitsPerComponent();
                bool hasAlpha = img->hasAlpha();
                
                log::info("[PauseLayer] Image loaded {}x{} (BPP: {}, Alpha: {})", 
                          width, height, bpp, hasAlpha);
                
                // Compute expected data size.
                int bytesPerPixel = hasAlpha ? 4 : 3;
                size_t expectedDataSize = static_cast<size_t>(width) * height * bytesPerPixel;
                
                // Copy/convert to RGBA as needed.
                size_t rgbaSize = static_cast<size_t>(width) * height * 4;
                std::vector<uint8_t> rgbaPixels(rgbaSize);
                
                if (hasAlpha) {
                    memcpy(rgbaPixels.data(), imgData, std::min(rgbaSize, expectedDataSize));
                    log::info("[PauseLayer] Alpha detected; copied {} bytes", expectedDataSize);
                } else {
                    log::info("[PauseLayer] RGB detected; converting to RGBA ({} -> {} bytes)", 
                              expectedDataSize, rgbaSize);
                    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                        rgbaPixels[i*4 + 0] = imgData[i*3 + 0]; // R
                        rgbaPixels[i*4 + 1] = imgData[i*3 + 1]; // G
                        rgbaPixels[i*4 + 2] = imgData[i*3 + 2]; // B
                        rgbaPixels[i*4 + 3] = 255;              // A (opaque)
                    }
                }
                
                delete img;
                
                log::debug("[PauseLayer] RGBA data ready ({} bytes)", rgbaSize);
                
                // Create texture (same approach as FramebufferCapture).
                CCTexture2D* texture = new CCTexture2D();
                if (!texture) {
                    log::error("[PauseLayer] Failed to create CCTexture2D");
                    Notification::create(Localization::get().getString("pause.create_texture_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                
                // Initialize texture from RGBA data.
                if (!texture->initWithData(
                    rgbaPixels.data(),
                    kCCTexture2DPixelFormat_RGBA8888,
                    width,
                    height,
                    CCSize(width, height)
                )) {
                    log::error("[PauseLayer] Failed to initialize texture from data");
                    delete texture;
                    Notification::create(Localization::get().getString("pause.init_texture_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                
                // Configure texture parameters for better quality.
                texture->setAntiAliasTexParameters();
                
                // Retain for popup lifetime.
                texture->retain();
                
                log::info("[PauseLayer] Texture created successfully using FramebufferCapture method");

                // Convert RGBA -> RGB (preview expects RGB).
                size_t rgbSize = static_cast<size_t>(width) * height * 3;
                std::shared_ptr<uint8_t> rgbData(new uint8_t[rgbSize], std::default_delete<uint8_t[]>());
                
                // RGBA -> RGB (same as FramebufferCapture::rgbaToRgb).
                for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                    rgbData.get()[i*3 + 0] = rgbaPixels[i*4 + 0]; // R
                    rgbData.get()[i*3 + 1] = rgbaPixels[i*4 + 1]; // G
                    rgbData.get()[i*3 + 2] = rgbaPixels[i*4 + 2]; // B
                }
                
                log::info("[PauseLayer] RGBA->RGB conversion complete; showing preview");
                
                // Show preview popup.
                auto popup = CapturePreviewPopup::create(
                    texture,
                    levelID,
                    rgbData,
                    width,
                    height,
                    [levelID](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                        if (accepted) {
                            log::info("[PauseLayer] User accepted image loaded from disk");

                            // Always extract dominant colors (cache/gradients).
                            auto pair = DominantColors::extract(buf.get(), w, h);
                            ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                            ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                            
                            LevelColors::get().set(lvlID, A, B);
                            
                            log::info("[PauseLayer] Saving locally");
                            LocalThumbs::get().saveRGB(lvlID, buf.get(), w, h);
                            ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);
                            
                            size_t rgbaSize = static_cast<size_t>(w) * h * 4;
                            std::vector<uint8_t> rgba(rgbaSize);
                            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                                rgba[i*4 + 0] = buf.get()[i*3 + 0];
                                rgba[i*4 + 1] = buf.get()[i*3 + 1];
                                rgba[i*4 + 2] = buf.get()[i*3 + 2];
                                rgba[i*4 + 3] = 255;
                            }

                            CCImage img;
                            if (!img.initWithImageData(rgba.data(), rgbaSize, CCImage::kFmtRawData, w, h)) {
                                log::error("[PauseLayer] Failed to create image for PNG");
                                Notification::create(Localization::get().getString("capture.process_error").c_str(), NotificationIcon::Error)->show();
                            } else {
                                auto tmpDir = Mod::get()->getSaveDir() / "tmp";
                                std::error_code dirEc;
                                std::filesystem::create_directories(tmpDir, dirEc);
                                auto tempPath = tmpDir / (std::string("thumb_") + std::to_string(lvlID) + ".png");
                                if (!img.saveToFile(tempPath.string().c_str(), false)) {
                                    log::error("[PauseLayer] Failed to save temporary PNG");
                                    Notification::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
                                } else {
                                    std::ifstream pngFile(tempPath, std::ios::binary);
                                    if (!pngFile) {
                                        log::error("[PauseLayer] Failed to reopen temporary PNG");
                                        Notification::create(Localization::get().getString("capture.read_png_error").c_str(), NotificationIcon::Error)->show();
                                    } else {
                                        pngFile.seekg(0, std::ios::end);
                                        size_t pngSize = (size_t)pngFile.tellg();
                                        pngFile.seekg(0, std::ios::beg);
                                        std::vector<uint8_t> pngData(pngSize);
                                        pngFile.read(reinterpret_cast<char*>(pngData.data()), pngSize);
                                        pngFile.close();
                                        std::error_code ec;
                                        std::filesystem::remove(tempPath, ec);
                                        if (ec) log::warn("[PauseLayer] Could not delete temporary PNG: {}", ec.message());

                                        log::info("[PauseLayer] PNG ready ({} bytes) for level {}", pngSize, lvlID);

                                        // Get username for upload.
                                        std::string username;
                                        int accountID = 0;
                                        try {
                                            auto* gm = GameManager::sharedState();
                                            if (gm) {
                                                username = gm->m_playerName;
                                                accountID = gm->m_playerUserID;
                                            }
                                        } catch(...) {}
                                        
                                        if (username.empty()) {
                                            log::error("[PauseLayer] Could not get username");
                                            Notification::create(Localization::get().getString("pause.username_error").c_str(), NotificationIcon::Error)->show();
                                            return;
                                        }

                                        if (accountID <= 0) {
                                            Notification::create("Tienes que tener cuenta para subir", NotificationIcon::Error)->show();
                                            return;
                                        }

                                        // Check moderator status before upload.
                                        Notification::create(Localization::get().getString("capture.verifying").c_str(), NotificationIcon::Info)->show();
                                        
                                        ThumbnailAPI::get().checkModeratorAccount(username, accountID, [lvlID, pngData, username, mode, replaceId](bool isMod, bool isAdmin) {
                                            bool allowModeratorFlow = (isMod || isAdmin);
                                            if (allowModeratorFlow) {
                                                log::info("[PauseLayer] User verified as moderator; uploading thumbnail");
                                                Notification::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();
                                                
                                                ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, mode, replaceId,
                                                    [lvlID](bool success, const std::string& message) {
                                                        if (success) {
                                                            Notification::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                                            PendingQueue::get().removeForLevel(lvlID);
                                                            log::info("[PauseLayer] Upload successful for level {}", lvlID);
                                                        } else {
                                                            Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                            log::error("[PauseLayer] Upload failed for level {}: {}", lvlID, message);
                                                        }
                                                    }
                                                );
                                            } else {
                                                log::info("[PauseLayer] User is not moderator; uploading suggestion");
                                                Notification::create(Localization::get().getString("capture.uploading_suggestion").c_str(), NotificationIcon::Info)->show();
                                                
                                                ThumbnailAPI::get().uploadSuggestion(lvlID, pngData, username, [lvlID, username](bool success, const std::string& msg) {
                                                    if (success) {
                                                        log::info("[PauseLayer] Suggestion uploaded successfully (from file)");
                                                        ThumbnailAPI::get().checkExists(lvlID, [lvlID, username](bool exists) {
                                                            auto cat = exists ? PendingCategory::Update : PendingCategory::Verify;
                                                            // Can't check creator status - level may be destroyed
                                                            bool isCreator = false;
                                                            PendingQueue::get().addOrBump(lvlID, cat, username, {}, isCreator);
                                                            Notification::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                                        });
                                                    } else {
                                                        log::error("[PauseLayer] Failed to upload suggestion: {}", msg);
                                                        Notification::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                                    }
                                                });
                                            }
                                        });
                                    }
                                }
                            }
                        } else {
                            log::info("[PauseLayer] User cancelled image preview");
                        }
                    }
                );
                
                if (popup) {
                    popup->show();
                } else {
                    log::error("[PauseLayer] Failed to create preview popup");
                    delete texture;
                }
            });
            
        } catch (std::exception const& e) {
            log::error("[PauseLayer] Exception in onSelectPNGFile: {}", e.what());
            Notification::create(Localization::get().getString("level.error_prefix") + std::string(e.what()), NotificationIcon::Error)->show();
        }
    }
    

    

};


