#pragma once
#include <optional>

// Detours on the game functions that drive the bot: level tick, input tick, chat send, mouse.
namespace gamehooks {

bool install();

// Clicks a mouse button (1 left, 2 right) through the game's mouse queue on its next mouse
// update: pressed then released one update later, like a real click.
void requestClick(int button);
// For the next `ms` milliseconds, right click on the first frame the game targets a block.
void autoUseWhileTargeting(int ms);
// Physical mouse events consumed by #click; polled only on the game frame thread.
struct SelectionClick { int button; bool pressed; };
void captureSelectionClicks(bool enabled);
std::optional<SelectionClick> pollSelectionClick();

}  // namespace gamehooks
