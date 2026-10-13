#include "core/Chat.h"

#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "baritone/Settings.h"
#include "core/Log.h"
#include "core/Notify.h"
#include "sdk/Game.h"

namespace chat {
namespace {

std::mutex g_mutex;
std::vector<std::string> g_pending;
std::atomic<bool> g_quiet{false};  // #quiet: messages go to the log only

// Bedrock formatting codes. Java: "[Baritone]", "[B]" with shortBaritonePrefix, nothing
// with useMessageTag (Java marks the message with a tag instead; Bedrock has no tags).
std::string prefix() {
    const auto& s = baritone::Settings::get();
    if (s.useMessageTag) return {};
    return s.shortBaritonePrefix ? "\xC2\xA7" "d[B]\xC2\xA7r " : "\xC2\xA7" "d[Baritone]\xC2\xA7r ";
}
constexpr std::string_view kRed = "\xC2\xA7" "c";

void push(std::string line) {
    std::lock_guard lock(g_mutex);
    g_pending.push_back(std::move(line));
}

}  // namespace

// logAsToast (Java Helper.logDirect): the message is shown as a popup instead of in the chat
void info(std::string_view message) {
    logger::info("[chat] {}", message);
    if (baritone::Settings::get().logAsToast) return notify::show("Baritone", message, false);
    push(std::format("{}{}", prefix(), message));
}

void error(std::string_view message) {
    logger::warn("[chat] {}", message);
    if (baritone::Settings::get().logAsToast) return notify::show("Baritone", message, true);
    push(std::format("{}{}{}", prefix(), kRed, message));
}

void setQuiet(bool quiet) { g_quiet = quiet; }

void flush() {
    std::vector<std::string> lines;
    {
        std::lock_guard lock(g_mutex);
        lines.swap(g_pending);
    }
    if (lines.empty() || g_quiet) return;
    auto* client = sdk::ClientInstance::get();
    auto* gui = client ? client->guiData() : nullptr;
    if (!gui) return;
    for (const std::string& line : lines) gui->displayClientMessage(line);
}

}  // namespace chat
