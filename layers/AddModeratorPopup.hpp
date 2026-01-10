#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

class AddModeratorPopup : public FLAlertLayer, public TextInputDelegate, public FLAlertLayerProtocol {
protected:
    CCTextInputNode* m_usernameInput = nullptr;
    LoadingCircle* m_loadingCircle = nullptr;
    bool m_closed = false;
    std::function<void(bool, const std::string&)> m_callback;
    
    bool init(std::function<void(bool, const std::string&)> callback);
    void FLAlert_Clicked(FLAlertLayer* layer, bool btn2) override;
    void keyBackClicked() override {
        m_closed = true;
        FLAlertLayer::keyBackClicked();
    }
    
public:
    static AddModeratorPopup* create(std::function<void(bool, const std::string&)> callback);
};

