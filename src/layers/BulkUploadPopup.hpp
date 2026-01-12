#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <filesystem>
#include <vector>

class BulkUploadPopup : public geode::Popup<> {
protected:
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_progressLabel = nullptr;
    cocos2d::CCMenu* m_buttonMenu = nullptr;
    
    std::vector<std::filesystem::path> m_thumbnailFiles;
    size_t m_currentIndex = 0;
    bool m_isUploading = false;
    int m_successCount = 0;
    int m_failCount = 0;
    
    bool setup() override;
    void onSelectFolder(cocos2d::CCObject*);
    void onStartUpload(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*) override;
    
    void scanFolder(const std::filesystem::path& folder);
    void uploadNext();
    void updateStatus(const std::string& message);
    void updateProgress();
    
    int extractLevelIdFromFilename(const std::string& filename);

public:
    static BulkUploadPopup* create();
};

