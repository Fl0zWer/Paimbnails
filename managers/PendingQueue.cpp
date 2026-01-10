#include "PendingQueue.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include <sstream>

using namespace geode::prelude;

PendingQueue& PendingQueue::get() {
    static PendingQueue q; return q;
}

std::filesystem::path PendingQueue::jsonPath() const {
    return Mod::get()->getSaveDir() / "thumbnails" / "pending_queue.json";
}

const char* PendingQueue::catToStr(PendingCategory c) {
    switch (c) {
        case PendingCategory::Verify: return "verify";
        case PendingCategory::Update: return "update";
        case PendingCategory::Report: return "report";
        case PendingCategory::Banner: return "banner";
    }
    return "verify";
}

PendingCategory PendingQueue::strToCat(std::string const& s) {
    if (s == "update") return PendingCategory::Update;
    if (s == "report") return PendingCategory::Report;
    if (s == "banner") return PendingCategory::Banner;
    return PendingCategory::Verify;
}

const char* PendingQueue::statusToStr(PendingStatus s) {
    switch (s) {
        case PendingStatus::Open: return "open";
        case PendingStatus::Accepted: return "accepted";
        case PendingStatus::Rejected: return "rejected";
    }
    return "open";
}

PendingStatus PendingQueue::strToStatus(std::string const& s) {
    if (s == "accepted") return PendingStatus::Accepted;
    if (s == "rejected") return PendingStatus::Rejected;
    return PendingStatus::Open;
}

std::string PendingQueue::escape(const std::string& s) {
    std::string out; out.reserve(s.size()+8);
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

bool PendingQueue::isLevelCreator(GJGameLevel* level, const std::string& username) {
    if (!level || username.empty()) return false;
    
    // Compare with m_creatorName from GJGameLevel
    std::string creatorName = level->m_creatorName;
    
    // Case-insensitive comparison
    auto toLower = [](std::string s) {
        for (auto& c : s) c = (char)tolower(c);
        return s;
    };
    
    return toLower(creatorName) == toLower(username);
}

void PendingQueue::load() {
    if (m_loaded) return;
    m_loaded = true;
    m_items.clear();
    auto p = jsonPath();
    if (!std::filesystem::exists(p)) return;
    auto data = file::readString(p).unwrapOr("");
    if (data.empty()) return;
    // Very small and tolerant manual parse: expect an array of objects in items field
    // We'll search for occurrences of {"levelID":, "category":, ...}
    try {
        size_t pos = data.find("\"items\"");
        if (pos == std::string::npos) return;
        pos = data.find('[', pos);
        if (pos == std::string::npos) return;
        size_t end = data.find(']', pos);
        if (end == std::string::npos) return;
        std::string arr = data.substr(pos+1, end-pos-1);
        // Split by '},{' naive
        size_t start = 0;
        while (start < arr.size()) {
            size_t objEnd = arr.find("},{", start);
            std::string obj = arr.substr(start, (objEnd==std::string::npos?arr.size():objEnd) - start);
            // Extract fields
            auto getStr = [&](const char* key)->std::string{
                std::string k = std::string("\"") + key + "\":";
                size_t p = obj.find(k);
                if (p==std::string::npos) return {};
                p += k.size();
                if (p<obj.size() && obj[p]=='\"') {
                    p++;
                    size_t q = obj.find('"', p);
                    if (q!=std::string::npos) return obj.substr(p, q-p);
                } else {
                    // number
                    size_t q = obj.find_first_of(",}", p);
                    return obj.substr(p, (q==std::string::npos?obj.size():q)-p);
                }
                return {};
            };
            PendingItem it{};
            it.levelID = std::atoi(getStr("levelID").c_str());
            it.category = strToCat(getStr("category"));
            it.timestamp = std::atoll(getStr("timestamp").c_str());
            it.submittedBy = getStr("submittedBy");
            it.note = getStr("note");
            it.status = strToStatus(getStr("status"));
            std::string creatorStr = getStr("isCreator");
            it.isCreator = (creatorStr == "true" || creatorStr == "1");
            if (it.levelID != 0) m_items.push_back(it);
            if (objEnd == std::string::npos) break;
            start = objEnd + 3;
        }
    } catch (...) {
        log::warn("[PendingQueue] Failed to load pending_queue.json; starting empty");
        m_items.clear();
    }
}

void PendingQueue::save() {
    // Keep history; just write full state
    std::string json = toJson();
    auto p = jsonPath();
    std::error_code ec; std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << json; out.close();
}

std::string PendingQueue::toJson() const {
    // Build: {"items":[...]} with all items
    std::stringstream ss;
    ss << "{\"items\":[";
    bool first = true;
    for (auto const& it : m_items) {
        if (!first) ss << ","; first = false;
        ss << "{"
           << "\"levelID\":" << it.levelID << ","
           << "\"category\":\"" << catToStr(it.category) << "\"," 
           << "\"timestamp\":" << it.timestamp << ","
           << "\"submittedBy\":\"" << escape(it.submittedBy) << "\"," 
           << "\"note\":\"" << escape(it.note) << "\"," 
           << "\"status\":\"" << statusToStr(it.status) << "\","
           << "\"isCreator\":" << (it.isCreator ? "true" : "false")
           << "}";
    }
    ss << "]}";
    return ss.str();
}

void PendingQueue::addOrBump(int levelID, PendingCategory cat, std::string submittedBy, std::string note, bool isCreator) {
    load();
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.category == cat && it.status == PendingStatus::Open) {
            it.timestamp = now; 
            if (!submittedBy.empty()) it.submittedBy = std::move(submittedBy); 
            if (!note.empty()) it.note = std::move(note);
            it.isCreator = isCreator;
            save(); syncNow();
            log::info("[PendingQueue] Updated item {} cat {} isCreator={}", levelID, catToStr(cat), isCreator);
            return;
        }
    }
    PendingItem it{}; 
    it.levelID = levelID; 
    it.category = cat; 
    it.timestamp = now; 
    it.submittedBy = std::move(submittedBy); 
    it.note = std::move(note); 
    it.status = PendingStatus::Open;
    it.isCreator = isCreator;
    m_items.push_back(std::move(it));
    save(); syncNow();
    log::info("[PendingQueue] Added item {} cat {} isCreator={}", levelID, catToStr(cat), isCreator);
}

void PendingQueue::removeForLevel(int levelID) {
    load();
    bool changed = false;
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.status == PendingStatus::Open) {
            it.status = PendingStatus::Accepted; changed = true;
        }
    }
    if (changed) { save(); syncNow(); }
    log::info("[PendingQueue] Marked items as accepted for level {}", levelID);
}

void PendingQueue::reject(int levelID, PendingCategory cat, std::string reason) {
    load();
    bool changed = false;
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.category == cat && it.status == PendingStatus::Open) {
            it.status = PendingStatus::Rejected; if (!reason.empty()) it.note = std::move(reason); changed = true;
        }
    }
    if (changed) { save(); syncNow(); }
    log::info("[PendingQueue] Rejected item {} cat {}", levelID, catToStr(cat));
}

void PendingQueue::accept(int levelID, PendingCategory cat) {
    load();
    bool changed = false;
    for (auto& it : m_items) {
        if (it.levelID == levelID && it.category == cat && it.status == PendingStatus::Open) {
            it.status = PendingStatus::Accepted; changed = true;
        }
    }
    if (changed) { save(); syncNow(); }
    log::info("[PendingQueue] Accepted item {} cat {}", levelID, catToStr(cat));
}

std::vector<PendingItem> PendingQueue::list(PendingCategory cat) const {
    const_cast<PendingQueue*>(this)->load();
    std::vector<PendingItem> out;
    for (auto const& it : m_items) if (it.category == cat && it.status == PendingStatus::Open) out.push_back(it);
    // Sort: creator suggestions first, then by timestamp desc
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b){ 
        if (a.isCreator != b.isCreator) return a.isCreator > b.isCreator; // creators first
        return a.timestamp > b.timestamp; // then newest first
    });
    return out;
}

void PendingQueue::syncNow() {
    // Server sync disabled - queue is now local only
    log::info("[PendingQueue] Server sync disabled - changes are saved locally only");
}

