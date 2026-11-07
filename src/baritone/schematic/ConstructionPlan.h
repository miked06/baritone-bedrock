#pragma once
#include "baritone/schematic/Schematic.h"
#include "baritone/world/WorldSnapshot.h"
#include "baritone/task/TaskQueue.h"
namespace baritone {
struct ConstructionRecovery;
class ConstructionPlan {
public:
    void start(SchematicPtr source, BlockPos origin, std::string support, int maxDepth, bool permanent);
    bool step(const WorldSnapshot& world, std::size_t budget = 512);
    bool ready() const { return ready_; }
    const std::string& failure() const { return failure_; }
    const auto& supports() const { return supports_; }
    const auto& dependencies() const { return dependencies_; }
    BlockPos phaseOrigin() const { return phaseOrigin_; }
    SchematicPtr phase(int phase) const;
    bool permanent() const { return permanent_; }
    const auto& targets() const { return final_; }
    std::string checkpoint(const WorldIdentity& identity,int phase) const;
    static ConstructionRecovery recover(const std::string& text);
private:
    SchematicPtr source_;
    BlockPos origin_, phaseOrigin_, size_;
    std::string support_, failure_;
    std::map<BlockPos, DesiredBlock> final_, supports_;
    std::vector<std::pair<BlockPos, BlockPos>> dependencies_;
    std::map<BlockPos, DesiredBlock>::const_iterator cursor_;
    std::size_t scanned_ = 0, volume_ = 0;
    int depth_ = 0;
    bool collected_ = false, ready_ = false, permanent_ = false;
};
struct ConstructionRecovery { ConstructionPlan plan; WorldIdentity identity; int phase=0; };
}
