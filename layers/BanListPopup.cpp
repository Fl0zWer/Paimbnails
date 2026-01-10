#include "BanListPopup.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/cocos/extensions/GUI/CCScrollView/CCScrollView.h>
#include <Geode/utils/cocos.hpp>

#include "../utils/HttpClient.hpp"
#include "../utils/Localization.hpp"

using namespace geode::prelude;
using namespace cocos2d;

BanListPopup* BanListPopup::create() {
    auto ret = new BanListPopup();
    if (ret && ret->initAnchored(360.f, 260.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BanListPopup::setup() {
    this->setTitle(Localization::get().getString("ban.list.title"));

    auto content = this->m_mainLayer->getContentSize();

    auto panel = CCScale9Sprite::create("square02_001.png");
    panel->setColor({0, 0, 0});
    panel->setOpacity(70);
    panel->setContentSize(CCSizeMake(content.width - 20.f, content.height - 60.f));
    panel->setPosition({content.width / 2, content.height / 2 - 10.f});
    this->m_mainLayer->addChild(panel);

    m_listMenu = CCMenu::create();
    m_listMenu->setPosition({0, 0});

    m_scroll = cocos2d::extension::CCScrollView::create();
    m_scroll->setViewSize(panel->getContentSize());
    m_scroll->setPosition(panel->getPosition() - panel->getContentSize() / 2);
    m_scroll->setDirection(kCCScrollViewDirectionVertical);
    m_scroll->setContainer(m_listMenu);
    this->m_mainLayer->addChild(m_scroll, 5);

    // Initial loading state
    {
        auto lbl = CCLabelBMFont::create(Localization::get().getString("ban.list.loading").c_str(), "goldFont.fnt");
        lbl->setScale(0.45f);
        lbl->setPosition({panel->getContentSize().width / 2, panel->getContentSize().height / 2});
        m_listMenu->addChild(lbl);
        m_listMenu->setContentSize(panel->getContentSize());
    }

    // Fetch ban list
    this->retain();
    HttpClient::get().getBanList([this](bool success, const std::string& jsonData) {
        std::vector<std::string> users;

        if (success) {
            try {
                auto parsed = matjson::parse(jsonData);
                if (parsed.isOk()) {
                    auto root = parsed.unwrap();
                    if (root.isObject()) {
                        auto banned = root["banned"];
                        if (banned.isArray()) {
                            for (auto const& v : banned.asArray().unwrap()) {
                                if (v.isString()) {
                                    users.push_back(v.asString().unwrap());
                                }
                            }
                        }

                        if (root.contains("details") && root["details"].isObject()) {
                            for (auto const& val : root["details"]) {
                                if (val.isObject()) {
                                    auto keyOpt = val.getKey();
                                    if (!keyOpt) continue;
                                    std::string key = *keyOpt;

                                    BanDetail d;
                                    if (val.contains("reason") && val["reason"].isString()) 
                                        d.reason = val["reason"].asString().unwrap();
                                    if (val.contains("bannedBy") && val["bannedBy"].isString()) 
                                        d.bannedBy = val["bannedBy"].asString().unwrap();
                                    if (val.contains("date") && val["date"].isString()) 
                                        d.date = val["date"].asString().unwrap();
                                    
                                    m_banDetails[key] = d;
                                }
                            }
                        }
                    }
                }
            } catch (...) {
                // ignore parse errors; show empty
            }
        }

        if (this->getParent()) {
            rebuildList(users);
        }
        this->release();
    });

    return true;
}

void BanListPopup::rebuildList(const std::vector<std::string>& users) {
    m_listMenu->removeAllChildren();

    auto viewSize = m_scroll->getViewSize();

    if (users.empty()) {
        auto lbl = CCLabelBMFont::create(Localization::get().getString("ban.list.empty").c_str(), "goldFont.fnt");
        lbl->setScale(0.5f);
        lbl->setPosition({viewSize.width / 2, viewSize.height / 2});
        m_listMenu->addChild(lbl);
        m_listMenu->setContentSize(viewSize);
        return;
    }

    float rowH = 30.f;
    float totalH = rowH * users.size() + 10.f;
    m_listMenu->setContentSize({viewSize.width, std::max(viewSize.height, totalH)});

    float left = 14.f;
    for (size_t i = 0; i < users.size(); ++i) {
        float y = m_listMenu->getContentSize().height - 18.f - (float)i * rowH;

        auto bg = CCScale9Sprite::create("square02_001.png");
        bg->setColor({0, 0, 0});
        bg->setOpacity(55);
        bg->setContentSize({viewSize.width - 18.f, 26.f});
        bg->setAnchorPoint({0, 0.5f});
        bg->setPosition({left - 6.f, y});
        m_listMenu->addChild(bg);

        auto name = CCLabelBMFont::create(users[i].c_str(), "chatFont.fnt");
        name->setScale(0.5f);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({left, y});
        m_listMenu->addChild(name);

        // Info Button
        auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        infoSpr->setScale(0.6f);
        auto infoBtn = CCMenuItemSpriteExtra::create(infoSpr, this, menu_selector(BanListPopup::onInfo));
        infoBtn->setUserObject(CCString::create(users[i]));
        infoBtn->setPosition({viewSize.width - 90.f, y});
        m_listMenu->addChild(infoBtn);

        // Unban Button
        auto unbanSpr = ButtonSprite::create(Localization::get().getString("ban.list.unban_btn").c_str(), 50, true, "goldFont.fnt", "GJ_button_05.png", 30.f, 0.6f);
        unbanSpr->setScale(0.7f);
        auto unbanBtn = CCMenuItemSpriteExtra::create(unbanSpr, this, menu_selector(BanListPopup::onUnban));
        unbanBtn->setUserObject(CCString::create(users[i]));
        unbanBtn->setPosition({viewSize.width - 45.f, y});
        m_listMenu->addChild(unbanBtn);
    }
}

void BanListPopup::onInfo(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = static_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();
    std::string body = Localization::get().getString("ban.info.no_info");
    
    if (m_banDetails.count(username)) {
        auto& d = m_banDetails[username];
        body = fmt::format(
            "{}: <cy>{}</c>\n"
            "{}: <cg>{}</c>\n"
            "{}: <cl>{}</c>",
            Localization::get().getString("ban.info.reason"), d.reason.empty() ? "N/A" : d.reason,
            Localization::get().getString("ban.info.by"), d.bannedBy.empty() ? "N/A" : d.bannedBy,
            Localization::get().getString("ban.info.date"), d.date.empty() ? "N/A" : d.date
        );
    }
    
    FLAlertLayer::create(Localization::get().getString("ban.info.title").c_str(), body, "OK")->show();
}

void BanListPopup::onUnban(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto strObj = static_cast<CCString*>(btn->getUserObject());
    if (!strObj) return;
    
    std::string username = strObj->getCString();

    Ref<BanListPopup> self = this;
    geode::createQuickPopup(
        Localization::get().getString("ban.unban.title").c_str(),
        fmt::format(fmt::runtime(Localization::get().getString("ban.unban.confirm")), username),
        "Cancel", Localization::get().getString("ban.list.unban_btn").c_str(),
        [self, username](auto, bool btn2) {
            if (btn2) {
                HttpClient::get().unbanUser(username, [self](bool success, const std::string& msg) {
                    if (success) {
                        Notification::create(Localization::get().getString("ban.unban.success"), NotificationIcon::Success)->show();
                        self->onClose(nullptr);
                    } else {
                        Notification::create(Localization::get().getString("ban.unban.error"), NotificationIcon::Error)->show();
                    }
                });
            }
        }
    );
}
