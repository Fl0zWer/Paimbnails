#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/UserInfoDelegate.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>

using namespace geode::prelude;

class ModeratorsLayer : public Popup<>, public UserInfoDelegate {
protected:
    CCArray* m_scores;
    CustomListView* m_listView;
    GJListLayer* m_listLayer;
    LoadingCircle* m_loadingCircle = nullptr;
    int m_pendingRequests = 0;
    std::vector<std::string> m_moderatorNames;

    bool setup() override;
    void createList();
    void fetchModerators();
    void fetchGDBrowserProfile(const std::string& username);
    void onProfileFetched(const std::string& username, const std::string& response);
    void onAllProfilesFetched();
    
    void getUserInfoFinished(GJUserScore* score) override;
    void getUserInfoFailed(int type) override;
    void userInfoChanged(GJUserScore* score) override;

    virtual ~ModeratorsLayer();

public:
    static ModeratorsLayer* s_instance;
    bool isScoreInList(GJUserScore* score);
    static ModeratorsLayer* create();
};
