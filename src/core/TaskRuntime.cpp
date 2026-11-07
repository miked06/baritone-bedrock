#include "core/Bot.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include "baritone/Settings.h"
#include "baritone/cache/WorldScanner.h"
#include "core/Chat.h"
#include "platform/WorldTracker.h"

baritone::WorldIdentity Bot::identity() const {
    auto world = platform::WorldTracker::get().snapshot();
    if (worldName_.empty() || !world || !player_.valid()) throw std::invalid_argument("Enter a world first");
    return {worldName_, world->dimension()};
}
void Bot::saveTasks() {
    if (!taskCheckpoint_.empty()) taskWriter_.submit(taskCheckpoint_, tasks_.encode());
}
void Bot::resumeTasks() {
    cancelAll();
    tasks_.resume(identity()); saveTasks();
}
baritone::TaskOutcome Bot::startTask(const baritone::Task& task) {
    using namespace baritone;
    baritone_->cancelEverything();
    taskGoal_.reset(); taskDiagnosticStart_ = baritone_->diagnostics().sequence();
    taskBuildStarted_=false;
    const auto& args = task.arguments;
    if (task.kind == "goto") {
        if (args.size() != 3) throw std::invalid_argument("Goto task requires x y z");
        if (Settings::get().disconnectOnArrival) return {TaskStatus::Blocked, "Disable disconnectOnArrival before running a task workflow"};
        taskGoal_ = std::make_shared<GoalBlock>(BlockPos{command::parseInt(args[0], "x"), command::parseInt(args[1], "y"), command::parseInt(args[2], "z")});
        baritone_->customGoal().setGoalAndPath(taskGoal_);
    } else if(task.kind=="harvest") {
        if(args.size()!=3)throw std::invalid_argument("Harvest node needs crop, absolute item target and radius");
        const auto item=FarmProcess::cropDrop(args[0]);const int target=command::parseInt(args[1],"target"),radius=command::parseInt(args[2],"radius");
        if(target<1 || target>2304 || radius<1 || radius>64)throw std::invalid_argument("Invalid harvest target or radius");
        const int remaining=target-player_.countItem(item);if(remaining<=0)return {TaskStatus::Succeeded,{}};
        baritone_->farm().harvest(args[0],remaining,radius,baritone_->playerFeet());
    } else if(task.kind=="gather") {
        if(args.size()!=3)throw std::invalid_argument("Gather node needs block, output item and absolute inventory target");
        const BlockFilter filter({args[0]});const auto item=BlockFilter::normalize(args[1]);
        const int target=command::parseInt(args[2],"item target");
        if(target<1 || target>2304 || !filter.dropNames().contains(item))throw std::invalid_argument("Invalid gathering target or block/drop combination");
        const int remaining=target-player_.countItem(item);if(remaining<=0)return {TaskStatus::Succeeded,{}};
        if(!filter.names().contains(item))for(int slot=0;slot<36;++slot)if(player_.hotbar(slot).tool==Tool::Pickaxe && player_.hotbar(slot).silkTouch)throw std::invalid_argument("Put Silk Touch pickaxes in storage before gathering ordinary drops");
        int aggregate=remaining;for(const auto& drop:filter.dropNames())aggregate+=player_.countItem(drop);
        baritone_->mine().mine(aggregate,filter);
    } else if (task.kind == "mine") {
        if (args.size() < 2 || command::parseInt(args[0], "quantity") <= 0) throw std::invalid_argument("Mine task requires a positive target inventory quantity and blocks");
        baritone_->mine().mine(command::parseInt(args[0], "quantity"), BlockFilter(std::vector<std::string>(args.begin() + 1, args.end())));
    } else if (task.kind == "produce") {
        if(args.size()<3 || args.size()>4)throw std::invalid_argument("Production node needs station, item, absolute output target and optional supply");
        const auto at=baritone_->storage().locations().find(args[0]);
        if(at==baritone_->storage().locations().end())throw std::invalid_argument("Unknown crafting station");
        const int target=command::parseInt(args[2],"absolute output target");
        if(target<1 || target>2304)throw std::invalid_argument("Output target must be 1..2304");
        const int remaining=target-player_.countItem(BlockFilter::normalize(args[1]));
        if(remaining<=0)return {TaskStatus::Succeeded,{}};
        baritone_->produce().start(recipeBook_,at->second,args[1],remaining,args.size()==4?args[3]:"");
    } else if (task.kind == "loadout") {
        if(args.empty() || args.size()>2)throw std::invalid_argument("Loadout node needs profile and optional source");
        std::map<std::string,int> targets;
        for(const auto& [item,rule]:baritone_->loadouts().profile(args[0]))targets[item]=rule.target;
        baritone_->logistics().start(targets,args.size()==2?args[1]:"",args[0]);
    } else if (task.kind == "placementbuild") {
        if(args.size()!=4)throw std::invalid_argument("Placement build requires file x y z");
        std::ostringstream line; line<<"schematic load"; for(const auto& arg:args)line<<' '<<std::quoted(arg);
        commands_.runCommand(line.str());
        if(!commands_.lastError().empty())return {TaskStatus::Blocked,commands_.lastError()};
    } else if (task.kind == "build") {
        if (args.size() != 4) throw std::invalid_argument("Build task requires file x y z");
        std::ostringstream line; line << "build";
        for (const auto& arg : args) line << ' ' << std::quoted(arg);
        commands_.runCommand(line.str());
        if (!commands_.lastError().empty()) return {TaskStatus::Blocked, commands_.lastError()};
    } else if (task.kind == "verify") {
        if (!args.empty()) throw std::invalid_argument("Verify task takes no arguments");
        startSchematicSurvey(false);
    } else if (task.kind == "withdraw" || task.kind == "deposit") {
        if (args.size() != 4) throw std::invalid_argument("Storage task requires a registered location, item, quantity and checkpointed inventory target");
        int have = player_.countItem(BlockFilter::normalize(args[1]));
        int target = command::parseInt(args[3], "inventory target");
        int remaining = task.kind == "withdraw" ? target - have : have - target;
        if (target < 0 || target > 2304) throw std::invalid_argument("Invalid inventory target");
        if (remaining <= 0) return {TaskStatus::Succeeded, {}};
        baritone_->storage().start(args[0], args[1], remaining, task.kind == "withdraw");
    } else if (task.kind == "wait") {
        if (args.size() != 1 || command::parseInt(args[0], "ticks") < 1) throw std::invalid_argument("Wait task needs a positive tick duration");
    } else if (task.kind == "inventory") {
        if (args.size() != 2 || command::parseInt(args[1], "quantity") < 1) throw std::invalid_argument("Inventory condition needs an item and positive quantity");
    } else if (task.kind == "ifitem") {
        if (args.size() != 2 || command::parseInt(args[1], "quantity") < 0) throw std::invalid_argument("Item condition needs an item and non-negative quantity");
        return {player_.countItem(BlockFilter::normalize(args[0])) >= command::parseInt(args[1], "quantity") ? TaskStatus::Succeeded : TaskStatus::Failed, "Inventory branch evaluated"};
    } else if (task.kind == "restock") {
        if (args.size() < 2 || args.size() > 3) throw std::invalid_argument("Restock node needs item, absolute inventory target and optional source");
        baritone_->logistics().start({{args[0], command::parseInt(args[1], "target")}}, args.size() == 3 ? args[2] : "");
    } else throw std::invalid_argument("Unsupported task kind: " + task.kind);
    return {};
}
baritone::TaskOutcome Bot::pollTask(const baritone::Task& task) {
    using namespace baritone;
    const auto& args = task.arguments;
    if (task.kind == "wait") return {task.elapsedTicks >= static_cast<uint64_t>(command::parseInt(args[0], "ticks")) ? TaskStatus::Succeeded : TaskStatus::Running, {}};
    if (task.kind == "inventory") return {player_.countItem(BlockFilter::normalize(args[0])) >= command::parseInt(args[1], "quantity") ? TaskStatus::Succeeded : TaskStatus::Running, {}};
    if (task.kind == "restock") {
        if (baritone_->logistics().isActive()) return {};
        return {baritone_->logistics().succeeded() ? TaskStatus::Succeeded : TaskStatus::Blocked, baritone_->logistics().failure()};
    }
    if (task.kind == "produce") {
        if(baritone_->produce().isActive())return {};
        return {baritone_->produce().succeeded()?TaskStatus::Succeeded:TaskStatus::Blocked,baritone_->produce().failure()};
    }
    if (task.kind == "loadout") {
        if(baritone_->logistics().isActive())return {};
        return {baritone_->logistics().succeeded()?TaskStatus::Succeeded:TaskStatus::Blocked,baritone_->logistics().failure()};
    }
    const auto error = baritone_->diagnostics().errorsAfter(taskDiagnosticStart_);
    if (task.kind == "goto") {
        if (baritone_->customGoal().isActive()) return {};
        if (taskGoal_ && taskGoal_->isInGoal(baritone_->playerFeet())) return {TaskStatus::Succeeded, {}};
        return {TaskStatus::Failed, error.empty() ? "Navigation stopped before reaching its destination" : error};
    }
    if (task.kind == "mine") {
        BlockFilter filter(std::vector<std::string>(args.begin() + 1, args.end()));
        uint64_t have = 0; for (const auto& name : filter.dropNames()) have += player_.countItem(name);
        if (have >= static_cast<uint64_t>(command::parseInt(args[0], "quantity"))) return {TaskStatus::Succeeded, {}};
        if (!baritone_->mine().isActive()) return {TaskStatus::Blocked, error.empty() ? "Mining stopped before the inventory target was reached" : error};
    }
    if(task.kind=="gather") {
        if(player_.countItem(BlockFilter::normalize(args[1]))>=command::parseInt(args[2],"item target"))return {TaskStatus::Succeeded,{}};
        if(!baritone_->mine().isActive())return {TaskStatus::Blocked,error.empty()?"Gathering stopped before the requested item quantity was observed":error};
    }
    if(task.kind=="harvest") {
        if(player_.countItem(FarmProcess::cropDrop(args[0]))>=command::parseInt(args[1],"target"))return {TaskStatus::Succeeded,{}};
        if(!baritone_->farm().isActive())return {TaskStatus::Blocked,baritone_->farm().failure().empty()?"Harvest stopped before its item target was observed":baritone_->farm().failure()};
    }
    if (task.kind == "build" || task.kind=="placementbuild") {
        if (schematicLoading()) return {};
        if (!lastLoadFailure_.empty()) return {TaskStatus::Blocked, lastLoadFailure_};
        if(task.kind=="placementbuild") {
            if(!schematics_.loaded())return {TaskStatus::Blocked,"The placement is no longer loaded"};
            if(!taskBuildStarted_) {
                taskPlacementRevision_=schematics_.revision();
                baritone_->builder().build(schematics_.name(),schematics_.schematic(),schematics_.origin(),false,false);
                taskBuildStarted_=true; return {};
            }
            if(taskPlacementRevision_!=schematics_.revision())return {TaskStatus::Blocked,"Placement changed during workflow construction"};
        }
        if (baritone_->builder().isPaused()) return {TaskStatus::Blocked, error.empty() ? "Builder paused; check materials, reachability and permissions" : error};
        if (!baritone_->builder().isActive()) {
            if (!error.empty()) return {TaskStatus::Blocked, error};
            return {TaskStatus::Succeeded, {}};
        }
    }
    if (task.kind == "verify") {
        if (!surveyReport_ && !survey_.ready()) return {TaskStatus::Failed, "Placement changed while verifying"};
        if (survey_.ready()) {
            const auto& result=survey_.result();
            if(result.unknown)return {TaskStatus::Blocked,"Load all placement chunks before continuing this workflow"};
            if(result.missing || result.wrong || result.extra)return {TaskStatus::Blocked,"Placement verification found missing, wrong or extra blocks"};
            return {TaskStatus::Succeeded,{}};
        }
    }
    if (task.kind == "withdraw" || task.kind == "deposit") {
        if (baritone_->storage().isActive()) return {};
        return {baritone_->storage().succeeded() ? TaskStatus::Succeeded : TaskStatus::Blocked, baritone_->storage().failure()};
    }
    return {};
}
void Bot::prepareTasks() {
    using namespace baritone;
    const auto flowFile = worldDataPath("workflows");
    if (workflowFile_ != flowFile) {
        workflow_.pause(); saveWorkflow(); workflowFile_ = flowFile; workflow_ = {}; workflow_.bind(identity()); workflowHealthy_ = true;
        try {
            if (std::filesystem::exists(flowFile)) {
                if (std::filesystem::file_size(flowFile) > 2 * 1024 * 1024) throw std::invalid_argument("Workflow checkpoint is too large");
                std::ifstream input(flowFile); if (!input) throw std::runtime_error("Cannot read workflow checkpoint");
                auto parsed = Workflow::decode(std::string(std::istreambuf_iterator<char>(input), {})); parsed.bind(identity()); workflow_ = std::move(parsed);
            }
        } catch (const std::exception& e) { workflowHealthy_ = false; chat::error(std::string("Workflow recovery failed; existing file preserved until explicitly cleared or loaded: ") + e.what()); }
    }
    const auto placementFile = worldDataPath("placements");
    if (placementsFile_ != placementFile) {
        placementsFile_ = placementFile;
        try { placements_.load(placementFile); }
        catch (const std::exception& error) { chat::error(std::string("Could not load placement presets: ") + error.what()); }
    }
    const auto portalFile = worldDataPath("portals", false);
    if (portalsFile_ != portalFile) {
        portalsFile_ = portalFile;
        try { portals_.load(portalFile); }
        catch (const std::exception& error) { chat::error(std::string("Could not load portal graph: ") + error.what()); }
    }
    const auto file = worldDataPath("tasks");
    if (taskCheckpoint_ != file) {
        tasks_.pause(); saveTasks(); taskCheckpoint_ = file;
        TaskQueue restored; restored.bind(identity());
        try {
            if (std::filesystem::exists(file)) {
                if (std::filesystem::file_size(file) > 2 * 1024 * 1024) throw std::invalid_argument("Task checkpoint exceeds two MiB");
                std::ifstream input(file, std::ios::binary);
                if (!input) throw std::runtime_error("Cannot read task checkpoint");
                restored = TaskQueue::decode(std::string(std::istreambuf_iterator<char>(input), {}));
                if (!(restored.identity() == identity())) throw std::invalid_argument("Checkpoint world/dimension mismatch");
                if (!restored.complete()) chat::info("Recovered task queue, paused. Inspect #task list, then use #task resume");
            }
        } catch (const std::exception& error) {
            chat::error(std::string("Could not recover tasks: ") + error.what());
            restored = {}; restored.bind(identity());
        }
        tasks_ = std::move(restored);
        try { baritone_->storage().load(worldDataPath("storage")); }
        catch (const std::exception& error) { chat::error(std::string("Could not load storage locations: ") + error.what()); }
    }
}
void Bot::tickTasks() {
    using namespace baritone;
    prepareTasks();
    if (workflow_.running()) { tickWorkflow(); return; }
    if (!tasks_.running() || baritone_->pauser().paused()) return;
    if(baritone_->workshop().isActive())return;
    const auto& activeKind=tasks_.tasks().at(tasks_.currentIndex()).kind;
    if ((baritone_->produce().isActive() && activeKind!="produce") || (baritone_->production().executing() && activeKind!="produce") || (baritone_->logistics().isActive() && activeKind!="produce" && activeKind!="loadout" && activeKind!="restock") || baritone_->construction().isActive() || baritone_->undo().isActive() || baritone_->repair().isActive() || baritone_->mending().isActive() || miningActive_ || portalJourney_.active()) return;
    if ((baritone_->craft().isActive() && activeKind!="produce") || baritone_->smelt().isActive()) return;
    if (baritone_->storage().isActive()) {
        const auto& kind = tasks_.tasks().at(tasks_.currentIndex()).kind;
        if (kind != "withdraw" && kind != "deposit" && kind!="produce" && kind!="loadout" && kind!="restock") return;
    }
    const auto index = tasks_.currentIndex();
    const auto status = tasks_.tasks().at(index).status;
    tasks_.tick(identity(), [this](const Task& task) {
        dispatchingTask_ = true;
        try { auto result = startTask(task); dispatchingTask_ = false; return result; }
        catch (...) { dispatchingTask_ = false; throw; }
    }, [this](const Task& task) { return pollTask(task); });
    if (!tasks_.running()) {
        baritone_->cancelEverything();
        if (tasks_.complete()) chat::info("Task workflow complete");
        else {
            cancelSchematicLoad();
            chat::error("Task workflow stopped: " + tasks_.tasks().at(tasks_.currentIndex()).reason + ". Use #task retry after resolving the issue");
        }
    }
    if (tasks_.currentIndex() != index || !tasks_.running() || tasks_.tasks().at(index).status != status || ticks_ % 100 == 0) saveTasks();
}
