#pragma once
#include <map>
#include <optional>
#include <unordered_set>
#include <vector>

#include "baritone/process/Process.h"
#include "baritone/schematic/Schematic.h"
#include "baritone/utils/Rotation.h"
#include "baritone/world/BlockProps.h"

namespace baritone {

class CalculationContext;
struct ExecContext;

// #build / #sel set...: makes the world match a schematic, breaking what's wrong and
// placing what's missing (Baritone's BuilderProcess: incorrect positions, layers, repeats,
// rotation/mirror, the same settings).
// Bedrock: block orientation (stairs, logs, slabs, furnaces...) comes from how a block is
// placed, so a placement is only done from a face/spot that gives the right orientation
// (schematic/Orientation.h) instead of predicting the placed state like Java.
class BuilderProcess : public Process {
public:
    using Process::Process;

    // fromFile: Java's build(File) path, which applies mapArtMode and buildOnlySelection.
    void build(std::string name, SchematicPtr schematic, BlockPos origin, bool fromFile = false);
    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    bool isPaused() const { return paused_; }

    bool isActive() override { return schematic_ != nullptr; }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override;
    std::string displayName() const override;
    void customizeContext(CalculationContext& ctx) override;

    // Java placeAt: the block wanted at a position while building (not air), if any.
    std::optional<std::string> placeAt(int x, int y, int z, const BlockProps& current) const;

    // Rendering: the box being built.
    std::optional<std::pair<BlockPos, BlockPos>> bounds() const;

private:
    std::optional<PathingCommand> tick(bool calcFailed, bool isSafeToCancel, int recursions);
    // Java BuilderCalculationContext.getSchematic: what the build wants there, nullopt if the
    // position isn't part of it (outside, other layer, skipped...).
    std::optional<DesiredBlock> getSchematic(int x, int y, int z, const BlockProps& current) const;
    static bool valid(const BlockProps& current, const std::optional<DesiredBlock>& desired, bool itemVerify);
    bool recalc(CalculationContext& ctx);
    void recalcNearby(CalculationContext& ctx);
    void fullRecalc(CalculationContext& ctx);
    void trim();
    std::optional<std::pair<BlockPos, Rotation>> toBreakNearPlayer(ExecContext& ec);
    // Java searchForPlacables + possibleToPlace: places a block near the player if a face
    // that gives the right orientation can be clicked. Returns true when it acted.
    bool searchForPlacables(ExecContext& ec, bool isSafeToCancel, std::vector<DesiredBlock>& desirableOnHotbar);
    bool tryPlace(const BlockPos& pos, const DesiredBlock& block, ExecContext& ec, bool act);
    GoalPtr assemble(CalculationContext& ctx, bool wholeInventory, bool logMissing);
    GoalPtr placementGoal(const BlockPos& pos, const DesiredBlock& block, CalculationContext& ctx) const;
    GoalPtr breakGoal(const BlockPos& pos, CalculationContext& ctx) const;
    std::optional<DesiredBlock> placeable(const DesiredBlock& wanted, bool wholeInventory);  // with buildSubstitutes
    bool haveItemFor(const std::string& block, bool wholeInventory);
    std::pair<int, int> layerBounds() const;  // schematic y range of the current layers

    std::string name_;
    SchematicPtr schematic_;  // with skip / rotation / mirror applied
    BlockPos origin_;
    int ticks_ = 0;
    bool clickedLeftLastTick_ = false;
    bool paused_ = false;
    int layer_ = 0;
    int numRepeats_ = 0;
    int stopAtHeight_ = 0;
    std::optional<std::unordered_set<BlockPos>> incorrectPositions_;
    std::unordered_set<int64_t> observedCompleted_;  // done, even when out of render distance now
    // Bedrock: standing spots that turned out useless for a target (no face visible from there)
    std::map<BlockPos, std::vector<BlockPos>> badSpots_;
    std::map<BlockPos, int> stuckHere_;
    int recenterTicks_ = 0;
    int failedCalcs_ = 0;  // path calculations failed in a row
};

}  // namespace baritone
