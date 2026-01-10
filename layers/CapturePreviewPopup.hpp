#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <functional>
#include <memory>

class CapturePreviewPopup : public geode::Popup<> {
public:
    static CapturePreviewPopup* create(
        cocos2d::CCTexture2D* texture, 
        int levelID,
        std::shared_ptr<uint8_t> buffer,
        int width,
        int height,
        std::function<void(bool accepted, int levelID, std::shared_ptr<uint8_t> buffer, int width, int height, std::string mode, std::string replaceId)> callback,
        std::function<void(bool hidePlayer, CapturePreviewPopup* popup)> recaptureCallback = nullptr,
        bool isPlayerHidden = false,
        bool isModerator = false
    );
    
    virtual ~CapturePreviewPopup();

    void updateContent(cocos2d::CCTexture2D* texture, std::shared_ptr<uint8_t> buffer, int width, int height);

protected:
    bool setup() override;

private:
    geode::Ref<cocos2d::CCTexture2D> m_texture;
    std::function<void(bool, int, std::shared_ptr<uint8_t>, int, int, std::string, std::string)> m_callback;
    std::function<void(bool, CapturePreviewPopup*)> m_recaptureCallback;
    std::shared_ptr<uint8_t> m_buffer;
    int m_levelID;
    int m_width = 0;
    int m_height = 0;
    bool m_isPlayerHidden = false;
    bool m_isModerator = false;
    
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCClippingNode* m_clippingNode = nullptr;
    cocos2d::CCLayerColor* m_frameBorder = nullptr;
    
    bool m_isCropped = false;
    bool m_fillMode = true;
    bool m_callbackExecuted = false;

    void updatePreviewScale();
    void onClose(cocos2d::CCObject*) override;

    void onAcceptBtn(cocos2d::CCObject*);
    void onCancelBtn(cocos2d::CCObject*);
    void onCropBtn(cocos2d::CCObject*);
    void onToggleFillBtn(cocos2d::CCObject*);
    void onTogglePlayerBtn(cocos2d::CCObject*);
    void onDownloadBtn(cocos2d::CCObject*);
    void onAddBtn(cocos2d::CCObject*);
    void onReplaceBtn(cocos2d::CCObject*);
    
    // Detectar bordes negros and retornar area of crop (x, and, width, height)
    struct CropRect { int x, y, width, height; };
    CropRect detectBlackBorders();
    
    // Aplicar crop a la imagen
    void applyCrop(const CropRect& rect);
};

