#include "CapturePreviewPopup.hpp"
#include "../managers/LocalThumbs.hpp"
#include "../managers/ThumbnailLoader.hpp"
#include "../utils/Localization.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../layers/ThumbnailSelectionPopup.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace geode::prelude;
using namespace cocos2d;

CapturePreviewPopup* CapturePreviewPopup::create(
    CCTexture2D* texture, 
    int levelID,
    std::shared_ptr<uint8_t> buffer,
    int width,
    int height,
    std::function<void(bool, int, std::shared_ptr<uint8_t>, int, int, std::string, std::string)> callback,
    std::function<void(bool, CapturePreviewPopup*)> recaptureCallback,
    bool isPlayerHidden,
    bool isModerator
) {
    log::info("Creating CapturePreviewPopup...");
    
    if (!texture) {
        log::error("Failed to create CapturePreviewPopup: texture is null");
        return nullptr;
    }
    
    auto ret = new CapturePreviewPopup();
    
    // Increase retain count because the popup reuses the texture multiple times
    texture->retain();
    
    ret->m_texture = texture;
    ret->m_levelID = levelID;
    ret->m_buffer = buffer;
    ret->m_width = width;
    ret->m_height = height;
    ret->m_callback = std::move(callback);
    ret->m_recaptureCallback = std::move(recaptureCallback);
    ret->m_isPlayerHidden = isPlayerHidden;
    ret->m_isModerator = isModerator;
    
    if (ret && ret->initAnchored(320.f, 240.f)) {  // Smaller popup: 320x240 instead of 450x320
        ret->autorelease();
        log::info("CapturePreviewPopup created successfully");
        return ret;
    }
    log::error("Failed to initialize CapturePreviewPopup");
    
    // If initialization fails, release the texture retained in create()
    texture->release();
    
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void CapturePreviewPopup::updateContent(CCTexture2D* texture, std::shared_ptr<uint8_t> buffer, int width, int height) {
    if (!texture) return;

    // Release old texture
    if (m_texture) {
        m_texture->release();
    }

    // Retain new texture
    texture->retain();
    m_texture = texture;
    m_buffer = buffer;
    m_width = width;
    m_height = height;

    // Re-create sprite to ensure correct state
    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }

    m_previewSprite = CCSprite::createWithTexture(m_texture);
    if (m_previewSprite && m_clippingNode) {
        m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
        m_clippingNode->addChild(m_previewSprite);
        updatePreviewScale();
    }
}

CapturePreviewPopup::~CapturePreviewPopup() {
    // Release the texture retained in create()
    if (m_texture) {
        m_texture->release();
        m_texture = nullptr;
    }
}

bool CapturePreviewPopup::setup() {
    log::info("Setting up CapturePreviewPopup...");
    
    try {
        this->setTitle(Localization::get().getString("preview.title").c_str());
        
        auto content = this->m_mainLayer->getContentSize();
        log::debug("Main layer size: {}x{}", content.width, content.height);

        // Sprite used to display the thumbnail
        if (!m_texture) {
            log::error("Texture is null in setup");
            return false;
        }
        
        m_previewSprite = CCSprite::createWithTexture(m_texture);
        if (!m_previewSprite) {
            log::error("Failed to create preview sprite");
            return false;
        }
        m_previewSprite->setAnchorPoint({0.5f, 0.5f});

        // Basic texture smoothing
        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        m_texture->setTexParameters(&params);

    // Custom popup margins
    const float kHorizontalMargin = 11.5f;
    const float kVerticalTotalMargin = 62.f;
    float previewAreaWidth = content.width - 2.f * kHorizontalMargin;
    float previewAreaHeight = content.height - kVerticalTotalMargin;

    // Initial scale (adjust mode)
    updatePreviewScale();

        // Clipping node that bounds the preview area
        m_clippingNode = CCClippingNode::create();
        if (!m_clippingNode) {
            log::error("Failed to create clipping node");
            return false;
        }
        m_clippingNode->setContentSize({previewAreaWidth, previewAreaHeight});
        m_clippingNode->ignoreAnchorPointForPosition(false);
        m_clippingNode->setAnchorPoint({0.5f, 0.5f});
        m_clippingNode->setPosition({content.width * 0.5f, content.height * 0.5f + 5.f});
        
        // Stencil that acts as a mask
        auto stencil = CCScale9Sprite::create("square02_001.png");
        if (!stencil) {
            log::warn("Failed to create stencil sprite, using fallback");
            stencil = CCScale9Sprite::create();
        }
        stencil->ignoreAnchorPointForPosition(false);
        stencil->setContentSize(m_clippingNode->getContentSize());
        stencil->setPosition(m_clippingNode->getContentSize() / 2);
        m_clippingNode->setStencil(stencil);
        
        // Center the sprite inside the mask
        m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
        m_clippingNode->addChild(m_previewSprite);

        // El modo fill cubre el marco por defecto
        m_fillMode = true;
        updatePreviewScale();
        
        this->m_mainLayer->addChild(m_clippingNode);
        log::debug("Clipping node added successfully");

        // No extra border is drawn

        // Botonera principal situada en la parte inferior
        auto buttonMenu = CCMenu::create();
        
        // First, try loading custom sprites from resources
        // Use an absolute path so CCSprite can reliably read dimensions
        auto okSprPath = (Mod::get()->getResourcesDir() / "botonaceptar.png").string();
        auto okSpr = CCSprite::create(okSprPath.c_str());
        
        if (!okSpr) {
            // Fallback a _spr si la ruta absoluta falla
            okSpr = CCSprite::create("botonaceptar.png"_spr);
        }

        if (!okSpr) {
            okSpr = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        } else {
            // Fix: Scale based on size
            float targetSize = 30.0f;
            float currentSize = std::max(okSpr->getContentWidth(), okSpr->getContentHeight());
            
            // If the size is valid, scale it. If it's 0 (error), force a safe scale.
            if (currentSize > 1.0f) {
                okSpr->setScale(targetSize / currentSize);
            } else {
                // Fallback scale if we cannot determine the size
                okSpr->setScale(0.5f); 
            }
        }
        
        auto cancelSprPath = (Mod::get()->getResourcesDir() / "botonrechazar.png").string();
        auto cancelSpr = CCSprite::create(cancelSprPath.c_str());
        
        if (!cancelSpr) {
            cancelSpr = CCSprite::create("botonrechazar.png"_spr);
        }

        if (!cancelSpr) {
            cancelSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        } else {
            // Fix: Scale based on size
            float targetSize = 30.0f;
            float currentSize = std::max(cancelSpr->getContentWidth(), cancelSpr->getContentHeight());
            
            if (currentSize > 1.0f) {
                cancelSpr->setScale(targetSize / currentSize);
            } else {
                cancelSpr->setScale(0.5f);
            }
        }
        
        // Crop button icon
        auto cropSpr = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");

        // Fill/fit toggle button icon
        auto fillSpr = CCSprite::createWithSpriteFrameName("GJ_zoomInBtn_001.png");
        
        // Player visibility toggle button icon
        // Reuse the player icon and adjust it based on state
        auto playerSpr = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
        if (playerSpr) {
            if (m_isPlayerHidden) {
                playerSpr->setColor({100, 100, 100}); // darker if hidden
                playerSpr->setOpacity(150);
            } else {
                playerSpr->setColor({255, 255, 255}); // normal if visible
                playerSpr->setOpacity(255);
            }
        }

        if (!cancelSpr || !okSpr || !cropSpr) {
            log::error("Failed to create button sprites");
            return false;
        }
        if (!fillSpr) {
            // Si no hay icono se crea un cuadrado simple
            fillSpr = CCSprite::create("square02_001.png");
            if (fillSpr) {
                fillSpr->setScale(0.6f);
                fillSpr->setColor({120, 180, 255});
            }
        }
        
        // Helper para escalar sprites consistentemente
        auto scaleSprite = [](CCSprite* spr, float targetSize) {
            if (!spr) return;
            float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
            if (currentSize > 1.0f) {
                spr->setScale(targetSize / currentSize);
            } else {
                spr->setScale(1.0f);
            }
        };

        // Scale of iconos of herramientas
        float toolSize = 25.0f;
        scaleSprite(cropSpr, toolSize);
        if (fillSpr) scaleSprite(fillSpr, toolSize);
        if (playerSpr) scaleSprite(playerSpr, toolSize);

        // Icono of the Button of descarga
        auto downloadSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
        if (!downloadSpr) {
             downloadSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"); // Fallback
             if (downloadSpr) downloadSpr->setRotation(-90);
        }
        if (downloadSpr) scaleSprite(downloadSpr, toolSize);

        auto cancelBtn = CCMenuItemSpriteExtra::create(cancelSpr, this, menu_selector(CapturePreviewPopup::onCancelBtn));
        
        // Always create Upload button initially
        CCMenuItemSpriteExtra* okBtn = CCMenuItemSpriteExtra::create(okSpr, this, menu_selector(CapturePreviewPopup::onAcceptBtn));
        okBtn->setTag(100);
        
        CCMenuItemSpriteExtra* addBtn = nullptr;
        CCMenuItemSpriteExtra* replaceBtn = nullptr;

        // Check moderator status and thumbnails dynamically
        std::string username = "Unknown";
        if (auto gm = GameManager::sharedState()) username = gm->m_playerName;

        this->retain();
        ThumbnailAPI::get().checkModerator(username, [this, username](bool isMod, bool isAdmin) {
            if (isMod || isAdmin) {
                m_isModerator = true; // Update local state
                
                // Now check for thumbnails
                ThumbnailAPI::get().getThumbnails(m_levelID, [this](bool success, const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbs) {
                    if (success && !thumbs.empty()) {
                        // Thumbnails exist! Switch UI to Add/Replace
                        if (auto menu = this->m_mainLayer->getChildByID("button-menu")) {
                            if (auto btn = menu->getChildByTag(100)) {
                                btn->setVisible(false); // Hide "Upload"
                            }
                            
                            // Create Add/Replace buttons if they don't exist
                            if (!menu->getChildByID("add-button")) {
                                auto addSpr = ButtonSprite::create("Add", 40, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f);
                                auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(CapturePreviewPopup::onAddBtn));
                                addBtn->setID("add-button");
                                addBtn->setPosition({this->m_mainLayer->getContentSize().width * 0.25f - (this->m_mainLayer->getContentSize().width * 0.5f), 0.f});
                                menu->addChild(addBtn);
                                PaimonButtonHighlighter::registerButton(addBtn);
                                
                                auto replaceSpr = ButtonSprite::create("Replace", 40, true, "goldFont.fnt", "GJ_button_01.png", 30, 0.6f);
                                auto replaceBtn = CCMenuItemSpriteExtra::create(replaceSpr, this, menu_selector(CapturePreviewPopup::onReplaceBtn));
                                replaceBtn->setID("replace-button");
                                replaceBtn->setPosition({this->m_mainLayer->getContentSize().width * 0.42f - (this->m_mainLayer->getContentSize().width * 0.5f), 0.f});
                                menu->addChild(replaceBtn);
                                PaimonButtonHighlighter::registerButton(replaceBtn);
                            }
                        }
                    }
                    // If no thumbnails, we keep the "Upload" button (okBtn) visible
                    this->release();
                });
            } else {
                this->release();
            }
        });

        auto cropBtn = CCMenuItemSpriteExtra::create(cropSpr, this, menu_selector(CapturePreviewPopup::onCropBtn));
        auto fillBtn = CCMenuItemSpriteExtra::create(fillSpr, this, menu_selector(CapturePreviewPopup::onToggleFillBtn));
        auto downloadBtn = CCMenuItemSpriteExtra::create(downloadSpr, this, menu_selector(CapturePreviewPopup::onDownloadBtn));
        
        CCMenuItemSpriteExtra* playerBtn = nullptr;
        if (playerSpr) {
            playerBtn = CCMenuItemSpriteExtra::create(playerSpr, this, menu_selector(CapturePreviewPopup::onTogglePlayerBtn));
        }

        PaimonButtonHighlighter::registerButton(cancelBtn);
        if (okBtn) PaimonButtonHighlighter::registerButton(okBtn);
        if (addBtn) PaimonButtonHighlighter::registerButton(addBtn);
        if (replaceBtn) PaimonButtonHighlighter::registerButton(replaceBtn);
        PaimonButtonHighlighter::registerButton(cropBtn);
        PaimonButtonHighlighter::registerButton(fillBtn);
        PaimonButtonHighlighter::registerButton(downloadBtn);
        if (playerBtn) PaimonButtonHighlighter::registerButton(playerBtn);

        cancelBtn->setID("cancel-button");
        if (okBtn) okBtn->setID("ok-button");
        if (addBtn) addBtn->setID("add-button");
        if (replaceBtn) replaceBtn->setID("replace-button");
        cropBtn->setID("crop-button");
        fillBtn->setID("fill-button");
        downloadBtn->setID("download-button");
        if (playerBtn) playerBtn->setID("player-toggle-button");

        buttonMenu->setID("button-menu");
        buttonMenu->addChild(cancelBtn);
        if (okBtn) buttonMenu->addChild(okBtn);
        // if (addBtn) buttonMenu->addChild(addBtn); // Added dynamically now
        // if (replaceBtn) buttonMenu->addChild(replaceBtn); // Added dynamically now
        
        // Tools menu (center)
        auto toolsMenu = CCMenu::create();
        if (playerBtn) toolsMenu->addChild(playerBtn);
        toolsMenu->addChild(fillBtn);
        toolsMenu->addChild(cropBtn);
        toolsMenu->addChild(downloadBtn);
        toolsMenu->alignItemsHorizontallyWithPadding(15.f);
        toolsMenu->setPosition({content.width * 0.5f, 20.f});
        this->m_mainLayer->addChild(toolsMenu);

        // Position action buttons at the extremes
        // Cancel on the left
        cancelBtn->setPosition({-content.width * 0.35f, 0.f});
        
        if (okBtn) {
            // Accept on the right
            okBtn->setPosition({content.width * 0.35f, 0.f});
        } 
        /*
        else {
            // Add and Replace on the right
            addBtn->setPosition({content.width * 0.25f, 0.f});
            replaceBtn->setPosition({content.width * 0.42f, 0.f});
        }
        */
        
        // Place the main menu at the bottom
        buttonMenu->setPosition({content.width * 0.5f, 20.f});
        
        this->m_mainLayer->addChild(buttonMenu);
        
        log::info("CapturePreviewPopup setup completed successfully");

        return true;
    } catch (std::exception& e) {
        log::error("Exception in CapturePreviewPopup::setup: {}", e.what());
        return false;
    } catch (...) {
        log::error("Unknown exception in CapturePreviewPopup::setup");
        return false;
    }
}

void CapturePreviewPopup::onTogglePlayerBtn(CCObject* sender) {
    if (!sender) return;
    if (m_recaptureCallback) {
        // Toggle state
        m_isPlayerHidden = !m_isPlayerHidden;
        // Call callback to recapture
        m_recaptureCallback(m_isPlayerHidden, this);
    } else {
        log::warn("No recapture callback provided");
        Notification::create(Localization::get().getString("preview.player_toggle_error").c_str(), NotificationIcon::Error)->show();
    }
}

void CapturePreviewPopup::updatePreviewScale() {
    if (!m_previewSprite || !this->m_mainLayer) return;
    auto content = this->m_mainLayer->getContentSize();
    const float kHorizontalMargin = 11.5f;
    const float kVerticalTotalMargin = 62.f;
    float previewAreaWidth = content.width - 2.f * kHorizontalMargin;
    float previewAreaHeight = content.height - kVerticalTotalMargin;

    float sx = previewAreaWidth / m_previewSprite->getContentWidth();
    float sy = previewAreaHeight / m_previewSprite->getContentHeight();
    float scale = m_fillMode ? std::max(sx, sy) : std::min(sx, sy);
    m_previewSprite->setScale(scale);
    if (m_clippingNode) {
        m_clippingNode->setContentSize({previewAreaWidth, previewAreaHeight});
        m_previewSprite->setPosition(m_clippingNode->getContentSize() / 2);
    }
    log::debug("[CapturePreviewPopup] mode={}, area={}x{}, img={}x{}, scale={}",
        m_fillMode ? "fill" : "fit",
        previewAreaWidth, previewAreaHeight,
        m_previewSprite->getContentWidth(), m_previewSprite->getContentHeight(),
        scale);
}

void CapturePreviewPopup::onToggleFillBtn(CCObject* sender) {
    // Check if sender is valid
    if (!sender) return;

    m_fillMode = !m_fillMode;
    updatePreviewScale();
    std::string msg = m_fillMode ? Localization::get().getString("preview.fill_mode_active") : Localization::get().getString("preview.fit_mode_active");
    Notification::create(msg.c_str(), NotificationIcon::Info)->show();
}

void CapturePreviewPopup::onClose(CCObject* sender) {
    log::info("[CapturePreviewPopup] onClose llamado");
    
    // If closed without explicitly calling Accept or Cancel,
    // treat it as a cancellation and run the callback
    if (!m_callbackExecuted && m_callback) {
        log::info("[CapturePreviewPopup] Ejecutando callback desde onClose (cerrado con X)");
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
        m_callbackExecuted = true;
    }
    
    // Call base onClose
    Popup::onClose(sender);
}

void CapturePreviewPopup::onAcceptBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario aceptó la captura para nivel {}", m_levelID);
    
    // Mark callback as executed to prevent double-calls
    m_callbackExecuted = true;
    
    // Invalidate local cache to ensure the new image is used
    ThumbnailLoader::get().invalidateLevel(m_levelID);
    
    // Call callback before closing
    if (m_callback) {
        m_callback(true, m_levelID, m_buffer, m_width, m_height, "", "");
    }
    
    // Close the popup (this will correctly handle game state)
    this->onClose(nullptr);
}

void CapturePreviewPopup::onCancelBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario canceló la captura");
    
    // Mark callback as executed to prevent double-calls
    m_callbackExecuted = true;
    
    // Call the callback with false
    if (m_callback) {
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
    }
    
    // Cerrar el popup
    this->onClose(nullptr);
}

void CapturePreviewPopup::onAddBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario seleccionó Add");
    
    m_callbackExecuted = true;
    ThumbnailLoader::get().invalidateLevel(m_levelID);
    
    if (m_callback) {
        m_callback(true, m_levelID, m_buffer, m_width, m_height, "add", "");
    }
    this->onClose(nullptr);
}

void CapturePreviewPopup::onReplaceBtn(CCObject* sender) {
    if (!sender) return;
    log::info("[CapturePreviewPopup] Usuario seleccionó Replace");
    
    // Fetch thumbnails and show selection popup
    ThumbnailAPI::get().getThumbnails(m_levelID, [this](bool success, const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbnails) {
        if (!success) {
            Notification::create("Failed to fetch thumbnails", NotificationIcon::Error)->show();
            return;
        }
        
        auto popup = ThumbnailSelectionPopup::create(thumbnails, [this](const std::string& id) {
            if (id == "CANCEL") return;
            
            m_callbackExecuted = true;
            ThumbnailLoader::get().invalidateLevel(m_levelID);
            
            std::string mode = id.empty() ? "add" : "replace";
            std::string replaceId = id;
            
            if (m_callback) {
                m_callback(true, m_levelID, m_buffer, m_width, m_height, mode, replaceId);
            }
            this->onClose(nullptr);
        });
        popup->show();
    });
}

void CapturePreviewPopup::onCropBtn(CCObject* sender) {
    // Check if sender is valid to prevent crashes
    if (!sender) return;

    if (m_isCropped) {
        Notification::create(Localization::get().getString("preview.borders_removed").c_str(), NotificationIcon::Info)->show();
        return;
    }
    
    log::info("[CropBtn] Detectando bordes negros...");
    
    auto cropRect = detectBlackBorders();
    
    if (cropRect.width == m_width && cropRect.height == m_height) {
        log::info("[CropBtn] No se detectaron bordes negros");
        Notification::create(Localization::get().getString("preview.no_borders").c_str(), NotificationIcon::Info)->show();
        return;
    }
    
    log::info("[CropBtn] Bordes detectados: x={}, y={}, w={}, h={}", 
              cropRect.x, cropRect.y, cropRect.width, cropRect.height);
    
    applyCrop(cropRect);
    m_isCropped = true;
    
    Notification::create(Localization::get().getString("preview.borders_deleted").c_str(), NotificationIcon::Success)->show();
}

CapturePreviewPopup::CropRect CapturePreviewPopup::detectBlackBorders() {
    // Simple detection based on sampling and percentage of dark pixels per line
    const int THRESHOLD = 20;
    const float BLACK_PERCENTAGE = 0.85f;
    const int SAMPLE_STEP = 4;
    
    const uint8_t* data = m_buffer.get();
    
    log::info("[detectBlackBorders] Iniciando detección en imagen {}x{}", m_width, m_height);
    
    // Lambda that checks whether a pixel is below the threshold
    auto isBlackPixel = [&](int x, int y) -> bool {
        int idx = (y * m_width + x) * 4; // RGBA
        int r = data[idx];
        int g = data[idx + 1];
        int b = data[idx + 2];
        // Consider black if all components are below the threshold
        return (r <= THRESHOLD && g <= THRESHOLD && b <= THRESHOLD);
    };
    
    // Lambda that estimates whether a full line is mostly black
    auto isBlackLine = [&](int linePos, bool isHorizontal) -> bool {
        int samples = isHorizontal ? (m_width / SAMPLE_STEP) : (m_height / SAMPLE_STEP);
        if (samples < 10) samples = isHorizontal ? m_width : m_height;  // if very small, sample everything
        
        int blackCount = 0;
        int totalSamples = 0;
        
        if (isHorizontal) {
            // Horizontal line (fixed Y)
            for (int x = 0; x < m_width; x += SAMPLE_STEP) {
                if (isBlackPixel(x, linePos)) blackCount++;
                totalSamples++;
            }
        } else {
            // Vertical line (fixed X)
            for (int y = 0; y < m_height; y += SAMPLE_STEP) {
                if (isBlackPixel(linePos, y)) blackCount++;
                totalSamples++;
            }
        }
        
        float blackRatio = (float)blackCount / totalSamples;
        return blackRatio >= BLACK_PERCENTAGE;
    };
    
    // Detect top border (advance from the top until a non-black line is found)
    int top = 0;
    for (int y = 0; y < m_height / 2; ++y) {  // Only search up to half
        if (!isBlackLine(y, true)) {
            top = y;
            break;
        }
    }
    
    // Detect bottom border (move up from the bottom until a non-black line is found)
    int bottom = m_height - 1;
    for (int y = m_height - 1; y >= m_height / 2; --y) {  // Only search from half
        if (!isBlackLine(y, true)) {
            bottom = y;
            break;
        }
    }
    
    // Detect left border
    int left = 0;
    for (int x = 0; x < m_width / 2; ++x) {  // Only search up to half
        if (!isBlackLine(x, false)) {
            left = x;
            break;
        }
    }
    
    // Detect right border
    int right = m_width - 1;
    for (int x = m_width - 1; x >= m_width / 2; --x) {  // Only search from half
        if (!isBlackLine(x, false)) {
            right = x;
            break;
        }
    }
    
    // Check that the crop makes sense (at least 50% of the original area)
    int cropWidth = right - left + 1;
    int cropHeight = bottom - top + 1;
    float cropRatio = (float)(cropWidth * cropHeight) / (m_width * m_height);
    
    log::info("[detectBlackBorders] Bordes detectados: L={}, T={}, R={}, B={} ({}x{}, {:.1f}% del original)", 
              left, top, right, bottom, cropWidth, cropHeight, cropRatio * 100.0f);
    
    // If the crop is very small (< 30% of the area) or very large (> 99%), it's probably a false positive
    if (cropRatio < 0.30f) {
        log::warn("[detectBlackBorders] Crop demasiado agresivo ({:.1f}%), usando imagen original", cropRatio * 100.0f);
        return { 0, 0, m_width, m_height };
    }
    
    if (cropRatio > 0.99f) {
        log::info("[detectBlackBorders] No se detectaron bordes significativos");
        return { 0, 0, m_width, m_height };
    }
    
    return { left, top, cropWidth, cropHeight };
}

void CapturePreviewPopup::applyCrop(const CropRect& rect) {
    log::info("[applyCrop] Aplicando crop: {}x{} @ ({}, {})", rect.width, rect.height, rect.x, rect.y);
    
    // Create a new buffer for the cropped image (RGBA)
    size_t newSize = rect.width * rect.height * 4;
    std::shared_ptr<uint8_t> croppedBuffer(new uint8_t[newSize], std::default_delete<uint8_t[]>());
    
    const uint8_t* srcData = m_buffer.get();
    uint8_t* dstData = croppedBuffer.get();
    
    // Copy line-by-line from the crop area
    for (int y = 0; y < rect.height; ++y) {
        int srcY = rect.y + y;
        const uint8_t* srcRow = srcData + (srcY * m_width + rect.x) * 4;
        uint8_t* dstRow = dstData + y * rect.width * 4;
        memcpy(dstRow, srcRow, rect.width * 4);
    }
    
    // Update buffer and dimensions
    m_buffer = croppedBuffer;
    m_width = rect.width;
    m_height = rect.height;
    
    // Create a new texture with the cropped data (already RGBA)
    auto* newTexture = new CCTexture2D();
    if (newTexture->initWithData(
        croppedBuffer.get(),
        kCCTexture2DPixelFormat_RGBA8888,
        rect.width,
        rect.height,
        CCSize(rect.width, rect.height)
    )) {
        newTexture->setAntiAliasTexParameters();
        newTexture->autorelease();
        m_texture = newTexture;
        
        // Update sprite with the new texture
        if (m_previewSprite) {
            m_previewSprite->setTexture(newTexture);
            
            // Recalculate scale/centering for the current mode (fit/fill)
            updatePreviewScale();
            
            log::info("[applyCrop] Textura y sprite actualizados exitosamente");
        }
    } else {
        log::error("[applyCrop] No se pudo crear nueva textura");
        delete newTexture;
    }
}

void CapturePreviewPopup::onDownloadBtn(CCObject* sender) {
    if (!sender) return;
    if (!m_buffer || m_width <= 0 || m_height <= 0) {
        Notification::create(Localization::get().getString("preview.no_image").c_str(), NotificationIcon::Error)->show();
        return;
    }

    // Create downloads directory
    auto downloadDir = Mod::get()->getSaveDir() / "downloaded_thumbnails";
    std::error_code ec;
    if (!std::filesystem::exists(downloadDir)) {
        std::filesystem::create_directory(downloadDir, ec);
        if (ec) {
            log::error("Failed to create download directory: {}", ec.message());
            Notification::create(Localization::get().getString("preview.folder_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
    }

    // Generate filename
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "thumbnail_" << m_levelID << "_" << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S") << ".png";
    auto filePath = downloadDir / ss.str();

    // Buffer is already RGBA
    size_t dataSize = m_width * m_height * 4;
    
    // Save image
    CCImage img;
    if (img.initWithImageData(const_cast<uint8_t*>(m_buffer.get()), dataSize, CCImage::kFmtRawData, m_width, m_height, 8)) {
        if (img.saveToFile(filePath.string().c_str(), false)) {
            Notification::create(Localization::get().getString("preview.downloaded").c_str(), NotificationIcon::Success)->show();
            log::info("Thumbnail saved to: {}", filePath.string());
            
            // Invalidate cache for this level so it reloads the new image
            ThumbnailLoader::get().invalidateLevel(m_levelID);
        } else {
            Notification::create(Localization::get().getString("preview.save_error").c_str(), NotificationIcon::Error)->show();
        }
    } else {
        Notification::create(Localization::get().getString("preview.process_error").c_str(), NotificationIcon::Error)->show();
    }
}




