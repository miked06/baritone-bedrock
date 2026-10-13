# Architecture

Baritone Bedrock is one DLL with two halves:

- a **game-independent core** (`src/baritone/`), a C++ port of Baritone's pathfinding, movements, behaviors and processes, built as a static library and tested offline;
- a **platform layer** that connects the core to the running game: hooks, game SDK, world copy, input, rendering and commands.

```text
src/
├── dllmain.cpp       entry point: resolve signatures, install hooks, start the bot
├── baritone/         the core (no game code here)
│   ├── api/          PlayerContext: everything the core needs from the player (interface)
│   ├── pathing/      A* (calc/), goals, movements (costs + execution), PathExecutor
│   ├── behavior/     PathingBehavior (threaded calculation, segments), InventoryBehavior (ToolSet)
│   ├── process/      custom goal, mine, follow, explore, farm, builder, backfill, elytra...
│   ├── cache/        long-distance cache (CachedChunk/Region/World), world scanner, waypoints
│   ├── elytra/       3D flight path finder and elytra physics
│   ├── schematic/    schematic formats, masks, rotation/mirror, Bedrock orientation rules
│   ├── world/        world snapshot, block properties, BlockStateInterface
│   └── Settings.*    every Baritone setting
├── platform/         bridge game -> core: WorldTracker, GamePlayerContext, block classifier, renderer
├── sdk/              game classes: offsets, vtable indices, EnTT components, NBT, rendering
├── hooks/            the game functions the bot hooks
├── memory/           signatures, hooking, safe memory access
├── core/             Bot (the tick loop), chat, logs, look behavior, input override, watchdog, dev pipe
└── command/          the # commands
tests/                offline tests of the core on simulated worlds
tools/                Python helpers: injection, dev commands, memory dump, docs generator
```

## One tick

The game calls our hooks on its client thread. Every game tick:

1. **`WorldTracker`** copies the chunks around the player into a `WorldSnapshot` (block state ids, about 1 ms per chunk, within a time budget). Chunks leaving the area are packed into the long-distance cache.
2. **`Baritone::tick`** runs the processes (`PathingControlManager` picks the one in control by priority), which return a `PathingCommand` (a goal and what to do with it).
3. **`PathingBehavior`** starts path calculations on a worker thread when needed, and drives the `PathExecutor` of the current segment.
4. The executor asks the current **movement** what to do: keys to hold, where to look, blocks to break or place.
5. **`Bot`** applies this through the game: simulated key presses (`InputOverride`, read by the game's input system), the camera rotation (`LookBehavior`), and `GameMode` calls to break and place blocks.

Path calculations only read the snapshot, never the live game, so they can run on another thread safely. Everything that touches the game happens during the game's tick.

## Talking to the game

- **Finding functions**: byte signatures (`memory/Signatures.h`) scanned with libhat when the DLL loads. Many come from the [Latite](https://github.com/LatiteClient/Latite) client's research.
- **Hooks** (`hooks/GameHooks.cpp`, MinHook): the level tick, the input update, the player's camera update, the frame update, chat sending, the level renderer, mouse input, the inventory request answers.
- **Data**: class members at known offsets (`sdk/Offsets.h`), virtual functions by vtable index, and the game's EnTT registry for components such as movement input, game mode, attributes, effects, equipment. A component's type id is the FNV-1a hash of its name (`ActorGameTypeComponent`...).
- **Safety**: every hook body runs under a structured exception handler (`core/Safe`); a fault turns the bot off (`#unfault`) instead of crashing the game. A watchdog logs the game thread's stack if it hangs.

## Acting like a player

The bot avoids calling internal game functions it does not fully understand: they are the main source of freezes and desyncs. It prefers what a player does:

- **Movement**: it sets the same input component the keyboard sets.
- **Looking**: it rotates the player (and the camera, in client look mode).
- **Using items** (fireworks, bone meal, buckets): it feeds a real right click into the game's mouse queue, so the game does exactly what it does for a player, including the server transaction.
- **Breaking and placing blocks**: it calls `GameMode::startDestroyBlock` / `continueDestroyBlock` / `buildBlock`, the functions the game's own input handler calls.
- **Inventory**: it sends `ItemStackRequest`s like the inventory screen; the client is updated once the server accepts.

## The long-distance cache

Each chunk the bot has seen is stored in 2 bits per block (air, water, avoid, solid) plus the positions of tracked blocks, grouped in 32×32 regions saved to disk (run-length encoded) under `%LOCALAPPDATA%\BaritoneBedrock\cache`. `BlockStateInterface` reads loaded chunks exactly and falls back to the cache for the rest, so the pathfinder can plan through unloaded terrain like Java Baritone.

## Rendering

`PathRenderer` collects lines on the game thread (path, goal, boxes, selections, elytra path) and draws them from the `LevelRenderer` hook with the game's own tessellator and the `selection_box` material.
