#include "core/Bot.h"
#include "core/Chat.h"
#include <chrono>
#include <fstream>

void Bot::tickConstructionJournal() {
    auto& job=baritone_->construction();
    if(job.isActive() && job.session()!=constructionObservedSession_) {
        constructionObservedSession_=job.session();constructionIdentity_=identity();constructionJournalFailed_=false;
    }
    if(constructionJournal_.valid()) {
        if(constructionJournal_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        try{constructionJournal_.get();}catch(const std::exception& error){constructionJournalFailed_=true;chat::error(std::string("Construction checkpoint failed; this session will not retry automatically: ")+error.what());return;}
    }
    if(constructionJournalFailed_ || !(constructionIdentity_==identity()) || job.session()!=constructionObservedSession_ || !job.journalPlan() || (!job.isActive() && job.phase()!=3) || (job.session()==constructionSavedSession_ && job.phase()==constructionSavedPhase_))return;
    const auto current=identity();const auto file=worldDataPath("construction");const auto phase=job.phase();const auto plan=job.journalPlan();
    constructionSavedSession_=job.session();constructionSavedPhase_=phase;
    constructionJournal_=std::async(std::launch::async,[file,current,phase,plan] {
        const auto text=plan->checkpoint(current,phase);
        std::filesystem::create_directories(file.parent_path());auto temporary=file;temporary+=".checkpoint.tmp";
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(text.data(),text.size());out.close();
        if(!out)throw std::runtime_error("Could not write construction checkpoint");
        std::filesystem::rename(temporary,file);
    });
}
