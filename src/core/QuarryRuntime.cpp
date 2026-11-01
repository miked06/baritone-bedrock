#include "core/Bot.h"
#include "baritone/Settings.h"
#include "core/Chat.h"
void Bot::startQuarry(baritone::Selection area, bool lighting, bool excavate) {
    if (!player_.valid()) throw std::invalid_argument("Enter a world first");
    auto plan=std::make_shared<baritone::QuarryPlan>(area,lighting);
    auto& settings=baritone::Settings::get();
    if (excavate && (!settings.allowBreak || settings.buildInLayers || settings.startAtLayer!=0 || settings.buildRepeat.x || settings.buildRepeat.y || settings.buildRepeat.z || settings.buildIgnoreExisting || !settings.buildIgnoreBlocks.empty()))
        throw std::invalid_argument("Quarry needs allowBreak, startAtLayer 0, buildInLayers false, buildRepeat 0 0 0, buildIgnoreExisting false and empty buildIgnoreBlocks");
    cancelAll(); quarry_.start(plan); quarryActive_=true; quarryExcavate_=excavate; quarryLayerStarted_=false; quarryLayer_=plan->size().y-1;
    chat::info("Checking loaded quarry blocks, ramp supports, protections and boundary liquids before any excavation");
}
void Bot::tickQuarry() {
    if (!quarryActive_ || !baritone_->world() || baritone_->pauser().paused()) return;
    if (quarryChecking_) {
        if (!quarryVerification_.step(*baritone_->world())) return;
        quarryActive_=false; const auto& result=quarryVerification_.result();
        if (result.missing || result.wrong || result.extra || result.unknown)
            chat::error(std::format("Quarry comparison incomplete: {} missing, {} wrong, {} extra, {} unloaded. Inspect before resuming",result.missing,result.wrong,result.extra,result.unknown));
        else chat::info("Quarry layers verified complete; access ramp and selected lighting remain");
        return;
    }
    if (!quarry_.ready()) {
        if (!quarry_.step(*baritone_->world())) return;
        if (!quarry_.failure().empty()) { quarryActive_=false; chat::error(quarry_.failure()); return; }
        chat::info(std::format("Quarry preflight passed: {} blocks to clear; perimeter access ramp preserved",quarry_.blocks()));
        if (!quarryExcavate_) { quarryActive_=false; return; }
    }
    if (baritone_->storage().isActive()) return;
    if (quarryUnloading_) {
        quarryUnloading_=false;
        if (!baritone_->storage().succeeded()) { quarryActive_=false; baritone_->builder().pause(); chat::error("Quarry unloading failed: "+baritone_->storage().failure()); return; }
    }
    if (!quarryStorage_.empty()) {
        int empty=0,biggest=-1,count=0;
        for (int i=0;i<36;++i) { auto s=player_.hotbar(i); if (s.empty()) ++empty; else if (s.isBlock && s.count>count && s.maxDamage==0) { biggest=i; count=s.count; } }
        if (empty==0 && biggest>=0) {
            try { baritone_->storage().start(quarryStorage_,player_.hotbar(biggest).name,count,false); quarryUnloading_=true; }
            catch (const std::exception& e) { quarryActive_=false; baritone_->builder().pause(); chat::error(e.what()); }
            return;
        }
    }
    if (baritone_->builder().isActive()) return;
    if (quarryLayerStarted_) {
        const auto error=baritone_->diagnostics().errorsAfter(quarryDiagnosticStart_);
        if (!error.empty()) { quarryActive_=false; chat::error("Quarry stopped: "+error); return; }
        --quarryLayer_;
        quarryLayerStarted_=false;
    }
    if (quarryLayer_<0) {
        quarryChecking_=true; quarryVerification_.start(quarry_.plan(),quarry_.plan()->origin(),0);
        return;
    }
    auto plan=quarry_.plan(); const int y=quarryLayer_;
    auto layer=std::make_shared<baritone::MaskSchematic>(plan,[y](int,int blockY,int) { return blockY==y; });
    quarryDiagnosticStart_=baritone_->diagnostics().sequence(); quarryLayerStarted_=true;
    baritone_->builder().build("Quarry layer "+std::to_string(y),layer,plan->origin(),false,false);
}
