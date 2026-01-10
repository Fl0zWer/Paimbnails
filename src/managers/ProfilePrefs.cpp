#include "ProfilePrefs.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <sstream>

using namespace geode::prelude;
using namespace cocos2d;

ProfilePrefs& ProfilePrefs::get() { static ProfilePrefs p; return p; }

std::filesystem::path ProfilePrefs::path() const {
    return Mod::get()->getSaveDir() / "thumbnails" / "profile_prefs.csv";
}

void ProfilePrefs::load() const {
    if (m_loaded) return; m_loaded = true; m_items.clear();
    auto p = path(); if (!std::filesystem::exists(p)) return;
    auto data = file::readString(p).unwrapOr("");
    std::stringstream ss(data); std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string id, grad, r1,g1,b1, r2,g2,b2;
        if (!std::getline(ls, id, ',')) continue;
        if (!std::getline(ls, grad, ',')) continue;
        if (!std::getline(ls, r1, ',')) continue;
        if (!std::getline(ls, g1, ',')) continue;
        if (!std::getline(ls, b1, ',')) continue;
        if (!std::getline(ls, r2, ',')) continue;
        if (!std::getline(ls, g2, ',')) continue;
        if (!std::getline(ls, b2, ',')) continue;
        ProfilePref pref{};
        pref.useGradient = (grad == "1");
        pref.a = { (GLubyte)std::atoi(r1.c_str()), (GLubyte)std::atoi(g1.c_str()), (GLubyte)std::atoi(b1.c_str()) };
        pref.b = { (GLubyte)std::atoi(r2.c_str()), (GLubyte)std::atoi(g2.c_str()), (GLubyte)std::atoi(b2.c_str()) };
        m_items.emplace_back(std::atoi(id.c_str()), pref);
    }
}

void ProfilePrefs::save() const {
    std::stringstream ss;
    for (auto const& it : m_items) {
        auto const& p = it.second;
        ss << it.first << "," << (p.useGradient?"1":"0")
           << "," << (int)p.a.r << "," << (int)p.a.g << "," << (int)p.a.b
           << "," << (int)p.b.r << "," << (int)p.b.g << "," << (int)p.b.b << "\n";
    }
    auto p = path(); std::filesystem::create_directories(p.parent_path());
    [[maybe_unused]] auto _ = file::writeString(p, ss.str());
}

std::optional<ProfilePref> ProfilePrefs::getFor(int accountID) const {
    load();
    for (auto const& it : m_items) if (it.first == accountID) return it.second;
    return std::nullopt;
}

void ProfilePrefs::setFor(int accountID, ProfilePref const& pref) {
    load();
    for (auto& it : m_items) if (it.first == accountID) { it.second = pref; save(); return; }
    m_items.emplace_back(accountID, pref); save();
}

