#include "command/SchematicCommands.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include "baritone/Settings.h"
#include "core/Bot.h"
#include "core/Chat.h"
#include "core/Paths.h"

namespace command {
namespace {
std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool supported(const std::filesystem::path& file) {
    auto ext = lower(file.extension().string());
    return ext == ".schem" || ext == ".schematic" || ext == ".litematic" || ext == ".mcstructure";
}
std::filesystem::path resolve(const std::string& name) {
    auto file = paths::dataDir() / "schematics" / name;
    if (!file.has_extension()) {
        for (const auto& ext : {"." + baritone::Settings::get().schematicFallbackExtension, std::string(".mcstructure"),
                               std::string(".schem"), std::string(".litematic"), std::string(".schematic")}) {
            auto candidate = file;
            candidate += ext;
            if (std::filesystem::is_regular_file(candidate)) { file = candidate; break; }
        }
    }
    if (!std::filesystem::is_regular_file(file)) throw CommandError("Schematic file not found: " + file.string());
    if (!supported(file)) throw CommandError("Supported files: .mcstructure, .schem, .litematic, .schematic");
    return file;
}
baritone::BlockPos feet(Bot& bot) {
    if (!bot.player().valid()) throw CommandError("Enter a world first");
    auto p = bot.player().position();
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y + 0.1251)), static_cast<int>(std::floor(p.z))};
}
baritone::BlockPos coordinates(const Args& args, std::size_t first, baritone::BlockPos base) {
    if (args.size() != first + 3) throw CommandError("Expected x y z (relative coordinates use ~)");
    return {parseCoord(args[first], base.x, "x"), parseCoord(args[first + 1], base.y, "y"), parseCoord(args[first + 2], base.z, "z")};
}
void info(Bot& bot) {
    auto& work = bot.schematics();
    if (!work.loaded()) throw CommandError("No schematic loaded. Use #schematic load <file>");
    const auto s = work.schematic()->size();
    chat::info(std::format("{}: {}x{}x{}, origin {}, rotation {} degrees, mirror {}, preview {}",
        work.name(), s.x, s.y, s.z, work.origin(), work.rotation() * 90,
        work.mirrorMode() == 1 ? "z" : work.mirrorMode() == 2 ? "x" : "none", work.previewEnabled() ? "on" : "off"));
    if (auto layer = work.layers()) chat::info(std::format("Active layers: {}..{} (zero-based; preview and build)", layer->first, layer->second));
}
void startBuild(Bot& bot) {
    if (bot.schematicLoading()) throw CommandError("A schematic is still loading; wait for Loaded before building");
    auto& work = bot.schematics();
    if (!work.loaded()) throw CommandError("No schematic loaded. Use #schematic load <file>");
    feet(bot);
    if (bot.baritone().pauser().paused()) throw CommandError("The bot is paused. Use #resume before building");
    auto size = work.schematic()->size(), at = work.origin();
    auto world = bot.baritone().world();
    if (!world || at.y < world->minY() || static_cast<int64_t>(at.y) + size.y > world->maxY())
        throw CommandError("The placement extends outside this dimension's build height. Use #schematic move");
    bot.cancelAll();
    // Already positioned and transformed: do not apply global file rotations,
    // orientation offsets or selection/map-art wrappers a second time.
    bot.baritone().builder().build(work.name(), work.schematic(), at, false, false);
    chat::info("Building the previewed placement. Use #pause, #resume or #stop to control it");
}
}
void registerSchematicCommands(CommandManager& manager, Bot& bot) {
    auto run = [&bot](const Args& args) {
        auto& work = bot.schematics();
        const std::string action = args.empty() ? "info" : lower(args[0]);
        if (action == "store" || action == "use" || action == "drop" || action == "placements") {
            if (bot.schematicLoading()) throw CommandError("Wait for the schematic load to finish");
            bot.prepareTasks();
            auto& catalog = bot.placements();
            if (action == "placements" && args.size() == 1) {
                for (const auto& [name, entry] : catalog.entries()) chat::info(std::format("{}: {}, origin {}, {}", name, entry.preset.file, entry.preset.origin, entry.resident ? "resident" : "saved preset"));
                return;
            }
            if (args.size() != 2) throw CommandError("Usage: #schematic store|use|drop name; #schematic placements");
            if (action == "use") { bot.selectPlacement(args[1]); if (!bot.schematicLoading()) info(bot); return; }
            if (action == "store") catalog.store(args[1], work);
            else if (action == "drop") catalog.remove(args[1]);
            else throw CommandError("Usage: #schematic placements");
            catalog.save(bot.worldDataPath("placements"));
            chat::info("Placement presets saved for this world and dimension"); return;
        }
        if (bot.schematicLoading() && action != "info" && action != "list" && action != "load" && action != "clear" && action != "unload")
            throw CommandError("A schematic is still loading; wait for Loaded before editing or building");
        auto stopBuild = [&] { if (bot.baritone().builder().isActive()) bot.cancelAll(); };
        if (action == "list") {
            if (args.size() != 1 && args.size() != 2) throw CommandError("Usage: #schematic list [page]");
            auto dir = paths::dataDir() / "schematics";
            std::filesystem::create_directories(dir);
            std::vector<std::string> names;
            for (const auto& entry : std::filesystem::directory_iterator(dir))
                if (entry.is_regular_file() && supported(entry.path())) names.push_back(entry.path().filename().string());
            std::ranges::sort(names);
            const int page = args.size() == 2 ? parseInt(args[1], "page") : 1;
            const int pages = std::max(1, static_cast<int>((names.size() + 7) / 8));
            if (page < 1 || page > pages) throw CommandError(std::format("Page must be 1..{}", pages));
            chat::info(std::format("Schematics ({}, page {}/{}): {}", names.size(), page, pages, dir.string()));
            for (std::size_t i = (page - 1) * 8; i < std::min(names.size(), static_cast<std::size_t>(page * 8)); ++i) chat::info(names[i]);
            return;
        }
        if (action == "load") {
            if (args.size() != 2 && args.size() != 5) throw CommandError("Usage: #schematic load <file> [x y z]");
            auto at = feet(bot);
            if (args.size() == 5) at = coordinates(args, 2, at);
            auto path = resolve(args[1]);
            bot.loadSchematicAsync(path.string(), at, true);
            return;
        }
        if (action == "info") {
            if (args.size() != 0 && args.size() != 1) throw CommandError("Usage: #schematic info");
            if (bot.schematicLoading()) {
                chat::info("A schematic is loading in the background. The previous placement remains until a successful load");
                if (!work.loaded()) return;
            }
            info(bot); return;
        }
        if (action == "build") {
            if (args.size() != 1) throw CommandError("Usage: #schematic build");
            startBuild(bot); return;
        }
        if (action == "verify" || action == "requirements") {
            if (args.size() == 2 && args[1] == "status") {
                const auto& survey = bot.schematicSurvey();
                chat::info(std::format("Comparison: {}/{} positions, {}", survey.scanned(), survey.volume(), survey.active() ? "running" : survey.ready() ? "complete" : "not started"));
                return;
            }
            if (args.size() != 1) throw CommandError("Usage: #schematic verify|requirements [status]");
            bot.startSchematicSurvey(action == "requirements"); return;
        }
        if (action == "repair") {
            if (args.size() != 1) throw CommandError("Usage: #schematic repair");
            startBuild(bot);
            chat::info("Repair compares each position again and modifies only differences permitted by your build settings");
            return;
        }
        if (action == "clear" || action == "unload") {
            if (args.size() != 1) throw CommandError("Usage: #schematic clear");
            bot.cancelSchematicLoad(); stopBuild(); work.clear(); chat::info("Schematic unloaded"); return;
        }
        if (action == "move") {
            if (!work.loaded()) throw CommandError("No schematic loaded");
            const auto at = args.size() == 2 && lower(args[1]) == "here" ? feet(bot) : coordinates(args, 1, work.origin());
            work.move(at); stopBuild(); info(bot); return;
        }
        if (action == "rotate") {
            if (args.size() != 2) throw CommandError("Usage: #schematic rotate <0|90|180|270>");
            const int angle = parseInt(args[1], "degrees");
            if (angle % 90 != 0) throw CommandError("Rotation must be a multiple of 90 degrees");
            work.rotate(angle / 90); stopBuild(); info(bot); return;
        }
        if (action == "mirror") {
            if (args.size() != 2) throw CommandError("Usage: #schematic mirror <none|x|z>");
            const auto mode = lower(args[1]);
            if (mode != "none" && mode != "x" && mode != "z") throw CommandError("Mirror: none, x or z");
            work.mirror(mode == "z" ? 1 : mode == "x" ? 2 : 0); stopBuild(); info(bot); return;
        }
        if (action == "layer") {
            if (args.size() == 2 && lower(args[1]) == "all") work.layer(std::nullopt);
            else {
                if (args.size() != 2 && args.size() != 3) throw CommandError("Usage: #schematic layer <all|y|yMin yMax>");
                int y = parseInt(args[1], "layer");
                work.layer(std::pair{y, args.size() == 3 ? parseInt(args[2], "last layer") : y});
            }
            stopBuild(); info(bot); return;
        }
        if (action == "preview") {
            if (!work.loaded()) throw CommandError("No schematic loaded");
            if (args.size() != 2 || (lower(args[1]) != "on" && lower(args[1]) != "off")) throw CommandError("Usage: #schematic preview <on|off>");
            work.setPreview(lower(args[1]) == "on"); info(bot); return;
        }
        if (action == "materials") {
            if (args.size() > 2) throw CommandError("Usage: #schematic materials [page]");
            const int page = args.size() == 2 ? parseInt(args[1], "page") : 1;
            auto counts = work.materials();
            const int pages = std::max(1, static_cast<int>((counts.blocks.size() + 7) / 8));
            if (page < 1 || page > pages) throw CommandError(std::format("Page must be 1..{}", pages));
            chat::info(std::format("Block positions by material (page {}/{}; full active placement, not remaining items):", page, pages));
            int i = 0;
            for (const auto& [name, count] : counts.blocks) {
                if (i >= (page - 1) * 8 && i < page * 8) chat::info(std::format("{}: {}", name, count));
                ++i;
            }
            chat::info(std::format("Air positions: {}, ignored positions: {}. Multi-block/double-slab placements can need different item counts", counts.air, counts.ignored));
            return;
        }
        throw CommandError("Actions: list, load, info, move, rotate, mirror, layer, preview, materials, build, clear");
    };
    manager.add({{"schematic", "schem", "blueprint"},
        "schematic <list|load <file> [x y z]|info|move <x y z|here>|rotate <degrees>|mirror <none|x|z>|layer <all|y|yMin yMax>|preview <on|off>|materials [page]|build|clear>",
        "Load and preview a schematic, adjust its placement, then build it", run});
    auto bridge = [run, &bot](const Args& args) {
        if (args.empty() || (args.size() == 1 && args.front() == "1")) { startBuild(bot); return; }
        const auto action = lower(args[0]);
        static const std::vector<std::string> actions{"list", "load", "info", "move", "rotate", "mirror", "layer", "preview", "materials", "build", "clear", "unload"};
        if (std::ranges::find(actions, action) != actions.end()) { run(args); return; }
        Args load{"load"}; load.insert(load.end(), args.begin(), args.end()); run(load);
    };
    manager.add({{"schematica"}, "schematica [file [x y z]|action]", "Preview a file, or build the currently loaded Bedrock schematic", bridge});
    manager.add({{"litematica"}, "litematica [file [x y z]|action|1]", "Preview a Litematica file, or build the currently loaded Bedrock schematic", bridge});
}
} // namespace command
