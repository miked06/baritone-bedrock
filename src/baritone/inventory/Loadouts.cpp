#include "baritone/inventory/Loadouts.h"
#include "baritone/cache/WorldScanner.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>

namespace baritone {
namespace {
void validName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        throw std::invalid_argument("Loadout names use 1..64 letters, digits, underscores or hyphens");
}
}
const LoadoutStore::Profile& LoadoutStore::profile(const std::string& name) const {
    const auto found = profiles_.find(name);
    if (found == profiles_.end()) throw std::invalid_argument("Unknown loadout");
    return found->second;
}
void LoadoutStore::set(const std::string& name, LoadoutRule rule) {
    validName(name);
    rule.item = BlockFilter::normalize(std::move(rule.item));
    if (rule.item.size() > 128 || rule.item.find_first_of(" \r\n\t") != std::string::npos || rule.item.ends_with(':') ||
        rule.minimum < 0 || rule.target < rule.minimum || rule.target > 2304 || rule.hotbarSlot < -1 || rule.hotbarSlot > 8 ||
        rule.durabilityPercent < 0 || rule.durabilityPercent > 100 || (rule.hotbarSlot >= 0 && rule.target < 1) || (!profiles_.contains(name) && profiles_.size() >= 64))
        throw std::invalid_argument("Loadout rule: minimum 0..target, target at most 2304, hotbar slot -1..8 and durability 0..100 percent");
    auto updated = profiles_.contains(name) ? profiles_.at(name) : Profile{};
    if (!updated.contains(rule.item) && updated.size() >= 36) throw std::invalid_argument("At most 36 item rules per loadout");
    for (const auto& [item, existing] : updated)
        if (item != rule.item && rule.hotbarSlot >= 0 && existing.hotbarSlot == rule.hotbarSlot)
            throw std::invalid_argument("Two different items cannot reserve the same hotbar slot");
    updated[rule.item] = std::move(rule);
    int total = 0; for (const auto& [item, entry] : updated) total += entry.target;
    if (total > 2304) throw std::invalid_argument("Combined loadout targets exceed inventory capacity");
    profiles_[name] = std::move(updated); healthy_ = true;
}
void LoadoutStore::capture(const std::string& name, const PlayerContext& player) {
    validName(name);
    LoadoutStore copy = *this; copy.profiles_.erase(name);
    std::map<std::string, LoadoutRule> captured;
    for (int i = 0; i < 36; ++i) {
        const auto item = player.hotbar(i); if (item.empty()) continue;
        auto& rule = captured[item.name]; rule.item = item.name;
        rule.minimum += item.count; rule.target += item.count;
        if (i < 9 && rule.hotbarSlot < 0) rule.hotbarSlot = i;
    }
    if (captured.empty()) throw std::invalid_argument("There are no inventory items to capture");
    for (auto& [item, rule] : captured) copy.set(name, std::move(rule));
    *this = std::move(copy);
}
void LoadoutStore::remove(const std::string& name) {
    if (!profiles_.erase(name)) throw std::invalid_argument("Unknown loadout");
    if (active_ == name) active_.clear();
}
void LoadoutStore::activate(const std::string& name) {
    if (!healthy_) throw std::invalid_argument("Loadout file could not be loaded; reload or repair it before enabling reserves");
    if (!name.empty()) (void)profile(name);
    active_ = name;
}
bool LoadoutStore::suitable(const HotbarStack& stack, const LoadoutRule& rule) {
    if (stack.empty() || stack.name != rule.item) return false;
    return stack.maxDamage <= 0 || int64_t(stack.maxDamage - stack.damage) * 100 >= int64_t(stack.maxDamage) * rule.durabilityPercent;
}
std::vector<LoadoutNeed> LoadoutStore::check(const std::string& name, const PlayerContext& player) const {
    std::vector<LoadoutNeed> result;
    for (const auto& [item, rule] : profile(name)) {
        const int have = player.countItem(item);
        result.push_back({rule, have, std::max(0, rule.target - have), rule.hotbarSlot < 0 || suitable(player.hotbar(rule.hotbarSlot), rule)});
    }
    return result;
}
int LoadoutStore::reserved(const std::string& item) const {
    if (!healthy_) throw std::invalid_argument("Inventory reserves unavailable: fix or reload the loadout file first");
    if (active_.empty()) return 0;
    const auto& entries = profile(active_);
    const auto found = entries.find(item);
    return found == entries.end() ? 0 : found->second.minimum;
}
void LoadoutStore::save(const std::filesystem::path& file) const {
    if (!healthy_) throw std::invalid_argument("Cannot save an unresolved loadout policy");
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    auto tmp = file; tmp += ".tmp";
    std::ofstream output(tmp);
    output << "BARITONE_LOADOUTS 1 " << std::quoted(active_) << '\n';
    for (const auto& [name, rules] : profiles_) for (const auto& [item, rule] : rules)
        output << std::quoted(name) << ' ' << std::quoted(item) << ' ' << rule.minimum << ' ' << rule.target << ' ' << rule.hotbarSlot << ' ' << rule.durabilityPercent << '\n';
    output.close(); if (!output) throw std::runtime_error("Could not save loadout profiles");
    std::filesystem::rename(tmp, file);
}
void LoadoutStore::load(const std::filesystem::path& file) {
    healthy_ = false;
    if (std::filesystem::file_size(file) > 1024 * 1024) throw std::invalid_argument("Loadout file exceeds one MiB");
    std::ifstream input(file); std::string magic, active; int version = 0;
    if (!(input >> magic >> version >> std::quoted(active)) || magic != "BARITONE_LOADOUTS" || version != 1)
        throw std::invalid_argument("Invalid loadout file header");
    LoadoutStore parsed;
    while (input >> std::ws && input.peek() != EOF) {
        std::string name; LoadoutRule rule;
        if (!(input >> std::quoted(name) >> std::quoted(rule.item) >> rule.minimum >> rule.target >> rule.hotbarSlot >> rule.durabilityPercent))
            throw std::invalid_argument("Invalid loadout rule");
        const auto normalized = BlockFilter::normalize(rule.item);
        if (parsed.profiles_.contains(name) && parsed.profiles_.at(name).contains(normalized)) throw std::invalid_argument("Duplicate loadout item rule");
        parsed.set(name, std::move(rule));
    }
    parsed.activate(active); *this = std::move(parsed);
}
}
