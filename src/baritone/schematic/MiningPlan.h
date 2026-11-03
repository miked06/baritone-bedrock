#pragma once
#include "baritone/schematic/Schematic.h"
#include "baritone/selection/SelectionManager.h"
#include "baritone/world/WorldSnapshot.h"
#include "baritone/api/PlayerContext.h"
#include "baritone/cache/WorldScanner.h"
namespace baritone {
class MiningPlan : public Schematic {
public:
    MiningPlan(Selection area, std::string method, BlockFilter objective);
    bool step(const WorldSnapshot& world, const PlayerContext& player, std::size_t budget = 512);
    bool ready() const { return ready_; }
    const std::string& failure() const { return failure_; }
    uint64_t blocks() const { return blocks_; }
    uint64_t objectives() const { return objectives_; }
    double ticks() const { return ticks_; }
    BlockPos origin() const { return origin_; }
    BlockPos size() const override { return size_; }
    bool restrictOutside() const override { return true; }
    bool mayPlace(int,int,int) const override { return false; }
    bool mayBreak(int x,int y,int z) const override { return cells_.contains({x,y,z}); }
    DesiredBlock desired(int x,int y,int z,const BlockProps&) const override { return mayBreak(x,y,z) ? DesiredBlock::air() : DesiredBlock::any(); }
private:
    BlockPos origin_, size_;
    std::string method_, failure_;
    BlockFilter objective_;
    std::unordered_set<BlockPos> cells_;
    std::size_t cursor_ = 0, volume_ = 0;
    uint64_t blocks_ = 0, objectives_ = 0;
    double ticks_ = 0;
    bool ready_ = false;
};
}
