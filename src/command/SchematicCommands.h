#pragma once
#include "command/CommandManager.h"
class Bot;
namespace command {
void registerSchematicCommands(CommandManager& manager, Bot& bot);
}
