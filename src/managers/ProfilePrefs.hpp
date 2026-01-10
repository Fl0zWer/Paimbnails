#pragma once

#include <Geode/DefaultInclude.hpp>
#include <optional>

struct ProfilePref {
    bool useGradient = false;
    cocos2d::ccColor3B a{255,255,255};
    cocos2d::ccColor3B b{255,255,255};
};

class ProfilePrefs {
public:
    static ProfilePrefs& get();
    std::optional<ProfilePref> getFor(int accountID) const;
    void setFor(int accountID, ProfilePref const& pref);

private:
    ProfilePrefs() = default;
    std::filesystem::path path() const;
    void load() const;
    void save() const;

    mutable bool m_loaded = false;
    mutable std::vector<std::pair<int, ProfilePref>> m_items;
};

