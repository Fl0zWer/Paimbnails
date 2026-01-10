#pragma once
#include <Geode/DefaultInclude.hpp>
#include <cocos2d.h>
#include <functional>
#include <string>

class GIFUploadPopup : public geode::Popup<> {
public:
    static GIFUploadPopup* create(
        const std::string& gifPath,
        int levelID,
        int frameCount,
        int fileSizeBytes,
        cocos2d::CCTexture2D* previewTexture,
        std::function<void(bool accepted, const std::string& gifPath, int levelID)> callback
    );
    
    virtual ~GIFUploadPopup();

protected:
    bool setup() override;

private:
    std::string m_gifPath;
    int m_levelID;
    int m_frameCount;
    int m_fileSizeBytes;
    std::function<void(bool, const std::string&, int)> m_callback;
    cocos2d::CCTexture2D* m_previewTexture = nullptr;

    void onAcceptBtn(cocos2d::CCObject*);
    void onCancelBtn(cocos2d::CCObject*);
};
