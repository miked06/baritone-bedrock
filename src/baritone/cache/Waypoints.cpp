#include "baritone/cache/Waypoints.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace baritone {

void WaypointStore::open(const std::string& path) {
    if (path == path_) return;
    path_ = path;
    waypoints_.clear();
    if (path.empty()) return;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        Waypoint wp;
        if (s >> wp.tag >> wp.name >> wp.pos.x >> wp.pos.y >> wp.pos.z >> wp.dimension >> wp.createdAt) {
            waypoints_.push_back(std::move(wp));
        }
    }
}

void WaypointStore::save() const {
    if (path_.empty()) return;
    std::ofstream out(path_, std::ios::trunc);
    for (const Waypoint& wp : waypoints_) {
        out << wp.tag << ' ' << wp.name << ' ' << wp.pos.x << ' ' << wp.pos.y << ' ' << wp.pos.z << ' ' << wp.dimension
            << ' ' << wp.createdAt << '\n';
    }
}

void WaypointStore::add(Waypoint wp) {
    if (wp.name.empty()) wp.name = wp.tag;
    waypoints_.push_back(std::move(wp));
    save();
}

bool WaypointStore::remove(const std::string& nameOrTag) {
    auto n = std::erase_if(waypoints_, [&](const Waypoint& w) { return w.name == nameOrTag || w.tag == nameOrTag; });
    if (n) save();
    return n > 0;
}

std::optional<Waypoint> WaypointStore::find(const std::string& nameOrTag) const {
    std::optional<Waypoint> best;
    for (const Waypoint& w : waypoints_) {
        if (w.name != nameOrTag && w.tag != nameOrTag) continue;
        if (!best || w.createdAt >= best->createdAt) best = w;
    }
    return best;
}

bool WaypointStore::removeExact(const Waypoint& wp) {
    auto n = std::erase_if(waypoints_, [&](const Waypoint& w) {
        return w.tag == wp.tag && w.name == wp.name && w.pos == wp.pos && w.createdAt == wp.createdAt;
    });
    if (n) save();
    return n > 0;
}

std::vector<Waypoint> WaypointStore::matching(const std::string& nameOrTag) const {
    std::string tag = tagOf(nameOrTag);
    std::vector<Waypoint> out;
    for (const Waypoint& w : waypoints_)
        if (tag.empty() ? w.name == nameOrTag : w.tag == tag) out.push_back(w);
    std::ranges::sort(out, [](const Waypoint& a, const Waypoint& b) { return a.createdAt > b.createdAt; });
    return out;
}

std::vector<Waypoint> WaypointStore::list(const std::string& tag) const {
    std::vector<Waypoint> out;
    for (const Waypoint& w : waypoints_)
        if (tag.empty() || w.tag == tag) out.push_back(w);
    std::ranges::sort(out, [](const Waypoint& a, const Waypoint& b) { return a.createdAt > b.createdAt; });
    return out;
}

}  // namespace baritone
