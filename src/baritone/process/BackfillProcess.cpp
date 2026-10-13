#include "baritone/process/BackfillProcess.h"

#include <algorithm>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"

namespace baritone {

bool BackfillProcess::isActive() {
    Settings& s = Settings::get();
    if (!s.backfill || !baritone_.player().valid() || !baritone_.liveContext()) return false;
    if (s.allowParkour) {
        baritone_.logDirect("backfill ne marche pas avec allowParkour : backfill desactive", true);
        s.backfill = false;
        return false;
    }
    CalculationContext& ctx = *baritone_.liveContext();
    std::erase_if(blocksToReplace_, [&](const auto& entry) {
        const BlockPos& p = entry.first;
        return !ctx.isLoadedLive(p.x, p.z) || ctx.props(p.x, p.y, p.z).type != BlockType::Air;
    });
    recordTargetedBlock();
    return !toFillIn().empty();
}

// amIBreakingABlockHMMMMMMM: what the crosshair is on while pathing may be broken soon.
void BackfillProcess::recordTargetedBlock() {
    if (!baritone_.pathing().isPathing()) return;
    CalculationContext& ctx = *baritone_.liveContext();
    ExecContext ec{baritone_.player(), ctx, baritone_.inventory(), baritone_.playerFeet()};
    auto hit = ec.objectMouseOver();
    if (!hit) return;
    const BlockProps& p = ctx.props(hit->pos.x, hit->pos.y, hit->pos.z);
    if (p.type != BlockType::Air) blocksToReplace_[hit->pos] = p.name;
}

std::vector<BlockPos> BackfillProcess::toFillIn() {
    CalculationContext& ctx = *baritone_.liveContext();
    BlockPos feet = baritone_.playerFeet();
    std::vector<BlockPos> movementBreaks = baritone_.pathing().currentMovementToBreak();
    std::vector<BlockPos> out;
    for (const auto& [pos, block] : blocksToReplace_) {
        if (ctx.props(pos.x, pos.y, pos.z).type != BlockType::Air) continue;
        if (movement_helper::intersectsPlayer(baritone_.player(), pos)) continue;  // placementPlausible
        if (std::ranges::find(movementBreaks, pos) != movementBreaks.end()) continue;  // still needed open
        out.push_back(pos);
    }
    // farthest first
    std::ranges::sort(out, [&](const BlockPos& a, const BlockPos& b) { return a.distSq(feet) > b.distSq(feet); });
    return out;
}

std::optional<PathingCommand> BackfillProcess::onTick(bool, bool isSafeToCancel) {
    if (!isSafeToCancel) return PathingCommand{nullptr, PathingCommandType::RequestPause};
    CalculationContext& ctx = *baritone_.liveContext();
    ExecContext ec{baritone_.player(), ctx, baritone_.inventory(), baritone_.playerFeet()};
    for (const BlockPos& pos : toFillIn()) {
        MovementState fake;
        auto result = movement_helper::attemptToPlaceABlock(ec, fake, pos, false, false);
        if (result != movement_helper::PlaceResult::NoOption) {
            // a block that never gets placed (entity in the way, refused by the server): forget it
            attemptTicks_ = pos == attempting_ ? attemptTicks_ + 1 : 0;
            attempting_ = pos;
            if (attemptTicks_ > 40) {
                blocksToReplace_.erase(pos);
                attemptTicks_ = 0;
                continue;
            }
        }
        switch (result) {
            case movement_helper::PlaceResult::NoOption: continue;
            case movement_helper::PlaceResult::ReadyToPlace:
                baritone_.controls().setInput(Input::ClickRight, true);
                if (fake.target.rotation) baritone_.controls().setTarget(fake.target);
                return PathingCommand{nullptr, PathingCommandType::RequestPause};
            case movement_helper::PlaceResult::Attempting:  // patience
                if (fake.target.rotation) baritone_.controls().setTarget({fake.target.rotation, true});
                return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
    }
    return PathingCommand{nullptr, PathingCommandType::Defer};  // cede to the other processes
}

}  // namespace baritone
