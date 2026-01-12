#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

class BanUserPopup : public geode::Popup<std::string const&> {
protected:
    std::string m_username;
    geode::TextInput* m_input;

    bool setup(std::string const& username) override;
    void onBan(cocos2d::CCObject*);

public:
    static BanUserPopup* create(std::string const& username);
};
