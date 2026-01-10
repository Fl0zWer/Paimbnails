#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

enum class PendingCategory { Verify, Update, Report, Banner };
enum class PendingStatus { Open, Accepted, Rejected };

struct Suggestion {
    std::string filename;
    std::string submittedBy;
    int64_t timestamp = 0;
    int accountID = 0;
};

struct PendingItem {
    int levelID = 0;
    PendingCategory category = PendingCategory::Verify;
    int64_t timestamp = 0; // unix seconds
    std::string submittedBy; // GD username if available
    std::string note;        // optional comment/reason
    std::string claimedBy;   // Moderator who claimed this level
    PendingStatus status = PendingStatus::Open;
    bool isCreator = false;  // true if submittedBy is the level creator
    
    std::vector<Suggestion> suggestions;
};

class PendingQueue {
public:
    static PendingQueue& get();

    // Add or update an item; if an open item for same level+category exists, just bump timestamp and update note/user
    void addOrBump(int levelID, PendingCategory cat, std::string submittedBy = {}, std::string note = {}, bool isCreator = false);

    // Remove all items (any category) for a level that are still open
    void removeForLevel(int levelID);

    // Mark item as rejected (and hide from open list)
    void reject(int levelID, PendingCategory cat, std::string reason = {});

    // Mark accepted (used if accepted outside upload callback)
    void accept(int levelID, PendingCategory cat);

    // List open items by category
    std::vector<PendingItem> list(PendingCategory cat) const;

    // Persist locally
    void load();
    void save();

    // Serialize entire queue state to JSON string for server sync
    std::string toJson() const;

    // Call this on any change; debounced sync to server
    void syncNow();
    
    // Make catToStr public for ThumbnailAPI access
    static const char* catToStr(PendingCategory c);
    
    // Helper to check if a username is the level creator
    static bool isLevelCreator(GJGameLevel* level, const std::string& username);

private:
    PendingQueue() = default;
    std::filesystem::path jsonPath() const;
    static PendingCategory strToCat(std::string const& s);
    static const char* statusToStr(PendingStatus s);
    static PendingStatus strToStatus(std::string const& s);
    static std::string escape(const std::string& s);

    bool m_loaded = false;
    mutable std::vector<PendingItem> m_items; // includes non-open for history
};

