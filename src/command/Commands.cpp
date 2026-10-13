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
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Safe.h"
#include "sdk/Game.h"

namespace command {
namespace {

sdk::LocalPlayer& requirePlayer() {
    auto* client = sdk::ClientInstance::get();
    auto* player = client ? client->localPlayer() : nullptr;
    if (!player) throw CommandError("Pas de joueur (es-tu dans un monde ?)");
    return *player;
}

std::string blockName(const sdk::Block* block) {
    if (!mem::isReadable(block, offsets::Block_legacy + 8)) return "<bloc illisible>";
    const sdk::BlockLegacy* legacy = block->legacy();
    if (!mem::isReadable(legacy, offsets::BlockLegacy_namespacedId + 0x30)) return "<legacy illisible>";
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
    if (page > pages) throw CommandError(std::format("Page {} : il n'y a que {} page(s)", page, pages));
    chat::info(header);
    for (std::size_t i = (page - 1) * kPageSize; i < std::min(lines.size(), page * kPageSize); ++i) chat::info(lines[i]);
    if (pages > 1) {
        const std::string& prefix = baritone::Settings::get().prefix;
        chat::info(std::format("Page {}/{}{}", page, pages,
                               page < pages ? std::format(" - suite : {}{} {}", prefix, command, page + 1) : ""));
    }
}

// Prints everything we read from the game: the quickest way to validate the offsets of
// sdk/Offsets.h after a game update.
void debugInfo() {
    auto* client = sdk::ClientInstance::get();
    logger::info("ClientInstance = {}", static_cast<void*>(client));
    auto& player = requirePlayer();
    logger::info("LocalPlayer = {}", static_cast<void*>(&player));

    chat::info(std::format("Joueur : {}", player.name()));
    if (auto* sv = player.stateVector(); mem::isReadable(sv, sizeof(sdk::StateVectorComponent))) {
        chat::info(std::format("Position : {}  vitesse : {}", sv->pos, sv->velocity));
    }
    sdk::Vec3 feet = player.feet();
    sdk::BlockPos feetPos = sdk::BlockPos::floor(feet);
    chat::info(std::format("Pieds : {}  (bloc {})", feet, feetPos));
    if (auto* rot = player.rotation(); mem::isReadable(rot, sizeof(sdk::ActorRotationComponent))) {
        chat::info(std::format("Rotation : yaw {:.1f}  pitch {:.1f}", rot->rotation.y, rot->rotation.x));
    }
    if (auto* region = client->region()) {
        chat::info(std::format("Bloc sous les pieds : {}", blockName(region->getBlock(feetPos.below()))));
        chat::info(std::format("Bloc aux pieds : {}", blockName(region->getBlock(feetPos))));
    }
    if (auto* dim = player.dimension(); mem::isReadable(dim, 0x40)) {
        chat::info(std::format("Dimension : {}", dim->name()));
    }
    if (auto* supplies = player.supplies(); mem::isReadable(supplies, 0xC0)) {
        int slot = supplies->selectedSlot();
        std::string held = "rien";
        if (auto* inv = supplies->inventory()) {
            auto* stack = inv->getItem(slot);
            if (stack && !stack->empty()) held = std::format("{} x{}", stack->item()->name(), stack->count());
        }
        chat::info(std::format("Slot {} : {}", slot, held));
    }
    {
        std::string worn;
        for (int i = 0; i < 4; ++i) {
            baritone::HotbarStack a = Bot::get().player().armor(i);
            worn += i ? ", " : "";
            worn += a.empty() ? "-" : a.maxDamage > 0 ? std::format("{} {}/{}", a.name, a.maxDamage - a.damage, a.maxDamage) : a.name;
        }
        chat::info("Armure : " + worn);
    }
    {
        auto hunger = player.attribute("minecraft:player.hunger");
        auto health = player.attribute("minecraft:health");
        chat::info(std::format("Faim {} / Vie {} / haste {} / fatigue {}", hunger ? std::format("{:.0f}", hunger->value) : "?",
                               health ? std::format("{:.0f}", health->value) : "?", Bot::get().player().effectAmplifier("haste"),
                               Bot::get().player().effectAmplifier("mining_fatigue")));
    }
    if (auto* level = Bot::get().player().level()) {
        const sdk::HitResult* hit = level->hitResult();
        if (mem::isReadable(hit, sizeof(sdk::HitResult))) {
            chat::info(hit->type == 0 ? std::format("Vise (jeu) : bloc {} face {} point {}", hit->block, hit->face, hit->pos)
                                      : std::format("Vise (jeu) : type {}", hit->type));
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
        default: throw CommandError("Trop d'arguments : x y z, x z ou y");
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
    // ---- pathing (Baritone: goto, goal, path, thisway, cancel) ----
    manager.add({{"goto"}, "goto <x y z | x z | y | bloc>", "Va a une position ou au bloc le plus proche",
                 [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #goto <x y z | x z | y | bloc>");
                     bool numeric = std::ranges::all_of(args, [](const std::string& a) {
                         return !a.empty() && (a[0] == '~' || a[0] == '-' || std::isdigit(static_cast<unsigned char>(a[0])));
                     });
                     if (!numeric) {
                         baritone::BlockFilter filter(args);
                         chat::info("Recherche de " + filter.toString());
                         bot.baritone().getToBlock().getToBlock(std::move(filter));
                         return;
                     }
                     auto goal = parseGoal(args, player);
                     chat::info(std::format("Goal : {}", goal->toString()));
                     bot.baritone().customGoal().setGoalAndPath(goal);
                 }});

    manager.add({{"goal"}, "goal [x y z | x z | y | clear]", "Definit le goal sans partir", [&bot](const Args& args) {
        auto& player = requirePlayer();
        if (!args.empty() && (args[0] == "clear" || args[0] == "reset" || args[0] == "none")) {
            if (bot.baritone().customGoal().goal()) {
                bot.baritone().customGoal().setGoal(nullptr);
                chat::info("Goal efface");
            } else {
                chat::info("Il n'y avait pas de goal a effacer");
            }
            return;
        }
        auto goal = parseGoal(args, player);
        bot.baritone().customGoal().setGoal(goal);
        chat::info(std::format("Goal : {}", goal->toString()));
    }});

    manager.add({{"path"}, "path", "Va au goal actuel", [&bot](const Args&) {
        auto& custom = bot.baritone().customGoal();
        if (!custom.goal()) {
            chat::info("Pas de goal");
            return;
        }
        auto* player = sdk::ClientInstance::get() ? sdk::ClientInstance::get()->localPlayer() : nullptr;
        if (player) platform::WorldTracker::get().refreshAround(feetPos(*player), 16);  // Java: repack
        custom.path();
        chat::info("En route");
    }});

    manager.add({{"mine"}, "mine [quantite] <bloc> [bloc...]", "Mine les blocs indiques (ex : #mine diamond_ore)",
                 [&bot](const Args& args) {
                     requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #mine [quantite] <bloc> [bloc...]");
                     int quantity = 0;
                     Args blocks = args;
                     if (std::isdigit(static_cast<unsigned char>(args[0][0]))) {
                         quantity = parseInt(args[0], "quantite");
                         blocks.erase(blocks.begin());
                     }
                     if (blocks.empty()) throw CommandError("Indique au moins un bloc");
                     baritone::BlockFilter filter(blocks);
                     chat::info(std::format("Minage de {}{}", filter.toString(),
                                            quantity > 0 ? std::format(" ({} voulus)", quantity) : ""));
                     bot.baritone().mine().mine(quantity, std::move(filter));
                 }});

    // Java PickupCommand
    manager.add({{"pickup"}, "pickup [objet...]", "Ramasse les objets au sol (tous, ou ceux indiques)", [&bot](const Args& args) {
        requirePlayer();
        std::unordered_set<std::string> items;
        for (const std::string& a : args) items.insert(baritone::BlockFilter::normalize(a));
        if (items.empty()) {
            bot.baritone().follow().pickup([](const std::string&) { return true; }, "tous les objets");
            chat::info("Ramassage de tous les objets");
        } else {
            std::string list;
            for (const auto& i : items) list += (list.empty() ? "" : ", ") + i;
            bot.baritone().follow().pickup([items](const std::string& item) { return items.contains(item); }, list);
            chat::info("Ramassage de ces objets : " + list);
        }
    }});

    manager.add({{"follow"}, "follow <players | player <nom> | entities | entity <type>>", "Suit des joueurs ou des entites",
                 [&bot](const Args& args) {
                     requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #follow players | player <nom> | entities | entity <type>");
                     const std::string& mode = args[0];
                     Args rest(args.begin() + 1, args.end());
                     baritone::FollowProcess::Filter filter;
                     std::string desc;
                     if (mode == "players") {
                         filter = [](const baritone::EntityInfo& e) { return e.isPlayer; };
                         desc = "tous les joueurs";
                     } else if (mode == "player") {
                         if (rest.empty()) throw CommandError("Indique le nom du joueur");
                         filter = [names = rest](const baritone::EntityInfo& e) {
                             if (!e.isPlayer) return false;
                             return std::ranges::any_of(names, [&](const std::string& n) {
                                 return std::ranges::equal(n, e.name, [](char a, char b) { return std::tolower(a) == std::tolower(b); });
                             });
                         };
                         desc = rest[0];
                     } else if (mode == "entities") {
                         filter = [](const baritone::EntityInfo& e) { return !e.isItem; };
                         desc = "toutes les entites";
                     } else if (mode == "entity") {
                         if (rest.empty()) throw CommandError("Indique le type (ex : cow)");
                         std::vector<std::string> types;
                         for (const auto& t : rest) types.push_back(baritone::BlockFilter::normalize(t));
                         filter = [types](const baritone::EntityInfo& e) {
                             return std::ranges::find(types, e.type) != types.end();
                         };
                         desc = rest[0];
                     } else {
                         throw CommandError("Mode inconnu : players, player, entities ou entity");
                     }
                     bot.baritone().follow().follow(std::move(filter), desc);
                     chat::info("Je suis " + desc);
                 }});

    // ---- waypoints (Baritone: sethome, home, wp) ----
    // Java aliases: sethome = "waypoints save home", home = "waypoints goto home"
    manager.add({{"sethome"}, "sethome", "Enregistre ta position comme maison", [&manager](const Args& args) {
        std::string line = "wp save home";
        for (const auto& a : args) line += " " + a;
        manager.runCommand(line, false);
    }});
    manager.add({{"home"}, "home", "Va a ta maison", [&manager](const Args& args) {
        std::string line = "wp goto home";
        for (const auto& a : args) line += " " + a;
        manager.runCommand(line, false);
    }});

    // Java WaypointsCommand: list, save, info, delete, clear, restore, goal, goto
    manager.add({{"wp", "waypoint", "waypoints"},
                 "wp [list [tag] [page] | save [tag] [nom] [x y z] | info|delete|goal|goto <nom|tag> [@ date] | clear <tag> | restore <n>]",
                 "Gere les waypoints du monde", [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     static std::vector<baritone::Waypoint> deleted;  // this session (Java deletedWaypoints)
                     std::string action = args.empty() ? "list" : lowerCase(args[0]);
                     auto describe = [](const baritone::Waypoint& w) {
                         std::time_t t = static_cast<std::time_t>(w.createdAt);
                         std::tm tm{};
                         localtime_s(&tm, &t);
                         char date[32];
                         std::strftime(date, sizeof(date), "%d/%m/%Y %H:%M", &tm);
                         return std::format("{} {} @ {} (date {})", w.tag, w.name.empty() ? "<vide>" : w.name, date, w.createdAt);
                     };
                     if (action == "list" || action == "get" || action == "l") {
                         std::string tag = args.size() > 1 ? baritone::WaypointStore::tagOf(args[1]) : "";
                         auto list = bot.waypoints().list(tag);
                         if (list.empty()) throw CommandError(tag.empty() ? "Aucun waypoint" : "Aucun waypoint avec ce tag");
                         std::vector<std::string> lines;
                         for (const auto& w : list) lines.push_back(describe(w));
                         std::size_t pageArg = tag.empty() ? 1 : 2;
                         paginate(lines, args.size() > pageArg ? args[pageArg] : "",
                                  tag.empty() ? "Tous les waypoints :" : std::format("Waypoints du tag {} :", tag),
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
                             throw CommandError("Usage : #wp save [tag] [nom] [x y z]");
                         }
                         baritone::Waypoint wp;
                         wp.tag = tag;
                         wp.name = name;
                         wp.pos = pos;
                         wp.dimension = platform::WorldTracker::get().dimension();
                         wp.createdAt = std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count();
                         bot.waypoints().add(wp);
                         chat::info("Waypoint ajoute : " + describe(wp));
                         return;
                     }
                     if (action == "clear" || action == "c") {
                         if (args.size() != 2 || !baritone::WaypointStore::isTag(lowerCase(args[1]))) {
                             throw CommandError(std::format("Tag invalide : \"{}\"", args.size() > 1 ? args[1] : ""));
                         }
                         auto list = bot.waypoints().list(baritone::WaypointStore::tagOf(lowerCase(args[1])));
                         for (const auto& w : list) bot.waypoints().removeExact(w);
                         deleted.insert(deleted.end(), list.begin(), list.end());
                         chat::info(std::format("{} waypoints effaces (#wp restore {} pour les remettre)", list.size(), list.size()));
                         return;
                     }
                     if (action == "restore") {
                         if (args.size() != 2) throw CommandError("Usage : #wp restore <nombre>");
                         int amount = std::min(static_cast<int>(deleted.size()), parseInt(args[1], "nombre"));
                         for (int i = 0; i < amount; ++i) {
                             bot.waypoints().add(deleted.back());
                             deleted.pop_back();
                         }
                         chat::info(std::format("{} waypoints remis", amount));
                         return;
                     }
                     bool info = action == "info" || action == "show" || action == "i";
                     bool del = action == "delete" || action == "d";
                     bool goal = action == "goal" || action == "g";
                     bool go = action == "goto";
                     if (!info && !del && !goal && !go) throw CommandError("Action inconnue : list, save, info, delete, clear, restore, goal, goto");
                     if (args.size() < 2) throw CommandError(std::format("Usage : #wp {} <nom|tag> [@ date]", action));
                     std::string key = baritone::WaypointStore::isTag(lowerCase(args[1])) ? lowerCase(args[1]) : args[1];
                     auto found = bot.waypoints().matching(key);
                     std::optional<baritone::Waypoint> wp;
                     if (args.size() == 4 && args[2] == "@") {
                         int64_t date = std::stoll(args[3]);
                         for (const auto& w : found)
                             if (w.createdAt == date) wp = w;
                         if (!wp) throw CommandError("Une date est indiquee mais aucun waypoint ne correspond");
                     } else if (found.empty()) {
                         throw CommandError("Aucun waypoint trouve");
                     } else if (found.size() == 1) {
                         wp = found.front();
                     }
                     if (!wp) {
                         std::vector<std::string> lines;
                         for (const auto& w : found) lines.push_back(describe(w) + std::format("  -> #wp {} {} @ {}", action, args[1], w.createdAt));
                         paginate(lines, args.size() > 2 ? args[2] : "", "Plusieurs waypoints trouves :", std::format("wp {} {}", action, args[1]));
                         return;
                     }
                     if (info) {
                         chat::info(describe(*wp));
                         chat::info(std::format("Position : {}  (dimension {})", wp->pos, wp->dimension));
                         chat::info(std::format("Pour le recreer : #wp save {} {} {} {} {}", wp->tag, wp->name, wp->pos.x, wp->pos.y, wp->pos.z));
                     } else if (del) {
                         bot.waypoints().removeExact(*wp);
                         deleted.push_back(*wp);
                         chat::info("Waypoint supprime (#wp restore 1 pour le remettre)");
                     } else {
                         if (wp->dimension != platform::WorldTracker::get().dimension()) throw CommandError("Ce waypoint est dans une autre dimension");
                         auto target = std::make_shared<baritone::GoalBlock>(wp->pos);
                         if (goal) {
                             bot.baritone().customGoal().setGoal(target);
                             chat::info(std::format("Goal : {}", target->toString()));
                         } else {
                             bot.baritone().customGoal().setGoalAndPath(target);
                             chat::info(std::format("En route vers {}", target->toString()));
                         }
                     }
                 }});

    // ---- other Baritone goals ----
    manager.add({{"tunnel"}, "tunnel [hauteur largeur profondeur]",
                 "Creuse tout droit dans la direction regardee (avec dimensions : vide la zone avec le builder)",
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
                         chat::info(std::format("Tunnel : {}", goal->toString()));
                         bot.baritone().customGoal().setGoalAndPath(goal);
                         return;
                     }
                     if (args.size() != 3) throw CommandError("Usage : #tunnel [hauteur largeur profondeur]");
                     int height = parseInt(args[0], "hauteur"), width = parseInt(args[1], "largeur"),
                         depth = parseInt(args[2], "profondeur");
                     if (width < 1 || height < 2 || depth < 1 || height > 255) {
                         throw CommandError("Largeur et profondeur >= 1, hauteur entre 2 et 255");
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
                     chat::info(std::format("Tunnel de {} de haut, {} de large et {} de long", height, width, depth));
                 }});

    manager.add({{"axis", "highway"}, "axis", "Va sur l'axe ou la diagonale la plus proche (y = axisHeight)",
                 [&bot](const Args&) {
                     requirePlayer();
                     auto goal = std::make_shared<baritone::GoalAxis>(baritone::Settings::get().axisHeight);
                     bot.baritone().customGoal().setGoalAndPath(goal);
                     chat::info(std::format("Goal : {}", goal->toString()));
                 }});

    manager.add({{"surface", "top"}, "surface", "Remonte a la surface", [&bot](const Args&) {
        auto& player = requirePlayer();
        auto snap = platform::WorldTracker::get().snapshot();
        if (!snap) throw CommandError("Pas de snapshot");
        baritone::BlockStateInterface bsi(snap);
        baritone::BlockPos p = feetPos(player);
        constexpr int seaLevel = 63;
        if (p.y > seaLevel && bsi.props(p.x, p.y + 1, p.z).type == baritone::BlockType::Air) {
            chat::info("Deja a la surface");
            return;
        }
        for (int y = std::max(p.y, seaLevel); y < snap->maxY(); ++y) {
            if (bsi.props(p.x, y, p.z).type != baritone::BlockType::Air && y > p.y) {
                // highest block of this column above us: stand on top of the last solid block
                int top = y;
                while (top + 1 < snap->maxY() && bsi.props(p.x, top + 1, p.z).type != baritone::BlockType::Air) ++top;
                if (top + 1 >= snap->maxY()) break;
                auto goal = std::make_shared<baritone::GoalBlock>(p.x, top + 1, p.z);
                chat::info(std::format("Vers la surface : {}", goal->toString()));
                bot.baritone().customGoal().setGoalAndPath(goal);
                return;
            }
        }
        chat::info("Pas de position plus haute trouvee");
    }});

    manager.add({{"invert"}, "invert", "Inverse le goal (s'en eloigner)", [&bot](const Args&) {
        auto& custom = bot.baritone().customGoal();
        baritone::GoalPtr goal = custom.mostRecentGoal();
        if (!goal) throw CommandError("Pas de goal a inverser");
        if (auto inv = std::dynamic_pointer_cast<const baritone::GoalInverted>(goal)) {
            throw CommandError("Le goal est deja inverse");
        }
        custom.setGoalAndPath(std::make_shared<baritone::GoalInverted>(goal));
        chat::info("Goal inverse");
    }});

    // Java ETACommand
    manager.add({{"eta"}, "eta", "Temps restant estime", [&bot](const Args&) {
        if (!bot.baritone().control().mostRecentInControl()) throw CommandError("Aucun processus en cours");
        double segment = bot.pathing().ticksRemainingInSegment().value_or(std::nan(""));
        double goal = bot.pathing().estimatedTicksToGoal().value_or(std::nan(""));
        chat::info(std::format("Segment suivant : {:.1f} s ({:.0f} ticks)", segment / 20, segment));
        chat::info(std::format("Goal : {:.1f} s ({:.0f} ticks)", goal / 20, goal));
    }});

    manager.add({{"version"}, "version", "Version du bot", [](const Args&) {
        chat::info("Baritone Bedrock " BARITONE_BEDROCK_VERSION " (port de cabaletta/baritone) - Minecraft Bedrock 26.52");
    }});

    manager.add({{"explore"}, "explore [x z]", "Explore les chunks jamais charges, en spirale", [&bot](const Args& args) {
        auto& player = requirePlayer();
        baritone::BlockPos p = feetPos(player);
        int x = args.size() >= 2 ? parseCoord(args[0], p.x, "x") : p.x;
        int z = args.size() >= 2 ? parseCoord(args[1], p.z, "z") : p.z;
        bot.baritone().explore().explore(x, z);
        chat::info(std::format("Exploration autour de {} {}", x, z));
    }});

    manager.add({{"farm"}, "farm [rayon]", "Recolte les cultures mures et replante", [&bot](const Args& args) {
        auto& player = requirePlayer();
        int range = args.empty() ? 0 : parseInt(args[0], "rayon");
        bot.baritone().farm().farm(range, feetPos(player));
        chat::info(range > 0 ? std::format("Farm dans un rayon de {} blocs", range) : "Farm");
    }});

    // ---- cache (Baritone: find, repack, blacklist) ----
    manager.add({{"find"}, "find <bloc> [bloc...]", "Positions connues de ces blocs (chunks charges + cache)",
                 [](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #find <bloc> [bloc...]");
                     auto snap = platform::WorldTracker::get().snapshot();
                     if (!snap) throw CommandError("Monde pas encore lu");
                     baritone::BlockFilter filter(args);
                     baritone::BlockPos feet = feetPos(player);
                     auto found = baritone::searchWorld(*snap, filter, feet, 10, baritone::Settings::get().maxScanChunkRadius);
                     if (found.empty()) {
                         chat::info("Aucun " + filter.toString() + " connu" +
                                    (std::ranges::any_of(filter.names(), [](const std::string& n) { return baritone::isTrackedBlock(n); })
                                         ? ""
                                         : " (bloc non suivi par le cache : seuls les chunks charges sont cherches)"));
                         return;
                     }
                     for (const auto& p : found) {
                         chat::info(std::format("{} {} {}  ({:.0f} blocs)", p.x, p.y, p.z, std::sqrt(static_cast<double>(p.distSq(feet)))));
                     }
                 }});

    manager.add({{"saveall"}, "saveall", "Ecrit le cache longue distance sur le disque", [](const Args&) {
        platform::WorldTracker::get().saveCache();
        chat::info("Cache enregistre");
    }});

    manager.add({{"reloadall"}, "reloadall", "Relit le cache longue distance depuis le disque", [](const Args&) {
        requirePlayer();
        platform::WorldTracker::get().reloadCache();
        chat::info("Cache recharge");
    }});

    manager.add({{"click"}, "click", "Va sur le bloc vise, meme tres loin (jusqu'a 512 blocs, chunks charges ou en cache)",
                 [&bot](const Args&) {
                     requirePlayer();
                     auto snap = platform::WorldTracker::get().snapshot();
                     if (!snap) throw CommandError("Monde pas encore lu");
                     baritone::BlockStateInterface bsi(snap);
                     auto hit = baritone::rayTrace(bsi, bot.player().eyePosition(), bot.player().rotation(), 512);
                     if (!hit) throw CommandError("Aucun bloc vise a moins de 512 blocs");
                     auto goal = std::make_shared<baritone::GoalBlock>(hit->pos.above());
                     chat::info(std::format("Goal : {}", goal->toString()));
                     bot.baritone().customGoal().setGoalAndPath(goal);
                 }});

    manager.add({{"explorefilter"}, "explorefilter <fichier.json> [invert]",
                 "Chunks a considerer comme deja explores pour #explore (liste JSON de {\"x\":..,\"z\":..})",
                 [&bot](const Args& args) {
                     if (args.empty() || args.size() > 2) throw CommandError("Usage : #explorefilter <fichier.json> [invert]");
                     bool invert = args.size() == 2 && args[1] == "invert";
                     std::filesystem::path file = args[0];
                     if (file.is_relative()) file = paths::dataDir() / file;
                     std::ifstream in(file);
                     if (!in) throw CommandError("Fichier introuvable : " + file.string());
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
                     if (chunks.empty()) throw CommandError("Aucun chunk {\"x\":..,\"z\":..} trouve dans le fichier");
                     std::size_t n = chunks.size();
                     bot.baritone().explore().setFilter(std::move(chunks), invert);
                     chat::info(std::format("Filtre d'exploration : {} chunks{}", n, invert ? " (inverse)" : ""));
                 }});

    manager.add({{"repack", "rescan"}, "repack", "Remet les chunks charges dans le cache longue distance", [](const Args&) {
        requirePlayer();
        std::size_t n = platform::WorldTracker::get().repack();
        chat::info(n ? std::format("{} chunks mis en cache", n) : "Cache desactive (chunkCaching) ou monde pas lu");
    }});

    manager.add({{"blacklist"}, "blacklist", "#goto <bloc> : ignore le bloc vise le plus proche", [&bot](const Args&) {
        if (!bot.baritone().getToBlock().isActive()) throw CommandError("Aucun #goto <bloc> en cours");
        if (!bot.baritone().getToBlock().blacklistClosest()) throw CommandError("Aucun bloc connu a ignorer");
        chat::info("Le plus proche est ignore");
    }});

    // ---- building (Baritone: sel, build, pause, resume) ----
    manager.add({{"sel", "selection", "s"},
                 "sel <pos1|pos2 [x y z] | clear | undo | set|walls|shell|sphere|hsphere <bloc> | cylinder|hcylinder <bloc> [axe] | "
                 "cleararea | replace <de...> <vers> | copy|paste [x y z] | expand|contract|shift [a|newest|oldest] <dir> <n>>",
                 "Selections (plusieurs possibles) et construction dedans", [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #sel pos1 | pos2 | clear | undo | set | walls | shell | cleararea | replace | expand");
                     auto& selections = bot.baritone().selections();
                     auto& pending = bot.selectionPos1();
                     std::string action = args[0];
                     std::ranges::transform(action, action.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                     if (action == "pos1" || action == "p1" || action == "1" || action == "pos2" || action == "p2" || action == "2") {
                         baritone::BlockPos p = feetPos(player);
                         if (args.size() == 4) {
                             p = {parseCoord(args[1], p.x, "x"), parseCoord(args[2], p.y, "y"), parseCoord(args[3], p.z, "z")};
                         } else if (args.size() != 1) {
                             throw CommandError("Usage : #sel pos1 [x y z]");
                         }
                         if (action.back() == '1') {
                             pending = p;
                             chat::info(std::format("Position 1 : {} {} {}", p.x, p.y, p.z));
                         } else {
                             if (!pending) throw CommandError("Definis d'abord #sel pos1");
                             selections.add({*pending, p});
                             pending.reset();
                             chat::info(std::format("Selection {} ajoutee jusqu'a {} {} {}", selections.all().size(), p.x, p.y, p.z));
                         }
                         return;
                     }
                     if (action == "clear" || action == "c") {
                         selections.clear();
                         pending.reset();
                         chat::info("Selections effacees");
                         return;
                     }
                     if (action == "undo" || action == "u") {
                         // Java: undo pos1, else remove the last selection and keep its pos1
                         if (pending) {
                             pending.reset();
                             chat::info("pos1 annulee");
                         } else {
                             if (selections.all().empty()) throw CommandError("Rien a annuler");
                             pending = selections.all().back().pos1;
                             selections.removeLast();
                             chat::info("pos2 annulee");
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
                             throw CommandError(std::format("Usage : #sel {} [x y z]", action));
                         }
                         if (action == "paste" || action == "p") {
                             if (!clipboard) throw CommandError("Copie d'abord une selection (#sel copy)");
                             bot.baritone().builder().build("Fill", clipboard, at + clipboardOffset);
                             chat::info("Construction en cours");
                             return;
                         }
                         if (selections.all().empty()) throw CommandError("Aucune selection");
                         auto snapshot = bot.baritone().world();
                         if (!snapshot) throw CommandError("Monde pas encore lu");
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
                         chat::info("Selection copiee");
                         return;
                     }
                     if (selections.all().empty()) throw CommandError("Aucune selection : #sel pos1 puis #sel pos2");
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
                         if (rest.size() != 2) throw CommandError(std::format("Usage : #sel {} [a|newest|oldest] <direction> <blocs>", action));
                         std::string dir = rest[0];
                         std::ranges::transform(dir, dir.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                         int n = parseInt(rest[1], "blocs");
                         static const std::pair<const char*, baritone::Direction> dirs[] = {
                             {"north", baritone::Direction::North}, {"south", baritone::Direction::South},
                             {"east", baritone::Direction::East},   {"west", baritone::Direction::West},
                             {"up", baritone::Direction::Up},       {"down", baritone::Direction::Down}};
                         auto it = std::ranges::find_if(dirs, [&](const auto& d) { return dir == d.first; });
                         if (it == std::end(dirs)) throw CommandError("Direction : north, south, east, west, up, down");
                         std::vector<baritone::Selection*> targets;
                         auto& all = selections.all();
                         if (target == "a" || target == "all") {
                             for (auto& sel : all) targets.push_back(&sel);
                         } else if (target == "o" || target == "oldest") {
                             targets.push_back(&all.front());
                         } else if (target == "n" || target == "newest") {
                             targets.push_back(&all.back());
                         } else {
                             throw CommandError("Cible : a (toutes), newest ou oldest");
                         }
                         for (baritone::Selection* sel : targets) {
                             if (action == "expand") sel->expand(it->second, n);
                             else if (action == "contract") sel->expand(it->second, -n);
                             else sel->shift(it->second, n);
                         }
                         chat::info(std::format("{} selection(s) modifiee(s)", targets.size()));
                         return;
                     }
                     auto desired = [](const std::string& name) {
                         std::string n = baritone::BlockFilter::normalize(name);
                         return n == "minecraft:air" ? baritone::DesiredBlock::air() : baritone::DesiredBlock::block(n);
                     };
                     std::function<baritone::SchematicPtr(baritone::BlockPos)> make;
                     bool sphere = action == "sphere" || action == "sph" || action == "hsphere" || action == "hsph";
                     bool cylinder = action == "cylinder" || action == "cyl" || action == "hcylinder" || action == "hcyl";
                     if (sphere || cylinder) {
                         if (args.size() < 2 || args.size() > (cylinder ? 3u : 2u)) {
                             throw CommandError(std::format("Usage : #sel {} <bloc>{}", action, cylinder ? " [x|y|z]" : ""));
                         }
                         auto block = desired(args[1]);
                         bool filled = action[0] != 'h';
                         char axis = args.size() == 3 ? static_cast<char>(std::tolower(static_cast<unsigned char>(args[2][0]))) : 'y';
                         if (cylinder && axis != 'x' && axis != 'y' && axis != 'z') throw CommandError("Axe : x, y ou z");
                         make = [block, filled, sphere, axis](baritone::BlockPos size) -> baritone::SchematicPtr {
                             auto fill = std::make_shared<baritone::FillSchematic>(size, block);
                             return std::make_shared<baritone::MaskSchematic>(
                                 fill, sphere ? baritone::sphereMask(size, filled) : baritone::cylinderMask(size, filled, axis));
                         };
                     } else if (action == "set" || action == "fill" || action == "s" || action == "f" || action == "walls" ||
                                action == "w" || action == "shell" || action == "shl") {
                         if (args.size() != 2) throw CommandError(std::format("Usage : #sel {} <bloc>", action));
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
                         if (args.size() < 3) throw CommandError("Usage : #sel replace <de...> <vers>");
                         std::unordered_set<std::string> from;
                         for (std::size_t i = 1; i + 1 < args.size(); ++i) from.insert(baritone::BlockFilter::normalize(args[i]));
                         auto to = desired(args.back());
                         make = [from, to](baritone::BlockPos size) {
                             return std::make_shared<baritone::ReplaceSchematic>(size, from, to);
                         };
                     } else {
                         throw CommandError("Action inconnue : " + args[0]);
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
                     chat::info(std::format("Construction ({}) sur {} selection(s)", action, selections.all().size()));
                 }});

    manager.add({{"build"}, "build [nom] [x y z]",
                 "Construit un fichier du dossier schematics (.mcstructure, .schem, .litematic ; sans argument : liste)",
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
                         chat::info(list.empty() ? "Aucune structure dans " + dir.string() : "Structures : " + list);
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
                     if (!std::filesystem::exists(file)) throw CommandError("Fichier introuvable : " + file.string());
                     baritone::BlockPos origin = feetPos(player);
                     if (args.size() == 4) {
                         origin = {parseCoord(args[1], origin.x, "x"), parseCoord(args[2], origin.y, "y"),
                                   parseCoord(args[3], origin.z, "z")};
                     } else if (args.size() != 1) {
                         throw CommandError("Usage : #build <nom> [x y z]");
                     }
                     baritone::SchematicPtr schematic;
                     try {
                         schematic = baritone::loadSchematic(file.string());
                     } catch (const std::exception& e) {
                         throw CommandError(std::format("Lecture de {} impossible : {}", file.filename().string(), e.what()));
                     }
                     baritone::BlockPos s = schematic->size();
                     bot.baritone().builder().build(file.filename().string(), schematic, origin, true);
                     chat::info(std::format("Structure {} chargee ({}x{}x{}), origine : {}", file.filename().string(), s.x, s.y, s.z, origin));
                 }});

    // Java ExecutionControlCommands
    manager.add({{"pause", "p", "paws"}, "pause", "Met le bot en pause jusqu'a #resume", [&bot](const Args&) {
        if (bot.baritone().pauser().paused()) throw CommandError("Deja en pause");
        bot.baritone().pauser().setPaused(true);
        chat::info("En pause");
    }});

    manager.add({{"resume", "r", "unpause", "unpaws"}, "resume", "Reprend ce que le bot faisait avant #pause",
                 [&bot](const Args&) {
                     bot.baritone().builder().resume();
                     if (!bot.baritone().pauser().paused()) throw CommandError("Pas en pause");
                     bot.baritone().pauser().setPaused(false);
                     chat::info("Reprise");
                 }});

    manager.add({{"paused"}, "paused", "Indique si le bot est en pause", [&bot](const Args&) {
        chat::info(bot.baritone().pauser().paused() ? "En pause" : "Pas en pause");
    }});

    // Java ElytraCommand: "elytra" flies to the most recent goal, "elytra reset|repack|supported".
    // Ours too: "elytra x z" / "elytra x y z".
    manager.add({{"elytra"}, "elytra [reset | repack | supported | x z | x y z]",
                 "Vole a l'elytre (avec fusees) jusqu'au dernier goal ou aux coordonnees, puis atterrit",
                 [&bot](const Args& args) {
                     auto& elytra = bot.baritone().elytra();
                     if (args.size() == 1 && args[0] == "supported") {
                         chat::info("oui");  // Java: the native path finder could be missing; here it always exists
                         return;
                     }
                     if (args.size() == 1 && args[0] == "reset") {
                         if (!elytra.currentDestination()) throw CommandError("Pas de vol elytre en cours");
                         elytra.resetState();
                         chat::info("Etat reinitialise, toujours en vol vers le meme goal");
                         return;
                     }
                     if (args.size() == 1 && args[0] == "repack") {
                         platform::WorldTracker::get().markAllStale();
                         chat::info("Tous les chunks charges sont remis en file pour etre relus");
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
                             chat::info("Pour ne plus voir ce message, active le reglage elytraTermsAccepted");
                             chat::info("L'elytre de Baritone est experimentale. Elle sert aux longs trajets dans le Nether "
                                        "mais marche aussi dans l'Overworld, avec des fusees pour la vitesse. Elle ne marche "
                                        "avec aucun autre mod (\"hack\") de vitesse.");
                             chat::info(std::format("Pour que Baritone decolle tout seul depuis le sol : {}set elytraAutoJump true "
                                                    "(deconseille sur un serveur qui lag).", s.prefix));
                             chat::info(std::format("Pour aller moins vite : {}set elytraConserveFireworks true et/ou baisser "
                                                    "elytraFireworkSpeed (ex. {}set elytraFireworkSpeed 0.6).", s.prefix, s.prefix));
                             chat::info("Bedrock : pas de prediction du terrain par la graine (elytraNetherSeed / elytraPredictTerrain "
                                        "sont ignores), le chemin est replanifie au fur et a mesure que les chunks chargent.");
                         }
                         baritone::GoalPtr goal = bot.baritone().customGoal().mostRecentGoal();
                         if (!goal) throw CommandError("Aucun goal defini");
                         if (auto error = elytra.pathTo(goal)) throw CommandError(*error);
                         int fireworks = bot.player().countItem("minecraft:firework_rocket");
                         if (fireworks == 0) chat::error("Aucune fusee : le vol se limitera a un plane");
                         chat::info(std::format("Elytre vers {} ({} fusees)", goal->toString(), fireworks));
                         return;
                     } else {
                         throw CommandError("Action invalide");
                     }
                     if (knownY && !elytra.isSupportedPos(dest)) throw CommandError("Le goal doit etre dans les limites du vol a l'elytre");
                     if (!elytra.isSupportedPos(p)) throw CommandError("Le joueur doit etre dans les limites du vol a l'elytre");
                     int fireworks = bot.player().countItem("minecraft:firework_rocket");
                     if (fireworks == 0) chat::error("Aucune fusee : le vol se limitera a un plane");
                     elytra.start(dest, knownY);
                     chat::info(std::format("Elytre vers {} {} ({} fusees)", dest.x, dest.z, fireworks));
                 }});

    manager.add({{"proc"}, "proc", "Affiche le processus en cours", [&bot](const Args&) {
        auto& b = bot.baritone();
        baritone::Process* p = b.control().mostRecentInControl();
        if (!p) {
            chat::info("Aucun processus actif");
            return;
        }
        const auto& cmd = b.control().mostRecentCommand();
        static constexpr const char* types[] = {"REQUEST_PAUSE", "CANCEL_AND_SET_GOAL", "REVALIDATE_GOAL_AND_PATH",
                                                "FORCE_REVALIDATE_GOAL_AND_PATH", "SET_GOAL_AND_PATH", "SET_GOAL_AND_PAUSE", "DEFER"};
        chat::info(std::format("{} (priorite {}, temporaire {}) - goal {}", p->displayName(), p->priority(),
                               p->isTemporary() ? "oui" : "non", cmd && cmd->goal ? cmd->goal->toString() : "aucun"));
        if (cmd) {
            auto t = static_cast<std::size_t>(cmd->type);
            chat::info(std::format("Derniere commande : {}", t < std::size(types) ? types[t] : "?"));
        }
        chat::info("Chemin : " + b.pathing().debugState());
    }});

    manager.add({{"thisway", "forward"}, "thisway <distance>", "Va tout droit dans la direction regardee",
                 [&bot](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #thisway <distance>");
                     double dist = parseDouble(args[0], "distance");
                     sdk::Vec3 f = player.feet();
                     auto goal = baritone::GoalXZ::fromDirection(f.x, f.z, player.rotation()->rotation.y, dist);
                     chat::info(std::format("Goal : {}", goal->toString()));
                     bot.baritone().customGoal().setGoalAndPath(goal);
                 }});

    manager.add({{"grab"}, "grab <0|1>", "Dev : force le drapeau 'curseur capture'", [](const Args& args) {
        requirePlayer();
        if (args.empty()) throw CommandError("Usage : #grab <0|1>");
        sdk::ClientInstance::get()->cursorGrabbedFlag() = args[0] == "1";
        chat::info(std::format("cursorGrabbed = {}", sdk::ClientInstance::get()->cursorGrabbedFlag()));
    }});

    manager.add({{"entities", "ent"}, "entities", "Dev : liste les entites chargees", [&bot](const Args&) {
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
        chat::info(std::format("{} entites", list.size()));
        for (std::size_t i = 0; i < list.size() && i < 10; ++i) {
            const auto& e = list[i];
            chat::info(std::format("{} {} a {:.1f} {:.1f} {:.1f}", e.type, e.name, e.position.x, e.position.y, e.position.z));
        }
    }});

    manager.add({{"ism"}, "ism [slot...]", "Dev : etat du gestionnaire d'objets (ItemStackNetManager) et net ids", [](const Args& args) {
        auto& player = requirePlayer();
        void* mgr = mem::member<void*>(&player, offsets::Player_itemStackNetManager);
        logger::info("ItemStackNetManager = {}", mgr);
        if (!mem::isReadable(mgr, 0x90)) return;
        void* queue = mem::member<void*>(mgr, 0x68);
        std::size_t queued = 0;
        if (mem::isReadable(queue, 0x18)) {
            auto first = mem::member<uintptr_t>(queue, 0), last = mem::member<uintptr_t>(queue, 8);
            queued = (last - first) / 8;
        }
        chat::info(std::format("enabled={} current={} queue={} ({} en attente)", mem::member<uint8_t>(mgr, 9),
                               mem::member<void*>(mgr, 0x60), queue, queued));
        hexDump("ItemStackNetManager", mgr, 0x90);
        auto* inv = player.supplies()->inventory();
        for (const auto& a : args) {
            int slot = parseInt(a, "slot");
            const sdk::ItemStack* st = inv->getItem(slot);
            if (!st) continue;
            auto* p = reinterpret_cast<const uint8_t*>(st) + offsets::ItemStack_netIdVariant;
            chat::info(std::format("slot {} : netId index {} valeurs {} / {}", slot, p[0x10],
                                   *reinterpret_cast<const int32_t*>(p), *reinterpret_cast<const int32_t*>(p + 8)));
            hexDump("ItemStack", st, 0xA0);
        }
    }});

    manager.add({{"swap"}, "swap <slot> <slot> [local|server]",
                 "Dev : echange deux cases d'inventaire (0-8 barre, 9-35 sac) ; local/server : un seul cote",
                 [&bot](const Args& args) {
                     requirePlayer();
                     if (args.size() < 2 || args.size() > 3) throw CommandError("Usage : #swap <slot> <slot> [local|server]");
                     int a = parseInt(args[0], "slot"), b = parseInt(args[1], "slot");
                     std::string mode = args.size() == 3 ? args[2] : "";
                     bot.runInTick([&bot, a, b, mode] {
                         auto* p = sdk::ClientInstance::get() ? sdk::ClientInstance::get()->localPlayer() : nullptr;
                         if (!p) return;
                         bool ok = mode.empty()      ? bot.player().swapSlots(a, b)
                                   : mode == "local" ? sdk::applyInventoryMoveLocally(*p, a, b)
                                                     : sdk::sendInventoryMove(*p, a, b) != 0;
                         chat::info(ok ? "Fait" : "Impossible");
                     });
                 }});

    manager.add({{"inv", "hotbar"}, "inv [all]", "Dev : contenu de la barre d'objets (all : tout l'inventaire)", [&bot](const Args& args) {
        requirePlayer();
        auto& p = bot.player();
        int slots = !args.empty() && args[0] == "all" ? 36 : 9;
        for (int i = 0; i < slots; ++i) {
            auto s = p.hotbar(i);
            if (s.empty()) continue;
            std::string extra;
            if (s.maxDamage > 0) extra += std::format(" [usure {}/{}]", s.damage, s.maxDamage);
            if (s.efficiency) extra += std::format(" [efficacite {}]", s.efficiency);
            if (s.silkTouch) extra += " [toucher de soie]";
            chat::info(std::format("{}{} : {} x{}{}{}{}", i == p.selectedSlot() ? ">" : " ", i, s.name, s.count,
                                   s.isBlock ? " [bloc]" : "",
                                   s.tool != baritone::Tool::None ? std::format(" [outil tier {} vitesse {}]", s.toolTier, s.toolSpeed) : "",
                                   extra));
        }
    }});

    manager.add({{"break"}, "break <dx> <dy> <dz>", "Dev : casse un bloc (relatif aux pieds)", [&bot](const Args& args) {
        auto& player = requirePlayer();
        if (args.size() < 3) throw CommandError("Usage : #break <dx> <dy> <dz>");
        baritone::BlockPos p = feetPos(player).offset(parseInt(args[0], "dx"), parseInt(args[1], "dy"), parseInt(args[2], "dz"));
        chat::info(std::format("Cassage de {}", p));
        bot.startTestAction(Bot::TestAction::Break, p);
    }});

    manager.add({{"place"}, "place <dx> <dy> <dz>", "Dev : pose un bloc (relatif aux pieds)", [&bot](const Args& args) {
        auto& player = requirePlayer();
        if (args.size() < 3) throw CommandError("Usage : #place <dx> <dy> <dz>");
        baritone::BlockPos p = feetPos(player).offset(parseInt(args[0], "dx"), parseInt(args[1], "dy"), parseInt(args[2], "dz"));
        chat::info(std::format("Pose en {}", p));
        bot.startTestAction(Bot::TestAction::Place, p);
    }});

    manager.add({{"cost"}, "cost", "Dev : couts des mouvements depuis la position du joueur", [&bot](const Args&) {
        auto& player = requirePlayer();
        auto snap = platform::WorldTracker::get().snapshot();
        if (!snap) throw CommandError("Pas de snapshot");
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

    manager.add({{"ecsdump"}, "ecsdump <Composant> [octets]", "Dev : contenu brut d'un composant EnTT du joueur dans le log",
                 [](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #ecsdump <NomDuComposant> [octets]");
                     int bytes = args.size() > 1 ? parseInt(args[1], "octets") : 0x100;
                     const auto* storage = std::as_const(player.entityContext().registry).storage(componentHash(args[0]));
                     if (!storage || !storage->contains(player.entityContext().entity)) throw CommandError("Composant absent");
                     const void* value = storage->value(player.entityContext().entity);
                     if (!value) throw CommandError("Composant vide (drapeau)");
                     hexDump(args[0], value, bytes);
                     chat::info(std::format("{} a {} : ecrit dans log.txt", args[0], value));
                 }});

    manager.add({{"ecs"}, "ecs <Composant...>", "Dev : le joueur a-t-il ces composants EnTT (ex. IsDeadFlagComponent) ?",
                 [](const Args& args) {
                     auto& player = requirePlayer();
                     if (args.empty()) throw CommandError("Usage : #ecs <NomDuComposant> [...]");
                     for (const std::string& name : args) {
                         uint32_t hash = componentHash(name);
                         const auto* storage = std::as_const(player.entityContext().registry).storage(hash);
                         chat::info(std::format("{} (0x{:08X}) : {}", name, hash,
                                                !storage ? "aucun stockage" : storage->contains(player.entityContext().entity)
                                                                                  ? "OUI"
                                                                                  : "non"));
                     }
                 }});

    manager.add({{"world"}, "world", "Dev : etat du snapshot du monde", [](const Args&) {
        auto& tracker = platform::WorldTracker::get();
        auto snap = tracker.snapshot();
        auto* client = sdk::ClientInstance::get();
        auto& player = requirePlayer();
        sdk::BlockPos feet = sdk::BlockPos::floor(player.feet());
        bool here = client->region()->hasChunksAt(feet, 0);
        bool distant = client->region()->hasChunksAt(feet.offset(100000, 0, 100000), 0);
        chat::info(std::format("Dimension {} : {} chunks, {} etats de blocs, dernier scan {:.1f} ms",
                               tracker.dimension(), snap ? snap->chunkCount() : 0, tracker.knownStates(),
                               tracker.lastChunkScanMs()));
        chat::info(std::format("hasChunksAt ici={} loin={}  hauteur {}..{}", here, distant, client->region()->minHeight(),
                               client->region()->maxHeight()));
        if (snap) {
            baritone::BlockStateInterface bsi(snap);
            const auto& below = bsi.props(feet.x, feet.y - 1, feet.z);
            chat::info(std::format("Snapshot sous les pieds : {} (fullCube={} hardness={})", below.name,
                                   below.fullCube, below.hardness));
        }
        if (auto cache = tracker.cache()) {
            chat::info(std::format("Cache longue distance : {} regions en memoire, {} chunks", cache->loadedRegions(),
                                   cache->cachedChunks()));
        } else {
            chat::info("Cache longue distance : desactive (chunkCaching)");
        }
    }});

    manager.add({{"calc"}, "calc <x> [y] <z>", "Dev : calcule un chemin sans bouger (capacites du joueur)", [](const Args& args) {
        if (args.size() != 2 && args.size() != 3) throw CommandError("Usage : #calc <x> [y] <z>");
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
        if (!snap) throw CommandError("Pas de snapshot");
        chat::info(std::format("Calcul de {} vers {}...", start, goal->toString()));
        baritone::PlayerAbilities abilities = Bot::get().baritone().inventory().abilities();
        std::thread([snap, start, goal, abilities] {
            auto t0 = std::chrono::steady_clock::now();
            auto ctx = std::make_unique<baritone::CalculationContext>(snap, abilities, true);
            baritone::AStarPathFinder finder(start, goal, std::move(ctx), {});
            auto result = finder.calculate(2000, 5000);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!result.path) {
                chat::error(std::format("Pas de chemin ({:.0f} ms, type {}) : {}", ms, static_cast<int>(result.type),
                                        result.error));
                return;
            }
            const char* kind = result.type == baritone::PathResultType::SuccessToGoal ? "complet" : "segment";
            chat::info(std::format("Chemin {} : {} positions, {:.1f} s estimees, {} noeuds, {:.0f} ms", kind,
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

    manager.add({{"nbt"}, "nbt [dx dy dz]", "Dev : etats du bloc (relatif aux pieds)", [](const Args& args) {
        auto* client = sdk::ClientInstance::get();
        auto& player = requirePlayer();
        sdk::BlockPos pos = sdk::BlockPos::floor(player.feet());
        if (args.size() >= 3) pos = pos.offset(parseInt(args[0], "dx"), parseInt(args[1], "dy"), parseInt(args[2], "dz"));
        const sdk::Block* block = client->region()->getBlock(pos);
        const auto& tag = block->serializationId();
        if (!mem::isReadable(&tag, sizeof(tag)) || tag.tags.size() > 64) throw CommandError("CompoundTag illisible");
        chat::info(std::format("{} : {}", pos, sdk::nbt::toString(tag)));
    }});

    manager.add({{"hex"}, "hex <block|legacy|region> [size] [dx dy dz]", "Dev : dump memoire d'un objet",
                 [](const Args& args) {
                     if (args.empty()) throw CommandError("Usage : #hex <objet> [taille] [dx dy dz]");
                     int size = args.size() > 1 ? parseInt(args[1], "taille") : 0x100;
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
                     else throw CommandError("Objet inconnu");
                     hexDump(std::format("{} at {}", args[0], pos), obj, size);
                     chat::info("dump ecrit dans log.txt");
                 }});

    manager.add({{"hexat"}, "hexat <adresse hex> [taille]", "Dev : dump memoire a une adresse (lecture seule)",
                 [](const Args& args) {
                     if (args.empty()) throw CommandError("Usage : #hexat <adresse> [taille]");
                     uintptr_t addr = 0;
                     try {
                         addr = static_cast<uintptr_t>(std::stoull(args[0], nullptr, 16));
                     } catch (...) {
                         throw CommandError("Adresse invalide");
                     }
                     int size = args.size() > 1 ? parseInt(args[1], "taille") : 0x80;
                     if (!mem::isReadable(reinterpret_cast<const void*>(addr), 8)) throw CommandError("Adresse illisible");
                     hexDump(args[0], reinterpret_cast<const void*>(addr), size);
                     chat::info("dump ecrit dans log.txt");
                 }});

    manager.add({{"attrs"}, "attrs", "Dev : attributs du joueur (defaut et actuel) dans le log",
                 [](const Args&) {
                     auto& player = requirePlayer();
                     auto* c = static_cast<const uint8_t*>(player.entityContext().raw(componentHash("AttributesComponent")));
                     if (!c) throw CommandError("AttributesComponent absent");
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
                     chat::info("attributs ecrits dans log.txt");
                 }});

    manager.add({{"slot"}, "slot <0-8>", "Dev : selectionne une case de la barre d'objets",
                 [&bot](const Args& args) {
                     if (args.empty()) throw CommandError("Usage : #slot <0-8>");
                     int slot = parseInt(args[0], "slot");
                     if (slot < 0 || slot > 8) throw CommandError("Case de 0 a 8");
                     requirePlayer();
                     bot.player().selectSlot(slot);
                     chat::info(std::format("Case {} : {}", slot, bot.player().hotbar(slot).name));
                 }});

    manager.add({{"vt"}, "vt <region|block|legacy|player|gamemode|level|inventory|supplies|item|stack> [count]", "Dev : ecrit une vtable dans le log",
                 [&bot](const Args& args) {
                     if (args.empty()) throw CommandError("Usage : #vt <objet> [count]");
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
                         if (!stack || stack->empty()) throw CommandError("Rien en main");
                         obj = args[0] == "item" ? static_cast<const void*>(stack->item()) : static_cast<const void*>(stack);
                     } else throw CommandError("Objet inconnu");
                     dumpVtable(args[0], obj, count);
                     chat::info("vtable ecrite dans log.txt");
                 }});

    // Java HelpCommand: "help [page]" lists the commands, "help <command>" details one
    manager.add({{"help", "?"}, "help [page|commande]", "Liste les commandes", [&manager](const Args& args) {
        const std::string& prefix = baritone::Settings::get().prefix;
        if (!args.empty() && !isNumber(args[0])) {
            const Command* cmd = manager.find(args[0]);
            if (!cmd) throw CommandError(std::format("Commande inconnue : {}", args[0]));
            chat::info(std::format("{} - {}", cmd->names.front(), cmd->description));
            chat::info(std::format("Usage : {}{}", prefix, cmd->usage));
            if (cmd->names.size() > 1) {
                std::string aliases;
                for (std::size_t i = 1; i < cmd->names.size(); ++i) aliases += (i > 1 ? ", " : "") + cmd->names[i];
                chat::info(std::format("Alias : {}", aliases));
            }
            return;
        }
        std::vector<std::string> lines;
        for (const Command& cmd : manager.commands()) lines.push_back(std::format("{}{} - {}", prefix, cmd.names.front(), cmd.description));
        paginate(lines, args.empty() ? "" : args[0], "Toutes les commandes :", "help");
    }});

    manager.add({{"debug", "info"}, "debug", "Affiche ce que le bot lit du jeu (verifie les offsets)",
                 [](const Args&) { debugInfo(); }});

    manager.add({{"walk"}, "walk <ticks> [sprint]", "Test : avance tout droit pendant N ticks",
                 [&bot](const Args& args) {
                     if (args.empty()) throw CommandError("Usage : #walk <ticks> [sprint]");
                     int ticks = parseInt(args[0], "ticks");
                     bool sprint = args.size() > 1 && args[1] == "sprint";
                     requirePlayer();
                     bot.walkFor(ticks, sprint);
                     chat::info(std::format("Marche pendant {} ticks{}", ticks, sprint ? " en sprint" : ""));
                 }});

    manager.add({{"look"}, "look <yaw> <pitch>", "Test : force la rotation du joueur", [&bot](const Args& args) {
        if (args.size() < 2) throw CommandError("Usage : #look <yaw> <pitch>");
        bot.lookAt(static_cast<float>(parseDouble(args[0], "yaw")), static_cast<float>(parseDouble(args[1], "pitch")));
        chat::info("Rotation forcee (#stop pour arreter)");
    }});

    manager.add({{"stop", "cancel"}, "stop", "Arrete tout ce que fait le bot", [&bot](const Args&) {
        bot.cancelAll();
        chat::info("Arrete.");
    }});

    // Java ForceCancelCommand
    manager.add({{"forcecancel"}, "forcecancel", "Annule tout, meme un mouvement en cours", [&bot](const Args&) {
        bot.cancelAll();
        chat::info("ok, annulation forcee");
    }});

    // Java ComeCommand: to the camera entity (the player itself on Bedrock: no free camera)
    manager.add({{"come"}, "come", "Vient a la position de la camera (le joueur)", [&bot](const Args&) {
        auto& player = requirePlayer();
        bot.baritone().customGoal().setGoalAndPath(std::make_shared<baritone::GoalBlock>(feetPos(player)));
        chat::info("J'arrive");
    }});

    // Java GcCommand: free memory (here: the cached regions far from the player)
    manager.add({{"gc"}, "gc", "Libere la memoire (regions du cache loin du joueur)", [](const Args&) {
        auto cache = platform::WorldTracker::get().cache();
        auto* player = sdk::ClientInstance::get() ? sdk::ClientInstance::get()->localPlayer() : nullptr;
        if (cache && player) {
            sdk::BlockPos feet = sdk::BlockPos::floor(player->feet());
            cache->prune(feet.x >> 4, feet.z >> 4, 1);
        }
        chat::info("ok, memoire liberee");
    }});

    // Java RenderCommand: the game redraws the chunks; here the bot's own copy of the world is
    // read again (what it draws over the world comes from it)
    manager.add({{"render"}, "render", "Relit tous les chunks charges (copie du monde du bot)", [](const Args&) {
        platform::WorldTracker::get().markAllStale();
        chat::info("Fait");
    }});

    // Java LitematicaCommand / SchematicaCommand: build what those mods have open. They don't
    // exist on Bedrock: the files are built with #build.
    manager.add({{"litematica"}, "litematica [numero]", "Litematica n'existe pas sur Bedrock : utilise #build <fichier.litematic>",
                 [](const Args&) {
                     throw CommandError("Litematica n'existe pas sur Bedrock. Mets le fichier .litematic dans le dossier schematics et tape #build <nom>");
                 }});
    manager.add({{"schematica"}, "schematica", "Schematica n'existe pas sur Bedrock : utilise #build <fichier.schematic>",
                 [](const Args&) {
                     throw CommandError("Schematica n'existe pas sur Bedrock. Mets le fichier .schematic dans le dossier schematics et tape #build <nom>");
                 }});

    manager.add({{"unfault"}, "unfault", "Reactive le bot apres une erreur interceptee", [](const Args&) {
        safe::resetFault();
        chat::info("Bot reactive.");
    }});

    // Java SetCommand (+ the "modified" and "reset" aliases)
    manager.add({{"set", "setting", "settings"},
                 "set [list|modified [filtre] [page]] | set <nom> [valeur] | set toggle <nom> | set reset <nom|all> | set save | set load",
                 "Affiche ou modifie un setting (memes noms que Baritone)", [&bot](const Args& args) {
                     using namespace baritone;
                     std::string arg = args.empty() ? "list" : lowerCase(args[0]);
                     if (arg == "s" || arg == "save") {
                         bot.saveSettings();
                         chat::info("Settings enregistres");
                         return;
                     }
                     if (arg == "load" || arg == "ld") {
                         for (const SettingInfo& st : settingsTable())
                             if (isModified(st)) resetSetting(st);
                         bot.loadSettings();
                         chat::info("Settings recharges depuis settings.txt");
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
                         std::string header = std::format("Tous les settings{}{} :", viewModified ? " modifies" : "",
                                                          search.empty() ? "" : std::format(" contenant '{}'", search));
                         paginate(lines, args.size() > next ? args[next] : "", header,
                                  std::format("set {}{}", arg, search.empty() ? "" : " " + search));
                         return;
                     }
                     bool resetting = arg == "reset";
                     bool toggling = arg == "toggle";
                     if (resetting) {
                         if (args.size() < 2) {
                             chat::info("Indique 'all' pour confirmer que tu veux tout remettre par defaut");
                             chat::info("TOUS les settings seront remis par defaut. #set modified ou #modified montre ce qui sera remis.");
                             chat::info("Indique un nom de setting a la place de 'all' pour n'en remettre qu'un");
                             return;
                         }
                         if (lowerCase(args[1]) == "all") {
                             for (const SettingInfo& st : settingsTable())
                                 if (isModified(st)) resetSetting(st);
                             chat::info("Tous les settings sont remis par defaut");
                             bot.saveSettings();
                             return;
                         }
                     }
                     if (toggling && args.size() < 2) throw CommandError("Usage : #set toggle <nom>");
                     const std::string& settingName = (resetting || toggling) ? args[1] : args[0];
                     const SettingInfo* setting = findSetting(settingName);
                     if (!setting) throw CommandError(std::format("Setting inconnu : {}", settingName));
                     bool doingSomething = resetting || toggling;
                     if (!doingSomething && args.size() == 1) {
                         chat::info(std::format("Valeur du setting {} :", setting->name));
                         chat::info(settingValueToString(setting->ptr));
                         chat::info(std::format("({})", setting->description));
                         return;
                     }
                     std::string oldValue = settingValueToString(setting->ptr);
                     if (resetting) {
                         resetSetting(*setting);
                     } else if (toggling) {
                         auto* b = std::get_if<bool*>(&setting->ptr);
                         if (!b) throw CommandError(std::format("{} n'est pas un setting true/false", setting->name));
                         **b = !**b;
                         chat::info(std::format("Setting {} bascule a {}", setting->name, **b ? "true" : "false"));
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
                         chat::info(std::format("{} {} a {}", resetting ? "Remis" : "Regle", setting->name, settingValueToString(setting->ptr)));
                     }
                     chat::info(std::format("Ancienne valeur : {}", oldValue));
                     const Settings& now = Settings::get();
                     if ((setting->name == "chatControl" && !now.chatControl && !now.chatControlAnyway) ||
                         (setting->name == "chatControlAnyway" && !now.chatControlAnyway && !now.chatControl)) {
                         chat::error("Attention : les commandes sans prefixe ne marcheront plus. Pour revenir, utilise le prefixe.");
                     } else if (setting->name == "prefixControl" && !now.prefixControl) {
                         chat::error("Attention : les commandes avec prefixe ne marcheront plus. Pour revenir, tape la commande sans prefixe (chatControl).");
                     }
                     bot.saveSettings();
                 }});

    manager.add({{"modified", "mod", "baritone", "modifiedsettings"}, "modified", "Liste les settings modifies",
                 [&manager](const Args& args) {
                     std::string line = "set modified";
                     for (const auto& a : args) line += " " + a;
                     manager.runCommand(line, false);
                 }});

    manager.add({{"reset"}, "reset [all|nom]", "Remet tous les settings par defaut, ou un seul", [&manager](const Args& args) {
        std::string line = "set reset";
        for (const auto& a : args) line += " " + a;
        manager.runCommand(line, false);
    }});

    manager.add({{"unload"}, "unload", "Desinjecte la DLL proprement", [&bot](const Args&) {
        chat::info("Dechargement...");
        bot.cancelAll();
        bot.requestUnload();
    }});
}

}  // namespace command
