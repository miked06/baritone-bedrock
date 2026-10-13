#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "baritone/utils/BlockPos.h"

namespace baritone {

// Baritone's waypoints: home, death, bed, user. Stored per world in a text file.
struct Waypoint {
    std::string tag;   // home, death, bed, user
    std::string name;
    BlockPos pos;
    int dimension = 0;
    int64_t createdAt = 0;  // unix seconds
};

class WaypointStore {
public:
    // Loads the file of a world ("" = no world). Saves happen on every change.
    void open(const std::string& path);
    const std::string& path() const { return path_; }

    void add(Waypoint wp);
    bool remove(const std::string& nameOrTag);
    // Most recent waypoint with this name or tag.
    std::optional<Waypoint> find(const std::string& nameOrTag) const;
    std::vector<Waypoint> list(const std::string& tag = "") const;

    // Removes this exact waypoint (same tag, name, position and creation time).
    bool removeExact(const Waypoint& wp);
    // Java ForWaypoints: a tag gives all waypoints of the tag, else those with this name;
    // most recent first.
    std::vector<Waypoint> matching(const std::string& nameOrTag) const;

    // Java IWaypoint.Tag names: home (base), death, bed (spawn), user. "" if not a tag.
    static std::string tagOf(const std::string& s) {
        if (s == "home" || s == "base") return "home";
        if (s == "death") return "death";
        if (s == "bed" || s == "spawn") return "bed";
        if (s == "user") return "user";
        return {};
    }
    static bool isTag(const std::string& s) { return !tagOf(s).empty(); }

private:
    void save() const;
    std::string path_;
    std::vector<Waypoint> waypoints_;
};

}  // namespace baritone
