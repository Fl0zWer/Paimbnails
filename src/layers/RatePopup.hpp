#pragma once
#include <Geode/Geode.hpp>
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;

class RatePopup : public Popup<int, std::string> {
protected:
    int m_levelID;
    std::string m_thumbnailId;
    int m_rating = 0;
    std::vector<CCMenuItemSpriteExtra*> m_starBtns;

    bool setup(int levelID, std::string thumbnailId) override;
    void onStar(CCObject* sender);
    void onSubmit(CCObject* sender);

public:
    std::function<void()> m_onRateCallback;
    static RatePopup* create(int levelID, std::string thumbnailId);
};
