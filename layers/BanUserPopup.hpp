#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

class BanUserPopup : public Popup<std::string const&> {
protected:
    std::string m_username;
    TextInput* m_input;

    bool setup(std::string const& username) override;
    void onBan(CCObject*);

public:
    static BanUserPopup* create(std::string const& username);
};
