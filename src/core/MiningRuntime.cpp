#include "core/Bot.h"
#include "core/Chat.h"
#include "baritone/Settings.h"
void Bot::startMiningPlan(baritone::Selection area, std::string method, std::string objective, bool execute) {
    auto plan = std::make_shared<baritone::MiningPlan>(area, std::move(method), baritone::BlockFilter({std::move(objective)}));
    const auto& s = baritone::Settings::get();
    if (execute && (!s.allowBreak || s.buildInLayers || s.startAtLayer || s.buildRepeat != baritone::Vec3i{} || s.buildIgnoreExisting || !s.buildIgnoreBlocks.empty() || !s.buildSkipBlocks.empty() || !s.buildValidSubstitutes.empty() || !s.buildSubstitutes.empty()))
        throw std::invalid_argument("Planned mining requires allowBreak and exact building: no layers, repeats, start layer, ignored existing blocks, skips or substitutions");
    cancelAll(); miningPlan_ = std::move(plan); miningExecute_ = execute; miningActive_ = true;
    chat::info("Checking selected mining cells, roof, floor, liquids and protections before excavation");
}
void Bot::tickMiningPlan() {
    if (!miningActive_ || !baritone_->world() || baritone_->pauser().paused()) return;
    if (miningChecking_) {
        if (!miningVerification_.step(*baritone_->world())) return;
        const auto& r = miningVerification_.result(); miningActive_ = false;
        if (r.missing || r.wrong || r.extra || r.unknown) chat::error("Planned mining comparison incomplete; inspect the selected excavation and loaded chunks");
        else chat::info("Selected mining excavation verified complete");
        return;
    }
    if (!miningPlan_->ready()) {
        if (!miningPlan_->step(*baritone_->world(), player_)) return;
        if (!miningPlan_->failure().empty()) { miningActive_ = false; chat::error(miningPlan_->failure()); return; }
        chat::info(std::format("Mining plan: {} blocks to break, {} known objective blocks, about {:.0f}s base breaking time (walking, effects and collection excluded)", miningPlan_->blocks(), miningPlan_->objectives(), miningPlan_->ticks()/20));
        if (!miningExecute_) { miningActive_ = false; return; }
    }
    if (!miningStarted_) {
        miningStarted_ = true; miningDiagnostic_ = baritone_->diagnostics().sequence();
        baritone_->builder().build("Selected mining plan", miningPlan_, miningPlan_->origin(), false, false); return;
    }
    if (baritone_->builder().isActive()) return;
    auto error = baritone_->diagnostics().errorsAfter(miningDiagnostic_);
    if (!error.empty()) { miningActive_ = false; chat::error("Planned mining stopped: " + error); return; }
    miningChecking_ = true; miningVerification_.start(miningPlan_, miningPlan_->origin(), 0);
}
