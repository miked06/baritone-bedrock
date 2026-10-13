#pragma once
#include <unordered_set>
#include <optional>

#include "baritone/process/Process.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone {

// #explore [x z]: go towards the closest chunks never loaded, in a spiral around an
// origin (Baritone's ExploreProcess).
class ExploreProcess : public Process {
public:
    using Process::Process;

    void explore(int centerX, int centerZ);
    // #explorefilter: chunks listed in a JSON file count as explored (or only those, inverted).
    void setFilter(std::unordered_set<int64_t> chunks, bool invert) {
        filter_ = std::move(chunks);
        invertFilter_ = invert;
    }
    std::size_t filterSize() const { return filter_ ? filter_->size() : 0; }
    bool isActive() override { return origin_.has_value(); }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override { origin_.reset(); }
    std::string displayName() const override;

private:
    std::vector<GoalPtr> closestUnexploredChunks();

    bool alreadyExplored(const WorldSnapshot& world, int cx, int cz) const;

    std::optional<BlockPos> origin_;
    int distanceCompleted_ = 0;
    int countRemain() const;
    std::optional<std::unordered_set<int64_t>> filter_;
    bool invertFilter_ = false;
};

}  // namespace baritone
