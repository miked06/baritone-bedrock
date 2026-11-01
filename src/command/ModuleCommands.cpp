#include "command/ModuleCommands.h"
#include <filesystem>
#include <format>
#include <cmath>
#include "baritone/schematic/SchematicExport.h"
#include "core/Bot.h"
#include "sdk/Recipes.h"
#include "core/Chat.h"
#include "core/Paths.h"

namespace command {
void registerModuleCommands(CommandManager& manager, Bot& bot) {
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
    manager.add({{"craft"}, "craft <list [item] | once item | state | station item count>", "Use live recipe entries and normal server-confirmed crafting actions", [&bot](const Args& args) {
        if (args.size()==1 && args[0]=="state") {
            chat::info(std::format("Craft state {}: {}",int(bot.player().craftState()),bot.player().craftError())); return;
        }
        if (args.size()==2 && args[0]=="once") {
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
        bot.prepareTasks(); const auto& locations=bot.baritone().storage().locations(); auto found=locations.find(args[0]);
        if (found==locations.end()) throw CommandError("Register the crafting table with #storage add first");
        bot.baritone().craft().start(found->second,args[1],parseInt(args[2],"additional output target"));
    }});
    manager.add({{"smelt"}, "smelt station input output count fuel fuelCount", "Load an empty registered furnace, wait for a one-to-one recipe and collect the output", [&bot](const Args& args) {
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
                const auto& next = graph.nodes().at(route.stops.front());
                bot.cancelAll(); bot.baritone().customGoal().setGoalAndPath(std::make_shared<baritone::GoalBlock>(next.position));
                chat::info("Walking to the next portal. After changing dimension, run #portal go again to continue the route");
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
    manager.add({{"storage"}, "storage <add|remove|list|open|inspect|withdraw|deposit>", "Register containers, transfer supplies and return to the interrupted task", [&bot](const Args& args) {
        bot.prepareTasks(); auto& storage = bot.baritone().storage();
        if (args.size()==2 && args[0]=="open") {
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
