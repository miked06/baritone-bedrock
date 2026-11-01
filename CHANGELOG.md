# Changelog

All notable changes to Baritone Bedrock. Versions follow [semantic versioning](https://semver.org/).

## 1.5.0

Module expansion for Windows Bedrock package `1.26.6005.0`. Commands and limits are
documented in [the module guide](docs/MODULES.md); the
[validation roadmap](docs/MODULE_ROADMAP.md) distinguishes implemented behavior
from completed native gameplay checks.

### Added

- Incremental schematic verification, repair and inventory-aware material planning.
- Named schematic placements with persisted transforms and shared preview limits;
  block/state export to `.mcstructure`.
- Named storage locations and confirmed whole-stack deposit/withdrawal.
- Native recipe discovery and workstation crafting; furnace batch smelting with
  confirmed input, fuel and collected output.
- Persistent task queues, paused recovery, bounded retries and failure diagnostics.
- Native Windows control panel, including live status, schematic and task controls.
- Survival food/retreat assistance; selected-tree harvesting/replanting and animal
  breeding/harvesting with observed age, breeding and population checks.
- Protected areas enforced in pathing, block actions and module interactions.
- Quarry preflight and layered excavation with preserved access ramps, optional
  lighting/unloading and a final comparison against the quarry plan.
- Registered portal graph routing and coordinate scaling; boat and saddled-mount
  corridor planning, boarding, navigation and dismount processes.
- Repeatable gameplay scenarios with command/status assertions and JSON reports.

### Fixed

- Reconcile native container transfers from original stack snapshots and accepted
  server receipts without overwriting independent furnace updates.
- Address furnace fuel/output by network slot 1/2 while retaining local UI model
  index 0. Queue normal screen closure to prevent expired furnace UI bindings.
- Confirm entity interactions from observed state rather than the native dispatch
  return value, which can report false after successful feeding.
- Allow swimming toward the surface when the destination is still water and the
  block above it is clear air; continue rejecting solid ceilings and hazards.

The adaptive performance controller is excluded. Recursive ingredient crafting,
automatic continuation through portal transitions and textured schematic previews
are not included. Native validation remains incomplete for the scenarios identified
in the roadmap.

## 1.4.1

Stability/performance fixes for Minecraft Bedrock 26.60 (`1.26.6005.0`). See [the audit report](docs/STABILITY_AUDIT.md) for the measured scope and remaining gameplay validation.

### Fixed

- Read and decompress schematic files on a background worker; prevent stale placement edits/builds while loading, discard cancelled results and stop builds across world/dimension changes. Report placement failures without claiming a successful load.
- Yield large builder scans across ticks and compact completed positions into section bitmaps. Enforce breaking/placing permissions, including placement cost overrides and fluid restrictions.
- Start farming even when periodic scans are disabled; respect breaking, planting and selection settings without destroying cached scan results.
- Search sparse exploration allowlists directly instead of enumerating huge rings around distant targets.
- Avoid joining a busy cache writer during periodic save/prune requests; retain concurrently changed chunks for the next save. Skip zero-limit cache searches.
- Preserve block-state selectors and exact ore allowlists when restricting mining permissions.
- Reject corrupt gzip/zlib checksums, sizes and headers in schematic imports.
- Track the `#calc` worker, reject concurrent diagnostic calculations and join it before DLL unload.

### Added

- Opt-in `#perf` timings with fixed-size sample buffers, disabled by default.
- Regression coverage for these failures, layered/repeated builder scans, cancellation, cache updates and deterministic malformed NBT inputs.

## 1.4.0

Supports **Minecraft Bedrock 26.60 for Windows x64**, package **`1.26.6005.0`**. Download `BaritoneBedrock.dll` from the release assets; the `.pdb` file is optional for crash diagnostics.

### Added

- Built-in schematic workspace: preview without building, origin editing, rotation/mirror, active layers, material-position counts and construction of the exact placement. `#schematica` / `#litematica` now use this Bedrock alternative instead of reporting an unavailable mod.
- Color-coded translucent preview differences and bounds, with bounded live-chunk comparisons; placement state clears between worlds/dimensions.
- Separate Java and Bedrock feature inventories, per-feature qualifications and reproducible complete command inventories.
- Quoted command arguments for schematic filenames with spaces; conversion/data-loss notes for imported structures.

### Fixed

- Validate schematic volumes, palette indices and Sponge varints; reject truncated/negative NBT collection lengths before allocation. Check Litematica region overflow and invalid packed indices.
- Keep an existing placement after a failed load; report file errors in chat.
- Use the native current shader color rather than its dark-color companion, and mark color uniforms dirty when drawing/restoring them.

### Preview, then build

Put schematic files in `%LOCALAPPDATA%\BaritoneBedrock\schematics\`, then use:

```text
#schematic load "house.litematic" ~5 ~ ~
#schematic rotate 90
#schematic materials
#schematic build
```

### Validation and limits

- Windows x64 RelWithDebInfo build and **59 passing core tests**. Preview, placement editing, invalid-file handling and a one-block automatic build were checked in an existing Minecraft world; the test block was removed and settings restored.
- The preview uses colored translucent cubes and outlines, capped at 512 differences within 16 blocks, rather than textured block models. Java mod binaries, arbitrary block-entity/entity data and every Java block state are not reproduced.
- This is not full Java behavioral parity: seed-based Nether terrain prediction, native chat Tab insertion and rich Java chat integration remain unimplemented. Large-build and long-distance flight campaigns have not been fully revalidated.
- See the [schematic guide](https://github.com/miked06/baritone-bedrock/blob/v1.4.0/docs/SCHEMATICS.md) and [Java/Bedrock feature comparison](https://github.com/miked06/baritone-bedrock/blob/v1.4.0/docs/FEATURE_COMPARISON.md) for the exact scope.

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
