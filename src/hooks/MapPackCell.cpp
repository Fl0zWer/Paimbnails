#include <Geode/Geode.hpp>
#include <Geode/modify/MapPackCell.hpp>
#include "../utils/ListThumbnailCarousel.hpp"
#include <sstream>

using namespace geode::prelude;

class $modify(PaimonMapPackCell, MapPackCell) {
    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<GJMapPack> m_pack = nullptr;
    };

    void loadFromMapPack(GJMapPack* pack) {
        MapPackCell::loadFromMapPack(pack);

        if (!pack) return;
        
        m_fields->m_pack = pack;

        // Remove existing carousel if any (for cell reuse)
        if (m_fields->m_carousel) {
            m_fields->m_carousel->removeFromParent();
            m_fields->m_carousel = nullptr;
        }

        // Delay creation to ensure data is ready and layout is settled
        this->retain();
        Loader::get()->queueInMainThread([this]() {
            if (this->getParent()) { // Check if still valid
                this->createCarousel();
            }
            this->release();
        });
    }

    void createCarousel() {
        auto pack = m_fields->m_pack;
        if (!pack) return;

        // Parse level IDs
        std::vector<int> levelIDs;
        
        if (pack->m_levels && pack->m_levels->count() > 0) {
            for (auto obj : CCArrayExt<CCObject*>(pack->m_levels)) {
                // Try as CCString (Level ID)
                if (auto str = typeinfo_cast<CCString*>(obj)) {
                    if (auto res = geode::utils::numFromString<int>(str->getCString())) {
                        levelIDs.push_back(res.unwrap());
                    }
                } 
                // Try as GJGameLevel
                else if (auto level = typeinfo_cast<GJGameLevel*>(obj)) {
                    levelIDs.push_back(level->m_levelID);
                }
                // Limit removed
            }
        }

        // Fallback: Parse m_levelStrings if m_levels is empty
        if (levelIDs.empty() && !pack->m_levelStrings.empty()) {
            std::string levelsStr(pack->m_levelStrings.c_str());
            std::stringstream ss(levelsStr);
            std::string segment;
            while (std::getline(ss, segment, ',')) {
                if (auto res = geode::utils::numFromString<int>(segment)) {
                    if (res.unwrap() > 0) levelIDs.push_back(res.unwrap());
                }
                // Limit removed
            }
        }

        if (levelIDs.empty()) return;

        auto size = this->getContentSize();
        
        // Force height to match typical cell height if needed
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }

        auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
        if (carousel) {
            carousel->setID("paimon-mappack-carousel");
            
            // Center the carousel in the cell
            carousel->setPosition({size.width / 2, size.height / 2});
            
            // Z=-1 to be behind text/buttons
            carousel->setZOrder(-1); 
            
            // Push background behind
            if (this->getChildrenCount() > 0) {
                auto bg = static_cast<CCNode*>(this->getChildren()->objectAtIndex(0));
                if (bg) {
                    bg->setZOrder(-2);
                }
            }
            
            carousel->setOpacity(255); 
            
            this->addChild(carousel);
            m_fields->m_carousel = carousel;
            
            carousel->startCarousel();
        }
    }
};
