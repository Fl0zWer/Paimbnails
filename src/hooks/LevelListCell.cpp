#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include "../utils/ListThumbnailCarousel.hpp"
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;

class $modify(PaimonLevelListCell, LevelListCell) {
    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<CCNode> m_listThumbnail = nullptr;
        int m_currentListID = 0;
    };

    // Removed init hook as it caused compilation errors

    
    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);

        if (!list) {
            log::warn("PaimonLevelListCell: list is null");
            return;
        }

        m_fields->m_currentListID = list->m_listID;
        log::info("PaimonLevelListCell: loadFromList called for list ID: {}", list->m_listID);

        // Remove existing carousel if any (for cell reuse)
        if (m_fields->m_carousel) {
            m_fields->m_carousel->removeFromParent();
            m_fields->m_carousel = nullptr;
        }
        
        // Remove existing list thumbnail if any
        if (m_fields->m_listThumbnail) {
            m_fields->m_listThumbnail->removeFromParent();
            m_fields->m_listThumbnail = nullptr;
        }

        // Get level IDs
        std::vector<int> levelIDs;
        
        // Check if m_levels is accessible
        log::info("PaimonLevelListCell: m_levels size: {}", list->m_levels.size());

        for (int id : list->m_levels) {
            if (id != 0) {
                levelIDs.push_back(id);
            }
            // Limit removed to allow full carousel cycling
        }

        auto size = this->getContentSize();
        // log::info("PaimonLevelListCell: Cell content size: {}, {}", size.width, size.height);
        
        // Fallback if size is 0
        if (size.width == 0 || size.height == 0) {
            size = CCSize(356, 50);
            this->setContentSize(size); // Force update content size
        }

        // Force height to match LevelCell (approx 90)
        // This ensures the thumbnail covers the entire cell and looks better
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }
        
        // 1. Create carousel (Default behavior)
        if (!levelIDs.empty()) {
            auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
            if (carousel) {
                carousel->setID("paimon-thumbnail-carousel");
                
                // Center the carousel in the cell and move up 20px (requested +5px more)
                carousel->setPosition({size.width / 2, size.height / 2 + 20.0f});
                
                // Z=-1 to be behind text/buttons
                carousel->setZOrder(-1); 
                
                // Attempt to push background behind the carousel
                // The background is usually the first child
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
                log::info("PaimonLevelListCell: Carousel created and added at {}, {}", size.width/2, size.height/2);
            } else {
                log::error("PaimonLevelListCell: Failed to create carousel");
            }
        }
    }
};
