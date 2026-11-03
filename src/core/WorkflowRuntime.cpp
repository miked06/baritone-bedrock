#include "core/Bot.h"
#include "core/Chat.h"
void Bot::saveWorkflow() { if (workflowHealthy_ && !workflowFile_.empty()) taskWriter_.submit(workflowFile_, workflow_.encode()); }
void Bot::resumeWorkflow() { if (!workflowHealthy_) throw std::invalid_argument("Repair the workflow file or explicitly clear/load it first"); cancelAll(); workflow_.resume(identity()); saveWorkflow(); }
void Bot::tickWorkflow() {
    if (baritone_->pauser().paused() || baritone_->production().executing() || baritone_->construction().isActive() || baritone_->undo().isActive() || baritone_->repair().isActive() || baritone_->mending().isActive() || miningActive_ || portalJourney_.active()) return;
    // Children continue below; polling a running storage/restock node observes
    // its completion. A newly started independent manual job suspends dispatch.
    const auto& task = workflow_.nodes().at(workflow_.current()).task;
    if ((baritone_->storage().isActive() && task.kind != "withdraw" && task.kind != "deposit" && task.kind != "restock") ||
        (baritone_->logistics().isActive() && task.kind != "restock")) return;
    const auto index = workflow_.current(); const auto status = task.status; const auto visits = workflow_.nodes().at(index).visits;
    workflow_.tick(identity(), [this](const baritone::Task& node) {
        dispatchingTask_ = true;
        try { auto result = startTask(node); dispatchingTask_ = false; return result; }
        catch (...) { dispatchingTask_ = false; throw; }
    }, [this](const baritone::Task& node) { return pollTask(node); });
    if (workflow_.current() != index || workflow_.nodes().at(index).task.status != baritone::TaskStatus::Running)
        baritone_->cancelEverything();
    if (!workflow_.running()) {
        baritone_->cancelEverything(); cancelSchematicLoad();
        if (workflow_.complete()) chat::info("Visual workflow complete");
        else chat::error("Visual workflow stopped: " + workflow_.reason());
    }
    if (index != workflow_.current() || status != workflow_.nodes().at(index).task.status || visits != workflow_.nodes().at(index).visits || !workflow_.running() || ticks_ % 100 == 0) saveWorkflow();
}
