# Changelog

All notable changes to Baritone Bedrock. Versions follow [semantic versioning](https://semver.org/).

## 1.7.0

Compatibility fix and Windows automation additions for **Minecraft Bedrock 26.63**,
Windows x64 package **`1.26.6301.0`**. Earlier packages require their matching older
release. Download `BaritoneBedrock.dll` and optionally `BaritoneControlPanel.exe`;
`BaritoneBedrock.pdb` provides crash symbols.
See the [1.7.0 feature and validation matrix](https://github.com/miked06/baritone-bedrock/blob/v1.7.0/docs/WINDOWS_NEXT.md)
and [native port notes](https://github.com/miked06/baritone-bedrock/blob/v1.7.0/docs/DEVELOPMENT.md#reverse-engineering-notes-2663).

### Added

- Production objectives with fresh resource planning, supported gathering, stock
  acquisition, confirmed recursive crafting and optional delivery.
- Local Bedrock shaped/shapeless recipe importer with skip/variant reports,
  explicit tag mappings and a 2048-output planning catalog limit.
- Multi-station furnace workshop with empty-station checks, distributed loading,
  later collection, explicit fuel budgets and no blind request retries.
- Manual schematic build hints, hotbar material selection and a static technical
  audit for unsupported states, components and block-entity contents.
- World/dimension-bound construction checkpoints retaining target descriptions,
  support columns and phase, with explicit recovery against current world policy.
- Native panel map of loaded/session-visited chunks, local markers, path and
  protection footprints, with world-bound destination submission.
- Road/bridge previews with bounded width/grade, headroom, optional lights and
  existing foundation-planner integration.
- Quantity-bounded crop-specific harvest jobs preserving base stems, including a
  harvest/store workflow template.
- Local diagnostic ZIP export and paused, inventory-aware workflow templates for
  building, gathering, production, loadouts and harvesting.

### Fixed

- Port the exact package profile to `1.26.6301.0`, relocate fixed workstation,
  inventory and empty-stack references, and validate them before hooks are installed.
  This fixes immediate unloading after the game updated from 26.60.
- Gathering objectives do not count ore blocks already in inventory as the
  requested ordinary resource output.
- Build templates load their own placement, and workflow verification blocks on
  missing, wrong or extra supported blocks instead of accepting any loaded survey.

### Validation and remaining work

- **139 C++ core tests and 5 Python importer tests passed**, including the
  AddressSanitizer core build. The DLL and panel compiled, and an independent ZIP
  reader validated exported archive fixtures.
- Native checks passed in the existing world: loading/reinjection, `#help` typed
  in Minecraft's chat, player/inventory inspection, short navigation and return,
  and server-confirmed full-source inventory transfers in both directions.
- Core tests, native UI checks and gameplay observations are tracked separately
  in the validation matrix. Multi-furnace execution, targeted harvests, complete
  production/delivery workflows, construction recovery and other new module
  combinations still need gameplay validation; this release does not certify
  every feature or guarantee zero bugs or lag.
- The installed package `1.26.6301.0` retains all 27 signature patterns, but the
  old fixed crafting/repair references differed. They are now updated; the exact
  package guard remains. Loading, navigation and reversible inventory moves pass
  in the existing world; broader gameplay validation remains pending.
- In-game textured ghosts, portable shulker logistics, villager trading and rail
  travel remain unimplemented. No adaptive performance controller was added.

## 1.6.0

Windows automation expansion for **Minecraft Bedrock 26.60**, Windows x64 package
**`1.26.6005.0`**. Download `BaritoneBedrock.dll` and optionally
`BaritoneControlPanel.exe`; `BaritoneBedrock.pdb` provides crash symbols.

### Added

- Recursive crafting plans and confirmed intermediate crafting, with an editable
  starter catalog of 18 recipe outputs.
- Inventory loadouts, reserved deposit quantities, fresh supply surveys, automatic
  builder restocking, compatible stack merging, chest sorting and last-seen search.
- Construction support/foundation plans with conditional temporary-support cleanup,
  and selected vein/tunnel/branch mining plans with hazard preflight.
- Local textured HTML/WebGL schematic export with resource-pack import, layers,
  opacity and orbit controls. No resource-pack textures are redistributed.
- Native visual workflow editor with conditions, bounded loops, failure branches
  and paused recovery; linked portal-journey continuation with exit validation.
- Tool replacement policy, explicit anvil/unenchanted-grindstone repair jobs and
  Mending jobs at an existing XP waypoint.
- Session block-action history with conditional undo of supported plain blocks,
  plus in-game and external read-only compatibility doctors.

### Fixed

- Open Creative inventory through the full native inventory-key handler on the
  frame thread, retaining normal request/response and screen ownership behavior.
- Reconcile partial/full-source stack merges using capacity and native receipts;
  address anvil inputs by network slots 1/2 and grindstone inputs by 16/17.
- Remove formerly mismatched conditional builder cells when they become ignored,
  so completed support cleanup can finish instead of pausing.
- Record the actual placed/replaced cell in history rather than the clicked
  support. Reject incompatible skip/substitution settings before planned jobs.

### Validation and limits

**124 core tests passed**, including an AddressSanitizer build. Native Creative
checks covered inventory swaps/merges, chest sorting, supply/loadout trips,
three-phase construction, one-block vein mining, conditional placement undo,
workflow branches, textured export and anvil preview/cost refusal/repair.

Survival execution, real multi-portal travel, complete automated grindstone
combining and live Mending progress at an XP source still need gameplay validation.
The in-game schematic overlay remains colored outlines; textured viewing uses the
local exported viewer and approximates complex geometry. No adaptive performance
controller is included, and the test results do not guarantee zero bugs or lag.

See the [expansion guide](https://github.com/miked06/baritone-bedrock/blob/v1.6.0/docs/WINDOWS_EXPANSION.md)
and [tool repair guide](https://github.com/miked06/baritone-bedrock/blob/v1.6.0/docs/TOOL_REPAIR.md)
for commands, supported cases and the detailed validation matrix.

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
