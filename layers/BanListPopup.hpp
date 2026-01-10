#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/DefaultInclude.hpp>

struct BanDetail {
    std::string reason;
    std::string bannedBy;
    std::string date;
};

class BanListPopup : public geode::Popup<> {
protected:
    cocos2d::CCMenu* m_listMenu = nullptr;
    cocos2d::extension::CCScrollView* m_scroll = nullptr;
    std::map<std::string, BanDetail> m_banDetails;

    bool setup() override;
    void rebuildList(const std::vector<std::string>& users);
    void onUnban(cocos2d::CCObject* sender);
    void onInfo(cocos2d::CCObject* sender);

public:
    static BanListPopup* create();
};
