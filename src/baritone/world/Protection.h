#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "baritone/selection/SelectionManager.h"

namespace baritone {
class ProtectionPolicy {
public:
    enum Mode { NoEntry = 1, NoModify = 2, Both = 3 };
    struct Zone { std::string name; Selection bounds; Mode mode = Both; };
    const std::vector<Zone>& zones() const { return zones_; }
    void add(Zone zone);
    bool remove(const std::string& name);
    bool permitsEntry(BlockPos position) const;
    bool permitsModify(BlockPos position) const;
    bool permitsSegment(BlockPos from, BlockPos to) const;
    void save(const std::filesystem::path& path) const;
    static ProtectionPolicy load(const std::filesystem::path& path);
private:
    std::vector<Zone> zones_;
};
}
