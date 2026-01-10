#include "ThumbnailSelectionPopup.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include <Geode/binding/ButtonSprite.hpp>

ThumbnailSelectionPopup* ThumbnailSelectionPopup::create(const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbnails, std::function<void(const std::string&)> callback) {
    auto ret = new ThumbnailSelectionPopup();
    if (ret && ret->initAnchored(400, 280, thumbnails, callback)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ThumbnailSelectionPopup::setup(const std::vector<ThumbnailAPI::ThumbnailInfo>& thumbnails, std::function<void(const std::string&)> callback) {
    m_thumbnails = thumbnails;
    m_callback = callback;
    
    this->setTitle("Manage Thumbnails");

    auto scroll = ScrollLayer::create({360, 180});
    scroll->setPosition((m_mainLayer->getContentSize().width - 360) / 2, 50);
    m_mainLayer->addChild(scroll);
    
    auto content = CCMenu::create();
    content->setContentSize({360, 200}); // Dynamic height later
    content->setPosition({0,0});
    scroll->m_contentLayer->addChild(content);
    
    float x = 20;
    float y = 160; // Start from top
    int col = 0;

    // "Add New" button removed as per refactor to separate Upload/Select

    
    for (const auto& thumb : m_thumbnails) {
        auto node = CCNode::create();
        node->setContentSize({100, 60});
        
        // Load image (async)
        auto sprite = CCSprite::create("GJ_imagePlaceholder.png"); // Placeholder
        sprite->setScale(0.5f);
        sprite->setPosition({50, 30});
        node->addChild(sprite);
        
        // Download and replace
        sprite->retain();
        ThumbnailAPI::get().downloadFromUrl(thumb.url, [sprite](bool success, CCTexture2D* tex) {
            if (success && tex && sprite) {
                sprite->setTexture(tex);
                sprite->setTextureRect({0, 0, tex->getContentSize().width, tex->getContentSize().height});
                
                float scaleX = 100.0f / tex->getContentSize().width;
                float scaleY = 60.0f / tex->getContentSize().height;
                sprite->setScale(std::min(scaleX, scaleY));
            }
            sprite->release();
        });
        
        auto btn = CCMenuItemSpriteExtra::create(
            node,
            this,
            menu_selector(ThumbnailSelectionPopup::onSelect)
        );
        btn->setUserObject(CCString::create(thumb.id));
        btn->setPosition({x + 50, y - 30});
        content->addChild(btn);
        
        // Type label
        auto label = CCLabelBMFont::create(thumb.type.c_str(), "chatFont.fnt");
        label->setScale(0.4f);
        label->setPosition({x + 50, y - 55});
        content->addChild(label);
        
        x += 110;
        col++;
        if (col >= 3) {
            col = 0;
            x = 20;
            y -= 80;
        }
    }
    
    content->setContentSize({360, std::max(180.0f, 180 + (y * -1))});
    if (y < 0) {
        // Adjust positions if content is larger
        // Simple scroll layer logic is tricky without proper layout
        // For now, assume few thumbnails
    }

    return true;
}

void ThumbnailSelectionPopup::onSelect(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    auto id = static_cast<CCString*>(btn->getUserObject())->getCString();
    
    m_selected = true;
    if (m_callback) {
        m_callback(id);
    }
    this->onClose(nullptr);
}

void ThumbnailSelectionPopup::onClose(CCObject* sender) {
    if (!m_selected && m_callback) {
        m_callback("CANCEL");
    }
    Popup::onClose(sender);
}

void ThumbnailSelectionPopup::onConfirm(CCObject*) {
    // Not used, direct select
}
