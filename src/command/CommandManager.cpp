#include "command/CommandManager.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>
#include <stdexcept>

#include "baritone/Settings.h"
#include "baritone/world/BlockProps.h"
#include "baritone/utils/CommandLine.h"
#include "core/Chat.h"
#include "core/Log.h"

namespace command {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

void CommandManager::add(Command command) {
    commands_.push_back(std::move(command));
}

const Command* CommandManager::find(std::string_view name) const {
    std::string key = lower(name);
    for (const Command& cmd : commands_) {
        if (std::ranges::find(cmd.names, key) != cmd.names.end()) return &cmd;
    }
    return nullptr;
}

std::vector<std::string> CommandManager::complete(std::string_view input) const {
    while (!input.empty() && input.front() == ' ') input.remove_prefix(1);
    std::vector<std::string> words;
    try { words = baritone::commandArguments(input, true); }
    catch (const std::invalid_argument&) { return {}; }
    if (words.empty() || input.ends_with(' ')) words.emplace_back();
    const std::string prefix = lower(words.back());
    std::vector<std::string> candidates;
    auto settings = [&](bool booleansOnly = false, bool modifiedOnly = false) {
        for (const auto& setting : baritone::settingsTable()) {
            if (booleansOnly && !std::holds_alternative<bool*>(setting.ptr)) continue;
            if (modifiedOnly && !baritone::isModified(setting)) continue;
            candidates.emplace_back(setting.name);
        }
    };
    auto values = [&](std::string_view name) {
        const auto* setting = baritone::findSetting(name);
        if (!setting) return;
        if (std::holds_alternative<bool*>(setting->ptr)) candidates = {"false", "true"};
        if (setting->name == "buildSchematicRotation") candidates = {"NONE", "CLOCKWISE_90", "CLOCKWISE_180", "COUNTERCLOCKWISE_90"};
        if (setting->name == "buildSchematicMirror") candidates = {"NONE", "LEFT_RIGHT", "FRONT_BACK"};
    };
    auto blocks = [&] {
        const auto& registry = baritone::BlockRegistry::get();
        for (std::size_t i = 0, count = registry.size(); i < count; ++i) {
            std::string name = registry.props(static_cast<baritone::StateId>(i)).name;
            if (prefix.find(':') == std::string::npos && name.starts_with("minecraft:")) name.erase(0, 10);
            candidates.push_back(std::move(name));
        }
    };
    if (words.size() == 1) {
        for (const auto& command : commands_) candidates.insert(candidates.end(), command.names.begin(), command.names.end());
        settings();
    } else {
        const Command* command = find(words.front());
        const std::string name = command ? command->names.front() : "";
        if (name.empty() && words.size() == 2) values(words.front());
        else if (name == "set") {
            if (words.size() == 2) {
                settings();
                candidates.insert(candidates.end(), {"list", "modified", "toggle", "reset", "save", "load"});
            } else if (words.size() == 3 && lower(words[1]) == "toggle") settings(true);
            else if (words.size() == 3 && lower(words[1]) == "reset") { settings(false, true); candidates.emplace_back("all"); }
            else if (words.size() == 3 && (lower(words[1]) == "list" || lower(words[1]) == "modified")) settings(false, lower(words[1]) == "modified");
            else if (words.size() == 3) values(words[1]);
        } else if (name == "reset") { settings(false, true); candidates.emplace_back("all"); }
        else if (name == "modified") settings(false, true);
        else if (name == "help") {
            for (const auto& cmd : commands_) candidates.insert(candidates.end(), cmd.names.begin(), cmd.names.end());
        } else if (name == "mine" || name == "goto" || name == "find" || name == "pickup") blocks();
        else if (name == "follow" && words.size() == 2) candidates = {"players", "player", "entities", "entity"};
        else if (name == "wp" && words.size() == 2) candidates = {"list", "save", "info", "delete", "clear", "restore", "goal", "goto"};
        else if (name == "elytra" && words.size() == 2) candidates = {"reset", "repack", "supported"};
        else if (name == "click" && words.size() == 2) candidates = {"cancel", "here"};
        else if (name == "schematic" || name == "schematica" || name == "litematica") {
            if (words.size() == 2) candidates = {"list", "load", "info", "move", "rotate", "mirror", "layer", "preview", "materials", "build", "clear"};
            else if (words.size() == 3 && lower(words[1]) == "rotate") candidates = {"0", "90", "180", "270"};
            else if (words.size() == 3 && lower(words[1]) == "mirror") candidates = {"none", "x", "z"};
            else if (words.size() == 3 && lower(words[1]) == "preview") candidates = {"on", "off"};
            else if (words.size() == 3 && lower(words[1]) == "layer") candidates = {"all"};
            else if (words.size() == 3 && lower(words[1]) == "move") candidates = {"here"};
        }
        else if (name == "sel") {
            if (words.size() == 2) candidates = {"pos1", "pos2", "clear", "undo", "set", "walls", "shell", "sphere", "hsphere", "cylinder", "hcylinder", "cleararea", "replace", "copy", "paste", "expand", "contract", "shift"};
            else if (words[1] == "expand" || words[1] == "contract" || words[1] == "shift")
                candidates = {"all", "newest", "oldest", "north", "south", "east", "west", "up", "down"};
            else if (words[1] == "set" || words[1] == "walls" || words[1] == "shell" || words[1].ends_with("sphere") || words[1] == "replace") blocks();
            else if (words[1].ends_with("cylinder")) {
                if (words.size() == 3) blocks(); else candidates = {"x", "y", "z"};
            }
        }
    }
    if (completionProvider_) {
        auto extra = completionProvider_(words);
        candidates.insert(candidates.end(), extra.begin(), extra.end());
    }
    std::erase_if(candidates, [&](const auto& candidate) { return !lower(candidate).starts_with(prefix); });
    std::ranges::sort(candidates, [](const auto& a, const auto& b) { return lower(a) < lower(b); });
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

bool CommandManager::execute(std::string_view line) {
    if (line.empty() || line.front() != kPrefix) return false;
    line.remove_prefix(1);
    if (!runCommand(line) && !line.empty()) {
        chat::error(std::format("Unknown command: {}. Type #help", line.substr(0, line.find(' '))));
    }
    return true;
}

bool CommandManager::onChat(std::string_view msg) {
    const baritone::Settings& s = baritone::Settings::get();
    if (msg.empty() || msg.front() == '/') return false;  // vanilla commands
    if (s.prefixControl && !s.prefix.empty() && msg.starts_with(s.prefix)) {
        std::string_view commandStr = msg.substr(s.prefix.size());
        if (!runCommand(commandStr) && !trim(commandStr).empty()) {
            chat::error(std::format("Unknown command: {}. Type {}help", commandStr.substr(0, commandStr.find(' ')), s.prefix));
        }
        return true;
    }
    if (s.chatControl || s.chatControlAnyway) return runCommand(msg);
    return false;
}

// Java logRanCommand: "> command args" (censorRanCommands: "> command ...")
void CommandManager::logRanCommand(std::string_view command, std::string_view rest) {
    const baritone::Settings& s = baritone::Settings::get();
    if (!s.echoCommands) return;
    chat::info(std::format("> {}{}", command, s.censorRanCommands ? std::string(" ...") : std::string(rest)));
}

bool CommandManager::runCommand(std::string_view msg, bool echo) {
    lastError_.clear();
    msg = trim(msg);
    if (msg.empty()) return runCommand("help", echo);
    Args words;
    try { words = baritone::commandArguments(msg); }
    catch (const std::invalid_argument& error) { lastError_ = error.what(); chat::error(error.what()); return true; }
    const std::string& name = words.front();
    std::string_view rest = msg.substr(std::min(msg.size(), name.size()));
    Args args(words.begin() + 1, words.end());

    // a setting name alone shows it (a boolean is toggled), with one value sets it
    if (const baritone::SettingInfo* setting = baritone::findSetting(name); setting && args.size() <= 1 && !find(name)) {
        if (echo) logRanCommand(name, rest);
        if (args.empty()) {
            if (std::holds_alternative<bool*>(setting->ptr)) return runCommand(std::format("set toggle {}", setting->name), false);
            return runCommand(std::format("set {}", setting->name), false);
        }
        return runCommand(std::format("set {} {}", setting->name, args[0]), false);
    }

    const Command* cmd = find(name);
    if (!cmd) return false;
    if (echo) logRanCommand(name, rest);
    logger::info("Command: {}", msg);
    try {
        cmd->run(args);
    } catch (const CommandError& e) {
        lastError_ = e.what();
        chat::error(e.what());
    } catch (const std::invalid_argument& e) {
        lastError_ = e.what();
        chat::error(e.what());
    } catch (const std::exception& e) {
        lastError_ = e.what();
        logger::error("Command {} threw: {}", name, e.what());
        chat::error(baritone::Settings::get().verboseCommandExceptions ? std::format("Internal error: {}", e.what())
                                                                      : "Internal error (details in log.txt)");
    }
    return true;
}

int parseInt(const std::string& s, std::string_view what) {
    int value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        throw CommandError(std::format("Invalid {}: '{}'", what, s));
    }
    return value;
}

double parseDouble(const std::string& s, std::string_view what) {
    double value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        throw CommandError(std::format("Invalid {}: '{}'", what, s));
    }
    return value;
}

int parseCoord(const std::string& s, int base, std::string_view what) {
    if (!s.empty() && s.front() == '~') {
        return s.size() == 1 ? base : base + parseInt(s.substr(1), what);
    }
    return parseInt(s, what);
}

}  // namespace command
