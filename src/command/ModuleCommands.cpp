#include "command/ModuleCommands.h"
#include <filesystem>
#include <format>
#include <cmath>
#include <fstream>
#include <sstream>
#include "baritone/schematic/PreviewExport.h"
#include "baritone/task/WorkflowTemplates.h"
#include "baritone/task/DiagnosticBundle.h"
#include "baritone/schematic/RoadPlan.h"
#include "PreviewViewerAssets.h"
#include "baritone/schematic/SchematicExport.h"
#include "core/Bot.h"
#include "sdk/Recipes.h"
#include "sdk/Containers.h"
#include "core/Chat.h"
#include "core/Paths.h"
#include "core/GameVersion.h"
#include "memory/Signatures.h"
#include "memory/Memory.h"

namespace command {
void registerModuleCommands(CommandManager& manager, Bot& bot) {
    manager.add({{"harvest"}, "harvest sugar_cane|bamboo|cactus|pumpkin|melon additional [radius]", "Harvest one crop to an observed inventory target while preserving base stems", [&bot](const Args& args) {
        if(args.size()<2 || args.size()>3 || !bot.player().valid())throw CommandError("Usage: #harvest sugar_cane|bamboo|cactus|pumpkin|melon additional [radius 1..64]");
        (void)baritone::FarmProcess::cropDrop(args[0]);const int count=parseInt(args[1],"additional quantity"),radius=args.size()==3?parseInt(args[2],"radius"):16;
        if(count<1 || count>2304 || radius<1 || radius>64)throw CommandError("Additional quantity 1..2304 and radius 1..64 are required");
        bot.cancelAll();bot.baritone().farm().harvest(args[0],count,radius,bot.baritone().playerFeet());
    }});
    manager.add({{"workshop"}, "workshop start input output count fuel fuelCount station...; status", "Distribute a one-to-one smelting batch across empty registered furnaces and collect confirmed output", [&bot](const Args& args) {
        auto& job=bot.baritone().workshop();
        if(args.size()==1 && args[0]=="status"){chat::info(job.isActive()?job.displayName():job.succeeded()?"Furnace pool complete":"Furnace pool idle");if(!job.failure().empty())chat::error(job.failure());return;}
        if(args.size()<7 || args.size()>22 || args[0]!="start")throw CommandError("Usage: #workshop start input output count fuel fuelCount registeredStation... (1..16 stations)");
        bot.prepareTasks();std::vector<baritone::BlockPos> positions;
        for(std::size_t i=6;i<args.size();++i){const auto at=bot.baritone().storage().locations().find(args[i]);if(at==bot.baritone().storage().locations().end())throw CommandError("Unknown registered station: "+args[i]);positions.push_back(at->second);}
        const int count=parseInt(args[3],"input/output quantity"),fuel=parseInt(args[5],"total fuel budget");
        bot.cancelAll();job.start(std::move(positions),args[1],args[2],count,args[4],fuel);
        chat::info("Furnace pool started. Input and the explicit total fuel budget are split across stations; Minecraft decides recipe/fuel validity. Inspect workstations after cancellation or failure");
    }});
    manager.add({{"mapgoto"}, "mapgoto world dimension x z", "Navigate from a map selection after checking the world and dimension", [&bot](const Args& args) {
        if(args.size()!=4 || !(bot.identity()==baritone::WorldIdentity{args[0],parseInt(args[1],"dimension")}))throw CommandError("The map selection belongs to another world/dimension; refresh the map");
        const int x=parseInt(args[2],"x"),z=parseInt(args[3],"z");
        if(x<-30000000 || x>30000000 || z<-30000000 || z>30000000)throw CommandError("Map destination is outside supported world bounds");
        bot.cancelAll();bot.baritone().customGoal().setGoalAndPath(std::make_shared<baritone::GoalXZ>(x,z));
    }});
    manager.add({{"road"}, "road x1 y1 z1 x2 y2 z2 width stone|cobblestone|dirt [lightInterval]", "Preview a bounded road or bridge before foundation planning and construction", [&bot](const Args& args) {
        if(args.size()<8 || args.size()>9 || !bot.player().valid() || bot.schematicLoading())throw CommandError("Usage: #road x1 y1 z1 x2 y2 z2 oddWidth material [lightInterval 0|4..64]; coordinates describe paving blocks");
        const auto feet=bot.baritone().playerFeet();
        const baritone::BlockPos from{parseCoord(args[0],feet.x,"x1"),parseCoord(args[1],feet.y-1,"y1"),parseCoord(args[2],feet.z,"z1")};
        const baritone::BlockPos to{parseCoord(args[3],feet.x,"x2"),parseCoord(args[4],feet.y-1,"y2"),parseCoord(args[5],feet.z,"z2")};
        auto plan=baritone::planRoad(from,to,parseInt(args[6],"width"),args[7],args.size()==9?parseInt(args[8],"light interval"):0);
        const auto size=plan.schematic->size();const auto world=bot.baritone().world();
        if(!world || plan.origin.y<world->minY() || int64_t(plan.origin.y)+size.y>world->maxY())throw CommandError("Road preview extends outside this dimension's build height");
        bot.cancelAll();bot.schematics().load("Road preview",plan.schematic,plan.origin);
        chat::info(std::format("Road preview: {} paving blocks, {} lights, origin {}. Inspect #schematic requirements and #construction plan permanent cobblestone 16 before #construction start permanent cobblestone 16",plan.paving,plan.lights,plan.origin));
        chat::info("Route uses X then Z, with at most one block of height change per four horizontal blocks. Headroom is part of the preview; supports require loaded dry ground within the planning depth");
    }});
    manager.add({{"assist"}, "assist on|off|status", "Guide manual schematic placement and select matching hotbar materials", [&bot](const Args& args) {
        if(args.size()!=1)throw CommandError("Usage: #assist on|off|status after loading a schematic");
        if(args[0]=="off"){bot.setBuildAssistance(false);return;}
        if(args[0]=="on") {
            if(!bot.player().valid() || !bot.schematics().loaded() || bot.schematicLoading())throw CommandError("Enter a world and load a schematic first");
            bot.cancelAll();bot.setBuildAssistance(true);chat::info("Manual building assistance enabled: gold highlight, matching hotbar selection, no automatic movement or clicks. #assist status shows orientation hints");return;
        }
        if(args[0]=="status") {
            chat::info(bot.buildAssistanceEnabled()?"Manual building assistance enabled":"Manual building assistance disabled");
            if(const auto& hint=bot.buildHint())chat::info(std::format("{} at {}: {}",hint->item,hint->position,hint->guidance));
            else chat::info("No supported missing block nearby; keep materials in the hotbar, prepare supports or inspect #technical");return;
        }
        throw CommandError("Usage: #assist on|off|status");
    }});
    manager.add({{"technical"}, "technical", "Audit schematic components and states requiring manual construction checks", [&bot](const Args& args) {
        if(!args.empty() || !bot.schematics().loaded() || bot.schematicLoading())throw CommandError("Usage: #technical after loading a schematic");
        const auto source=bot.schematics().schematic();const auto origin=bot.schematics().origin();
        bot.runDiagnosticAsync([source,origin] {
            const auto result=baritone::auditTechnicalPlacement(*source,origin);
            chat::info(std::format("Technical audit: {} cells, {} components, {} state checks, {}{} manual-check entries",result.cells,result.technical,result.stateChecks,result.issues.size(),result.truncated?"+":""));
            for(std::size_t i=0;i<std::min<std::size_t>(32,result.issues.size());++i){const auto& issue=result.issues[i];chat::info(std::format("{} at {}: {}",issue.block,issue.position,issue.reason));}
            chat::info("This is a bounded static placement audit. It does not simulate circuits or certify redstone timing, connections, contents or arbitrary block states");
        });
    }});
    manager.add({{"report"}, "report new-output.zip", "Export a local diagnostic archive; inspect it before sharing", [&bot](const Args& args) {
        if(args.size()!=1)throw CommandError("Usage: #report new-output.zip");
        const auto file=std::filesystem::u8path(args[0]);
        if(file.extension()!=".zip" || std::filesystem::exists(file))throw CommandError("Choose a new .zip file; existing files are preserved");
        std::map<std::string,std::string> entries;
        entries["README.txt"]="Baritone Bedrock local diagnostic report. Nothing was uploaded.\nInspect before sharing: logs/settings may include coordinates, world names, commands and local file paths.\nNo world saves, credentials or chunk caches were collected. Log tail is bounded; ZIP entries use fixed timestamps.\n";
        std::ostringstream status;
        status<<"version="<<BARITONE_BEDROCK_VERSION<<"\nsupported_game="<<gameversion::supported<<"\ntick="<<bot.ticks()<<"\nworld="<<bot.worldName()<<"\n";
        if(bot.player().valid())status<<std::format("position={}\ndimension={}\n",bot.baritone().playerFeet(),bot.identity().dimension);
        for(const auto* signature:sigs::all())status<<"signature."<<signature->name<<"="<<(signature->address?"resolved":"missing")<<"\n";
        entries["status.txt"]=status.str();
        std::ostringstream events;
        for(const auto& event:bot.baritone().diagnostics().events())events<<event.sequence<<" "<<(event.error?"ERROR":"INFO")<<" "<<event.code<<": "<<event.message<<"\n";
        entries["events.txt"]=events.str();
        entries["tasks.txt"]=bot.tasks().encode();entries["workflow.txt"]=bot.workflow().encode();
        const auto data=paths::dataDir();
        bot.runDiagnosticAsync([file,data,entries=std::move(entries)]() mutable {
            for(const auto* name:{"settings.txt","loadouts.txt","recipes.txt"})entries[name]=baritone::diagnosticFileTail(data/name,512*1024);
            entries["log-tail.txt"]=baritone::diagnosticFileTail(data/"log.txt",2*1024*1024);
            baritone::writeDiagnosticBundle(file,entries);
            chat::info("Diagnostic archive exported locally. Inspect logs, coordinates and file paths before sharing; nothing was uploaded");
        });
        chat::info("Exporting diagnostic archive in the background");
    }});
    manager.add({{"produce"}, "produce station item additional [supply|-] [delivery|-]; status", "Gather supported missing resources, craft confirmed output and optionally deliver it", [&bot](const Args& args) {
        auto& job=bot.baritone().produce();
        if (args.size()==1 && args[0]=="status") { chat::info(job.isActive()?job.displayName():job.succeeded()?"Production objective complete":"Production objective idle"); if(!job.failure().empty())chat::error(job.failure()); return; }
        if(args.size()<3 || args.size()>5)throw CommandError("Usage: #produce registeredTable item additional [supply|-] [delivery|-]; status");
        bot.prepareTasks(); const auto at=bot.baritone().storage().locations().find(args[0]);
        if(at==bot.baritone().storage().locations().end())throw CommandError("Register the crafting table with #storage add first");
        const auto station=at->second; const int amount=parseInt(args[2],"additional output");
        if(amount<1 || amount>2304)throw CommandError("Additional output must be 1..2304");
        const std::string source=args.size()>3 && args[3]!="-"?args[3]:"", delivery=args.size()>4 && args[4]!="-"?args[4]:"";
        for(const auto& location:{source,delivery})if(!location.empty()&&!bot.baritone().storage().locations().contains(location))throw CommandError("Unknown registered container: "+location);
        bot.cancelAll(); job.start(bot.recipeBook(),station,args[1],amount,source,delivery);
    }});
    manager.add({{"repair"}, "repair <preview|start> <anvil|grindstone> station toolSlot materialSlot materialCount maxLevels; mend xpSource toolSlot targetPercent timeoutSeconds; open station; close; status", "Repair through confirmed workstation actions or observed Mending progress at a real XP source", [&bot](const Args& args) {
        if (args.size()==5 && args[0]=="mend") {
            bot.prepareTasks();
            const auto sources=bot.waypoints().matching(args[1]);
            if(sources.size()!=1)throw CommandError("Use one uniquely named XP waypoint; stand at the source and run #wp save user xp_farm");
            if(sources.front().dimension!=bot.identity().dimension)throw CommandError("The XP waypoint is in another dimension");
            const auto position=sources.front().pos;
            const int slot=parseInt(args[2],"tool slot"), percent=parseInt(args[3],"durability target percent"), timeout=parseInt(args[4],"timeout seconds");
            if(slot<0 || slot>=36)throw CommandError("Inventory slot must be 0..35");
            baritone::validateMendingItem(bot.player().hotbar(slot),percent,timeout);
            bot.cancelAll(); bot.baritone().mending().start(position,slot,percent,timeout); return;
        }
        if (args.size()==7 && (args[0]=="preview" || args[0]=="start")) {
            bot.prepareTasks(); const auto found=bot.baritone().storage().locations().find(args[2]);
            if (found==bot.baritone().storage().locations().end()) throw CommandError("Register the repair station with #storage add first");
            const auto position=found->second;
            const int tool=parseInt(args[3],"tool slot"), material=parseInt(args[4],"material slot"), count=parseInt(args[5],"material count"), levels=parseInt(args[6],"maximum XP-level cost");
            if (tool<0 || tool>=36 || material<0 || material>=36 || tool==material) throw CommandError("Choose two distinct inventory slots 0..35");
            baritone::validateRepairInputs(args[1],bot.player().hotbar(tool),bot.player().hotbar(material),count,levels);
            bot.cancelAll(); bot.baritone().repair().start(args[1],position,tool,material,count,levels,args[0]=="start"); return;
        }
        if (args.size()==2 && args[0]=="open") {
            bot.prepareTasks();
            const auto found=bot.baritone().storage().locations().find(args[1]);
            if (found==bot.baritone().storage().locations().end()) throw CommandError("Register the repair station with #storage add first");
            const auto position=found->second;
            bot.cancelAll(); bot.runInTick([&bot,position] {
                if (!bot.player().openContainerAt(position)) chat::error("Repair station could not be opened; move within reach and close other interfaces");
                else chat::info("Repair station interaction submitted; #repair status inspects the open models");
            }); return;
        }
        if (args.size()==1 && args[0]=="close") { bot.baritone().repair().onLostControl(); bot.baritone().mending().onLostControl(); bot.runInTick([&bot] { bot.player().closeOwnedContainer(); }); return; }
        if (args.size()==1 && args[0]=="status") {
            auto& process=bot.baritone().repair();
            chat::info(process.isActive()?process.displayName():process.succeeded()?"Repair completed":"Repair idle");
            if (!process.failure().empty()) chat::error(process.failure());
            auto& mending=bot.baritone().mending();
            chat::info(mending.isActive()?mending.displayName():mending.succeeded()?"Mending completed":"Mending idle");
            if(!mending.failure().empty())chat::error(mending.failure());
            if (!bot.player().containerOpen()) return;
            auto preview=bot.player().repairPreview();
            if (preview.supported) chat::info(std::format("{} preview: {} damage {}/{}, cost {} levels, {} material item(s)",preview.method,preview.output.name,preview.output.damage,preview.output.maxDamage,preview.levels,preview.materialCost));
            for (const auto& view:sdk::openContainerViews(*sdk::ClientInstance::get()->localPlayer()))
                chat::info(std::format("{}: network {}, {} slots",view.name,view.networkName,view.size));
            return;
        }
        throw CommandError("Usage: #repair preview|start anvil|grindstone registeredStation toolSlot materialSlot materialCount maxLevels; mend xpSource toolSlot targetPercent timeoutSeconds; open station; close; status");
    }});
    manager.add({{"merge"}, "merge sourceSlot destinationSlot count", "Move a quantity between compatible inventory piles through a normal server request", [&bot](const Args& args) {
        if (args.size()!=3) throw CommandError("Usage: #merge sourceSlot destinationSlot count (slots 0..35)");
        const int from=parseInt(args[0],"source slot"),to=parseInt(args[1],"destination slot"),count=parseInt(args[2],"quantity");
        if (from<0 || from>=36 || to<0 || to>=36 || from==to || count<1 || count>64) throw CommandError("Use two distinct slots 0..35 and count 1..64");
        if ((bot.baritone().workshop().isActive() || bot.baritone().produce().isActive() || bot.baritone().production().executing()) || bot.baritone().logistics().isActive() || bot.baritone().loadout().isActive() || bot.baritone().storage().isActive() || bot.baritone().repair().isActive() || bot.baritone().mending().isActive()) throw CommandError("Finish current production/logistics/inventory/repair jobs first");
        bot.runInTick([&bot,from,to,count] {
            if (!bot.player().valid() || !bot.player().mergeInventorySlots(from,to,count)) chat::error("Inventory transfer unavailable: check matching plain items, capacity, quantity and pending actions");
            else chat::info("Inventory quantity transfer submitted; verify #inv all after the server response");
        });
    }});
    manager.add({{"preview3d"}, "preview3d output.html", "Export the transformed placement to a local interactive textured viewer", [&bot](const Args& args) {
        if (args.size() != 1 || !bot.schematics().loaded() || bot.schematicLoading()) throw CommandError("Usage: #preview3d output.html after loading a schematic");
        auto file = std::filesystem::u8path(args[0]);
        if (file.extension() != ".html" || std::filesystem::exists(file)) throw CommandError("Choose a new .html output file; existing files are preserved");
        auto source = bot.schematics().schematic(); const auto origin = bot.schematics().origin(); const auto name = bot.schematics().name();
        bot.runDiagnosticAsync([source,origin,name,file] {
            std::ostringstream json; baritone::writePreviewJson(json,*source,origin,name);
            std::string html(previewViewerHtml); const auto marker = html.find("/*BARITONE_SCHEMATIC*/null");
            if (marker == html.npos) throw std::runtime_error("Missing preview viewer data marker");
            html.replace(marker,std::string_view("/*BARITONE_SCHEMATIC*/null").size(),json.str());
            auto temporary = file; temporary += ".baritone-preview.tmp";
            if (std::filesystem::exists(temporary)) throw std::runtime_error("A preview temporary file already exists; choose another output name");
            if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
            std::ofstream output(temporary,std::ios::binary); output << html; output.close();
            if (!output) { std::filesystem::remove(temporary); throw std::runtime_error("Could not write the preview file"); }
            try { std::filesystem::rename(temporary,file); } catch (...) { std::filesystem::remove(temporary); throw; }
            chat::info("Textured viewer exported. Open the HTML file and select an unpacked Bedrock resource pack; no files are uploaded");
        });
        chat::info("Exporting the transformed preview in the background");
    }});
    manager.add({{"history"}, "history <list|undo [count]|clear>", "Inspect observed bot block changes and conditionally restore supported placements/removals", [&bot](const Args& args) {
        auto& history = bot.baritone().history();
        if (args.empty() || (args.size() == 1 && args[0] == "list")) {
            const auto& entries = history.entries(); const auto first = entries.size() > 16 ? entries.size() - 16 : 0;
            for (std::size_t i = first; i < entries.size(); ++i) { const auto& e = entries[i]; chat::info(std::format("{} at {}: {} -> {}{}", e.id,e.position,baritone::BlockRegistry::get().props(e.before).name,baritone::BlockRegistry::get().props(e.after).name,e.undone ? " (restored)" : "")); }
            chat::info("Session history: observations following bot actions; unrelated changes can coincide. Undo supports plain block placements/removals and skips changed, unloaded or protected cells"); return;
        }
        if (args.size() == 1 && args[0] == "clear") { bot.cancelAll(); history.clear(); return; }
        if ((args.size() == 1 || args.size() == 2) && args[0] == "undo") {
            if (!bot.player().valid()) throw CommandError("Enter a world first");
            const int count = args.size() == 2 ? parseInt(args[1], "recent change count") : 1;
            if (count < 1 || count > 64) throw CommandError("Undo count must be 1..64");
            auto entries = history.recent(unsigned(count)); bot.cancelAll(); bot.baritone().undo().start(std::move(entries)); return;
        }
        throw CommandError("Usage: #history list; undo [count 1..64]; clear");
    }});
    manager.add({{"mining"}, "mining <plan|start> <vein|tunnel|branch> [objectiveBlock]", "Survey and excavate a bounded selected mining layout", [&bot](const Args& args) {
        if (args.size() < 2 || args.size() > 3 || (args[0] != "plan" && args[0] != "start")) throw CommandError("Usage: #mining plan|start vein|tunnel|branch [objectiveBlock]; select an area with #sel first");
        if (!bot.player().valid() || bot.baritone().selections().all().empty()) throw CommandError("Enter a world and select the mining volume first");
        bot.startMiningPlan(bot.baritone().selections().all().back(), args[1], args.size() == 3 ? args[2] : "diamond_ore", args[0] == "start");
    }});
    manager.add({{"maintenance"}, "maintenance <on [remainingPercent]|off|check>", "Replace worn tools through confirmed swaps; pause if a suitable spare is unavailable", [&bot](const Args& args) {
        auto& maintenance = bot.baritone().maintenance();
        if (!bot.player().valid()) throw CommandError("Enter a world first");
        if (args.size() >= 1 && args.size() <= 2 && args[0] == "on") { maintenance.enable(args.size() == 2 ? parseInt(args[1], "remaining durability percent") : 10); chat::info("Automatic tool protection enabled; it preserves worn tools and requires a suitable spare. Stop/world changes disable it"); return; }
        if (args.size() == 1 && args[0] == "off") { maintenance.onLostControl(); return; }
        if (args.size() == 1 && args[0] == "check") {
            for (int slot = 0; slot < 36; ++slot) { const auto s = bot.player().hotbar(slot); if (s.maxDamage > 0) chat::info(std::format("Slot {}: {} {}/{} durability, {}", slot, s.name, s.maxDamage - s.damage, s.maxDamage, maintenance.permits(s) ? "usable" : "replacement needed")); }
            chat::info("Maintenance replaces tools. #repair previews or runs explicit anvil/unenchanted-grindstone jobs; Mending requires an XP source"); return;
        }
        throw CommandError("Usage: #maintenance on [remainingPercent 1..90]; off; check");
    }});
    manager.add({{"construction"}, "construction <plan|start> [temporary|permanent] [supportBlock] [depth]", "Plan foundations and support dependencies before a three-phase build", [&bot](const Args& args) {
        if(args.size()==1 && args[0]=="recover") {
            bot.prepareTasks();const auto file=bot.worldDataPath("construction");
            if(std::filesystem::file_size(file)>8*1024*1024)throw CommandError("Construction checkpoint exceeds eight MiB");
            std::ifstream input(file,std::ios::binary);if(!input)throw CommandError("Could not read the construction checkpoint");
            auto recovery=baritone::ConstructionPlan::recover(std::string(std::istreambuf_iterator<char>(input),{}));
            if(!(recovery.identity==bot.identity()))throw CommandError("Construction checkpoint belongs to another world/dimension");
            if(recovery.phase>=3)throw CommandError("The saved construction phases are already complete");
            bot.cancelAll();bot.baritone().construction().resume(std::move(recovery));chat::info("Construction recovery requested. Loaded chunks, protection and obstacles are checked before rebuilding the saved phase; cleanup only removes matching support blocks");return;
        }
        if (args.empty() || args.size() > 4 || (args[0] != "plan" && args[0] != "start")) throw CommandError("Usage: #construction plan|start [temporary|permanent] [cobblestone|stone|dirt] [depth 1..16]");
        if (!bot.player().valid() || !bot.schematics().loaded() || bot.schematicLoading()) throw CommandError("Enter a world and load a schematic first");
        if (args.size() > 1 && args[1] != "temporary" && args[1] != "permanent") throw CommandError("Foundation policy must be temporary or permanent");
        const auto source = bot.schematics().schematic(); const auto origin = bot.schematics().origin();
        const auto material = baritone::BlockFilter::normalize(args.size() > 2 ? args[2] : "cobblestone");
        const int depth = args.size() > 3 ? parseInt(args[3], "support depth") : 16;
        if (args[0] == "start") bot.cancelAll();
        bot.baritone().construction().start(source, origin, material, depth, args.size() > 1 && args[1] == "permanent", args[0] == "start");
    }});
    manager.add({{"workflow"}, "workflow <node|list|run|pause|restart|clear>", "Edit a bounded graph with inventory branches, loops and failure paths", [&bot](const Args& args) {
        bot.prepareTasks(); auto& flow = bot.workflow();
        if(args.size()==1 && args[0]=="templates") {
            for(const auto& entry:baritone::workflowTemplates())chat::info(entry.name+" "+entry.arguments+": "+entry.description);
            chat::info("#workflow template name arguments... loads a paused graph; inspect it before #workflow run"); return;
        }
        if(args.size()>=2 && args[0]=="template") {
            std::map<std::string,int> stock; for(int slot=0;slot<36;++slot){const auto s=bot.player().hotbar(slot);if(!s.empty())stock[s.name]+=s.count;}
            auto parsed=baritone::instantiateWorkflow(args[1],Args(args.begin()+2,args.end()),bot.identity(),stock);
            bot.cancelAll(); bot.replaceWorkflow(std::move(parsed)); chat::info("Workflow template loaded and paused; inspect #workflow list before running"); return;
        }
        if (args.empty() || (args.size() == 1 && args[0] == "list")) {
            chat::info(std::format("Workflow: {} nodes, current {}, {}. {}", flow.nodes().size(), flow.current(), flow.running() ? "running" : "paused", flow.reason()));
            for (std::size_t i = 0; i < flow.nodes().size(); ++i) { const auto& n = flow.nodes()[i]; chat::info(std::format("{}: {} -> success {}, failure {}, visits {}/{}", i, n.task.kind, n.success, n.failure, n.visits, n.visitLimit)); } return;
        }
        if (args.size() == 1 && args[0] == "run") { bot.resumeWorkflow(); return; }
        if (args.size() == 1 && args[0] == "pause") { bot.cancelAll(); return; }
        if (args.size() == 1 && args[0] == "restart") { bot.cancelAll(); flow.restart(); bot.saveWorkflow(); return; }
        if (args.size() == 1 && args[0] == "clear") { bot.cancelAll(); baritone::Workflow fresh; fresh.bind(bot.identity()); bot.replaceWorkflow(std::move(fresh)); return; }
        if (args.size() == 2 && args[0] == "load") {
            const auto file = std::filesystem::u8path(args[1]);
            if (std::filesystem::file_size(file) > 2 * 1024 * 1024) throw CommandError("Workflow file exceeds two MiB");
            std::ifstream input(file); if (!input) throw CommandError("Cannot read workflow file");
            auto parsed = baritone::Workflow::decode(std::string(std::istreambuf_iterator<char>(input), {})); parsed.bind(bot.identity()); parsed.restart();
            bot.cancelAll(); bot.replaceWorkflow(std::move(parsed)); chat::info("Workflow graph loaded and paused; #workflow run starts it"); return;
        }
        if (args.size() >= 7 && args[0] == "node") {
            if (!bot.workflowHealthy()) throw CommandError("Repair the workflow checkpoint or explicitly clear/load it before editing");
            const int index = parseInt(args[1], "node index"), limit = parseInt(args[4], "visit limit"), seconds = parseInt(args[5], "timeout seconds");
            if (index < 0 || limit < 1 || seconds < 1 || seconds > 86400) throw CommandError("Invalid workflow index, visits or timeout (1..86400 seconds)");
            baritone::Workflow::Node node; node.success = parseInt(args[2], "success branch"); node.failure = parseInt(args[3], "failure branch"); node.visitLimit = unsigned(limit); node.task.timeoutTicks = uint64_t(seconds) * 20;
            node.task.kind = args[6]; node.task.arguments.assign(args.begin() + 7, args.end());
            flow.bind(bot.identity()); flow.set(std::size_t(index), std::move(node)); bot.saveWorkflow(); chat::info("Workflow node saved; edits reset graph progress"); return;
        }
        throw CommandError("Usage: #workflow node index success failure visits timeoutSeconds kind arguments...; -1 finishes, -2 stops. list|run|pause|restart|clear");
    }});
    manager.add({{"doctor"}, "doctor", "Read compatibility and module prerequisites without changing game state", [&bot](const Args& args) {
        if (!args.empty()) throw CommandError("Usage: #doctor");
        const bool compatible = gameversion::check() && gameversion::validateNativeProfile();
        chat::info(std::format("Game profile {}: {}", compatible ? "matched" : "unsupported", gameversion::supported));
        std::size_t resolved = 0;
        for (const auto* signature : sigs::all()) {
            if (signature->address && mem::isReadable(reinterpret_cast<const void*>(signature->address), 1)) ++resolved;
            else chat::error("Missing native prerequisite: " + std::string(signature->name));
        }
        chat::info(std::format("Resolved native signatures: {}/{}", resolved, sigs::all().size()));
        auto group = [](const char* name, std::initializer_list<const sigs::Signature*> required) {
            const bool ready = std::ranges::all_of(required, [](const auto* sig) { return sig->address && mem::isReadable(reinterpret_cast<const void*>(sig->address), 1); });
            chat::info(std::string(name) + ": " + (ready ? "native entry points resolved" : "native entry points unavailable"));
        };
        group("Movement", {&sigs::Level_subTick, &sigs::ClientInput_tick, &sigs::LocalPlayer_applyTurnDelta});
        group("Inventory requests", {&sigs::ItemStackNet_beginRequest, &sigs::ItemStackNet_addRequestAction, &sigs::ItemStackNet_endRequest, &sigs::ItemStackNet_handleResponse, &sigs::ItemStackBase_copy});
        group("Overlay rendering", {&sigs::LevelRenderer_renderLevel, &sigs::Tessellator_begin, &sigs::Tessellator_vertex, &sigs::MeshHelpers_renderImmediately});
        chat::info(bot.player().valid() ? "World/player adapter: available" : "World/player adapter: enter an existing world");
        chat::info(std::format("Recipe descriptions: {} outputs; loadout reserve policy {}", bot.recipeBook().recipes().size(), bot.baritone().loadouts().healthy() ? "ready" : "blocked by an unreadable file"));
        chat::info("Resolved addresses establish prerequisites, not complete gameplay validation. See the module validation roadmap");
    }});
    manager.add({{"loadout"}, "loadout <capture|set|list|check|equip|use|off|remove|load|reset>", "Save inventory quantities, equip hotbar roles and preserve minimum item reserves", [&bot](const Args& args) {
        if (args.empty()) throw CommandError("Usage: #loadout capture name; set name item minimum target [hotbarSlot] [durabilityPercent]; list; check|equip|use|remove name; off; load; reset");
        auto& store = bot.baritone().loadouts(); const auto file = paths::dataDir() / "loadouts.txt";
        if (args[0] == "fill" && (args.size() == 2 || args.size() == 3)) {
            bot.prepareTasks(); std::map<std::string, int> targets;
            for (const auto& [item, rule] : store.profile(args[1])) if (rule.target > 0) targets[item] = rule.target;
            bot.baritone().logistics().start(std::move(targets), args.size() == 3 ? args[2] : "", args[1]); return;
        }
        if (args[0] == "list" && args.size() == 1) {
            for (const auto& [name, rules] : store.profiles()) chat::info(std::format("{}: {} item rules{}", name, rules.size(), store.active() == name ? " (active reserves)" : ""));
            chat::info(store.healthy() ? "Loadout policy ready" : "Loadout policy blocked; reload, repair or explicitly reset it"); return;
        }
        if (args[0] == "check" && args.size() == 2) {
            if (!bot.player().valid()) throw CommandError("Enter a world first");
            for (const auto& need : store.check(args[1], bot.player()))
                chat::info(std::format("{}: have {}, minimum {}, target {}, missing {}, hotbar {}", need.rule.item, need.have, need.rule.minimum, need.rule.target, need.missing, need.hotbarReady ? "ready" : "needs equipping"));
            return;
        }
        auto updated = store;
        if (args[0] == "capture" && args.size() == 2) {
            if (!bot.player().valid()) throw CommandError("Enter a world first");
            updated.capture(args[1], bot.player());
        } else if (args[0] == "set" && args.size() >= 5 && args.size() <= 7) {
            updated.set(args[1], {args[2], parseInt(args[3], "minimum quantity"), parseInt(args[4], "target quantity"),
                args.size() > 5 ? parseInt(args[5], "hotbar slot") : -1, args.size() > 6 ? parseInt(args[6], "minimum remaining durability percent") : 0});
        } else if ((args[0] == "use" || args[0] == "equip") && args.size() == 2) {
            updated.activate(args[1]);
            if (args[0] == "equip" && (!bot.player().valid() || bot.player().containerOpen() || bot.player().inventoryMovePending()))
                throw CommandError("Close containers and wait for inventory actions before equipping");
        } else if (args[0] == "off" && args.size() == 1) updated.activate("");
        else if (args[0] == "remove" && args.size() == 2) updated.remove(args[1]);
        else if (args[0] == "load" && args.size() == 1) {
            // A failed reload must leave reserves blocked until explicitly resolved.
            store.load(file); chat::info("Loadout profiles loaded"); return;
        } else if (args[0] == "reset" && args.size() == 1) updated = {};
        else throw CommandError("Usage: #loadout capture|set|list|check|equip|use|off|remove|load|reset");
        updated.save(file); store = std::move(updated);
        if (args[0] == "equip") {
            const auto rules = store.profile(args[1]); bot.cancelAll();
            bot.baritone().loadout().start(args[1], rules);
        }
        chat::info("Loadout profiles saved; active minimum quantities are protected from storage deposits");
    }});
    manager.add({{"recipe"}, "recipe <list|set|remove|load|save|reset>", "Describe ingredient dependencies; native recipes and server output remain authoritative", [&bot](const Args& args) {
        if (args.empty()) throw CommandError("Usage: #recipe list; set output count quantity*item|alternative ...; remove output; load; save; reset");
        const auto file = paths::dataDir() / "recipes.txt";
        if (args[0] == "list" && (args.size() == 1 || args.size() == 2)) {
            std::size_t shown = 0;
            for (const auto& [id, definition] : bot.recipeBook().recipes()) {
                if (args.size() == 2 && id.find(args[1]) == std::string::npos) continue;
                if (++shown <= 32) chat::info(std::format("{} x{}: {} ingredient groups", id, definition.count, definition.ingredients.size()));
            }
            chat::info(std::format("{} matching recipe descriptions; use #craft plan item count for dependencies", shown)); return;
        }
        auto updated = bot.recipeBook();
        if (args[0] == "set" && args.size() >= 4 && args.size() <= 12) {
            baritone::ProductionRecipe definition{args[1], parseInt(args[2], "recipe output count"), {}};
            for (std::size_t i = 3; i < args.size(); ++i) {
                const auto separator = args[i].find('*');
                if (separator == std::string::npos) throw CommandError("Ingredients use quantity*item|alternative, for example 2*oak_planks|spruce_planks");
                baritone::RecipeIngredient ingredient;
                ingredient.count = parseInt(args[i].substr(0, separator), "ingredient count");
                std::string alternatives = args[i].substr(separator + 1);
                for (std::size_t from = 0;;) {
                    const auto end = alternatives.find('|', from);
                    ingredient.alternatives.push_back(alternatives.substr(from, end == std::string::npos ? end : end - from));
                    if (end == std::string::npos) break;
                    from = end + 1;
                }
                definition.ingredients.push_back(std::move(ingredient));
            }
            updated.set(std::move(definition));
        } else if (args[0] == "remove" && args.size() == 2) updated.remove(args[1]);
        else if (args[0] == "reset" && args.size() == 1) updated = baritone::RecipeBook::vanillaBasics();
        else if (args[0] == "load" && args.size() == 1) updated.load(file);
        else if (args[0] == "import" && args.size() == 2) updated.load(std::filesystem::u8path(args[1]));
        else if (!(args[0] == "save" && args.size() == 1)) throw CommandError("Usage: #recipe list [item]; set output count quantity*item|alternative ...; remove output; load; save; reset");
        updated.save(file); bot.recipeBook() = std::move(updated);
        chat::info("Recipe descriptions saved; they do not override Minecraft's actual crafting recipes");
    }});
    manager.add({{"testdiscard"}, "testdiscard slot expectedItem expectedCount", "Dev: discard an exact Creative test stack through the normal inventory action", [&bot](const Args& args) {
        if (args.size()!=3) throw CommandError("Usage: #testdiscard slot expectedItem expectedCount");
        const int slot=parseInt(args[0],"slot"),count=parseInt(args[2],"count");
        const auto item=baritone::BlockFilter::normalize(args[1]);
        if (slot<0 || slot>=36 || count<1 || count>64) throw CommandError("Slot 0..35; expected count 1..64");
        bot.runInTick([&bot,slot,count,item] {
            auto& player=bot.player();
            if (!player.valid() || !player.isCreative() || !player.containerOpen() || player.inventoryMovePending()) {
                chat::error("Discard refused: open Creative inventory and wait for pending actions"); return;
            }
            const auto stack=player.hotbar(slot);
            if (stack.name!=item || stack.count!=count) {
                chat::error("Discard refused: open Creative inventory and match the exact current item/count"); return;
            }
            if (!sdk::discardInventoryStack(*sdk::ClientInstance::get()->localPlayer(),slot,count)) chat::error("The normal Creative discard action was unavailable");
            else chat::info("Creative discard submitted; verify the normal server response and #inv before continuing");
        });
    }});
    manager.add({{"craft"}, "craft <plan item count | auto station item count | status | list [item] | once item | state | station item count>", "Use live recipe entries and normal server-confirmed crafting actions", [&bot](const Args& args) {
        if (args.size() == 1 && args[0] == "status") {
            auto& production = bot.baritone().production();
            chat::info((production.isActive() ? production.displayName() : "Recursive production idle") +
                (production.failure().empty() ? "" : ": " + production.failure())); return;
        }
        if (args.size() == 3 && args[0] == "plan") {
            bot.baritone().production().start(bot.recipeBook(), {}, args[1], parseInt(args[2], "additional output"), false); return;
        }
        if (args.size() == 4 && args[0] == "auto") {
            bot.prepareTasks(); const auto& locations = bot.baritone().storage().locations();
            const auto found = locations.find(args[1]);
            if (found == locations.end()) throw CommandError("Register the crafting table with #storage add first");
            const auto position = found->second;
            const int count = parseInt(args[3], "additional output");
            if (count < 1 || count > 2304) throw CommandError("Additional output must be 1..2304");
            bot.cancelAll();
            bot.baritone().production().start(bot.recipeBook(), position, args[2], count, true); return;
        }
        if (args.size()==1 && args[0]=="state") {
            chat::info(std::format("Craft state {}: {}",int(bot.player().craftState()),bot.player().craftError())); return;
        }
        if (args.size()==2 && args[0]=="once") {
            if ((bot.baritone().workshop().isActive() || bot.baritone().produce().isActive() || bot.baritone().production().executing())) throw CommandError("Stop recursive production before manually crafting");
            if (!bot.player().containerOpen()) throw CommandError("Open a crafting interface first");
            const auto item=baritone::BlockFilter::normalize(args[1]);
            bot.runInTick([&bot,item] {
                const auto choices=bot.player().recipes(item);
                if (choices.empty() || !bot.player().craftRecipe(choices.front())) chat::error(bot.player().craftError().empty() ? "No available recipe or inventory action is pending" : bot.player().craftError());
                else chat::info("One normal recipe action submitted; #craft state and #inv show its result");
            }); return;
        }
        if ((args.size()==1 || args.size()==2) && args[0]=="list") {
            if (!bot.player().containerOpen()) throw CommandError("Open a crafting interface in Minecraft first");
            auto choices=bot.player().recipes(args.size()==2 ? baritone::BlockFilter::normalize(args[1]) : "");
            for (std::size_t i=0;i<std::min<std::size_t>(choices.size(),32);++i) chat::info(std::format("{} x{}: {} slot {}",choices[i].item,choices[i].count,choices[i].collection,choices[i].slot));
            chat::info(std::format("{} visible recipes; ingredients and unlocks are checked by Minecraft when crafting",choices.size())); return;
        }
        if (args.size()!=3) throw CommandError("Usage: #craft list [item]; #craft station item count");
        if ((bot.baritone().workshop().isActive() || bot.baritone().produce().isActive() || bot.baritone().production().executing())) throw CommandError("Stop recursive production before starting another crafting job");
        bot.prepareTasks(); const auto& locations=bot.baritone().storage().locations(); auto found=locations.find(args[0]);
        if (found==locations.end()) throw CommandError("Register the crafting table with #storage add first");
        bot.baritone().craft().start(found->second,args[1],parseInt(args[2],"additional output target"));
    }});
    manager.add({{"smelt"}, "smelt station input output count fuel fuelCount", "Load an empty registered furnace, wait for a one-to-one recipe and collect the output", [&bot](const Args& args) {
        if ((bot.baritone().workshop().isActive() || bot.baritone().produce().isActive() || bot.baritone().production().executing())) throw CommandError("Stop recursive production before starting a smelting job");
        if (args.size()!=6) throw CommandError("Usage: #smelt station input output count fuel fuelCount. Example: #smelt furnace raw_iron iron_ingot 8 coal 1");
        bot.prepareTasks(); const auto& locations=bot.baritone().storage().locations();
        auto location=locations.find(args[0]); if (location==locations.end()) throw CommandError("Register the furnace position with #storage add first");
        bot.baritone().smelt().start(location->second,args[1],args[2],parseInt(args[3],"input/output count"),args[4],parseInt(args[5],"fuel count"));
    }});
    manager.add({{"vehicle"}, "vehicle <board boat|horse | goto boat|horse x y z | dismount>", "Navigate wide water/ground corridors using a boat or saddled adult mount", [&bot](const Args& args) {
        if (args.size()==1 && args[0]=="dismount") { bot.cancelAll(); bot.baritone().vehicle().dismount(); return; }
        if ((args.size()==2 && args[0]=="board") || (args.size()==5 && args[0]=="goto")) {
            if (args[1]!="boat" && args[1]!="horse") throw CommandError("Vehicle kind must be boat or horse");
            auto kind=args[1]=="boat" ? baritone::VehicleKind::Boat : baritone::VehicleKind::Horse;
            const auto here=bot.baritone().playerFeet(); bot.cancelAll();
            if (args[0]=="board") bot.baritone().vehicle().board(kind);
            else bot.baritone().vehicle().navigate(kind,{parseCoord(args[2],here.x,"x"),parseCoord(args[3],here.y,"y"),parseCoord(args[4],here.z,"z")});
            return;
        }
        throw CommandError("Usage: #vehicle board boat|horse; #vehicle goto boat|horse x y z; #vehicle dismount");
    }});
    manager.add({{"trees"}, "trees [count]", "Harvest complete trees in the selection and replant matching saplings", [&bot](const Args& args) {
        if (args.size()>1) throw CommandError("Usage: #trees [count]");
        const auto& sels=bot.baritone().selections().all(); if (sels.empty()) throw CommandError("Select the complete trees and canopy with #sel first");
        const auto selection=sels.back(); const int count=args.empty() ? 1 : parseInt(args[0],"tree count");
        bot.cancelAll(); bot.baritone().trees().start(selection,count);
    }});
    manager.add({{"quarry"}, "quarry <plan|start [lights]|status|unload location|unload off>", "Excavate selected terrain top-down while preserving an access ramp", [&bot](const Args& args) {
        if (args.size()==1 && args[0]=="status") {
            chat::info(bot.quarry().failure().empty() ? bot.quarry().ready() ? "Quarry preflight passed" : "Quarry preflight not complete" : bot.quarry().failure()); return;
        }
        if (args.size()==2 && args[0]=="unload") {
            bot.prepareTasks();
            if (args[1]!="off" && !bot.baritone().storage().locations().contains(args[1])) throw CommandError("Register the storage location first");
            bot.setQuarryStorage(args[1]=="off" ? "" : args[1]); return;
        }
        if ((args.size()==1 || (args.size()==2 && args[1]=="lights")) && (args[0]=="plan" || args[0]=="start")) {
            const auto& sels=bot.baritone().selections().all(); if (sels.empty()) throw CommandError("Select the quarry volume with #sel first");
            bot.startQuarry(sels.back(),args.size()==2,args[0]=="start"); return;
        }
        throw CommandError("Usage: #quarry plan|start [lights]; status; unload location|off");
    }});
    manager.add({{"portal"}, "portal <add|remove|link|unlink|list|route|go|scale>", "Plan routes through registered dimension links and walk to the next portal", [&bot](const Args& args) {
        bot.prepareTasks(); auto& graph = bot.portals();
        if (args.empty()) throw CommandError("Usage: #portal add name [dimension x y z]; link from to [oneway]; route destination; go destination; scale dimension");
        const auto here = bot.baritone().playerFeet();
        if (args[0] == "list" && args.size() == 1) {
            for (const auto& [name,node] : graph.nodes()) chat::info(std::format("{}: dimension {}, {}",name,node.dimension,node.position));
            for (const auto& [a,b] : graph.links()) chat::info(a + " -> " + b);
            return;
        }
        if (args[0] == "scale" && args.size() == 2) {
            chat::info(std::format("Equivalent position: {}. Actual portal connections must be registered explicitly", baritone::PortalGraph::equivalent(here,bot.identity().dimension,parseInt(args[1],"dimension")))); return;
        }
        if ((args[0] == "route" || args[0] == "go") && args.size() == 2) {
            const auto route = graph.route(bot.identity().dimension,here,args[1]);
            chat::info(std::format("Estimated route cost {:.0f} (terrain and portal loading time vary)",route.cost));
            for (const auto& stop : route.stops) { const auto& node = graph.nodes().at(stop); chat::info(std::format("{}: dimension {}, {}",stop,node.dimension,node.position)); }
            if (args[0] == "go") {
                bot.startPortalJourney(args[1]);
            }
            return;
        }
        if (args[0] == "add" && (args.size() == 2 || args.size() == 6)) {
            graph.add(args[1],args.size()==2 ? bot.identity().dimension : parseInt(args[2],"dimension"),args.size()==2 ? here : baritone::BlockPos{parseInt(args[3],"x"),parseInt(args[4],"y"),parseInt(args[5],"z")});
        } else if (args[0] == "remove" && args.size() == 2) graph.remove(args[1]);
        else if (args[0] == "link" && (args.size() == 3 || (args.size()==4 && args[3]=="oneway"))) graph.link(args[1],args[2],args.size()==3);
        else if (args[0] == "unlink" && args.size() == 3) graph.unlink(args[1],args[2]);
        else throw CommandError("Usage: #portal add name [dimension x y z]; remove name; link from to [oneway]; unlink from to; list; route|go destination; scale dimension");
        graph.save(bot.worldDataPath("portals", false)); chat::info("Portal graph saved for this world");
    }});
    manager.add({{"restock"}, "restock <item item target [source] | auto on [source] | auto off | status>", "Refill absolute inventory targets and resume a builder after a confirmed supply trip", [&bot](const Args& args) {
        bot.prepareTasks(); auto& b = bot.baritone();
        if (args.size() == 1 && args[0] == "status") {
            chat::info(std::string("Automatic builder restocking: ") + (b.autoRestockEnabled() ? "enabled" : "disabled"));
            chat::info(b.logistics().isActive() ? b.logistics().displayName() : "Logistics idle: " + b.logistics().failure()); return;
        }
        if (args.size() >= 2 && args.size() <= 3 && args[0] == "auto" && (args[1] == "on" || (args[1] == "off" && args.size() == 2))) {
            b.autoRestock(args[1] == "on", args.size() == 3 ? args[2] : "");
            chat::info(args[1] == "on" ? "Builder restocking enabled; stop/world changes disable it" : "Automatic builder restocking disabled"); return;
        }
        if ((args.size() == 3 || args.size() == 4) && args[0] == "item") {
            b.logistics().start({{args[1], parseInt(args[2], "absolute inventory target")}}, args.size() == 4 ? args[3] : ""); return;
        }
        throw CommandError("Usage: #restock item item target [source]; auto on [source]; auto off; status");
    }});
    manager.add({{"storage"}, "storage <add|remove|list|open|inspect|scan|find|sort|withdraw|deposit>", "Register containers, transfer supplies and return to the interrupted task", [&bot](const Args& args) {
        bot.prepareTasks(); auto& storage = bot.baritone().storage();
        if (args.size() == 2 && args[0] == "find") {
            const auto item = baritone::BlockFilter::normalize(args[1]); std::size_t found = 0;
            for (const auto& [name, seen] : storage.observations()) {
                const auto count = seen.items.find(item);
                if (count != seen.items.end()) { ++found; chat::info(std::format("{}: {} x{} (last observed at Unix time {}; inspect again if changed)", name, item, count->second, seen.observedAt)); }
            }
            chat::info(std::format("{} last-seen matches; #storage scan all refreshes registered containers", found)); return;
        }
        if (args.size() == 2 && args[0] == "scan") { bot.baritone().logistics().survey(args[1] == "all" ? "" : args[1]); return; }
        if (args.size() == 2 && args[0] == "sort") {
            if (bot.baritone().logistics().isActive() || (bot.baritone().workshop().isActive() || bot.baritone().produce().isActive() || bot.baritone().production().executing())) throw CommandError("Finish current production/logistics first");
            storage.sort(args[1]); return;
        }
        if (args.size()==2 && args[0]=="open") {
            if ((bot.baritone().workshop().isActive() || bot.baritone().produce().isActive() || bot.baritone().production().executing()) || bot.baritone().logistics().isActive()) throw CommandError("Stop production/logistics before opening another container");
            auto found=storage.locations().find(args[1]); if (found==storage.locations().end()) throw CommandError("Unknown storage location");
            const auto position=found->second;
            bot.runInTick([&bot,position] { if (!bot.player().openContainerAt(position)) chat::error("Could not open the registered container; check reach, line of sight and current screen"); });
            return;
        }
        if (args.size() == 1 && args[0] == "list") {
            for (const auto& [name, pos] : storage.locations()) chat::info(std::format("{}: {}", name, pos)); return;
        }
        if (args.size() == 1 && args[0] == "inspect") {
            if (!bot.player().containerOpen()) throw CommandError("Open a container in Minecraft first");
            for (const auto& slot : bot.player().containerSlots()) if (!slot.playerInventory && !slot.stack.empty())
                chat::info(std::format("{} slot {}: {} x{}", slot.collection, slot.slot, slot.stack.name, slot.stack.count));
            return;
        }
        if (args.size() == 5 && args[0] == "add") {
            auto p = bot.player().position();
            storage.addLocation(args[1], {parseCoord(args[2], int(std::floor(p.x)), "x"), parseCoord(args[3], int(std::floor(p.y)), "y"), parseCoord(args[4], int(std::floor(p.z)), "z")});
            storage.save(bot.worldDataPath("storage")); chat::info("Storage location saved"); return;
        }
        if (args.size() == 2 && args[0] == "remove") { storage.removeLocation(args[1]); storage.save(bot.worldDataPath("storage")); return; }
        if ((args.size() == 3 || args.size() == 4) && (args[0] == "withdraw" || args[0] == "deposit")) {
            if ((bot.baritone().workshop().isActive() || bot.baritone().produce().isActive() || bot.baritone().production().executing()) || bot.baritone().logistics().isActive()) throw CommandError("Stop production/logistics before starting a manual storage job");
            const auto item = baritone::BlockFilter::normalize(args[2]);
            int count = args.size() == 4 ? parseInt(args[3], "count") : args[0] == "withdraw" ? 64 : bot.player().countItem(item);
            storage.start(args[1], item, count, args[0] == "withdraw"); return;
        }
        throw CommandError("Usage: #storage add name x y z; remove name; list; inspect; withdraw|deposit name item [count]");
    }});
    manager.add({{"animals"}, "animals <list|breed species count [radius]|harvest species count keep [radius]>", "Breed confirmed adults or harvest while preserving a minimum adult population", [&bot](const Args& args) {
        if (!bot.player().valid()) throw CommandError("Enter a world first");
        if (args.size() == 1 && args[0] == "list") {
            for (const auto& entity : bot.player().entities()) if (!entity.isItem && !entity.isPlayer)
                chat::info(std::format("{} id {}: age {}, breeding {}, position {:.1f} {:.1f} {:.1f}", entity.type, entity.id,
                    !entity.baby ? "unknown" : *entity.baby ? "baby" : "adult", !entity.inLove ? "unknown" : *entity.inLove ? "in love" : "not in love", entity.position.x, entity.position.y, entity.position.z));
            return;
        }
        if (args.size() >= 3 && (args[0] == "breed" || args[0] == "harvest")) {
            bool harvest = args[0] == "harvest";
            if ((!harvest && args.size() != 3 && args.size() != 4) || (harvest && args.size() != 4 && args.size() != 5)) throw CommandError("Usage: #animals breed species count [radius]; #animals harvest species count keep [radius]");
            int radius = args.size() == (harvest ? 5 : 4) ? parseInt(args.back(), "radius") : 16;
            bot.cancelAll();
            bot.baritone().animals().start(args[1], harvest, parseInt(args[2], "count"), harvest ? parseInt(args[3], "minimum kept adults") : 2, radius);
            return;
        }
        throw CommandError("Usage: #animals list|breed|harvest");
    }});
    manager.add({{"survival"}, "survival <on|off|home x y z|threshold food health>", "Eat safe food, retreat from nearby hostiles and return home for supplies", [&bot](const Args& args) {
        auto& assistant = bot.baritone().survival();
        if (args.empty()) {
            if (!bot.player().valid()) throw CommandError("Enter a world first");
            chat::info(std::format("Survival assistant {}. Mode: {}, health {:.0f}, food {}. Eating below {}, retreating at health {:.0f}",
                assistant.enabled() ? "enabled" : "disabled", bot.player().isCreative() ? "Creative (assistant inactive)" : "Survival/Adventure",
                bot.player().health(), bot.player().foodLevel(), assistant.eatBelow, assistant.retreatBelow)); return;
        }
        if (args.size() == 1 && (args[0] == "on" || args[0] == "off")) {
            assistant.setEnabled(args[0] == "on"); chat::info(args[0] == "on" ? "Survival assistant enabled; #stop disables it" : "Survival assistant disabled"); return;
        }
        if (args.size() == 4 && args[0] == "home") {
            const auto p = bot.player().position();
            assistant.setHome({parseCoord(args[1], int(std::floor(p.x)), "x"), parseCoord(args[2], int(std::floor(p.y)), "y"), parseCoord(args[3], int(std::floor(p.z)), "z")});
            chat::info("Survival home set in this dimension"); return;
        }
        if (args.size() == 3 && args[0] == "threshold") {
            int food = parseInt(args[1], "food"), health = parseInt(args[2], "health");
            if (food < 1 || food > 20 || health < 1 || health > 16) throw CommandError("Food must be 1..20; health must be 1..16");
            assistant.eatBelow = food; assistant.retreatBelow = static_cast<float>(health); return;
        }
        throw CommandError("Usage: #survival on|off; #survival home x y z; #survival threshold food health");
    }});
    manager.add({{"task", "queue"}, "task <add|list|resume|pause|retry|clear|configure>", "Persistent world-specific task workflows with conditions and recovery", [&bot](const Args& args) {
        bot.prepareTasks();
        auto& queue = bot.tasks();
        if (args.empty() || (args.size() == 1 && (args[0] == "list" || args[0] == "status"))) {
            chat::info(std::format("Task workflow: {}, current index {}, {} entries", queue.running() ? "running" : "paused", queue.currentIndex(), queue.tasks().size()));
            for (std::size_t i = 0; i < queue.tasks().size(); ++i) {
                const auto& task = queue.tasks()[i];
                chat::info(std::format("{}: {} [{}], attempt {}, {} ticks. {}", i, task.kind, baritone::taskStatusName(task.status), task.attempts, task.elapsedTicks, task.reason));
            }
            return;
        }
        if (args.size() == 1 && args[0] == "resume") { bot.resumeTasks(); chat::info("Task workflow resumed"); return; }
        if (args.size() == 1 && args[0] == "pause") { bot.cancelAll(); chat::info("Task workflow paused and checkpointed"); return; }
        if (args.size() == 1 && args[0] == "clear") { bot.cancelAll(); queue.clear(); bot.saveTasks(); chat::info("Task workflow cleared"); return; }
        if (args.size() == 1 && args[0] == "retry") { queue.retryCurrent(); bot.resumeTasks(); chat::info("Retrying task against the current world"); return; }
        if (args.size() == 4 && args[0] == "configure") {
            int index = parseInt(args[1], "index"), seconds = parseInt(args[2], "timeout seconds"), retries = parseInt(args[3], "retries");
            if (index < 0 || seconds < 1 || retries < 0) throw CommandError("Index/retries must be non-negative and timeout positive");
            queue.configure(static_cast<std::size_t>(index), uint64_t(seconds) * 20, static_cast<unsigned>(retries)); bot.saveTasks(); return;
        }
        if (args.size() >= 2 && args[0] == "add") {
            baritone::Task task; task.kind = args[1]; task.arguments.assign(args.begin() + 2, args.end());
            const auto feet = bot.player().position();
            const baritone::BlockPos base{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y + .1251)), static_cast<int>(std::floor(feet.z))};
            auto coordinates = [&](std::size_t first) {
                task.arguments[first] = std::to_string(parseCoord(task.arguments[first], base.x, "x"));
                task.arguments[first + 1] = std::to_string(parseCoord(task.arguments[first + 1], base.y, "y"));
                task.arguments[first + 2] = std::to_string(parseCoord(task.arguments[first + 2], base.z, "z"));
            };
            if (task.kind == "goto" && task.arguments.size() == 3) coordinates(0);
            else if (task.kind == "build" && (task.arguments.size() == 1 || task.arguments.size() == 4)) {
                if (task.arguments.size() == 1) {
                    task.arguments.push_back(std::to_string(base.x)); task.arguments.push_back(std::to_string(base.y)); task.arguments.push_back(std::to_string(base.z));
                } else coordinates(1);
            } else if (task.kind == "wait" && task.arguments.size() == 1) {
                int seconds = parseInt(task.arguments[0], "seconds");
                if (seconds < 1 || seconds > 30 * 24 * 60 * 60) throw CommandError("Wait must be 1..2592000 seconds");
                auto ticks = uint64_t(seconds) * 20; task.arguments[0] = std::to_string(ticks); task.timeoutTicks = std::min<uint64_t>(ticks + 1200,uint64_t(30)*24*60*60*20);
            } else if (task.kind == "mine" && task.arguments.size() >= 2 && parseInt(task.arguments[0], "quantity") > 0) {}
            else if (task.kind == "inventory" && task.arguments.size() == 2 && parseInt(task.arguments[1], "quantity") > 0) {}
            else if ((task.kind == "withdraw" || task.kind == "deposit") && task.arguments.size() == 3 && parseInt(task.arguments[2], "quantity") > 0) {
                int have = bot.player().countItem(baritone::BlockFilter::normalize(task.arguments[1]));
                int quantity = parseInt(task.arguments[2], "quantity");
                int64_t target = int64_t(have) + (task.kind == "withdraw" ? quantity : -quantity);
                if (target < 0 || target > 2304) throw CommandError("The requested storage quantity exceeds current inventory or capacity");
                task.arguments.push_back(std::to_string(target));
            }
            else if (task.kind == "verify" && task.arguments.empty()) {}
            else throw CommandError("Tasks: goto x y z; build file [x y z]; mine quantity block...; wait seconds; inventory item quantity; verify");
            queue.bind(bot.identity()); queue.add(std::move(task)); bot.saveTasks();
            chat::info("Task added and checkpointed; #task resume starts the workflow"); return;
        }
        throw CommandError("Usage: #task add|list|resume|pause|retry|clear|configure");
    }});
    manager.add({{"why"}, "why", "Show recent task status and concrete failure reasons", [&bot](const Args& args) {
        if (!args.empty()) throw CommandError("Usage: #why");
        const auto& events = bot.baritone().diagnostics().events();
        if (events.empty()) { chat::info("No task diagnostics recorded in this session"); return; }
        auto begin = events.size() > 8 ? events.size() - 8 : 0;
        for (std::size_t i = begin; i < events.size(); ++i) chat::info("[" + events[i].code + "] " + events[i].message);
    }});
    manager.add({{"protect"}, "protect <add name entry|modify|both | remove name | list>", "Persist protected zones from the newest selection", [&bot](const Args& args) {
        auto world = bot.baritone().world();
        if (!world || !bot.player().valid()) throw CommandError("Enter a world first");
        auto current = world->protection();
        auto next = std::make_shared<baritone::ProtectionPolicy>(current ? *current : baritone::ProtectionPolicy{});
        if (args.size() == 1 && args[0] == "list") {
            chat::info(std::format("{} protected zones in this world/dimension", next->zones().size()));
            for (const auto& zone : next->zones()) chat::info(std::format("{}: {} to {}, mode {}", zone.name, zone.bounds.min(), zone.bounds.max(), int(zone.mode)));
            return;
        }
        if (args.size() == 3 && args[0] == "add") {
            const auto& selections = bot.baritone().selections().all();
            if (selections.empty()) throw CommandError("Make a selection with #sel first");
            const auto mode = args[2] == "entry" ? baritone::ProtectionPolicy::NoEntry : args[2] == "modify" ? baritone::ProtectionPolicy::NoModify : args[2] == "both" ? baritone::ProtectionPolicy::Both : baritone::ProtectionPolicy::Mode(0);
            next->add({args[1], selections.back(), mode});
        } else if (args.size() == 2 && args[0] == "remove") {
            if (!next->remove(args[1])) throw CommandError("Protected zone not found");
        } else throw CommandError("Usage: #protect add <name> <entry|modify|both>, #protect remove <name>, #protect list");
        bot.applyProtection(std::move(next));
        chat::info("Protected zones saved. Bot activity stopped so the next task uses the new policy");
    }});
    manager.add({{"export"}, "export <name.mcstructure>", "Export the newest selection's loaded blocks and supported states", [&bot](const Args& args) {
        if (args.size() != 1) throw CommandError("Usage: #export <name.mcstructure>");
        const auto& selections = bot.baritone().selections().all();
        if (selections.empty()) throw CommandError("Make a selection with #sel first");
        const auto name = std::filesystem::path(args[0]);
        if (name != name.filename() || name.extension() != ".mcstructure") throw CommandError("Choose a filename ending in .mcstructure without directories");
        const auto world = bot.baritone().world();
        if (!world || !bot.player().valid()) throw CommandError("Enter a world first");
        const auto selection = selections.back();
        auto path = paths::dataDir() / "schematics" / name;
        std::filesystem::create_directories(path.parent_path());
        bot.runDiagnosticAsync([world, selection, path] {
            baritone::exportMcStructure(*world, selection, path);
            chat::info("Exported " + path.filename().string() + ". Block entities, entities and secondary fluid layers are not included");
        });
    }});
}
}
