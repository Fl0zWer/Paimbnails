#pragma once
#include <Geode/Geode.hpp>
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;

class ThumbnailSelectionPopup : public Popup<const std::vector<ThumbnailAPI::ThumbnailInfo>&, std::function<void(const std::string&)>> {
protected:
    std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
    std::function<void(const std::string&)> m_callback;
    CCMenuItemSpriteExtra* m_selectedBtn = nullptr;
    std::string m_selectedId;

    bool setup(const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbnails, std::function<void(const std::string&)> callback) override;
    void onSelect(CCObject* sender);
    void onConfirm(CCObject*);
    void onClose(CCObject* sender) override;
    bool m_selected = false;

public:
    static ThumbnailSelectionPopup* create(const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbnails, std::function<void(const std::string&)> callback);
};
