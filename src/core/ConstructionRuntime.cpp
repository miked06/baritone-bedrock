#include "core/Bot.h"
#include "core/Chat.h"

#include <chrono>
#include <fstream>

void Bot::tickConstructionJournal() {
    auto& job = baritone_->construction();
    if (job.isActive() && job.session() != constructionObservedSession_) {
        constructionObservedSession_ = job.session();
        constructionIdentity_ = identity();
    }
    if (constructionJournal_.valid()) {
        if (constructionJournal_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try {
            constructionJournal_.get();
            job.checkpointWritten(constructionSavedSession_, constructionSavedPhase_);
        } catch (const std::exception& error) {
            job.checkpointFailed(constructionSavedSession_);
            chat::error(std::string("Could not save the construction checkpoint: ") + error.what());
        }
    }

    const auto phase = job.phase();
    if (job.session() != constructionObservedSession_ || !job.journalPlan() ||
        (!job.isActive() && phase != 3) ||
        (job.session() == constructionSavedSession_ && phase == constructionSavedPhase_)) return;
    const auto current = identity();
    if (!(constructionIdentity_ == current)) return;

    const auto file = worldDataPath("construction");
    const auto plan = job.journalPlan();
    constructionSavedSession_ = job.session();
    constructionSavedPhase_ = phase;
    try {
        constructionJournal_ = std::async(std::launch::async, [file, current, phase, plan] {
            const auto content = plan->checkpoint(current, phase);
            std::filesystem::create_directories(file.parent_path());
            auto temporary = file;
            temporary += ".checkpoint.tmp";
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(content.data(), content.size());
            out.close();
            if (!out) throw std::runtime_error("Could not write construction checkpoint");
            std::filesystem::rename(temporary, file);
        });
    } catch (const std::exception& error) {
        job.checkpointFailed(constructionSavedSession_);
        chat::error(std::string("Could not start the construction checkpoint writer: ") + error.what());
    }
}
