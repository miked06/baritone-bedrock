#include "baritone/process/ConstructionProcess.h"
#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include <format>
namespace baritone {
void ConstructionProcess::start(SchematicPtr source, BlockPos origin, std::string support, int depth, bool permanent, bool execute) {
    const auto& s = Settings::get();
    if (active_) throw std::invalid_argument("A construction plan is already active");
    if (execute && (s.buildInLayers || s.startAtLayer || s.buildRepeat != Vec3i{} || s.buildIgnoreExisting || !s.buildIgnoreBlocks.empty() || s.buildIgnoreDirection || !s.buildSkipBlocks.empty() || !s.buildValidSubstitutes.empty() || !s.buildSubstitutes.empty()))
        throw std::invalid_argument("Planned construction requires exact building: no layers, repeat, start layer, ignored existing blocks, skipped blocks, ignored orientation or substitutions");
    plan_.start(std::move(source), origin, std::move(support), depth, permanent); execute_ = execute; active_ = true; child_ = false; phase_ = ticks_ = 0;
}
bool ConstructionProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void ConstructionProcess::onLostControl() { active_ = false; if (child_) baritone_.builder().onLostControl(); child_ = false; }
std::string ConstructionProcess::displayName() const { return plan_.ready() ? std::format("Construction phase {}/3", phase_ + 1) : "Planning construction supports and dependencies"; }
std::optional<PathingCommand> ConstructionProcess::onTick(bool failed, bool safe) {
    if (!active_ || !baritone_.world()) return {};
    auto stop = [&](std::string message, bool error) -> std::optional<PathingCommand> { baritone_.logDirect(message, error); onLostControl(); return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal}; };
    if (failed || ++ticks_ > 20 * 60 * 60) return stop("Construction stopped: path failure or one-hour deadline; inspect any temporary supports left in place", true);
    if (!plan_.ready()) {
        if (!plan_.step(*baritone_.world())) return PathingCommand{nullptr, PathingCommandType::RequestPause};
        if (!plan_.failure().empty()) return stop(plan_.failure(), true);
        baritone_.logDirect(std::format("Construction plan: {} support blocks, {} support dependencies; {}", plan_.supports().size(), plan_.dependencies().size(), plan_.permanent() ? "foundations remain" : "temporary columns will be removed after building"));
        if (!execute_) return stop("Construction preview complete; no blocks changed", false);
    }
    if (child_) {
        if (baritone_.builder().isActive()) return PathingCommand{nullptr, PathingCommandType::Defer};
        const auto error = baritone_.diagnostics().errorsAfter(diagnostic_);
        if (!error.empty()) return stop("Construction phase did not complete: " + error, true);
        child_ = false; ++phase_;
    }
    if (phase_ == 3) return stop("Planned construction phases complete; run #schematic verify to compare the final structure", false);
    if (!safe) return PathingCommand{nullptr, PathingCommandType::RequestPause};
    diagnostic_ = baritone_.diagnostics().sequence();
    baritone_.builder().build("Planned construction phase " + std::to_string(phase_ + 1), plan_.phase(phase_), plan_.phaseOrigin(), false, false); child_ = true;
    return PathingCommand{nullptr, PathingCommandType::Defer};
}
}
