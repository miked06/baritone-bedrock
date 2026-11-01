#include "baritone/world/Protection.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace baritone {
void ProtectionPolicy::add(Zone zone) {
    if (zone.name.empty() || zone.name.size() > 128 || zone.name.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Zone name must contain 1..128 characters without line breaks");
    if (zone.mode < NoEntry || zone.mode > Both) throw std::invalid_argument("Zone mode must be entry, modify or both");
    if (zones_.size() >= 1024) throw std::invalid_argument("At most 1024 protected zones are supported");
    if (std::ranges::any_of(zones_, [&](const Zone& z) { return z.name == zone.name; })) throw std::invalid_argument("Zone name already exists");
    zones_.push_back(std::move(zone));
}
bool ProtectionPolicy::remove(const std::string& name) { return std::erase_if(zones_, [&](const Zone& z) { return z.name == name; }) != 0; }
bool ProtectionPolicy::permitsEntry(BlockPos p) const {
    return std::ranges::none_of(zones_, [&](const Zone& z) { return (z.mode & NoEntry) && z.bounds.contains(p); });
}
bool ProtectionPolicy::permitsModify(BlockPos p) const {
    return std::ranges::none_of(zones_, [&](const Zone& z) { return (z.mode & NoModify) && z.bounds.contains(p); });
}
bool ProtectionPolicy::permitsSegment(BlockPos from, BlockPos to) const {
    // Segment-vs-box intersection, expanded for the player's 0.6x1.8 body.
    // Unlike endpoint-only checks this also covers parkour and long falls.
    for (const auto& zone : zones_) {
        if (!(zone.mode & NoEntry)) continue;
        const auto lo = zone.bounds.min(), hi = zone.bounds.max();
        double enter = 0, leave = 1;
        bool hits = true;
        const double starts[] = {from.x + .5, double(from.y), from.z + .5};
        const double deltas[] = {double(to.x) - from.x, double(to.y) - from.y, double(to.z) - from.z};
        const double lower[] = {lo.x - .3, lo.y - 1.8, lo.z - .3};
        const double upper[] = {double(hi.x) + 1.3, double(hi.y) + 1, double(hi.z) + 1.3};
        for (int axis = 0; axis < 3; ++axis) {
            if (deltas[axis] == 0) { if (starts[axis] <= lower[axis] || starts[axis] >= upper[axis]) hits = false; continue; }
            double a = (lower[axis] - starts[axis]) / deltas[axis], b = (upper[axis] - starts[axis]) / deltas[axis];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a); leave = std::min(leave, b);
        }
        if (hits && enter < leave && leave > 0 && enter < 1) return false;
    }
    return true;
}
void ProtectionPolicy::save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    auto tmp = path; tmp += ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    out << "BARITONE_ZONES 1\n";
    for (const auto& z : zones_) out << std::quoted(z.name) << ' ' << int(z.mode) << ' '
        << z.bounds.pos1.x << ' ' << z.bounds.pos1.y << ' ' << z.bounds.pos1.z << ' '
        << z.bounds.pos2.x << ' ' << z.bounds.pos2.y << ' ' << z.bounds.pos2.z << '\n';
    out.close(); if (!out) throw std::runtime_error("Could not save protected zones");
    std::filesystem::rename(tmp, path);
}
ProtectionPolicy ProtectionPolicy::load(const std::filesystem::path& path) {
    ProtectionPolicy policy;
    if (!std::filesystem::exists(path)) return policy;
    std::ifstream in(path); std::string magic; int version = 0;
    if (!(in >> magic >> version) || magic != "BARITONE_ZONES" || version != 1) throw std::runtime_error("Invalid protected-zone file");
    while (in >> std::ws && in.peek() != EOF) {
        Zone z; int mode = 0;
        if (!(in >> std::quoted(z.name) >> mode >> z.bounds.pos1.x >> z.bounds.pos1.y >> z.bounds.pos1.z
            >> z.bounds.pos2.x >> z.bounds.pos2.y >> z.bounds.pos2.z)) throw std::runtime_error("Truncated protected-zone file");
        z.mode = static_cast<Mode>(mode); policy.add(std::move(z));
    }
    return policy;
}
}
