<h1 align="center">Baritone Bedrock</h1>

<p align="center">
  <b>The <a href="https://github.com/cabaletta/baritone">Baritone</a> pathfinding bot, ported to Minecraft Bedrock Edition for Windows.</b><br>
  Walk, mine, build, farm, explore and fly with an elytra — automatically, from chat commands.
</p>

<p align="center">
  <a href="https://miked06.github.io/bartione-bedrock-website/"><b>Website</b></a> ·
  <a href="https://github.com/miked06/baritone-bedrock/releases/latest"><b>Download</b></a> ·
  <a href="docs/COMMANDS.md"><b>Commands</b></a> ·
  <a href="docs/SETTINGS.md"><b>Settings</b></a>
</p>

<p align="center">
  <a href="https://github.com/miked06/baritone-bedrock/actions/workflows/build.yml"><img alt="Build" src="https://github.com/miked06/baritone-bedrock/actions/workflows/build.yml/badge.svg"></a>
  <a href="COPYING.LESSER"><img alt="License: LGPL-3.0" src="https://img.shields.io/badge/license-LGPL--3.0-blue.svg"></a>
  <img alt="Minecraft Bedrock 26.52" src="https://img.shields.io/badge/Minecraft%20Bedrock-26.52-green.svg">
  <img alt="Platform: Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-orange.svg">
</p>

---

Baritone Bedrock is a native C++ rewrite of Baritone that runs **inside** the Windows version of Minecraft Bedrock as a single DLL. No resource pack, no behavior pack, no server plugin: inject the DLL and type `#goto 1000 64 -200` in the chat.

The pathfinding engine, the movements, the processes and all the settings follow Baritone's Java code (1.21.11) closely, so the commands and settings you know from Java Edition work the same way.

> [!IMPORTANT]
> **Supported game version: Minecraft Bedrock 26.52 for Windows** (package `1.26.5203.0`).
> The DLL reads the game's memory directly, so other versions need updated signatures and offsets (see [Updating to a new game version](docs/DEVELOPMENT.md#updating-to-a-new-game-version)).

> [!WARNING]
> Using a bot on a multiplayer server may be against its rules and can get you banned. This project is meant for singleplayer worlds and servers that allow it. Use it at your own risk.

## Features

| | |
|---|---|
| 🧭 **Pathfinding** | A* over the real world with Baritone's movements: walk, sprint, ascend, descend, fall, diagonal, pillar, parkour (also diagonally), ladders, swimming, breaking and placing blocks on the way. Long trips are planned in segments, through chunks remembered in a long-distance cache. |
| ⛏️ **Mining** | `#mine diamond` finds ores (loaded chunks + cache), mines them and picks up the drops. With no ore in sight it goes down to the right level and branch-mines. Ore families (`deepslate_*`, `nether_gold_ore`...) and item names are understood. |
| 🏗️ **Building** | `#build` loads `.mcstructure`, `.schem` (Sponge), `.litematic` and `.schematic` files, converts Java block names to Bedrock and reproduces block orientation (stairs, logs, slabs, furnaces, chests...). `#sel` builds boxes, walls, spheres and cylinders. |
| 🌾 **Farming** | `#farm` harvests ripe crops, replants, uses bone meal and picks everything up. |
| 🪂 **Elytra** | `#elytra` plans a 3D flight path, steers by simulating the elytra physics, fires rockets when needed, swaps a worn elytra and lands safely. |
| 💧 **Water bucket clutch** | Falls of up to 20 blocks are planned with a water bucket: the bot places the water just before landing and picks it back up. |
| 👣 **Follow, explore, more** | `#follow`, `#explore`, `#tunnel`, `#surface`, `#axis`, `#click`, `#come`, waypoints (`#wp`, automatic `death` and `bed` waypoints), `#find`... |
| 🎨 **Rendering** | The path, the goal, the blocks to break or place and the selections are drawn in the world with the game's own renderer. |
| ⚙️ **Settings** | All of Baritone's settings, with the same names and defaults, saved between sessions. See [docs/SETTINGS.md](docs/SETTINGS.md). |

## Getting started

### Requirements

- Windows 10 or 11, x64.
- Minecraft Bedrock **26.52** (the Microsoft Store / Xbox app version).
- A DLL injector (any `LoadLibrary` injector works, for example FateInjector).

### Install

1. Download `BaritoneBedrock.dll` from the [latest release](https://github.com/miked06/baritone-bedrock/releases/latest), or [build it yourself](docs/BUILDING.md).
2. Start Minecraft and open a world.
3. In Minecraft's settings, turn off **"Pause game when focus is lost"**. Otherwise the world (and the bot) stops whenever you switch windows.
4. Inject `BaritoneBedrock.dll` into `Minecraft.Windows.exe` with your injector.
5. A console window titled *Baritone Bedrock* opens, and the chat shows `[Baritone] Charge` once the bot is ready.

Type commands in the chat, starting with `#`. They are handled locally and never sent to the server. `#unload` removes the bot cleanly.

> [!NOTE]
> The bot's chat messages are currently in French (for example `Arrive !` for "Arrived!"). Commands and settings use Baritone's English names.

### First commands

```text
#goto 100 64 -300          walk to a position (~ for relative: #goto ~10 ~ ~)
#goto crafting_table       walk to the closest crafting table
#mine 32 diamond           mine 32 diamonds and pick them up
#follow player Steve       follow a player
#build house.mcstructure   build a schematic from the schematics folder
#elytra 2000 -500          fly there with an elytra and fireworks
#stop                      stop everything
#help                      list every command
```

See the full list in **[docs/COMMANDS.md](docs/COMMANDS.md)**.

### Files

Everything the bot writes is in `%LOCALAPPDATA%\BaritoneBedrock\`:

| Path | Content |
|---|---|
| `log.txt` | The log of the last session (attach it to bug reports) |
| `settings.txt` | The settings you changed, loaded on every injection |
| `waypoints\<world>.txt` | Waypoints of each world |
| `schematics\` | Files for `#build` |
| `cache\<world>\dim<n>\` | The long-distance cache (2 bits per block) |

## Documentation

| | |
|---|---|
| [Commands](docs/COMMANDS.md) | Every command with its arguments and examples |
| [Settings](docs/SETTINGS.md) | All settings, their defaults and meaning |
| [Differences with Baritone Java](docs/PARITY.md) | What works the same, what differs on Bedrock and why |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common problems and how to fix them |
| [Building](docs/BUILDING.md) | Compiling the DLL and running the tests |
| [Architecture](docs/ARCHITECTURE.md) | How the bot is organized and how it talks to the game |
| [Development](docs/DEVELOPMENT.md) | Dev tools, reverse engineering notes, updating to a new game version |
| [Changelog](CHANGELOG.md) | What changed in each version |
| [Contributing](CONTRIBUTING.md) | How to report bugs and send changes |

## How it works, in short

The DLL hooks a few functions of the game (tick, input, camera, chat, rendering) with [MinHook](https://github.com/TsudaKageyu/minhook), found by byte signatures with [libhat](https://github.com/BasedInc/libhat). Every tick it copies the chunks around the player into a snapshot that the pathfinder reads on its own thread. The bot then plays like a player would: it presses keys, turns the camera, clicks, and uses the game's own functions to break and place blocks — so the server sees normal player actions.

The pathfinding core (`src/baritone/`) does not depend on the game at all and is covered by offline tests on simulated worlds. More in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Status

The port covers Baritone's features and has been tested in game on 26.52: long trips, mining in the overworld and the nether, building schematics, farming, elytra flights, water bucket falls. Some things cannot be done the same way on Bedrock (for example the elytra terrain prediction from the world seed); they are listed in [docs/PARITY.md](docs/PARITY.md).

Found a bug? Please [open an issue](https://github.com/miked06/baritone-bedrock/issues/new/choose) with your `log.txt`.

## Credits

- [**Baritone**](https://github.com/cabaletta/baritone) by cabaletta and contributors — the original bot this project ports. Its design, algorithms, costs and settings are reproduced here.
- [**Latite Client**](https://github.com/LatiteClient/Latite) — reference for many of the game signatures and SDK layouts.
- [**MinHook**](https://github.com/TsudaKageyu/minhook), [**libhat**](https://github.com/BasedInc/libhat), [**EnTT**](https://github.com/skypjack/entt) — hooking, signature scanning and the game's ECS layout.

## License

Baritone Bedrock is free software under the **GNU Lesser General Public License v3.0**, like Baritone itself. See [COPYING.LESSER](COPYING.LESSER) and [COPYING](COPYING).

This project is not affiliated with, endorsed by or associated with Mojang Studios or Microsoft. "Minecraft" is a trademark of Mojang Synergies AB.
