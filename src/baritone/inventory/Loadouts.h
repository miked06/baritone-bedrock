#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "baritone/api/PlayerContext.h"

namespace baritone {
struct LoadoutRule {
    std::string item;
    int minimum = 0;
    int target = 0;
    int hotbarSlot = -1;
    int durabilityPercent = 0;
};
struct LoadoutNeed {
    LoadoutRule rule;
    int have = 0;
    int missing = 0;
    bool hotbarReady = true;
};
class LoadoutStore {
public:
    using Profile = std::map<std::string, LoadoutRule>;
    void set(const std::string& profile, LoadoutRule rule);
    void capture(const std::string& profile, const PlayerContext& player);
    void remove(const std::string& profile);
    void activate(const std::string& profile);
    const std::string& active() const { return active_; }
    const std::map<std::string, Profile>& profiles() const { return profiles_; }
    const Profile& profile(const std::string& name) const;
    std::vector<LoadoutNeed> check(const std::string& name, const PlayerContext& player) const;
    int reserved(const std::string& item) const;
    bool healthy() const { return healthy_; }
    void save(const std::filesystem::path& file) const;
    void load(const std::filesystem::path& file);
    static bool suitable(const HotbarStack& stack, const LoadoutRule& rule);
private:
    std::map<std::string, Profile> profiles_;
    std::string active_;
    bool healthy_ = true;
};
}
