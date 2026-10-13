#pragma once
// A small in-memory world to test the pathfinder without the game.
#include <map>
#include <memory>
#include <string>

#include "baritone/world/BlockProps.h"
#include "baritone/world/WorldSnapshot.h"

namespace test {

using namespace baritone;

// Block states used by the tests, registered once.
struct Blocks {
    StateId air = kAir, stone, dirt, grass, sand, gravel, water, flowingWater, lava, bottomSlab, ladder, glass,
            bedrock, flower, cobweb, fence;
    static const Blocks& get();
};

class TestWorld {
public:
    // A world of chunks [-r, r) around the origin, filled with air.
    explicit TestWorld(int chunkRadius = 4, int minY = -64, int maxY = 320);

    void set(int x, int y, int z, StateId id) { snapshot_->setBlock(x, y, z, id); }
    // Fills x0..x1, y0..y1, z0..z1 inclusive.
    void fill(int x0, int y0, int z0, int x1, int y1, int z1, StateId id);
    // Stone floor at y = floorY over the whole loaded area.
    void floor(int floorY, StateId id);

    std::shared_ptr<const WorldSnapshot> snapshot() const { return snapshot_; }
    void setCache(std::shared_ptr<CachedWorld> cache) { snapshot_->setCache(std::move(cache)); }
    int extent() const { return radius_ * 16; }

private:
    int radius_;
    std::shared_ptr<WorldSnapshot> snapshot_;
};

}  // namespace test
