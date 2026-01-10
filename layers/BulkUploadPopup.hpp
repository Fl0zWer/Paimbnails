#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <filesystem>
#include <vector>

using namespace geode::prelude;

class BulkUploadPopup : public Popup<> {
protected:
    CCLabelBMFont* m_statusLabel = nullptr;
    CCLabelBMFont* m_progressLabel = nullptr;
    CCMenu* m_buttonMenu = nullptr;
    
    std::vector<std::filesystem::path> m_thumbnailFiles;
    size_t m_currentIndex = 0;
    bool m_isUploading = false;
    int m_successCount = 0;
    int m_failCount = 0;
    
    bool setup() override;
    void onSelectFolder(CCObject*);
    void onStartUpload(CCObject*);
    void onClose(CCObject*) override;
    
    void scanFolder(const std::filesystem::path& folder);
    void uploadNext();
    void updateStatus(const std::string& message);
    void updateProgress();
    
    int extractLevelIdFromFilename(const std::string& filename);

public:
    static BulkUploadPopup* create();
};

