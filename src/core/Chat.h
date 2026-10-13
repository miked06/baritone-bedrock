#pragma once
#include <string_view>

// Messages shown in the game chat, prefixed like Baritone's "[Baritone]".
// Safe to call from any thread: messages are queued and printed on the next tick.
namespace chat {

void info(std::string_view message);
void error(std::string_view message);

// Called on the game thread every tick.
void flush();

}  // namespace chat
