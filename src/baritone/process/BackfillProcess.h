#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "baritone/process/Process.h"

namespace baritone {

// backfill setting: puts back blocks that were broken while walking a path, once they are
// behind the player (Baritone's BackfillProcess). Temporary, above the other processes.
class BackfillProcess : public Process {
public:
    using Process::Process;

    bool isActive() override;
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    bool isTemporary() const override { return true; }
    void onLostControl() override {}
    double priority() const override { return 5; }
    std::string displayName() const override { return "Rebouchage"; }

private:
    void recordTargetedBlock();
    std::vector<BlockPos> toFillIn();

    std::unordered_map<BlockPos, std::string> blocksToReplace_;  // position -> block that was there
    BlockPos attempting_;  // the block being placed and for how long: given up after a while
    int attemptTicks_ = 0;
};

}  // namespace baritone
