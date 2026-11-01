#pragma once
#include "command/CommandManager.h"
class Bot;
namespace command { void registerModuleCommands(CommandManager& manager, Bot& bot); }
