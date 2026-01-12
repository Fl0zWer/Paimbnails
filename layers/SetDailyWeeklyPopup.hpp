#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

class SetDailyWeeklyPopup : public geode::Popup<int> {
protected:
    int m_levelID;

    bool setup(int value) override;
    
    void onSetDaily(cocos2d::CCObject* sender);
    void onSetWeekly(cocos2d::CCObject* sender);
    void onUnset(cocos2d::CCObject* sender);

public:
    static SetDailyWeeklyPopup* create(int levelID);
};
