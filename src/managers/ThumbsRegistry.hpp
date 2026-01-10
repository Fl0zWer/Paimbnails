#pragma once
#include <Geode/DefaultInclude.hpp>
#include <optional>
#include <vector>

enum class ThumbKind { Level, Profile };

struct ThumbRecord {
    ThumbKind kind;
    int id;      // levelID or accountID
    bool verified;
};

class ThumbsRegistry {
public:
    static ThumbsRegistry& get();

    void mark(ThumbKind kind, int id, bool verified);
    bool isVerified(ThumbKind kind, int id) const;
    std::vector<ThumbRecord> list(ThumbKind kind, bool onlyUnverified) const;

private:
    ThumbsRegistry() = default;
    std::filesystem::path path() const;
    void load() const; // lazy
    void save() const;

    mutable bool m_loaded = false;
    mutable std::vector<ThumbRecord> m_items;
};

