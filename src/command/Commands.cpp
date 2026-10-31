#include "command/Commands.h"

#include <Windows.h>

#include <format>
#include <ctime>
#include <map>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <optional>
#include <fstream>
#include <charconv>
#include <filesystem>
#include <thread>
#include <unordered_set>

#include "baritone/Settings.h"
#include "baritone/cache/CachedChunk.h"
#include "baritone/cache/WorldScanner.h"
#include "baritone/pathing/calc/AStarPathFinder.h"
#include "baritone/pathing/movement/MovementHelper.h"
#include "baritone/schematic/Schematic.h"
#include "baritone/utils/RayTrace.h"
#include "core/Bot.h"
#include "platform/WorldTracker.h"
#include "core/Chat.h"
#include "core/GameVersion.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Safe.h"
#include "sdk/Game.h"

namespace command {
namespace {

sdk::LocalPlayer& requirePlayer() {
    auto* client = sdk::ClientInstance::get();
    auto* player = client ? client->localPlayer() : nullptr;
    if (!player) throw CommandError("No player available (are you in a world?)");
    return *player;
}

std::string blockName(const sdk::Block* block) {
    if (!mem::isReadable(block, offsets::Block_legacy + 8)) return "<unreadable block>";
    const sdk::BlockLegacy* legacy = block->legacy();
    if (!mem::isReadable(legacy, offsets::BlockLegacy_namespacedId + 0x30)) return "<unreadable legacy>";
    return legacy->name();
}

std::string lowerCase(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool isNumber(const std::string& s) {
    return !s.empty() && std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c); });
}

// Java Paginator: 8 lines per page, then how to see the next one.
void paginate(const std::vector<std::string>& lines, const std::string& pageArg, const std::string& header,
              const std::string& command) {
    constexpr std::size_t kPageSize = 8;
    std::size_t pages = std::max<std::size_t>(1, (lines.size() + kPageSize - 1) / kPageSize);
    std::size_t page = 1;
    if (!pageArg.empty()) page = static_cast<std::size_t>(std::max(1, parseInt(pageArg, "page")));
    if (page > pages) throw CommandError(std::format("Page {}: only {} page(s) available", page, pages));
    chat::info(header);
    for (std::size_t i = (page - 1) * kPageSize; i < std::min(lines.size(), page * kPageSize); ++i) chat::info(lines[i]);
    if (pages > 1) {
        const std::string& prefix = baritone::Settings::get().prefix;
        chat::info(std::format("Page {}/{}{}", page, pages,
                               page < pages ? std::format(" - next: {}{} {}", prefix, command, page + 1) : ""));
    }
}

// Prints everything we read from the game: the quickest way to validate the offsets of
// sdk/Offsets.h after a game update.
void debugInfo() {
    auto* client = sdk::ClientInstance::get();
    logger::info("ClientInstance = {}", static_cast<void*>(client));
    auto& player = requirePlayer();
    logger::info("LocalPlayer = {}", static_cast<void*>(&player));

    chat::info(std::format("Player: {}", player.name()));
    if (auto* sv = player.stateVector(); mem::isReadable(sv, sizeof(sdk::StateVectorComponent))) {
        chat::info(std::format("Position: {}  velocity: {}", sv->pos, sv->velocity));
    }
    sdk::Vec3 feet = player.feet();
    sdk::BlockPos feetPos = sdk::BlockPos::floor(feet);
    chat::info(std::format("Feet: {}  (block {})", feet, feetPos));
    if (auto* rot = player.rotation(); mem::isReadable(rot, sizeof(sdk::ActorRotationComponent))) {
        chat::info(std::format("Rotation: yaw {:.1f}  pitch {:.1f}", rot->rotation.y, rot->rotation.x));
    }
    if (auto* region = client->region()) {
        chat::info(std::format("Block below feet: {}", blockName(region->getBlock(feetPos.below()))));
        chat::info(std::format("Block at feet: {}", blockName(region->getBlock(feetPos))));
    }
    if (auto* dim = player.dimension(); mem::isReadable(dim, 0x40)) {
        chat::info(std::format("Dimension: {}", dim->name()));
    }
    if (auto* supplies = player.supplies(); mem::isReadable(supplies, 0xC0)) {
        int slot = supplies->selectedSlot();
        std::string held = "nothing";
        if (auto* inv = supplies->inventory()) {
            auto* stack = inv->getItem(slot);
            if (stack && !stack->empty()) held = std::format("{} x{}", stack->item()->name(), stack->count());
        }
        chat::info(std::format("Slot {}: {}", slot, held));
    }
    {
        std::string worn;
        for (int i = 0; i < 4; ++i) {
            baritone::HotbarStack a = Bot::get().player().armor(i);
            worn += i ? ", " : "";
            worn += a.empty() ? "-" : a.maxDamage > 0 ? std::format("{} {}/{}", a.name, a.maxDamage - a.damage, a.maxDamage) : a.name;
        }
        chat::info("Armor: " + worn);
    }
    {
        auto hunger = player.attribute("minecraft:player.hunger");
        auto health = player.attribute("minecraft:health");
        chat::info(std::format("Hunger {} / Health {} / haste {} / fatigue {}", hunger ? std::format("{:.0f}", hunger->value) : "?",
                               health ? std::format("{:.0f}", health->value) : "?", Bot::get().player().effectAmplifier("haste"),
                               Bot::get().player().effectAmplifier("mining_fatigue")));
    }
    if (auto* level = Bot::get().player().level()) {
        const sdk::HitResult* hit = level->hitResult();
        if (mem::isReadable(hit, sizeof(sdk::HitResult))) {
            chat::info(hit->type == 0 ? std::format("Game target: block {} face {} point {}", hit->block, hit->face, hit->pos)
                                      : std::format("Game target: type {}", hit->type));
        }
    }
    auto* input = player.moveInput();
    logger::info("MoveInputComponent = {}  GameMode = {}  ticks = {}  inputTicks = {}  game = {} cursorGrabbed = {}",
                 static_cast<void*>(input), static_cast<void*>(player.gameMode()), Bot::get().ticks(),
                 Bot::get().input().inputTicks(), client->minecraftGame(), client->cursorGrabbedFlag());
}

baritone::BlockPos feetPos(sdk::LocalPlayer& player) {
    sdk::Vec3 f = player.feet();
    return {static_cast<int>(std::floor(f.x)), static_cast<int>(std::floor(f.y + 0.1251)), static_cast<int>(std::floor(f.z))};
}

// Baritone's goal arguments: [] = here, [y] = GoalYLevel, [x z] = GoalXZ, [x y z] = GoalBlock.
// "~" means relative to the player.
baritone::GoalPtr parseGoal(const Args& args, sdk::LocalPlayer& player) {
    baritone::BlockPos p = feetPos(player);
    switch (args.size()) {
        case 0: return std::make_shared<baritone::GoalBlock>(p);
        case 1: return std::make_shared<baritone::GoalYLevel>(parseCoord(args[0], p.y, "y"));
        case 2: return std::make_shared<baritone::GoalXZ>(parseCoord(args[0], p.x, "x"), parseCoord(args[1], p.z, "z"));
        case 3:
            return std::make_shared<baritone::GoalBlock>(parseCoord(args[0], p.x, "x"), parseCoord(args[1], p.y, "y"),
                                                         parseCoord(args[2], p.z, "z"));
        default: throw CommandError("Too many arguments: use x y z, x z or y");
    }
}

// Logs the vtable of a game object as module offsets, to find functions in a dump.
void dumpVtable(std::string_view name, const void* obj, int count) {
    if (!mem::isReadable(obj)) {
        logger::warn("vtable {}: object not readable", name);
        return;
    }
    void** vt = mem::vtable(obj);
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"Minecraft.Windows.exe"));
    logger::info("vtable {} = Minecraft.Windows.exe+0x{:X} (object {})", name, reinterpret_cast<uintptr_t>(vt) - base, obj);
    for (int i = 0; i < count && mem::isReadable(vt + i); ++i) {
        logger::info("  [0x{:X}] +0x{:X}", i, reinterpret_cast<uintptr_t>(vt[i]) - base);
    }
}

// Logs an object as qwords, annotating pointers into the game module and short strings.
void hexDump(std::string_view name, const void* obj, int size) {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"Minecraft.Windows.exe"));
    logger::info("hexdump {} at {}", name, obj);
    for (int off = 0; off < size; off += 8) {
        auto p = reinterpret_cast<const uint64_t*>(reinterpret_cast<uintptr_t>(obj) + off);
        if (!mem::isReadable(p, 8)) break;
        uint64_t v = *p;
        std::string note;
        if (v >= base && v < base + 0x13000000) note = std::format("Minecraft.Windows.exe+0x{:X}", v - base);
        else if (mem::isReadable(reinterpret_cast<const void*>(v), 16)) {
            auto s = reinterpret_cast<const char*>(v);
            bool printable = true;
            int len = 0;
            for (; len < 16 && s[len]; ++len) printable &= (s[len] >= 32 && s[len] < 127);
            if (printable && len >= 3) note = std::format("-> \"{}\"", std::string_view(s, len));
            else note = "-> heap";
        }
        // inline short strings
        std::string ascii;
        for (int i = 0; i < 8; ++i) {
            char c = static_cast<char>((v >> (i * 8)) & 0xFF);
            ascii += (c >= 32 && c < 127) ? c : '.';
        }
        logger::info("  +0x{:03X}: {:016X} {} {}", off, v, ascii, note);
    }
}

}  // namespace

void registerAll(CommandManager& manager, Bot& bot) {
    manager.setCompletionProvider([&bot, &manager](const Args& words) -> Args {
        if (words.size() < 2) return {};
        const auto* command = manager.find(words[0]);
        if (!command) return {};
        const std::string& name = command->names.front();
        const std::string action = lowerCase(words[1]);
        Args result;
        if (name == "follow" && words.size() == 3 && bot.player().valid()) {
            for (const auto& entity : bot.player().entities()) {
                if (action == "player" && entity.isPlayer) result.push_back(entity.name);
                if (action == "entity") {
                    std::string type = entity.type;
                    if (words.back().find(':') == std::string::npos && type.starts_with("minecraft:")) type.erase(0, 10);
                    result.push_back(std::move(type));
                }
            }
        } else if (name == "wp" && words.size() == 3 && bot.player().valid()) {
            for (const auto& waypoint : bot.waypoints().list()) {
                if (!waypoint.name.empty()) result.push_back(waypoint.name);
                result.push_back(waypoint.tag);
            }
            result.insert(result.end(), {"home", "base", "death", "bed", "spawn", "user"});
        } else if ((name == "build" && words.size() == 2) ||
                   (name == "set" && words.size() == 3 && (action == "load" || action == "ld" || action == "save"))) {
            auto folder = name == "build" ? paths::dataDir() / "schematics" : paths::dataDir();
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
                if (!entry.is_regular_file(error)) continue;
                std::string extension = lowerCase(entry.path().extension().string());
                if (name == "build" ? extension == ".schem" || extension == ".schematic" || extension == ".litematic" || extension == ".mcstructure"
                                    : extension == ".txt") result.push_back(entry.path().filename().string());
            }
        }
        // State suggestions use the actual properties observed in this game.
        const std::string& prefix = words.back();
        const auto bracket = prefix.find('[');
        if (bracket != std::string::npos && (name == "goto" || name == "find" || name == "mine" || name == "sel")) {
            std::string block = baritone::BlockFilter::normalize(prefix.substr(0, bracket));
            const auto comma = prefix.rfind(',');
            std::string head = prefix.substr(0, comma != std::string::npos && comma > bracket ? comma + 1 : bracket + 1);
            const auto& registry = baritone::BlockRegistry::get();
            for (std::size_t i = 0, count = registry.size(); i < count; ++i) {
                const auto& state = registry.props(static_cast<baritone::StateId>(i));
                if (state.name != block) continue;
                for (const auto& [key, value] : state.states)
                    if (head.find(key + "=") == std::string::npos) result.push_back(head + key + "=" + value + "]");
            }
        }
        return result;
    });
    // ---- pathing (Baritone: goto, goal, path, thisway, cancel) ----
    manager.add({{"goto"}, "goto <x y z | x z | y | block>", "Go to a position or the nearest matching block",
                 [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #goto <x y z | x z | y | block>");
                     bool numeric = std::ranges::all_of(args, [](const std::string& a) {
                         return !a.empty() && (a[0] == '~' || a[0] == '-' || std::isdigit(static_cast<unsigned char>(a[0])));
                     });
                     if (!numeric) {
                         baritone::BlockFilter filter(args);
                         chat::info("Searching for " + filter.toString());
                         bot.baritone().getToBlock().getToBlock(std::move(filter));
                         return;
                     }
                     auto goal = parseGoal(args, player);
                     chat::info(std::format("Goal: {}", goal->toString()));
                     bot.baritone().customGoal().setGoalAndPath(goal);
                 }});

    manager.add({{"goal"}, "goal [x y z | x z | y | clear]", "Set the goal without starting to move", [&bot](const Args& args) {
        auto& player = requirePlayer();
        if (!args.empty() && (args[0] == "clear" || args[0] == "reset" || args[0] == "none")) {
            if (bot.baritone().customGoal().goal()) {
                bot.baritone().customGoal().setGoal(nullptr);
                chat::info("Goal cleared");
            } else {
                chat::info("No goal to clear");
            }
            return;
        }
        auto goal = parseGoal(args, player);
        bot.baritone().customGoal().setGoal(goal);
        chat::info(std::format("Goal: {}", goal->toString()));
    }});

    manager.add({{"path"}, "path", "Go to the current goal", [&bot](const Args&) {
        auto& custom = bot.baritone().customGoal();
        if (!custom.goal()) {
            chat::info("No goal set");
            return;
        }
        auto* player = sdk::ClientInstance::get() ? sdk::ClientInstance::get()->localPlayer() : nullptr;
        if (player) platform::WorldTracker::get().refreshAround(feetPos(*player), 16);  // Java: repack
        custom.path();
        chat::info("On the way");
    }});

    manager.add({{"mine"}, "mine [quantity] <block> [block...]", "Mine the specified blocks (e.g. #mine diamond_ore)",
                 [&bot](const Args& args) {
                     requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #mine [quantity] <block> [block...]");
                     int quantity = 0;
                     Args blocks = args;
                     if (std::isdigit(static_cast<unsigned char>(args[0][0]))) {
                         quantity = parseInt(args[0], "quantity");
                         blocks.erase(blocks.begin());
                     }
                     if (blocks.empty()) throw CommandError("Specify at least one block");
                     baritone::BlockFilter filter(blocks);
                     chat::info(std::format("Mining {}{}", filter.toString(),
                                            quantity > 0 ? std::format(" ({} requested)", quantity) : ""));
                     bot.baritone().mine().mine(quantity, std::move(filter));
                 }});

    // Java PickupCommand
    manager.add({{"pickup"}, "pickup [item...]", "Pick up dropped items (all or the specified types)", [&bot](const Args& args) {
        requirePlayer();
        std::unordered_set<std::string> items;
        for (const std::string& a : args) items.insert(baritone::BlockFilter::normalize(a));
        if (items.empty()) {
            bot.baritone().follow().pickup([](const std::string&) { return true; }, "all items");
            chat::info("Picking up all items");
        } else {
            std::string list;
            for (const auto& i : items) list += (list.empty() ? "" : ", ") + i;
            bot.baritone().follow().pickup([items](const std::string& item) { return items.contains(item); }, list);
            chat::info("Picking up these items: " + list);
        }
    }});

    manager.add({{"follow"}, "follow <players | player <name> | entities | entity <type>>", "Follow players or entities",
                 [&bot](const Args& args) {
                     requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #follow players | player <name> | entities | entity <type>");
                     const std::string& mode = args[0];
                     Args rest(args.begin() + 1, args.end());
                     baritone::FollowProcess::Filter filter;
                     std::string desc;
                     if (mode == "players") {
                         filter = [](const baritone::EntityInfo& e) { return e.isPlayer; };
                         desc = "all players";
                     } else if (mode == "player") {
                         if (rest.empty()) throw CommandError("Specify the player name");
                         filter = [names = rest](const baritone::EntityInfo& e) {
                             if (!e.isPlayer) return false;
                             return std::ranges::any_of(names, [&](const std::string& n) {
                                 return std::ranges::equal(n, e.name, [](char a, char b) { return std::tolower(a) == std::tolower(b); });
                             });
                         };
                         desc = rest[0];
                     } else if (mode == "entities") {
                         filter = [](const baritone::EntityInfo& e) { return !e.isItem; };
                         desc = "all entities";
                     } else if (mode == "entity") {
                         if (rest.empty()) throw CommandError("Specify the entity type (e.g. cow)");
                         std::vector<std::string> types;
                         for (const auto& t : rest) types.push_back(baritone::BlockFilter::normalize(t));
                         filter = [types](const baritone::EntityInfo& e) {
                             return std::ranges::find(types, e.type) != types.end();
                         };
                         desc = rest[0];
                     } else {
                         throw CommandError("Unknown mode: use players, player, entities or entity");
                     }
                     bot.baritone().follow().follow(std::move(filter), desc);
                     chat::info("Following " + desc);
                 }});

    // ---- waypoints (Baritone: sethome, home, wp) ----
    // Java aliases: sethome = "waypoints save home", home = "waypoints goto home"
    manager.add({{"sethome"}, "sethome", "Save your current position as home", [&manager](const Args& args) {
        std::string line = "wp save home";
        for (const auto& a : args) line += " " + a;
        manager.runCommand(line, false);
    }});
    manager.add({{"home"}, "home", "Go home", [&manager](const Args& args) {
        std::string line = "wp goto home";
        for (const auto& a : args) line += " " + a;
        manager.runCommand(line, false);
    }});

    // Java WaypointsCommand: list, save, info, delete, clear, restore, goal, goto
    manager.add({{"wp", "waypoint", "waypoints"},
                 "wp [list [tag] [page] | save [tag] [name] [x y z] | info|delete|goal|goto <name|tag> [@ date] | clear <tag> | restore <n>]",
                 "Manage this world's waypoints", [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     static std::vector<baritone::Waypoint> deleted;  // this session (Java deletedWaypoints)
                     std::string action = args.empty() ? "list" : lowerCase(args[0]);
                     auto describe = [](const baritone::Waypoint& w) {
                         std::time_t t = static_cast<std::time_t>(w.createdAt);
                         std::tm tm{};
                         localtime_s(&tm, &t);
                         char date[32];
                         std::strftime(date, sizeof(date), "%d/%m/%Y %H:%M", &tm);
                         return std::format("{} {} @ {} (date {})", w.tag, w.name.empty() ? "<empty>" : w.name, date, w.createdAt);
                     };
                     if (action == "list" || action == "get" || action == "l") {
                         std::string tag = args.size() > 1 ? baritone::WaypointStore::tagOf(args[1]) : "";
                         auto list = bot.waypoints().list(tag);
                         if (list.empty()) throw CommandError(tag.empty() ? "No waypoints" : "No waypoints with this tag");
                         std::vector<std::string> lines;
                         for (const auto& w : list) lines.push_back(describe(w));
                         std::size_t pageArg = tag.empty() ? 1 : 2;
                         paginate(lines, args.size() > pageArg ? args[pageArg] : "",
                                  tag.empty() ? "All waypoints:" : std::format("Waypoints tagged {}:", tag),
                                  "wp list" + (tag.empty() ? std::string() : " " + tag));
                         return;
                     }
                     if (action == "save" || action == "s") {
                         Args rest(args.begin() + 1, args.end());
                         std::string tag = "user";
                         if (!rest.empty() && baritone::WaypointStore::isTag(lowerCase(rest[0]))) {
                             tag = baritone::WaypointStore::tagOf(lowerCase(rest[0]));
                             rest.erase(rest.begin());
                         }
                         std::string name;
                         if (rest.size() == 1 || rest.size() == 4) {
                             name = rest[0];
                             rest.erase(rest.begin());
                         }
                         baritone::BlockPos pos = feetPos(player);
                         if (rest.size() == 3) {
                             pos = {parseCoord(rest[0], pos.x, "x"), parseCoord(rest[1], pos.y, "y"), parseCoord(rest[2], pos.z, "z")};
                         } else if (!rest.empty()) {
                             throw CommandError("Usage: #wp save [tag] [name] [x y z]");
                         }
                         baritone::Waypoint wp;
                         wp.tag = tag;
                         wp.name = name;
                         wp.pos = pos;
                         wp.dimension = platform::WorldTracker::get().dimension();
                         wp.createdAt = std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count();
                         bot.waypoints().add(wp);
                         chat::info("Waypoint added: " + describe(wp));
                         return;
                     }
                     if (action == "clear" || action == "c") {
                         if (args.size() != 2 || !baritone::WaypointStore::isTag(lowerCase(args[1]))) {
                             throw CommandError(std::format("Invalid tag: \"{}\"", args.size() > 1 ? args[1] : ""));
                         }
                         auto list = bot.waypoints().list(baritone::WaypointStore::tagOf(lowerCase(args[1])));
                         for (const auto& w : list) bot.waypoints().removeExact(w);
                         deleted.insert(deleted.end(), list.begin(), list.end());
                         chat::info(std::format("{} waypoints deleted (use #wp restore {} to restore them)", list.size(), list.size()));
                         return;
                     }
                     if (action == "restore") {
                         if (args.size() != 2) throw CommandError("Usage: #wp restore <count>");
                         int amount = std::min(static_cast<int>(deleted.size()), parseInt(args[1], "count"));
                         for (int i = 0; i < amount; ++i) {
                             bot.waypoints().add(deleted.back());
                             deleted.pop_back();
                         }
                         chat::info(std::format("{} waypoints restored", amount));
                         return;
                     }
                     bool info = action == "info" || action == "show" || action == "i";
                     bool del = action == "delete" || action == "d";
                     bool goal = action == "goal" || action == "g";
                     bool go = action == "goto";
                     if (!info && !del && !goal && !go) throw CommandError("Unknown action: use list, save, info, delete, clear, restore, goal or goto");
                     if (args.size() < 2) throw CommandError(std::format("Usage: #wp {} <name|tag> [@ date]", action));
                     std::string key = baritone::WaypointStore::isTag(lowerCase(args[1])) ? lowerCase(args[1]) : args[1];
                     auto found = bot.waypoints().matching(key);
                     std::optional<baritone::Waypoint> wp;
                     if (args.size() == 4 && args[2] == "@") {
                         int64_t date = std::stoll(args[3]);
                         for (const auto& w : found)
                             if (w.createdAt == date) wp = w;
                         if (!wp) throw CommandError("No waypoint matches the specified date");
                     } else if (found.empty()) {
                         throw CommandError("No waypoints found");
                     } else if (found.size() == 1) {
                         wp = found.front();
                     }
                     if (!wp) {
                         std::vector<std::string> lines;
                         for (const auto& w : found) lines.push_back(describe(w) + std::format("  -> #wp {} {} @ {}", action, args[1], w.createdAt));
                         paginate(lines, args.size() > 2 ? args[2] : "", "Multiple waypoints found:", std::format("wp {} {}", action, args[1]));
                         return;
                     }
                     if (info) {
                         chat::info(describe(*wp));
                         chat::info(std::format("Position: {}  (dimension {})", wp->pos, wp->dimension));
                         chat::info(std::format("To recreate it: #wp save {} {} {} {} {}", wp->tag, wp->name, wp->pos.x, wp->pos.y, wp->pos.z));
                     } else if (del) {
                         bot.waypoints().removeExact(*wp);
                         deleted.push_back(*wp);
                         chat::info("Waypoint deleted (use #wp restore 1 to restore it)");
                     } else {
                         if (wp->dimension != platform::WorldTracker::get().dimension()) throw CommandError("This waypoint is in another dimension");
                         auto target = std::make_shared<baritone::GoalBlock>(wp->pos);
                         if (goal) {
                             bot.baritone().customGoal().setGoal(target);
                             chat::info(std::format("Goal: {}", target->toString()));
                         } else {
                             bot.baritone().customGoal().setGoalAndPath(target);
                             chat::info(std::format("On the way to {}", target->toString()));
                         }
                     }
                 }});

    // ---- other Baritone goals ----
    manager.add({{"tunnel"}, "tunnel [height width depth]",
                 "Dig straight ahead (with dimensions: clear the area using the builder)",
                 [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     float y = baritone::Rotation::normalizeYaw(player.rotation()->rotation.y);
                     baritone::Direction dir = y >= -45 && y < 45     ? baritone::Direction::South
                                               : y >= 45 && y < 135   ? baritone::Direction::West
                                               : y >= -135 && y < -45 ? baritone::Direction::East
                                                                      : baritone::Direction::North;
                     baritone::BlockPos p = feetPos(player);
                     if (args.empty()) {
                         auto goal = std::make_shared<baritone::GoalStrictDirection>(p, dir);
                         chat::info(std::format("Tunnel: {}", goal->toString()));
                         bot.baritone().customGoal().setGoalAndPath(goal);
                         return;
                     }
                     if (args.size() != 3) throw CommandError("Usage: #tunnel [height width depth]");
                     int height = parseInt(args[0], "height"), width = parseInt(args[1], "width"),
                         depth = parseInt(args[2], "depth");
                     if (width < 1 || height < 2 || depth < 1 || height > 255) {
                         throw CommandError("Width and depth must be >= 1; height must be between 2 and 255");
                     }
                     // Baritone's TunnelCommand corners
                     int add = width % 2 == 0 ? 0 : 1;
                     baritone::BlockPos c1, c2;
                     switch (dir) {
                         case baritone::Direction::East:
                             c1 = {p.x, p.y, p.z - width / 2};
                             c2 = {p.x + depth, p.y + height - 1, p.z + width / 2 - 1 + add};
                             break;
                         case baritone::Direction::West:
                             c1 = {p.x, p.y, p.z + width / 2 - 1 + add};
                             c2 = {p.x - depth, p.y + height - 1, p.z - width / 2};
                             break;
                         case baritone::Direction::North:
                             c1 = {p.x - width / 2, p.y, p.z};
                             c2 = {p.x + width / 2 - 1 + add, p.y + height - 1, p.z - depth};
                             break;
                         default:
                             c1 = {p.x + width / 2 - 1 + add, p.y, p.z};
                             c2 = {p.x - width / 2, p.y + height - 1, p.z + depth};
                             break;
                     }
                     baritone::BlockPos origin{std::min(c1.x, c2.x), std::min(c1.y, c2.y), std::min(c1.z, c2.z)};
                     baritone::BlockPos size{std::abs(c1.x - c2.x) + 1, std::abs(c1.y - c2.y) + 1, std::abs(c1.z - c2.z) + 1};
                     bot.baritone().builder().build("tunnel", std::make_shared<baritone::FillSchematic>(size, baritone::DesiredBlock::air()),
                                                    origin);
                     chat::info(std::format("Tunnel: {} blocks high, {} wide and {} long", height, width, depth));
                 }});

    manager.add({{"axis", "highway"}, "axis", "Go to the nearest axis or diagonal (y = axisHeight)",
                 [&bot](const Args&) {
                     requirePlayer();
                     auto goal = std::make_shared<baritone::GoalAxis>(baritone::Settings::get().axisHeight);
                     bot.baritone().customGoal().setGoalAndPath(goal);
                     chat::info(std::format("Goal: {}", goal->toString()));
                 }});

    manager.add({{"surface", "top"}, "surface", "Go up to the surface", [&bot](const Args&) {
        auto& player = requirePlayer();
        auto snap = platform::WorldTracker::get().snapshot();
        if (!snap) throw CommandError("No world snapshot available");
        baritone::BlockStateInterface bsi(snap);
        baritone::BlockPos p = feetPos(player);
        constexpr int seaLevel = 63;
        if (p.y > seaLevel && bsi.props(p.x, p.y + 1, p.z).type == baritone::BlockType::Air) {
            chat::info("Already at the surface");
            return;
        }
        for (int y = std::max(p.y, seaLevel); y < snap->maxY(); ++y) {
            if (bsi.props(p.x, y, p.z).type != baritone::BlockType::Air && y > p.y) {
                // highest block of this column above us: stand on top of the last solid block
                int top = y;
                while (top + 1 < snap->maxY() && bsi.props(p.x, top + 1, p.z).type != baritone::BlockType::Air) ++top;
                if (top + 1 >= snap->maxY()) break;
                auto goal = std::make_shared<baritone::GoalBlock>(p.x, top + 1, p.z);
                chat::info(std::format("Heading to the surface: {}", goal->toString()));
                bot.baritone().customGoal().setGoalAndPath(goal);
                return;
            }
        }
        chat::info("No higher position found");
    }});

    manager.add({{"invert"}, "invert", "Invert the goal (move away from it)", [&bot](const Args&) {
        auto& custom = bot.baritone().customGoal();
        baritone::GoalPtr goal = custom.mostRecentGoal();
        if (!goal) throw CommandError("No goal to invert");
        if (auto inv = std::dynamic_pointer_cast<const baritone::GoalInverted>(goal)) {
            throw CommandError("The goal is already inverted");
        }
        custom.setGoalAndPath(std::make_shared<baritone::GoalInverted>(goal));
        chat::info("Goal inverted");
    }});

    // Java ETACommand
    manager.add({{"eta"}, "eta", "Estimated time remaining", [&bot](const Args&) {
        if (!bot.baritone().control().mostRecentInControl()) throw CommandError("No process running");
        double segment = bot.pathing().ticksRemainingInSegment().value_or(std::nan(""));
        double goal = bot.pathing().estimatedTicksToGoal().value_or(std::nan(""));
        chat::info(std::format("Next segment: {:.1f} s ({:.0f} ticks)", segment / 20, segment));
        chat::info(std::format("Goal: {:.1f} s ({:.0f} ticks)", goal / 20, goal));
    }});

    manager.add({{"version"}, "version", "Show the bot version", [](const Args&) {
        chat::info(std::format("Baritone Bedrock " BARITONE_BEDROCK_VERSION
                              " (port of cabaletta/baritone) - Minecraft Bedrock {}", gameversion::supported));
    }});

    manager.add({{"explore"}, "explore [x z]", "Explore chunks that have never been loaded, in a spiral", [&bot](const Args& args) {
        auto& player = requirePlayer();
        baritone::BlockPos p = feetPos(player);
        int x = args.size() >= 2 ? parseCoord(args[0], p.x, "x") : p.x;
        int z = args.size() >= 2 ? parseCoord(args[1], p.z, "z") : p.z;
        bot.baritone().explore().explore(x, z);
        chat::info(std::format("Exploring around {} {}", x, z));
    }});

    manager.add({{"farm"}, "farm [radius]", "Harvest mature crops and replant them", [&bot](const Args& args) {
        auto& player = requirePlayer();
        int range = args.empty() ? 0 : parseInt(args[0], "radius");
        bot.baritone().farm().farm(range, feetPos(player));
        chat::info(range > 0 ? std::format("Farming within a radius of {} blocks", range) : "Farming");
    }});

    // ---- cache (Baritone: find, repack, blacklist) ----
    manager.add({{"find"}, "find <block> [block...]", "Show known block locations (loaded chunks and cache)",
                 [](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #find <block> [block...]");
                     auto snap = platform::WorldTracker::get().snapshot();
                     if (!snap) throw CommandError("World has not been scanned yet");
                     baritone::BlockFilter filter(args);
                     baritone::BlockPos feet = feetPos(player);
                     auto found = baritone::searchWorld(*snap, filter, feet, 10, baritone::Settings::get().maxScanChunkRadius);
                     if (found.empty()) {
                         chat::info("No known " + filter.toString() + " found" +
                                    (std::ranges::any_of(filter.names(), [](const std::string& n) { return baritone::isTrackedBlock(n); })
                                         ? ""
                                         : " (block not tracked by the cache: only loaded chunks are searched)"));
                         return;
                     }
                     for (const auto& p : found) {
                         chat::info(std::format("{} {} {}  ({:.0f} blocks)", p.x, p.y, p.z, std::sqrt(static_cast<double>(p.distSq(feet)))));
                     }
                 }});

    manager.add({{"saveall"}, "saveall", "Save the long-distance cache to disk", [](const Args&) {
        platform::WorldTracker::get().saveCache();
        chat::info("Cache saved");
    }});

    manager.add({{"reloadall"}, "reloadall", "Reload the long-distance cache from disk", [](const Args&) {
        requirePlayer();
        platform::WorldTracker::get().reloadCache();
        chat::info("Cache reloaded");
    }});

    manager.add({{"click"}, "click [cancel|here]", "Choose a goal or drag a selection using the crosshair and mouse buttons",
                 [&bot](const Args& args) {
                     if (args.size() == 1 && args[0] == "cancel") {
                         bot.stopClickSelection();
                         chat::info("Click mode cancelled");
                         return;
                     }
                     requirePlayer();
                     if (args.empty()) { bot.startClickSelection(); return; }
                     if (args.size() != 1 || args[0] != "here") throw CommandError("Usage: #click [cancel|here]");
                     auto snap = platform::WorldTracker::get().snapshot();
                     if (!snap) throw CommandError("World has not been scanned yet");
                     baritone::BlockStateInterface bsi(snap);
                     auto hit = baritone::rayTrace(bsi, bot.player().eyePosition(), bot.player().rotation(), 512);
                     if (!hit) throw CommandError("No targeted block within 512 blocks");
                     auto goal = std::make_shared<baritone::GoalBlock>(hit->pos.above());
                     chat::info(std::format("Goal: {}", goal->toString()));
                     bot.baritone().customGoal().setGoalAndPath(goal);
                 }});

    manager.add({{"explorefilter"}, "explorefilter <file.json> [invert]",
                 "Mark chunks as already explored for #explore (JSON list of {\"x\":..,\"z\":..})",
                 [&bot](const Args& args) {
                     if (args.empty() || args.size() > 2) throw CommandError("Usage: #explorefilter <file.json> [invert]");
                     bool invert = args.size() == 2 && args[1] == "invert";
                     std::filesystem::path file = args[0];
                     if (file.is_relative()) file = paths::dataDir() / file;
                     std::ifstream in(file);
                     if (!in) throw CommandError("File not found: " + file.string());
                     std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                     // [{"x": 1, "z": -2}, ...]: chunk coordinates, like Baritone's JsonChunkFilter
                     std::unordered_set<int64_t> chunks;
                     auto number = [&](std::size_t from, std::size_t end, const char* key) -> std::optional<int> {
                         std::size_t k = text.find(std::string("\"") + key + "\"", from);
                         if (k == std::string::npos || k > end) return std::nullopt;
                         std::size_t colon = text.find(':', k);
                         if (colon == std::string::npos || colon > end) return std::nullopt;
                         std::size_t p = text.find_first_not_of(" \t\r\n", colon + 1);
                         if (p == std::string::npos) return std::nullopt;
                         int v = 0;
                         auto [ptr, ec] = std::from_chars(text.data() + p, text.data() + end, v);
                         if (ec != std::errc{}) return std::nullopt;
                         return v;
                     };
                     for (std::size_t open = text.find('{'); open != std::string::npos; open = text.find('{', open + 1)) {
                         std::size_t close = text.find('}', open);
                         if (close == std::string::npos) break;
                         auto x = number(open, close, "x");
                         auto z = number(open, close, "z");
                         if (x && z) chunks.insert(baritone::WorldSnapshot::key(*x, *z));
                     }
                     if (chunks.empty()) throw CommandError("No {\"x\":..,\"z\":..} chunks found in the file");
                     std::size_t n = chunks.size();
                     bot.baritone().explore().setFilter(std::move(chunks), invert);
                     chat::info(std::format("Exploration filter: {} chunks{}", n, invert ? " (inverted)" : ""));
                 }});

    manager.add({{"repack", "rescan"}, "repack", "Repack loaded chunks into the long-distance cache", [](const Args&) {
        requirePlayer();
        std::size_t n = platform::WorldTracker::get().repack();
        chat::info(n ? std::format("{} chunks cached", n) : "Cache disabled (chunkCaching) or world not scanned yet");
    }});

    manager.add({{"blacklist"}, "blacklist", "#goto <block>: blacklist the nearest matching block", [&bot](const Args&) {
        if (!bot.baritone().getToBlock().isActive()) throw CommandError("No #goto <block> running");
        if (!bot.baritone().getToBlock().blacklistClosest()) throw CommandError("No known block to blacklist");
        chat::info("Nearest matching block blacklisted");
    }});

    // ---- building (Baritone: sel, build, pause, resume) ----
    manager.add({{"sel", "selection", "s"},
                 "sel <pos1|pos2 [x y z] | clear | undo | set|walls|shell|sphere|hsphere <block> | cylinder|hcylinder <block> [axis] | "
                 "cleararea | replace <from...> <to> | copy|paste [x y z] | expand|contract|shift [a|newest|oldest] <dir> <n>>",
                 "Manage selections and build within them", [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #sel pos1 | pos2 | clear | undo | set | walls | shell | cleararea | replace | expand");
                     auto& selections = bot.baritone().selections();
                     auto& pending = bot.selectionPos1();
                     std::string action = args[0];
                     std::ranges::transform(action, action.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                     if (action == "pos1" || action == "p1" || action == "1" || action == "pos2" || action == "p2" || action == "2") {
                         baritone::BlockPos p = feetPos(player);
                         if (args.size() == 4) {
                             p = {parseCoord(args[1], p.x, "x"), parseCoord(args[2], p.y, "y"), parseCoord(args[3], p.z, "z")};
                         } else if (args.size() != 1) {
                             throw CommandError("Usage: #sel pos1 [x y z]");
                         }
                         if (action.back() == '1') {
                             pending = p;
                             chat::info(std::format("Position 1: {} {} {}", p.x, p.y, p.z));
                         } else {
                             if (!pending) throw CommandError("Set #sel pos1 first");
                             selections.add({*pending, p});
                             pending.reset();
                             chat::info(std::format("Selection {} added, ending at {} {} {}", selections.all().size(), p.x, p.y, p.z));
                         }
                         return;
                     }
                     if (action == "clear" || action == "c") {
                         selections.clear();
                         pending.reset();
                         chat::info("Selections cleared");
                         return;
                     }
                     if (action == "undo" || action == "u") {
                         // Java: undo pos1, else remove the last selection and keep its pos1
                         if (pending) {
                             pending.reset();
                             chat::info("pos1 undone");
                         } else {
                             if (selections.all().empty()) throw CommandError("Nothing to undo");
                             pending = selections.all().back().pos1;
                             selections.removeLast();
                             chat::info("pos2 undone");
                         }
                         return;
                     }
                     // Java copy / paste: the blocks of the selections, pasted relative to the player
                     static std::shared_ptr<baritone::CompositeSchematic> clipboard;
                     static baritone::BlockPos clipboardOffset;
                     if (action == "copy" || action == "cp" || action == "paste" || action == "p") {
                         baritone::BlockPos at = feetPos(player);
                         if (args.size() == 4) {
                             at = {parseCoord(args[1], at.x, "x"), parseCoord(args[2], at.y, "y"), parseCoord(args[3], at.z, "z")};
                         } else if (args.size() != 1) {
                             throw CommandError(std::format("Usage: #sel {} [x y z]", action));
                         }
                         if (action == "paste" || action == "p") {
                             if (!clipboard) throw CommandError("Copy a selection first (#sel copy)");
                             bot.baritone().builder().build("Fill", clipboard, at + clipboardOffset);
                             chat::info("Building");
                             return;
                         }
                         if (selections.all().empty()) throw CommandError("No selection");
                         auto snapshot = bot.baritone().world();
                         if (!snapshot) throw CommandError("World has not been scanned yet");
                         baritone::BlockStateInterface bsi(snapshot);
                         baritone::BlockPos lo = selections.all().front().min(), hi = selections.all().front().max();
                         for (const auto& sel : selections.all()) {
                             baritone::BlockPos a = sel.min(), b = sel.max();
                             lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
                             hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
                         }
                         std::vector<baritone::CompositeSchematic::Part> parts;
                         for (const auto& sel : selections.all()) {
                             baritone::BlockPos min = sel.min(), sz = sel.size();
                             std::vector<baritone::DesiredBlock> palette;
                             std::map<std::string, int> paletteIndex;
                             std::vector<int> indices(static_cast<std::size_t>(sz.x) * sz.y * sz.z);
                             for (int x = 0; x < sz.x; ++x)
                                 for (int y = 0; y < sz.y; ++y)
                                     for (int z = 0; z < sz.z; ++z) {
                                         const baritone::BlockProps& bp = bsi.props(min.x + x, min.y + y, min.z + z);
                                         baritone::DesiredBlock d = bp.type == baritone::BlockType::Air ? baritone::DesiredBlock::air()
                                                                                                        : baritone::DesiredBlock::block(bp.name);
                                         if (d.kind == baritone::DesiredBlock::Kind::Block) d.states = bp.states;
                                         std::string key = bp.name;
                                         for (const auto& [k, v] : bp.states) key += "|" + k + "=" + v;
                                         auto [it, inserted] = paletteIndex.try_emplace(key, static_cast<int>(palette.size()));
                                         if (inserted) palette.push_back(d);
                                         indices[(static_cast<std::size_t>(x) * sz.y + y) * sz.z + z] = it->second;
                                     }
                             parts.push_back({min - lo, std::make_shared<baritone::StaticSchematic>(sz, std::move(palette), std::move(indices))});
                         }
                         baritone::BlockPos size{hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1};
                         clipboard = std::make_shared<baritone::CompositeSchematic>(size, std::move(parts));
                         clipboardOffset = lo - at;
                         chat::info("Selection copied");
                         return;
                     }
                     if (selections.all().empty()) throw CommandError("No selection: use #sel pos1, then #sel pos2");
                     if (action == "ex") action = "expand";
                     if (action == "ct") action = "contract";
                     if (action == "sh") action = "shift";
                     if (action == "expand" || action == "contract" || action == "shift") {
                         // Baritone: #sel expand <a|all|n|newest|o|oldest> <direction> <blocks>
                         Args rest(args.begin() + 1, args.end());
                         std::string target = "newest";
                         if (rest.size() == 3) {
                             target = rest[0];
                             rest.erase(rest.begin());
                         }
                         if (rest.size() != 2) throw CommandError(std::format("Usage: #sel {} [a|newest|oldest] <direction> <blocks>", action));
                         std::string dir = rest[0];
                         std::ranges::transform(dir, dir.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                         int n = parseInt(rest[1], "blocks");
                         static const std::pair<const char*, baritone::Direction> dirs[] = {
                             {"north", baritone::Direction::North}, {"south", baritone::Direction::South},
                             {"east", baritone::Direction::East},   {"west", baritone::Direction::West},
                             {"up", baritone::Direction::Up},       {"down", baritone::Direction::Down}};
                         auto it = std::ranges::find_if(dirs, [&](const auto& d) { return dir == d.first; });
                         if (it == std::end(dirs)) throw CommandError("Direction: north, south, east, west, up or down");
                         std::vector<baritone::Selection*> targets;
                         auto& all = selections.all();
                         if (target == "a" || target == "all") {
                             for (auto& sel : all) targets.push_back(&sel);
                         } else if (target == "o" || target == "oldest") {
                             targets.push_back(&all.front());
                         } else if (target == "n" || target == "newest") {
                             targets.push_back(&all.back());
                         } else {
                             throw CommandError("Target: a (all), newest or oldest");
                         }
                         for (baritone::Selection* sel : targets) {
                             if (action == "expand") sel->expand(it->second, n);
                             else if (action == "contract") sel->expand(it->second, -n);
                             else sel->shift(it->second, n);
                         }
                         chat::info(std::format("{} selection(s) modified", targets.size()));
                         return;
                     }
                     auto desired = [](const std::string& name) {
                         const auto selector = baritone::BlockSelector::parse(name);
                         if (selector.name == "minecraft:air") return baritone::DesiredBlock::air();
                         auto block = baritone::DesiredBlock::block(selector.name);
                         block.states = selector.states;
                         return block;
                     };
                     std::function<baritone::SchematicPtr(baritone::BlockPos)> make;
                     bool sphere = action == "sphere" || action == "sph" || action == "hsphere" || action == "hsph";
                     bool cylinder = action == "cylinder" || action == "cyl" || action == "hcylinder" || action == "hcyl";
                     if (sphere || cylinder) {
                         if (args.size() < 2 || args.size() > (cylinder ? 3u : 2u)) {
                             throw CommandError(std::format("Usage: #sel {} <block>{}", action, cylinder ? " [x|y|z]" : ""));
                         }
                         auto block = desired(args[1]);
                         bool filled = action[0] != 'h';
                         char axis = args.size() == 3 ? static_cast<char>(std::tolower(static_cast<unsigned char>(args[2][0]))) : 'y';
                         if (cylinder && axis != 'x' && axis != 'y' && axis != 'z') throw CommandError("Axis: x, y or z");
                         make = [block, filled, sphere, axis](baritone::BlockPos size) -> baritone::SchematicPtr {
                             auto fill = std::make_shared<baritone::FillSchematic>(size, block);
                             return std::make_shared<baritone::MaskSchematic>(
                                 fill, sphere ? baritone::sphereMask(size, filled) : baritone::cylinderMask(size, filled, axis));
                         };
                     } else if (action == "set" || action == "fill" || action == "s" || action == "f" || action == "walls" ||
                                action == "w" || action == "shell" || action == "shl") {
                         if (args.size() != 2) throw CommandError(std::format("Usage: #sel {} <block>", action));
                         auto block = desired(args[1]);
                         if (action == "walls" || action == "w") {
                             make = [block](baritone::BlockPos size) { return std::make_shared<baritone::WallsSchematic>(size, block); };
                         } else if (action == "shell" || action == "shl") {
                             make = [block](baritone::BlockPos size) { return std::make_shared<baritone::ShellSchematic>(size, block); };
                         } else {
                             make = [block](baritone::BlockPos size) { return std::make_shared<baritone::FillSchematic>(size, block); };
                         }
                     } else if (action == "cleararea" || action == "ca") {
                         make = [](baritone::BlockPos size) {
                             return std::make_shared<baritone::FillSchematic>(size, baritone::DesiredBlock::air());
                         };
                     } else if (action == "replace" || action == "r") {
                         if (args.size() < 3) throw CommandError("Usage: #sel replace <from...> <to>");
                         std::unordered_set<std::string> from;
                         for (std::size_t i = 1; i + 1 < args.size(); ++i) from.insert(baritone::BlockFilter::normalize(args[i]));
                         auto to = desired(args.back());
                         make = [from, to](baritone::BlockPos size) {
                             return std::make_shared<baritone::ReplaceSchematic>(size, from, to);
                         };
                     } else {
                         throw CommandError("Unknown action: " + args[0]);
                     }
                     // one schematic per selection, in a box around all of them
                     baritone::BlockPos lo = selections.all().front().min(), hi = selections.all().front().max();
                     for (const auto& sel : selections.all()) {
                         baritone::BlockPos a = sel.min(), b = sel.max();
                         lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
                         hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
                     }
                     std::vector<baritone::CompositeSchematic::Part> parts;
                     for (const auto& sel : selections.all()) parts.push_back({sel.min() - lo, make(sel.size())});
                     baritone::BlockPos size{hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1};
                     bot.baritone().builder().build("selection " + action,
                                                    std::make_shared<baritone::CompositeSchematic>(size, std::move(parts)), lo);
                     chat::info(std::format("Building ({}) in {} selection(s)", action, selections.all().size()));
                 }});

    manager.add({{"build"}, "build [name] [x y z]",
                 "Build a file from the schematics folder (.mcstructure, .schem, .litematic; no arguments: list files)",
                 [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     auto dir = paths::dataDir() / "schematics";
                     std::error_code ec;
                     std::filesystem::create_directories(dir, ec);
                     if (args.empty()) {
                         std::string list;
                         for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
                             if (e.is_regular_file()) list += e.path().filename().string() + "  ";
                         }
                         chat::info(list.empty() ? "No schematics in " + dir.string() : "Schematics: " + list);
                         return;
                     }
                     std::filesystem::path file = dir / args[0];
                     if (!file.has_extension()) {
                         // Java: schematicFallbackExtension; then (Bedrock) the other known formats
                         std::vector<std::string> exts{"." + baritone::Settings::get().schematicFallbackExtension};
                         for (const char* e : {".mcstructure", ".schem", ".litematic", ".schematic"}) exts.push_back(e);
                         for (const std::string& ext : exts) {
                             std::filesystem::path candidate = file;
                             candidate += ext;
                             if (std::filesystem::exists(candidate)) {
                                 file = candidate;
                                 break;
                             }
                         }
                     }
                     if (!std::filesystem::exists(file)) throw CommandError("File not found: " + file.string());
                     baritone::BlockPos origin = feetPos(player);
                     if (args.size() == 4) {
                         origin = {parseCoord(args[1], origin.x, "x"), parseCoord(args[2], origin.y, "y"),
                                   parseCoord(args[3], origin.z, "z")};
                     } else if (args.size() != 1) {
                         throw CommandError("Usage: #build <name> [x y z]");
                     }
                     baritone::SchematicPtr schematic;
                     try {
                         schematic = baritone::loadSchematic(file.string());
                     } catch (const std::exception& e) {
                         throw CommandError(std::format("Could not read {}: {}", file.filename().string(), e.what()));
                     }
                     baritone::BlockPos s = schematic->size();
                     bot.baritone().builder().build(file.filename().string(), schematic, origin, true);
                     chat::info(std::format("Schematic {} loaded ({}x{}x{}), origin: {}", file.filename().string(), s.x, s.y, s.z, origin));
                 }});

    // Java ExecutionControlCommands
    manager.add({{"pause", "p", "paws"}, "pause", "Pause the bot until #resume", [&bot](const Args&) {
        if (bot.baritone().pauser().paused()) throw CommandError("Already paused");
        bot.baritone().pauser().setPaused(true);
        chat::info("Paused");
    }});

    manager.add({{"resume", "r", "unpause", "unpaws"}, "resume", "Resume the activity interrupted by #pause",
                 [&bot](const Args&) {
                     bot.baritone().builder().resume();
                     if (!bot.baritone().pauser().paused()) throw CommandError("Not paused");
                     bot.baritone().pauser().setPaused(false);
                     chat::info("Resumed");
                 }});

    manager.add({{"paused"}, "paused", "Show whether the bot is paused", [&bot](const Args&) {
        chat::info(bot.baritone().pauser().paused() ? "Paused" : "Not paused");
    }});

    // Java ElytraCommand: "elytra" flies to the most recent goal, "elytra reset|repack|supported".
    // Ours too: "elytra x z" / "elytra x y z".
    manager.add({{"elytra"}, "elytra [reset | repack | supported | x z | x y z]",
                 "Fly with elytra and fireworks to the last goal or given coordinates, then land",
                 [&bot](const Args& args) {
                     auto& elytra = bot.baritone().elytra();
                     if (args.size() == 1 && args[0] == "supported") {
                         chat::info("yes");  // Java: the native path finder could be missing; here it always exists
                         return;
                     }
                     if (args.size() == 1 && args[0] == "reset") {
                         if (!elytra.currentDestination()) throw CommandError("No elytra flight in progress");
                         elytra.resetState();
                         chat::info("State reset; still flying toward the same goal");
                         return;
                     }
                     if (args.size() == 1 && args[0] == "repack") {
                         platform::WorldTracker::get().markAllStale();
                         chat::info("All loaded chunks queued for rescanning");
                         return;
                     }
                     auto& player = requirePlayer();
                     baritone::BlockPos p = feetPos(player);
                     baritone::BlockPos dest;
                     bool knownY = false;
                     if (args.size() == 2) {
                         dest = {parseCoord(args[0], p.x, "x"), p.y, parseCoord(args[1], p.z, "z")};
                     } else if (args.size() == 3) {
                         dest = {parseCoord(args[0], p.x, "x"), parseCoord(args[1], p.y, "y"), parseCoord(args[2], p.z, "z")};
                         knownY = true;
                     } else if (args.empty()) {
                         const auto& s = baritone::Settings::get();
                         if (!s.elytraTermsAccepted) {
                             // Java gatekeep, without the seed part (no terrain prediction on Bedrock)
                             chat::info("Enable elytraTermsAccepted to hide this message");
                             chat::info("Baritone's elytra flight is experimental. It is designed for long trips in the Nether "
                                        "and also works in the Overworld, using fireworks for speed. It is incompatible "
                                        "with other speed mods (\"hacks\").");
                             chat::info(std::format("To let Baritone take off automatically from the ground: {}set elytraAutoJump true "
                                                    "(not recommended on a lagging server).", s.prefix));
                             chat::info(std::format("To fly more slowly: {}set elytraConserveFireworks true and/or lower "
                                                    "elytraFireworkSpeed (e.g. {}set elytraFireworkSpeed 0.6).", s.prefix, s.prefix));
                             chat::info("Bedrock: seed-based terrain prediction is unavailable (elytraNetherSeed / elytraPredictTerrain "
                                        "are ignored); the route is replanned as chunks load.");
                         }
                         baritone::GoalPtr goal = bot.baritone().customGoal().mostRecentGoal();
                         if (!goal) throw CommandError("No goal set");
                         if (auto error = elytra.pathTo(goal)) throw CommandError(*error);
                         int fireworks = bot.player().countItem("minecraft:firework_rocket");
                         if (fireworks == 0) chat::error("No fireworks: flight will be limited to gliding");
                         chat::info(std::format("Flying with elytra to {} ({} fireworks)", goal->toString(), fireworks));
                         return;
                     } else {
                         throw CommandError("Invalid action");
                     }
                     if (knownY && !elytra.isSupportedPos(dest)) throw CommandError("The goal must be within elytra flight bounds");
                     if (!elytra.isSupportedPos(p)) throw CommandError("The player must be within elytra flight bounds");
                     int fireworks = bot.player().countItem("minecraft:firework_rocket");
                     if (fireworks == 0) chat::error("No fireworks: flight will be limited to gliding");
                     elytra.start(dest, knownY);
                     chat::info(std::format("Flying with elytra to {} {} ({} fireworks)", dest.x, dest.z, fireworks));
                 }});

    manager.add({{"proc"}, "proc", "Show the current process", [&bot](const Args&) {
        auto& b = bot.baritone();
        baritone::Process* p = b.control().mostRecentInControl();
        if (!p) {
            chat::info("No active process");
            return;
        }
        const auto& cmd = b.control().mostRecentCommand();
        static constexpr const char* types[] = {"REQUEST_PAUSE", "CANCEL_AND_SET_GOAL", "REVALIDATE_GOAL_AND_PATH",
                                                "FORCE_REVALIDATE_GOAL_AND_PATH", "SET_GOAL_AND_PATH", "SET_GOAL_AND_PAUSE", "DEFER"};
        chat::info(std::format("{} (priority {}, temporary {}) - goal {}", p->displayName(), p->priority(),
                               p->isTemporary() ? "yes" : "no", cmd && cmd->goal ? cmd->goal->toString() : "none"));
        if (cmd) {
            auto t = static_cast<std::size_t>(cmd->type);
            chat::info(std::format("Last command: {}", t < std::size(types) ? types[t] : "?"));
        }
        chat::info("Path: " + b.pathing().debugState() + (b.pathing().isCalculating() ? " (calculating)" : ""));
    }});

    manager.add({{"thisway", "forward"}, "thisway <distance>", "Go straight in the direction you are looking",
                 [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #thisway <distance>");
                     double dist = parseDouble(args[0], "distance");
                     sdk::Vec3 f = player.feet();
                     auto goal = baritone::GoalXZ::fromDirection(f.x, f.z, player.rotation()->rotation.y, dist);
                     chat::info(std::format("Goal: {}", goal->toString()));
                     bot.baritone().customGoal().setGoalAndPath(goal);
                 }});

    manager.add({{"grab"}, "grab <0|1>", "Dev: force the cursor-grabbed flag", [](const Args& args) {
        requirePlayer();
        if (args.empty()) throw CommandError("Usage: #grab <0|1>");
        sdk::ClientInstance::get()->cursorGrabbedFlag() = args[0] == "1";
        chat::info(std::format("cursorGrabbed = {}", sdk::ClientInstance::get()->cursorGrabbedFlag()));
    }});

    manager.add({{"entities", "ent"}, "entities", "Dev: list loaded entities", [&bot](const Args&) {
        auto& player = requirePlayer();
        auto list = bot.player().entities();
        sdk::Vec3 me = player.feet();
        std::ranges::sort(list, [&](const auto& a, const auto& b) {
            auto d = [&](const baritone::EntityInfo& e) {
                double dx = e.position.x - me.x, dz = e.position.z - me.z;
                return dx * dx + dz * dz;
            };
            return d(a) < d(b);
        });
        chat::info(std::format("{} entities", list.size()));
        for (std::size_t i = 0; i < list.size() && i < 10; ++i) {
            const auto& e = list[i];
            chat::info(std::format("{} {} at {:.1f} {:.1f} {:.1f}", e.type, e.name, e.position.x, e.position.y, e.position.z));
        }
    }});

    manager.add({{"ism"}, "ism [slot...]", "Dev: show ItemStackNetManager state and network IDs", [](const Args& args) {
        auto& player = requirePlayer();
        void* mgr = mem::member<void*>(&player, offsets::Player_itemStackNetManager);
        logger::info("ItemStackNetManager = {}", mgr);
        if (!mem::isReadable(mgr, 0x90)) return;
        void* queue = mem::member<void*>(mgr, offsets::ItemStackNetManager_requestQueue);
        std::size_t queued = 0;
        if (mem::isReadable(queue, 0x18)) {
            auto first = mem::member<uintptr_t>(queue, 0), last = mem::member<uintptr_t>(queue, 8);
            queued = (last - first) / 8;
        }
        chat::info(std::format("enabled={} current={} queue={} ({} pending)",
                               mem::member<uint8_t>(mgr, offsets::ItemStackNetManager_enabled),
                               mem::member<void*>(mgr, offsets::ItemStackNetManager_currentRequest), queue, queued));
        hexDump("ItemStackNetManager", mgr, 0x90);
        auto* inv = player.supplies()->inventory();
        for (const auto& a : args) {
            int slot = parseInt(a, "slot");
            const sdk::ItemStack* st = inv->getItem(slot);
            if (!st) continue;
            auto* p = reinterpret_cast<const uint8_t*>(st) + offsets::ItemStack_netIdVariant;
            chat::info(std::format("slot {}: netId index {} values {} / {}", slot, p[0x10],
                                   *reinterpret_cast<const int32_t*>(p), *reinterpret_cast<const int32_t*>(p + 8)));
            hexDump("ItemStack", st, 0xA0);
        }
    }});

    manager.add({{"swap"}, "swap <slot> <slot> [local|server]",
                 "Dev: swap inventory slots (0-8 hotbar, 9-35 main inventory); local/server: one side only",
                 [&bot](const Args& args) {
                     requirePlayer();
                     if (args.size() < 2 || args.size() > 3) throw CommandError("Usage: #swap <slot> <slot> [local|server]");
                     int a = parseInt(args[0], "slot"), b = parseInt(args[1], "slot");
                     std::string mode = args.size() == 3 ? args[2] : "";
                     bot.runInTick([&bot, a, b, mode] {
                         auto* p = sdk::ClientInstance::get() ? sdk::ClientInstance::get()->localPlayer() : nullptr;
                         if (!p) return;
                         bool ok = mode.empty()      ? bot.player().swapSlots(a, b)
                                   : mode == "local" ? sdk::applyInventoryMoveLocally(*p, a, b)
                                                     : sdk::sendInventoryMove(*p, a, b) != 0;
                         chat::info(ok ? "Done" : "Failed");
                     });
                 }});

    manager.add({{"inv", "hotbar"}, "inv [all]", "Dev: show hotbar contents (all: entire inventory)", [&bot](const Args& args) {
        requirePlayer();
        auto& p = bot.player();
        int slots = !args.empty() && args[0] == "all" ? 36 : 9;
        for (int i = 0; i < slots; ++i) {
            auto s = p.hotbar(i);
            if (s.empty()) continue;
            std::string extra;
            if (s.maxDamage > 0) extra += std::format(" [damage {}/{}]", s.damage, s.maxDamage);
            if (s.efficiency) extra += std::format(" [efficiency {}]", s.efficiency);
            if (s.silkTouch) extra += " [silk touch]";
            chat::info(std::format("{}{}: {} x{}{}{}{}", i == p.selectedSlot() ? ">" : " ", i, s.name, s.count,
                                   s.isBlock ? " [block]" : "",
                                   s.tool != baritone::Tool::None ? std::format(" [tool tier {} speed {}]", s.toolTier, s.toolSpeed) : "",
                                   extra));
        }
    }});

    manager.add({{"break"}, "break <dx> <dy> <dz>", "Dev: break a block (relative to feet)", [&bot](const Args& args) {
        auto& player = requirePlayer();
        if (args.size() < 3) throw CommandError("Usage: #break <dx> <dy> <dz>");
        baritone::BlockPos p = feetPos(player).offset(parseInt(args[0], "dx"), parseInt(args[1], "dy"), parseInt(args[2], "dz"));
        chat::info(std::format("Breaking {}", p));
        bot.startTestAction(Bot::TestAction::Break, p);
    }});

    manager.add({{"place"}, "place <dx> <dy> <dz>", "Dev: place a block (relative to feet)", [&bot](const Args& args) {
        auto& player = requirePlayer();
        if (args.size() < 3) throw CommandError("Usage: #place <dx> <dy> <dz>");
        baritone::BlockPos p = feetPos(player).offset(parseInt(args[0], "dx"), parseInt(args[1], "dy"), parseInt(args[2], "dz"));
        chat::info(std::format("Placing at {}", p));
        bot.startTestAction(Bot::TestAction::Place, p);
    }});

    manager.add({{"cost"}, "cost", "Dev: show movement costs from the player's position", [&bot](const Args&) {
        auto& player = requirePlayer();
        auto snap = platform::WorldTracker::get().snapshot();
        if (!snap) throw CommandError("No world snapshot available");
        baritone::BlockPos p = feetPos(player);
        baritone::CalculationContext ctx(snap, bot.inventory().abilities(), false);
        namespace h = baritone::helper;
        auto describe = [&](int dy) {
            const auto& b = ctx.props(p.x, p.y + dy, p.z);
            return std::format("y{:+d} {} (cube={} top={} h={:.2f} walkOn={} through={} mine={:.1f} avoid={})", dy,
                               b.name, b.fullCube, b.solidTop, b.height, h::canWalkOn(ctx, p.x, p.y + dy, p.z, b),
                               h::canWalkThrough(ctx, p.x, p.y + dy, p.z, b),
                               h::getMiningDurationTicks(ctx, p.x, p.y + dy, p.z, b, false),
                               h::avoidBreaking(ctx, p.x, p.y + dy, p.z, b));
        };
        for (int dy : {1, 0, -1, -2}) chat::info(describe(dy));
        baritone::MoveResult res;
        for (const auto& move : baritone::allMoves()) {
            res.reset();
            move.apply(move, ctx, p.x, p.y, p.z, res);
            if (res.cost < baritone::costs::COST_INF) {
                logger::info("  {} -> {} {} {} cost {:.2f}", move.name, res.x, res.y, res.z, res.cost);
            }
        }
        chat::info(std::format("downward = {:.2f}", baritone::movement_costs::downward(ctx, p.x, p.y, p.z)));
    }});

    manager.add({{"ecsdump"}, "ecsdump <Component> [bytes]", "Dev: log the raw contents of a player's EnTT component",
                 [](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #ecsdump <ComponentName> [bytes]");
                     int bytes = args.size() > 1 ? parseInt(args[1], "bytes") : 0x100;
                     const auto* storage = std::as_const(player.entityContext().registry).storage(componentHash(args[0]));
                     if (!storage || !storage->contains(player.entityContext().entity)) throw CommandError("Component not present");
                     const void* value = storage->value(player.entityContext().entity);
                     if (!value) throw CommandError("Empty component (flag)");
                     hexDump(args[0], value, bytes);
                     chat::info(std::format("{} at {}: written to log.txt", args[0], value));
                 }});

    manager.add({{"ecs"}, "ecs <Component...>", "Dev: check whether the player has these EnTT components (e.g. IsDeadFlagComponent)",
                 [](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage: #ecs <ComponentName> [...]");
                     for (const std::string& name : args) {
                         uint32_t hash = componentHash(name);
                         const auto* storage = std::as_const(player.entityContext().registry).storage(hash);
                         chat::info(std::format("{} (0x{:08X}): {}", name, hash,
                                                !storage ? "no storage" : storage->contains(player.entityContext().entity)
                                                                                  ? "YES"
                                                                                  : "no"));
                     }
                 }});

    manager.add({{"world"}, "world", "Dev: show world snapshot state", [](const Args&) {
        auto& tracker = platform::WorldTracker::get();
        auto snap = tracker.snapshot();
        auto* client = sdk::ClientInstance::get();
        auto& player = requirePlayer();
        sdk::BlockPos feet = sdk::BlockPos::floor(player.feet());
        bool here = client->region()->hasChunksAt(feet, 0);
        bool distant = client->region()->hasChunksAt(feet.offset(100000, 0, 100000), 0);
        chat::info(std::format("Dimension {}: {} chunks, {} block states, last scan {:.1f} ms",
                               tracker.dimension(), snap ? snap->chunkCount() : 0, tracker.knownStates(),
                               tracker.lastChunkScanMs()));
        chat::info(std::format("hasChunksAt here={} far={}  height {}..{}", here, distant, client->region()->minHeight(),
                               client->region()->maxHeight()));
        if (snap) {
            baritone::BlockStateInterface bsi(snap);
            const auto& below = bsi.props(feet.x, feet.y - 1, feet.z);
            chat::info(std::format("Snapshot below feet: {} (fullCube={} hardness={})", below.name,
                                   below.fullCube, below.hardness));
        }
        if (auto cache = tracker.cache()) {
            chat::info(std::format("Chunk packer: {} queued (limit {})", tracker.pendingPacks(), baritone::Settings::get().chunkPackerQueueMaxSize));
            chat::info(std::format("Long-distance cache: {} regions in memory, {} chunks", cache->loadedRegions(),
                                   cache->cachedChunks()));
        } else {
            chat::info("Long-distance cache: disabled (chunkCaching)");
        }
    }});

    manager.add({{"calc"}, "calc <x> [y] <z>", "Dev: calculate a path without moving (using the player's abilities)", [](const Args& args) {
        if (args.size() != 2 && args.size() != 3) throw CommandError("Usage: #calc <x> [y] <z>");
        auto& player = requirePlayer();
        sdk::BlockPos feet = sdk::BlockPos::floor(player.feet());
        baritone::BlockPos start{feet.x, feet.y, feet.z};
        baritone::GoalPtr goal;
        if (args.size() == 3) {
            goal = std::make_shared<baritone::GoalBlock>(baritone::BlockPos{
                parseCoord(args[0], feet.x, "x"), parseCoord(args[1], feet.y, "y"), parseCoord(args[2], feet.z, "z")});
        } else {
            goal = std::make_shared<baritone::GoalXZ>(parseCoord(args[0], feet.x, "x"), parseCoord(args[1], feet.z, "z"));
        }
        auto snap = platform::WorldTracker::get().snapshot();
        if (!snap) throw CommandError("No world snapshot available");
        chat::info(std::format("Calculating a path from {} to {}...", start, goal->toString()));
        baritone::PlayerAbilities abilities = Bot::get().baritone().inventory().abilities();
        std::thread([snap, start, goal, abilities] {
            auto t0 = std::chrono::steady_clock::now();
            auto ctx = std::make_unique<baritone::CalculationContext>(snap, abilities, true);
            baritone::AStarPathFinder finder(start, goal, std::move(ctx), {});
            auto result = finder.calculate(2000, 5000);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!result.path) {
                chat::error(std::format("No path ({:.0f} ms, type {}): {}", ms, static_cast<int>(result.type),
                                        result.error));
                return;
            }
            const char* kind = result.type == baritone::PathResultType::SuccessToGoal ? "complete" : "segment";
            chat::info(std::format("Path {}: {} positions, estimated {:.1f} s, {} nodes, {:.0f} ms", kind,
                                   result.path->length(), result.path->totalTicks() / 20.0,
                                   result.path->numNodesConsidered(), ms));
            std::string moves;
            for (const auto& m : result.path->movements()) {
                static constexpr const char* names[] = {"T", "A", "D", "F", "Di", "P", "Dw", "Pk"};
                moves += names[static_cast<int>(m.type)];
                moves += ' ';
            }
            logger::info("path: {}", moves);
        }).detach();
    }});

    manager.add({{"nbt"}, "nbt [dx dy dz]", "Dev: show block states (relative to feet)", [](const Args& args) {
        auto* client = sdk::ClientInstance::get();
        auto& player = requirePlayer();
        sdk::BlockPos pos = sdk::BlockPos::floor(player.feet());
        if (args.size() >= 3) pos = pos.offset(parseInt(args[0], "dx"), parseInt(args[1], "dy"), parseInt(args[2], "dz"));
        const sdk::Block* block = client->region()->getBlock(pos);
        const auto& tag = block->serializationId();
        if (!mem::isReadable(&tag, sizeof(tag)) || tag.tags.size() > 64) throw CommandError("Unreadable CompoundTag");
        chat::info(std::format("{} : {}", pos, sdk::nbt::toString(tag)));
    }});

    manager.add({{"hex"}, "hex <block|legacy|region> [size] [dx dy dz]", "Dev: dump an object's memory",
                 [](const Args& args) {
                     if (args.empty()) throw CommandError("Usage: #hex <object> [size] [dx dy dz]");
                     int size = args.size() > 1 ? parseInt(args[1], "size") : 0x100;
                     auto* client = sdk::ClientInstance::get();
                     auto& player = requirePlayer();
                     sdk::BlockPos pos = sdk::BlockPos::floor(player.feet()).below();
                     if (args.size() >= 5) {
                         pos = pos.offset(parseInt(args[2], "dx"), parseInt(args[3], "dy") + 1, parseInt(args[4], "dz"));
                     }
                     const void* obj = nullptr;
                     if (args[0] == "block") obj = client->region()->getBlock(pos);
                     else if (args[0] == "legacy") obj = client->region()->getBlock(pos)->legacy();
                     else if (args[0] == "region") obj = client->region();
                     else throw CommandError("Unknown object");
                     hexDump(std::format("{} at {}", args[0], pos), obj, size);
                     chat::info("Dump written to log.txt");
                 }});

    manager.add({{"hexat"}, "hexat <hex address> [size]", "Dev: dump memory at an address (read-only)",
                 [](const Args& args) {
                     if (args.empty()) throw CommandError("Usage: #hexat <address> [size]");
                     uintptr_t addr = 0;
                     try {
                         addr = static_cast<uintptr_t>(std::stoull(args[0], nullptr, 16));
                     } catch (...) {
                         throw CommandError("Invalid address");
                     }
                     int size = args.size() > 1 ? parseInt(args[1], "size") : 0x80;
                     if (!mem::isReadable(reinterpret_cast<const void*>(addr), 8)) throw CommandError("Unreadable address");
                     hexDump(args[0], reinterpret_cast<const void*>(addr), size);
                     chat::info("Dump written to log.txt");
                 }});

    manager.add({{"quiet"}, "quiet <on|off>", "Dev: hide bot messages in chat (they remain in log.txt)",
                 [](const Args& args) {
                     bool on = args.empty() || args[0] == "on";
                     if (!on) chat::setQuiet(false);
                     chat::info(on ? "Chat muted (messages in log.txt)" : "Chat unmuted");
                     if (on) chat::setQuiet(true);
                 }});

    manager.add({{"attrs"}, "attrs", "Dev: log the player's default and current attributes",
                 [](const Args&) {
                     auto& player = requirePlayer();
                     auto* c = static_cast<const uint8_t*>(player.entityContext().raw(componentHash("AttributesComponent")));
                     if (!c) throw CommandError("AttributesComponent not present");
                     auto begin = mem::member<const uint8_t*>(c, offsets::Attributes_instances);
                     auto end = mem::member<const uint8_t*>(c, offsets::Attributes_instances + 8);
                     for (const uint8_t* inst = begin; inst && inst < end && mem::isReadable(inst, offsets::AttributeInstance_size);
                          inst += offsets::AttributeInstance_size) {
                         auto attr = mem::member<const uint8_t*>(inst, offsets::AttributeInstance_attribute);
                         if (!mem::isReadable(attr, 0x30)) continue;
                         const std::string& n = mem::member<std::string>(attr, offsets::Attribute_name);
                         auto f = reinterpret_cast<const float*>(inst + 0x68);
                         logger::info("attr {}: default {} {} {} current {} {} {}", n, f[0], f[1], f[2], f[3], f[4], f[5]);
                     }
                     chat::info("Attributes written to log.txt");
                 }});

    manager.add({{"slot"}, "slot <0-8>", "Dev: select a hotbar slot",
                 [&bot](const Args& args) {
                     if (args.empty()) throw CommandError("Usage: #slot <0-8>");
                     int slot = parseInt(args[0], "slot");
                     if (slot < 0 || slot > 8) throw CommandError("Slot must be between 0 and 8");
                     requirePlayer();
                     bot.player().selectSlot(slot);
                     chat::info(std::format("Slot {}: {}", slot, bot.player().hotbar(slot).name));
                 }});

    manager.add({{"vt"}, "vt <region|block|legacy|player|gamemode|level|inventory|supplies|item|stack> [count]", "Dev: log a vtable",
                 [&bot](const Args& args) {
                     if (args.empty()) throw CommandError("Usage: #vt <object> [count]");
                     int count = args.size() > 1 ? parseInt(args[1], "count") : 64;
                     auto* client = sdk::ClientInstance::get();
                     auto& player = requirePlayer();
                     const void* obj = nullptr;
                     sdk::BlockPos below = sdk::BlockPos::floor(player.feet()).below();
                     if (args[0] == "region") obj = client->region();
                     else if (args[0] == "block") obj = client->region()->getBlock(below);
                     else if (args[0] == "legacy") obj = client->region()->getBlock(below)->legacy();
                     else if (args[0] == "player") obj = &player;
                     else if (args[0] == "gamemode") obj = player.gameMode();
                     else if (args[0] == "level") obj = bot.level();
                     else if (args[0] == "inventory") obj = player.supplies()->inventory();
                     else if (args[0] == "supplies") obj = player.supplies();
                     else if (args[0] == "item" || args[0] == "stack") {
                         auto* stack = player.supplies()->inventory()->getItem(player.supplies()->selectedSlot());
                         if (!stack || stack->empty()) throw CommandError("Nothing held");
                         obj = args[0] == "item" ? static_cast<const void*>(stack->item()) : static_cast<const void*>(stack);
                     } else throw CommandError("Unknown object");
                     dumpVtable(args[0], obj, count);
                     chat::info("Vtable written to log.txt");
                 }});

    // Java HelpCommand: "help [page]" lists the commands, "help <command>" details one
    manager.add({{"complete"}, "complete [command or setting prefix...]", "Suggest commands, settings and arguments", [&manager](const Args& args) {
        std::string input;
        for (const auto& word : args) input += (input.empty() ? "" : " ") + word;
        if (!args.empty() && ((args.size() == 1 && manager.find(args[0])) || baritone::findSetting(args.back()) ||
            (args[0] == "set" && (args.back() == "toggle" || args.back() == "reset" || args.back() == "load" || args.back() == "save")) ||
            (args[0] == "follow" && (args.back() == "player" || args.back() == "entity")) ||
            (args[0] == "wp" && args.size() == 2))) input += ' ';
        const auto suggestions = manager.complete(input);
        if (suggestions.empty()) { chat::info("No completions"); return; }
        chat::info(std::format("{} suggestions:", suggestions.size()));
        std::string line;
        std::size_t shown = 0;
        for (const auto& suggestion : suggestions) {
            line += (line.empty() ? "" : ", ") + suggestion;
            if (++shown % 8 == 0) { chat::info(line); line.clear(); }
            if (shown == 64) break;
        }
        if (!line.empty()) chat::info(line);
        if (suggestions.size() > shown) chat::info("Type a longer prefix to narrow the suggestions.");
    }});

    manager.add({{"help", "?"}, "help [page|command]", "List commands", [&manager](const Args& args) {
        const std::string& prefix = baritone::Settings::get().prefix;
        if (!args.empty() && !isNumber(args[0])) {
            const Command* cmd = manager.find(args[0]);
            if (!cmd) throw CommandError(std::format("Unknown command: {}", args[0]));
            chat::info(std::format("{} - {}", cmd->names.front(), cmd->description));
            chat::info(std::format("Usage: {}{}", prefix, cmd->usage));
            if (cmd->names.size() > 1) {
                std::string aliases;
                for (std::size_t i = 1; i < cmd->names.size(); ++i) aliases += (i > 1 ? ", " : "") + cmd->names[i];
                chat::info(std::format("Aliases: {}", aliases));
            }
            return;
        }
        std::vector<std::string> lines;
        for (const Command& cmd : manager.commands()) lines.push_back(std::format("{}{} - {}", prefix, cmd.names.front(), cmd.description));
        paginate(lines, args.empty() ? "" : args[0], "All commands:", "help");
    }});

    manager.add({{"debug", "info"}, "debug", "Show data read from the game (check offsets)",
                 [](const Args&) { debugInfo(); }});

    manager.add({{"walk"}, "walk <ticks> [sprint]", "Test: walk straight ahead for N ticks",
                 [&bot](const Args& args) {
                     if (args.empty()) throw CommandError("Usage: #walk <ticks> [sprint]");
                     int ticks = parseInt(args[0], "ticks");
                     bool sprint = args.size() > 1 && args[1] == "sprint";
                     requirePlayer();
                     bot.walkFor(ticks, sprint);
                     chat::info(std::format("Walking for {} ticks{}", ticks, sprint ? " while sprinting" : ""));
                 }});

    manager.add({{"look"}, "look <yaw> <pitch>", "Test: force the player's rotation", [&bot](const Args& args) {
        if (args.size() < 2) throw CommandError("Usage: #look <yaw> <pitch>");
        bot.lookAt(static_cast<float>(parseDouble(args[0], "yaw")), static_cast<float>(parseDouble(args[1], "pitch")));
        chat::info("Rotation forced (use #stop to stop)");
    }});

    manager.add({{"stop", "cancel", "c"}, "stop", "Stop all bot activity", [&bot](const Args&) {
        bot.cancelAll();
        chat::info("Stopped.");
    }});

    // Java ForceCancelCommand
    manager.add({{"forcecancel"}, "forcecancel", "Cancel everything, including the current movement", [&bot](const Args&) {
        bot.cancelAll();
        chat::info("OK, forced cancellation");
    }});

    // Java ComeCommand: to the camera entity (the player itself on Bedrock: no free camera)
    manager.add({{"come"}, "come", "Come to the camera position (the player)", [&bot](const Args&) {
        auto& player = requirePlayer();
        bot.baritone().customGoal().setGoalAndPath(std::make_shared<baritone::GoalBlock>(feetPos(player)));
        chat::info("On my way");
    }});

    // Java GcCommand: free memory (here: the cached regions far from the player)
    manager.add({{"gc"}, "gc", "Free memory (cached regions far from the player)", [](const Args&) {
        auto cache = platform::WorldTracker::get().cache();
        auto* player = sdk::ClientInstance::get() ? sdk::ClientInstance::get()->localPlayer() : nullptr;
        if (cache && player) {
            sdk::BlockPos feet = sdk::BlockPos::floor(player->feet());
            cache->prune(feet.x >> 4, feet.z >> 4, 1);
        }
        chat::info("OK, memory freed");
    }});

    // Java RenderCommand: the game redraws the chunks; here the bot's own copy of the world is
    // read again (what it draws over the world comes from it)
    manager.add({{"render"}, "render", "Rescan all loaded chunks in the bot's world snapshot", [](const Args&) {
        platform::WorldTracker::get().markAllStale();
        chat::info("Done");
    }});

    // Java LitematicaCommand / SchematicaCommand: build what those mods have open. They don't
    // exist on Bedrock: the files are built with #build.
    manager.add({{"litematica"}, "litematica [number]", "Litematica is unavailable on Bedrock: use #build <file.litematic>",
                 [](const Args&) {
                     throw CommandError("Litematica is unavailable on Bedrock. Put the .litematic file in the schematics folder and type #build <name>");
                 }});
    manager.add({{"schematica"}, "schematica", "Schematica is unavailable on Bedrock: use #build <file.schematic>",
                 [](const Args&) {
                     throw CommandError("Schematica is unavailable on Bedrock. Put the .schematic file in the schematics folder and type #build <name>");
                 }});

    manager.add({{"unfault"}, "unfault", "Re-enable the bot after a caught error", [](const Args&) {
        safe::resetFault();
        chat::info("Bot re-enabled.");
    }});

    // Java SetCommand (+ the "modified" and "reset" aliases)
    manager.add({{"set", "setting", "settings"},
                 "set [list|modified [filter] [page]] | set <name> [value] | set toggle <name> | set reset <name|all> | set save|load [file]",
                 "Show or change a setting (same names as Baritone)", [&bot](const Args& args) {
                     using namespace baritone;
                     std::string arg = args.empty() ? "list" : lowerCase(args[0]);
                     if (arg == "s" || arg == "save") {
                         if (args.size() > 2) throw CommandError("Usage: #set save [file]");
                         auto file = paths::dataDir() / (args.size() == 2 ? args[1] : "settings.txt");
                         if (!baritone::saveSettings(file.string())) throw CommandError("Could not write settings: " + file.string());
                         chat::info("Settings saved to " + file.string());
                         return;
                     }
                     if (arg == "load" || arg == "ld") {
                         if (args.size() > 2) throw CommandError("Usage: #set load [file]");
                         auto file = paths::dataDir() / (args.size() == 2 ? args[1] : "settings.txt");
                         int loaded = baritone::loadSettings(file.string(), true);
                         if (loaded < 0) throw CommandError("Could not read settings: " + file.string());
                         chat::info(std::format("Loaded {} settings from {}", loaded, file.string()));
                         return;
                     }
                     bool viewModified = arg == "m" || arg == "mod" || arg == "modified";
                     bool viewAll = arg == "all" || arg == "l" || arg == "list";
                     if (viewModified || viewAll) {
                         std::string search;
                         std::size_t next = 1;
                         if (args.size() > next && !isNumber(args[next])) search = lowerCase(args[next++]);
                         std::vector<const SettingInfo*> list;
                         for (const SettingInfo& st : settingsTable()) {
                             if (viewModified && !isModified(st)) continue;
                             if (!search.empty() && lowerCase(st.name).find(search) == std::string::npos) continue;
                             list.push_back(&st);
                         }
                         std::ranges::sort(list, [](const SettingInfo* a, const SettingInfo* b) { return lowerCase(a->name) < lowerCase(b->name); });
                         std::vector<std::string> lines;
                         for (const SettingInfo* st : list) {
                             lines.push_back(std::format("{} = {}", st->name, settingValueToString(st->ptr)));
                         }
                         std::string header = std::format("All settings{}{}:", viewModified ? " modified" : "",
                                                          search.empty() ? "" : std::format(" containing '{}'", search));
                         paginate(lines, args.size() > next ? args[next] : "", header,
                                  std::format("set {}{}", arg, search.empty() ? "" : " " + search));
                         return;
                     }
                     bool resetting = arg == "reset";
                     bool toggling = arg == "toggle";
                     if (resetting) {
                         if (args.size() < 2) {
                             chat::info("Specify 'all' to confirm resetting everything to defaults");
                             chat::info("ALL settings will be reset to defaults. Use #set modified or #modified to see what will be reset.");
                             chat::info("Specify a setting name instead of 'all' to reset just one setting");
                             return;
                         }
                         if (lowerCase(args[1]) == "all") {
                             for (const SettingInfo& st : settingsTable())
                                 if (isModified(st)) resetSetting(st);
                             chat::info("All settings reset to defaults");
                             bot.saveSettings();
                             return;
                         }
                     }
                     if (toggling && args.size() < 2) throw CommandError("Usage: #set toggle <name>");
                     const std::string& settingName = (resetting || toggling) ? args[1] : args[0];
                     const SettingInfo* setting = findSetting(settingName);
                     if (!setting) throw CommandError(std::format("Unknown setting: {}", settingName));
                     bool doingSomething = resetting || toggling;
                     if (!doingSomething && args.size() == 1) {
                         chat::info(std::format("Value of setting {}:", setting->name));
                         chat::info(settingValueToString(setting->ptr));
                         chat::info(std::format("({})", setting->description));
                         return;
                     }
                     std::string oldValue = settingValueToString(setting->ptr);
                     if (resetting) {
                         resetSetting(*setting);
                     } else if (toggling) {
                         auto* b = std::get_if<bool*>(&setting->ptr);
                         if (!b) throw CommandError(std::format("{} is not a true/false setting", setting->name));
                         **b = !**b;
                         chat::info(std::format("Setting {} toggled to {}", setting->name, **b ? "true" : "false"));
                     } else {
                         std::string value = args[1];
                         for (std::size_t i = 2; i < args.size(); ++i) value += "," + args[i];
                         try {
                             parseSettingValue(*setting, value);
                         } catch (const std::invalid_argument& e) {
                             throw CommandError(e.what());
                         }
                     }
                     if (!toggling) {
                         chat::info(std::format("{} {} to {}", resetting ? "Reset" : "Set", setting->name, settingValueToString(setting->ptr)));
                     }
                     chat::info(std::format("Previous value: {}", oldValue));
                     const Settings& now = Settings::get();
                     if ((setting->name == "chatControl" && !now.chatControl && !now.chatControlAnyway) ||
                         (setting->name == "chatControlAnyway" && !now.chatControlAnyway && !now.chatControl)) {
                         chat::error("Warning: commands without a prefix will no longer work. Use the prefix to re-enable them.");
                     } else if (setting->name == "prefixControl" && !now.prefixControl) {
                         chat::error("Warning: commands with a prefix will no longer work. Use a command without the prefix (chatControl) to re-enable them.");
                     }
                     bot.saveSettings();
                 }});

    manager.add({{"modified", "mod", "baritone", "modifiedsettings"}, "modified", "List modified settings",
                 [&manager](const Args& args) {
                     std::string line = "set modified";
                     for (const auto& a : args) line += " " + a;
                     manager.runCommand(line, false);
                 }});

    manager.add({{"reset"}, "reset [all|name]", "Reset all settings or a single setting to defaults", [&manager](const Args& args) {
        std::string line = "set reset";
        for (const auto& a : args) line += " " + a;
        manager.runCommand(line, false);
    }});

    manager.add({{"unload"}, "unload", "Unload the DLL cleanly", [&bot](const Args&) {
        chat::info("Dechargement...");
        bot.cancelAll();
        bot.requestUnload();
    }});
}

}  // namespace command
