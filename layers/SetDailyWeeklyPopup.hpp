#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

class SetDailyWeeklyPopup : public Popup<int> {
protected:
    int m_levelID;

    bool setup(int value) override;
    
    void onSetDaily(CCObject* sender);
    void onSetWeekly(CCObject* sender);
    void onUnset(CCObject* sender);

public:
    static SetDailyWeeklyPopup* create(int levelID);
};
