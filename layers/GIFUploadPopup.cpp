#include "GIFUploadPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include "../utils/Localization.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include <Geode/binding/GJAccountManager.hpp>
#include <fstream>

using namespace geode::prelude;

GIFUploadPopup* GIFUploadPopup::create(
    const std::string& gifPath,
    int levelID,
    int frameCount,
    int fileSizeBytes,
    cocos2d::CCTexture2D* previewTexture,
    std::function<void(bool, const std::string&, int)> callback
) {
    auto ret = new GIFUploadPopup();
    ret->m_gifPath = gifPath;
    ret->m_levelID = levelID;
    ret->m_frameCount = frameCount;
    ret->m_fileSizeBytes = fileSizeBytes;
    ret->m_previewTexture = previewTexture;
    if (previewTexture) previewTexture->retain();
    ret->m_callback = callback;
    
    if (ret && ret->initAnchored(520.0f, 380.0f)) {
        ret->autorelease();
        return ret;
    }
    
    CC_SAFE_DELETE(ret);
    return nullptr;
}

GIFUploadPopup::~GIFUploadPopup() {
    if (m_previewTexture) {
        m_previewTexture->release();
        m_previewTexture = nullptr;
    }
}

bool GIFUploadPopup::setup() {
    this->setTitle(Localization::get().getString("gif.upload.title"));
    
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto contentSize = m_mainLayer->getContentSize();
    
    // Preview of the último frame of the GIF
    CCNode* previewNode = nullptr;
    CCSize previewSize;
    
    // Usar the textura estática of the último frame
    if (m_previewTexture && m_previewTexture->getName() != 0) {
        auto previewSprite = CCSprite::createWithTexture(m_previewTexture);
        if (previewSprite) {
            previewNode = previewSprite;
            previewSize = previewSprite->getContentSize();
            log::info("[GIFUploadPopup] Preview desde textura estática: {}x{}", previewSize.width, previewSize.height);
        }
    }
    
    // Last fallback: placeholder
    if (!previewNode) {
        log::warn("[GIFUploadPopup] Usando placeholder");
        auto placeholder = CCSprite::create("GJ_square05.png");
        placeholder->setScale(3.0f);
        placeholder->setColor({100, 150, 255});
        
        auto label = CCLabelBMFont::create(Localization::get().getString("gif.label").c_str(), "goldFont.fnt");
        label->setScale(0.8f);
        label->setPosition(placeholder->getContentSize() / 2);
        placeholder->addChild(label);
        
        previewNode = placeholder;
        previewSize = CCSize(placeholder->getContentSize().width * 3.0f, 
                            placeholder->getContentSize().height * 3.0f);
    }
    
    if (previewNode) {
        // Scale the preview to that quepa bien (máximo 280x160)
        float maxWidth = 280.0f;
        float maxHeight = 160.0f;
        float scaleX = maxWidth / previewSize.width;
        float scaleY = maxHeight / previewSize.height;
        float scale = std::min(scaleX, scaleY);
        
        previewNode->setScale(scale);
        previewNode->setPosition(contentSize / 2 + CCPoint(0, 60));
        m_mainLayer->addChild(previewNode);
        
        // Marco decorativo alrededor del preview
        auto border = CCScale9Sprite::create("square02b_001.png");
        border->setContentSize(CCSizeMake(
            previewSize.width * scale + 8,
            previewSize.height * scale + 8
        ));
        border->setPosition(contentSize / 2 + CCPoint(0, 60));
        border->setOpacity(100);
        border->setZOrder(-1);
        m_mainLayer->addChild(border);
    }
    
    // Información of the GIF
    std::string info = fmt::format(
        "Frames: {} | Tamaño: {:.2f} MB | Level ID: {}",
        m_frameCount,
        m_fileSizeBytes / 1024.0f / 1024.0f,
        m_levelID
    );
    
    auto infoLabel = CCLabelBMFont::create(info.c_str(), "bigFont.fnt");
    infoLabel->setScale(0.4f);
    infoLabel->setPosition(contentSize / 2 + CCPoint(0, -50));
    m_mainLayer->addChild(infoLabel);
    
    // Texto de pregunta
    auto questionLabel = CCLabelBMFont::create("¿Subir GIF al servidor?", "goldFont.fnt");
    questionLabel->setScale(0.6f);
    questionLabel->setPosition(contentSize / 2 + CCPoint(0, -80));
    m_mainLayer->addChild(questionLabel);
    
    // Button Aceptar
    auto acceptBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Subir", "goldFont.fnt", "GJ_button_01.png", 0.8f),
        this,
        menu_selector(GIFUploadPopup::onAcceptBtn)
    );
    acceptBtn->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint(-80, -130));
    
    // Button Cancelar
    auto cancelBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Cancelar", "goldFont.fnt", "GJ_button_06.png", 0.8f),
        this,
        menu_selector(GIFUploadPopup::onCancelBtn)
    );
    cancelBtn->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint(80, -130));
    
    auto menu = CCMenu::create();
    menu->setID("main-menu");
    menu->addChild(acceptBtn);
    menu->addChild(cancelBtn);
    menu->setPosition(0, 0);
    m_mainLayer->addChild(menu);
    
    return true;
}

void GIFUploadPopup::onAcceptBtn(CCObject* sender) {
    auto accountManager = GJAccountManager::sharedState();
    std::string username = accountManager->m_username;
    
    if (username.empty()) {
        Notification::create("Debes iniciar sesión", NotificationIcon::Error)->show();
        return;
    }

    // Disable button to prevent double click
    if (auto menu = static_cast<CCMenu*>(m_mainLayer->getChildByID("main-menu"))) {
        menu->setEnabled(false);
    }
    if (sender) static_cast<CCMenuItem*>(sender)->setEnabled(false);

    Notification::create("Verificando permisos...", NotificationIcon::Loading)->show();

    ThumbnailAPI::get().checkModerator(username, [this, username](bool isMod, bool isAdmin) {
        if (!isMod && !isAdmin) {
            Notification::create("Solo moderadores pueden subir GIFs", NotificationIcon::Error)->show();
            this->onClose(nullptr);
            return;
        }

        // Read file
        std::ifstream file(m_gifPath, std::ios::binary | std::ios::ate);
        if (!file) {
            Notification::create("Error leyendo archivo", NotificationIcon::Error)->show();
            this->onClose(nullptr);
            return;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (!file.read((char*)buffer.data(), size)) {
            Notification::create("Error leyendo archivo", NotificationIcon::Error)->show();
            this->onClose(nullptr);
            return;
        }

        Notification::create("Subiendo GIF...", NotificationIcon::Loading)->show();

        ThumbnailAPI::get().uploadGIF(m_levelID, buffer, username, [this](bool success, const std::string& message) {
            if (success) {
                Notification::create("GIF subido correctamente", NotificationIcon::Success)->show();
                if (m_callback) {
                    m_callback(true, m_gifPath, m_levelID);
                }
            } else {
                Notification::create("Error al subir: " + message, NotificationIcon::Error)->show();
            }
            this->onClose(nullptr);
        });
    });
}

void GIFUploadPopup::onCancelBtn(CCObject*) {
    if (m_callback) {
        m_callback(false, m_gifPath, m_levelID);
    }
    this->onClose(nullptr);
}
