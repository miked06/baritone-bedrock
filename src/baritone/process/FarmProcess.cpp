#include "baritone/process/FarmProcess.h"

#include <cmath>
#include <unordered_set>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"

namespace baritone {

namespace {

// Java FarmProcess scan: the Harvest blocks, plus farmland and jungle logs with
// replantCrops, plus soul sand with replantNetherWart
std::vector<std::string> scannedBlocks() {
    std::vector<std::string> blocks{
        "wheat", "carrots", "potatoes", "beetroot", "pumpkin", "melon_block", "nether_wart", "cocoa", "reeds", "bamboo", "cactus",
    };
    const Settings& s = Settings::get();
    if (s.replantCrops) {
        blocks.push_back("farmland");
        blocks.push_back("jungle_log");
        if (s.replantNetherWart) blocks.push_back("soul_sand");
    }
    return blocks;
}

bool isCropDrop(const std::string& item) {
    static const std::unordered_set<std::string> drops{
        "minecraft:wheat", "minecraft:wheat_seeds", "minecraft:carrot", "minecraft:potato", "minecraft:poisonous_potato",
        "minecraft:beetroot", "minecraft:beetroot_seeds", "minecraft:melon_slice", "minecraft:pumpkin",
        "minecraft:sugar_cane", "minecraft:cactus", "minecraft:nether_wart", "minecraft:cocoa_beans", "minecraft:bamboo",
    };
    return drops.contains(item);
}

// crops that bone meal grows (Baritone: IGrowable blocks that accept it)
bool isBonemealable(const BlockProps& p) {
    if (p.matureGrowth == 0 || p.growth >= p.matureGrowth) return false;
    return p.name != "minecraft:nether_wart";
}

bool isBoneMeal(const HotbarStack& s) { return s.name == "minecraft:bone_meal"; }
bool isCocoa(const HotbarStack& s) { return s.name == "minecraft:cocoa_beans"; }

}  // namespace

bool FarmProcess::isPlantable(const std::string& item) {
    static const std::unordered_set<std::string> seeds{
        "minecraft:wheat_seeds", "minecraft:beetroot_seeds", "minecraft:carrot", "minecraft:potato",
        "minecraft:melon_seeds", "minecraft:pumpkin_seeds", "minecraft:torchflower_seeds", "minecraft:pitcher_pod",
    };
    return seeds.contains(item);
}

void FarmProcess::farm(int range, BlockPos center) {
    active_ = true;
    range_ = range;
    center_ = center;
    tickCount_ = 0;
    locations_.reset();
}

bool FarmProcess::readyForHarvest(CalculationContext& ctx, const BlockPos& pos, const BlockProps& p) {
    if (p.matureGrowth > 0) return p.growth >= p.matureGrowth;
    const std::string& n = p.name;
    if (n == "minecraft:pumpkin" || n == "minecraft:melon_block") return true;
    if (n == "minecraft:reeds" || n == "minecraft:cactus" || n == "minecraft:bamboo") {
        // replantCrops: keep the bottom block so it regrows, else take it all
        if (Settings::get().replantCrops) return ctx.props(pos.x, pos.y - 1, pos.z).name == n;
        return true;
    }
    return false;
}

std::optional<PathingCommand> FarmProcess::onTick(bool calcFailed, bool isSafeToCancel) {
    if (!active_ || !baritone_.liveContext()) return std::nullopt;
    CalculationContext& ctx = *baritone_.liveContext();
    const Settings& st = Settings::get();
    if (st.mineGoalUpdateInterval != 0 && tickCount_++ % st.mineGoalUpdateInterval == 0) {
        // Java scanChunkRadius(ctx, scan, farmMaxScanSize, 10, 10)
        scan_.start(baritone_.world(), BlockFilter(scannedBlocks()), baritone_.playerFeet(), st.farmMaxScanSize, 10);
    }
    if (auto result = scan_.poll()) locations_ = std::move(*result);
    if (!locations_) return PathingCommand{nullptr, PathingCommandType::RequestPause};
    if (st.farmUsingSelection && !baritone_.selections().all().empty()) {
        const Selection& last = baritone_.selections().all().back();
        std::erase_if(*locations_, [&](const BlockPos& p) { return !last.contains(p); });
    }

    std::vector<BlockPos> toBreak, openFarmland, bonemealable, openSoulSand, openLog;
    for (const BlockPos& pos : *locations_) {
        if (range_ != 0 && pos.distSq(center_) > static_cast<int64_t>(range_) * range_) continue;
        const BlockProps& p = ctx.props(pos.x, pos.y, pos.z);
        bool airAbove = ctx.props(pos.x, pos.y + 1, pos.z).type == BlockType::Air;
        if (p.name == "minecraft:farmland") {
            if (airAbove) openFarmland.push_back(pos);
            continue;
        }
        if (p.name == "minecraft:soul_sand") {
            if (airAbove) openSoulSand.push_back(pos);
            continue;
        }
        if (p.name == "minecraft:jungle_log") {
            for (Direction d : kHorizontals)
                if (ctx.props(pos.offset(d).x, pos.y, pos.offset(d).z).type == BlockType::Air) {
                    openLog.push_back(pos);
                    break;
                }
            continue;
        }
        if (readyForHarvest(ctx, pos, p)) {
            toBreak.push_back(pos);
            continue;
        }
        if (isBonemealable(p)) bonemealable.push_back(pos);
    }

    ExecContext ec{baritone_.player(), ctx, baritone_.inventory(), baritone_.playerFeet()};
    BlockPos feet = baritone_.playerFeet();
    double reach = baritone_.player().blockReachDistance();
    MovementState& controls = baritone_.controls();
    auto inReach = [&](const BlockPos& pos) { return static_cast<double>(feet.distSq(pos)) <= reach * reach; };

    // harvest what's in reach
    for (const BlockPos& pos : toBreak) {
        if (!inReach(pos)) continue;
        auto rot = movement_helper::reachable(ec, pos);
        if (rot && isSafeToCancel) {
            controls.setTarget({*rot, true});
            movement_helper::switchToBestToolFor(ec, ctx.props(pos.x, pos.y, pos.z));
            if (ec.isLookingAt(pos)) controls.setInput(Input::ClickLeft, true);
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
    }
    // replant: seeds on farmland, nether wart on soul sand (aimed at the top face)
    std::function<bool(const HotbarStack&)> plantable = [](const HotbarStack& s) { return isPlantable(s.name); };
    std::function<bool(const HotbarStack&)> wart = [](const HotbarStack& s) { return s.name == "minecraft:nether_wart"; };
    std::vector<std::pair<BlockPos, bool>> both;
    for (const BlockPos& p : openFarmland) both.emplace_back(p, false);
    for (const BlockPos& p : openSoulSand) both.emplace_back(p, true);
    for (const auto& [pos, soulSand] : both) {
        if (!inReach(pos)) continue;
        Vec3d top{pos.x + 0.5, pos.y + 1.0, pos.z + 0.5};
        Rotation rot = rotation_utils::calcRotationFromVec3d(ec.playerHead(), top, ec.playerRotations());
        auto hit = rayTrace(ctx.bsi, ec.playerHead(), rot, reach);
        if (!hit || hit->pos != pos) continue;
        if (!isSafeToCancel || !baritone_.inventory().selectItem(soulSand ? wart : plantable, true)) continue;
        if (hit->face != Direction::Up) continue;
        controls.setTarget({rot, true});
        if (ec.isLookingAt(pos)) controls.setInput(Input::ClickRight, true);
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    // cocoa beans on the free sides of jungle logs
    std::function<bool(const HotbarStack&)> cocoa = isCocoa;
    for (const BlockPos& pos : openLog) {
        if (!inReach(pos)) continue;
        for (Direction dir : kHorizontals) {
            BlockPos side = pos.offset(dir);
            if (ctx.props(side.x, side.y, side.z).type != BlockType::Air) continue;
            DirOffset o = offsetOf(dir);
            Vec3d faceCenter{pos.x + 0.5 + o.x * 0.5, pos.y + 0.5, pos.z + 0.5 + o.z * 0.5};
            Rotation rot = rotation_utils::calcRotationFromVec3d(ec.playerHead(), faceCenter, ec.playerRotations());
            auto hit = rayTrace(ctx.bsi, ec.playerHead(), rot, reach);
            if (!hit || hit->pos != pos || hit->face != dir) continue;
            if (!isSafeToCancel || !baritone_.inventory().selectItem(cocoa, true)) continue;
            controls.setTarget({rot, true});
            if (ec.isLookingAt(pos)) controls.setInput(Input::ClickRight, true);
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
    }
    // bone meal on growing crops in reach
    std::function<bool(const HotbarStack&)> boneMeal = isBoneMeal;
    for (const BlockPos& pos : bonemealable) {
        if (!inReach(pos)) continue;
        // a young crop is only a few pixels high: the game's own ray must hit it, so aim low
        Vec3d low{pos.x + 0.5, pos.y + 0.08, pos.z + 0.5};
        Rotation rot = rotation_utils::calcRotationFromVec3d(ec.playerHead(), low, ec.playerRotations());
        auto hit = rayTrace(ctx.bsi, ec.playerHead(), rot, reach);
        if (!hit || hit->pos != pos) continue;
        if (!isSafeToCancel || !baritone_.inventory().selectItem(boneMeal, true)) continue;
        controls.setTarget({rot, true});
        if (ec.isLookingAt(pos)) controls.setInput(Input::ClickRight, true);
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }

    auto failed = [&] {
        baritone_.logDirect("Farm echoue", true);
        if (st.notificationOnFarmFail) baritone_.logNotification("Farm echoue", true);
        onLostControl();
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    };
    if (calcFailed) return failed();

    std::vector<GoalPtr> goals;
    for (const BlockPos& pos : toBreak) goals.push_back(std::make_shared<GoalBreak>(pos));
    if (baritone_.inventory().selectItem(plantable, false)) {
        for (const BlockPos& pos : openFarmland) goals.push_back(std::make_shared<GoalBlock>(pos.above()));
    }
    if (baritone_.inventory().selectItem(wart, false)) {
        for (const BlockPos& pos : openSoulSand) goals.push_back(std::make_shared<GoalBlock>(pos.above()));
    }
    if (baritone_.inventory().selectItem(cocoa, false)) {
        for (const BlockPos& pos : openLog)
            for (Direction d : kHorizontals) {
                BlockPos side = pos.offset(d);
                if (ctx.props(side.x, side.y, side.z).type == BlockType::Air) goals.push_back(std::make_shared<GoalGetToBlock>(side));
            }
    }
    if (baritone_.inventory().selectItem(boneMeal, false)) {
        for (const BlockPos& pos : bonemealable) goals.push_back(std::make_shared<GoalBlock>(pos));
    }
    for (const EntityInfo& e : baritone_.player().entities()) {
        if (!e.isItem || !isCropDrop(e.itemName)) continue;
        if (range_ != 0 && (e.position - Vec3d::center(center_)).lengthSq() > static_cast<double>(range_) * range_) continue;
        goals.push_back(std::make_shared<GoalBlock>(static_cast<int>(std::floor(e.position.x)),
                                                    static_cast<int>(std::floor(e.position.y + 0.1)),
                                                    static_cast<int>(std::floor(e.position.z))));
    }
    if (goals.empty()) return failed();
    return PathingCommand{std::make_shared<GoalComposite>(std::move(goals)), PathingCommandType::SetGoalAndPath};
}

}  // namespace baritone
