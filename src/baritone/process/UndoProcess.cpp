#include "baritone/process/UndoProcess.h"
#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/schematic/Orientation.h"
#include <format>
namespace baritone {
namespace {
class UndoCell : public Schematic {
public:
    UndoCell(StateId before, StateId after) : before_(before), after_(after) {}
    BlockPos size() const override { return {1,1,1}; }
    bool restrictOutside() const override { return true; }
    DesiredBlock desired(int,int,int,const BlockProps& current) const override {
        const auto& after = BlockRegistry::get().props(after_);
        if (current.name != after.name || current.states != after.states) return DesiredBlock::any();
        const auto& before = BlockRegistry::get().props(before_);
        return before.type == BlockType::Air ? DesiredBlock::air() : DesiredBlock{DesiredBlock::Kind::Block, before.name, before.states};
    }
private: StateId before_, after_;
};
}
void UndoProcess::start(std::vector<ActionHistory::Entry> entries) {
    const auto& s = Settings::get();
    if (entries.empty() || entries.size() > 64) throw std::invalid_argument("No observed changes to undo (limit 64)");
    if (s.buildInLayers || s.startAtLayer || s.buildRepeat != Vec3i{} || s.buildIgnoreExisting || !s.buildSkipBlocks.empty() || !s.buildIgnoreBlocks.empty() || s.buildIgnoreDirection || !s.buildValidSubstitutes.empty() || !s.buildSubstitutes.empty())
        throw std::invalid_argument("Conditional undo requires exact builder settings without layers, repeats, skips, ignored properties or substitutions");
    entries_ = std::move(entries); next_ = restored_ = skipped_ = ticks_ = 0; active_ = true; child_ = false;
}
bool UndoProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void UndoProcess::onLostControl() { active_ = false; if (child_) baritone_.builder().onLostControl(); child_ = false; }
std::optional<PathingCommand> UndoProcess::onTick(bool failed, bool safe) {
    if (!active_ || !baritone_.world()) return {};
    auto stop = [&](std::string message, bool error) -> std::optional<PathingCommand> { baritone_.logDirect(message, error); onLostControl(); return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal}; };
    if (failed || ++ticks_ > 20 * 60 * 30) return stop("Conditional undo stopped: path failure or deadline",true);
    if (next_ == entries_.size()) return stop(std::format("Conditional undo finished: {} restored, {} skipped (unsupported, unloaded, protected or changed)",restored_,skipped_),false);
    const auto& entry = entries_[next_]; const auto world = baritone_.world();
    baritone_.player().refreshWorld(entry.position,0);
    auto chunk = world->chunk(entry.position.x>>4,entry.position.z>>4);
    std::optional<StateId> current;
    if (chunk && entry.position.y >= world->minY() && entry.position.y < world->maxY()) current = chunk->get(entry.position.x&15,entry.position.y,entry.position.z&15);
    if (child_ && current == entry.before) {
        baritone_.builder().onLostControl(); child_ = false; baritone_.history().markUndone(entry.id); ++restored_; ++next_;
        return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};
    }
    const bool allowed = current == entry.after && ActionHistory::reversible(entry.before) && ActionHistory::reversible(entry.after) &&
        (BlockRegistry::get().props(entry.before).type == BlockType::Air || BlockRegistry::get().props(entry.after).type == BlockType::Air) &&
        (!world->protection() || world->protection()->permitsModify(entry.position));
    if (!allowed) { if (child_) baritone_.builder().onLostControl(); child_ = false; ++skipped_; ++next_; return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal}; }
    if (child_) {
        if (baritone_.builder().isActive()) return PathingCommand{nullptr,PathingCommandType::Defer};
        return stop("Undo action did not produce the recorded previous state; no automatic retry was attempted",true);
    }
    if (!safe) return PathingCommand{nullptr,PathingCommandType::RequestPause};
    baritone_.builder().build("Conditional undo",std::make_shared<UndoCell>(entry.before,entry.after),entry.position,false,false); child_ = true;
    return PathingCommand{nullptr,PathingCommandType::Defer};
}
}
