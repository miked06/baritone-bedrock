#pragma once
#include "command/CommandManager.h"

class Bot;

namespace command {

void registerAll(CommandManager& manager, Bot& bot);

}  // namespace command
