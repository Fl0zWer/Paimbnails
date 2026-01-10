#pragma once
#include <Geode/ui/Popup.hpp>
#include <Geode/DefaultInclude.hpp>
#include "VerificationQueuePopup.hpp" // for PendingCategory
#include "../managers/ThumbnailAPI.hpp"

class ThumbnailViewPopup : public geode::Popup<> {
protected:
    int m_levelID = 0;
    PendingCategory m_category = PendingCategory::Verify;
    cocos2d::CCNode* m_contentNode = nullptr;
    
    // Rating
    cocos2d::CCMenu* m_ratingMenu = nullptr;
    std::vector<cocos2d::CCSprite*> m_stars;
    cocos2d::CCLabelBMFont* m_ratingLabel = nullptr;
    int m_userVote = 0;
    
    // Gallery
    std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
    int m_currentIndex = 0;
    cocos2d::CCSprite* m_currentSprite = nullptr;

    bool setup() override;
    void loadThumbs();
    void setupRating();
    void onRate(cocos2d::CCObject* sender);
    void onReport(cocos2d::CCObject* sender);
    void onDownload(cocos2d::CCObject* sender);
    void onPrev(cocos2d::CCObject* sender);
    void onNext(cocos2d::CCObject* sender);
    void displayThumbnail(int index);
    void onClose(cocos2d::CCObject* sender) override;

public:
    static ThumbnailViewPopup* create(int levelID, PendingCategory cat);
};
