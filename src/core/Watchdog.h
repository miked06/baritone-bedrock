#pragma once

// Detects a frozen game thread ("Minecraft ne repond pas") and writes its call stack to
// the log, so a hang can be traced back to the game or to the bot.
namespace watchdog {

void start();      // loader thread
void stop();       // before unloading
void heartbeat();  // game thread, every frame

}  // namespace watchdog
