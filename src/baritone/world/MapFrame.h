#pragma once
#include "baritone/world/WorldSnapshot.h"
#include "baritone/task/TaskQueue.h"
namespace baritone {
struct MapMarker { char kind='W';std::string label;BlockPos position; };
struct MapFrame {
    static constexpr int radius=12,side=25;
    WorldIdentity identity;BlockPos player;
    // 0 unknown, 1 visited this session, 2 currently loaded.
    std::string chunks;
    std::vector<BlockPos> path;
    std::vector<MapMarker> markers;
    std::vector<Selection> zones;
    std::string encode() const;
    static MapFrame decode(const std::string& text);
};
MapFrame surveyMap(const WorldSnapshot& world,WorldIdentity identity,BlockPos player);
}
