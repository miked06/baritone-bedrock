# Changelog

All notable changes to Baritone Bedrock. Versions follow [semantic versioning](https://semver.org/).

## 1.3.0

Compared against Baritone Java 26.3 at `25111daedf1d59e6a8dfb5a3e61885cdb8d953df`. See [the feature audit](docs/JAVA_PARITY_AUDIT.md) for scope, verification and remaining differences.

### Added

- Block-state selectors for searching/mining and selection placement, named settings profiles, the `#c` alias, interactive `#click` goals/drag selections, and `#complete` suggestions.
- Normal asynchronous disconnect on arrival, configurable depth rendering, translucent cached-terrain surfaces, filled goal beams, and flight ray/hitbox diagnostics.
- Bounded chunk packing, flight cache culling, and an arena allocator option for flight searches.

### Fixed

- Replacement masks persist after breaking the old block and reset through schematic wrappers/repeats.
- Tool planning respects `autoTool=false`; inventory pickaxe retrieval considers efficiency and remaining durability. Water costs account for Depth Strider.
- `extendCacheOnThreshold` controls scans and rejects stale cached locations. Failed cache writes stay dirty so memory pruning cannot discard unsaved data.
- Missing profile files preserve current settings; overflowing vector/color components are rejected.

## 1.2.0

Supports Minecraft Bedrock 26.60 for Windows (package `1.26.6005.0`). This build replaces support for package `1.26.5203.0`.

### Changed

- All built-in output is now in English: command help and argument names, chat messages, process status, notifications, diagnostics, and settings/cache/schematic errors.

### Fixed

- Updated 13 function/global signatures, client map layout, player and world offsets, and inventory request offsets for 26.60.
- Updated the render context and camera origin offsets, and the scalar `Tessellator::color` calling convention. Using the previous tessellator offset could corrupt a lifetime control block and crash the game on a later frame.
- Main-inventory transfers use the game's server-authorized inventory-open request, wait for the response, and close the temporary screen. Bot movement pauses during the transfer; an inventory opened by the player stays open. Updated `#ism` diagnostics for the new manager layout.

### Added

- Exact package-version check before accessing game objects or installing hooks. Unsupported packages are logged and the DLL unloads.
- Runtime rejection of ambiguous signatures and an offline checker, `tools/sigcheck.py`, with optional JSON output.
- Reverse engineering notes for 26.60 in `docs/DEVELOPMENT.md`.

### Validation

- RelWithDebInfo build and offline core tests pass.
- All 27 signatures match uniquely in the 26.60 dump and resolve during injection.
- Checked in game: player/world diagnostics, entity listing, short navigation over uneven terrain, goal rendering, survival block placement and mining with automatic tool selection, durability and drops, hotbar/main inventory transfers (empty and occupied slots), automatic pickaxe retrieval, a one-block selection build with automatic material retrieval, and unload/reinjection.
- Complete farming, schematic-building, elytra and water-clutch scenarios have not been repeated on 26.60.

## 1.1.0

Supports Minecraft Bedrock 26.52 (package 1.26.5203.0), like 1.0.0.

### Added

- **Diagonal parkour.** The bot can now jump over a gap along a diagonal: 2 blocks diagonally, 3 when it can sprint, landing at the same height. Before, two blocks that only met diagonally across a gap had no path at all, because Baritone only jumps along the four axes. It never jumps over lava, needs head room and clear corner cells (the body sweeps through them), and follows `allowParkour`.
- **Thick render lines.** `pathRenderLineWidthPixels`, `goalRenderLineWidthPixels` and `selectionLineWidth` are now applied: lines wider than one pixel are drawn as two crossed ribbons, so the path, the goal and the selections are easy to see in game.
- `#quiet [on|off]` developer command: hides the bot's chat messages (they still go to `log.txt`), handy for screenshots and videos.

### Fixed

- **Builder:** when a path has to place a block where the schematic wants one (for example bridging over a gap that is part of the build), the bot now uses the schematic's block, like Baritone's `placeAt`. Before, it only looked for throwaway blocks and could get stuck.
- **Builder:** when the path to the blocks to place keeps failing, the bot now says so once in the chat, with the usual cause: no throwaway blocks to pillar or bridge with. Before, it retried silently forever.

### Changed

- `#proc` shows when a path calculation is in progress.
- With `chatDebug`, each path calculation reports its result (success, segment, failure and why) and the builder lists the goals it is working towards.

### Known issues

- A build whose lowest blocks float above the ground can stay without any goal to walk to before it is finished.
- During some elytra flights the automatic firework clicks are not detected, and the bot asks the player to fire the rockets.
- Thick lines have a width in blocks, so a path segment passing right next to the camera looks very wide.
- When the player's game mode is Bedrock's "Default" (the world's default mode), creative mode is not detected; use `/gamemode creative` explicitly.

## 1.0.0

First release: Baritone ported to Minecraft Bedrock 26.52 for Windows as an injected DLL.

- Pathfinding with Baritone's A*, cost model and movements (walk, sprint, ascend, descend, fall, diagonal, pillar, parkour, ladders, swimming), long trips in segments, long-distance chunk cache.
- Processes: `#goto`, `#mine`, `#follow`, `#explore`, `#farm`, `#build` (`.mcstructure`, `.schem`, `.litematic`, `.schematic`), `#sel`, `#tunnel`, `#elytra`, waypoints, backfill.
- Water bucket falls, elytra flight with automatic fireworks and safe landing, inventory management.
- All of Baritone's commands and settings, rendering of the path, goals and selections, Windows notifications.
- See [docs/PARITY.md](docs/PARITY.md) for the differences with Baritone Java.
