#include "ThumbsRegistry.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

namespace {
    const char* kindToStr(ThumbKind k) { return k == ThumbKind::Level ? "level" : "profile"; }
    ThumbKind strToKind(std::string const& s) { return s == "profile" ? ThumbKind::Profile : ThumbKind::Level; }
}

ThumbsRegistry& ThumbsRegistry::get() { static ThumbsRegistry r; return r; }

std::filesystem::path ThumbsRegistry::path() const {
    return Mod::get()->getSaveDir() / "thumbnails" / "registry.csv";
}

void ThumbsRegistry::load() const {
    if (m_loaded) return;
    m_loaded = true;
    m_items.clear();
    auto p = path();
    if (!std::filesystem::exists(p)) return;
    auto data = file::readString(p).unwrapOr("");
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string kind, idStr, verStr;
        if (!std::getline(ls, kind, ',')) continue;
        if (!std::getline(ls, idStr, ',')) continue;
        if (!std::getline(ls, verStr, ',')) verStr = "0";
        ThumbRecord r{};
        r.kind = strToKind(kind);
        r.id = std::atoi(idStr.c_str());
        r.verified = (verStr == "1");
        if (r.id != 0) m_items.push_back(r);
    }
}

void ThumbsRegistry::save() const {
    std::stringstream ss;
    for (auto const& r : m_items) {
        ss << kindToStr(r.kind) << "," << r.id << "," << (r.verified ? "1" : "0") << "\n";
    }
    auto p = path();
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << ss.str();
    out.close();
}

void ThumbsRegistry::mark(ThumbKind kind, int id, bool verified) {
    load();
    for (auto& r : m_items) {
        if (r.kind == kind && r.id == id) { r.verified = verified; save(); return; }
    }
    m_items.push_back({kind, id, verified});
    save();
}

bool ThumbsRegistry::isVerified(ThumbKind kind, int id) const {
    load();
    for (auto const& r : m_items) if (r.kind == kind && r.id == id) return r.verified;
    return false;
}

std::vector<ThumbRecord> ThumbsRegistry::list(ThumbKind kind, bool onlyUnverified) const {
    load();
    std::vector<ThumbRecord> out;
    for (auto const& r : m_items) {
        if (r.kind != kind) continue;
        if (onlyUnverified && r.verified) continue;
        out.push_back(r);
    }
    return out;
}

