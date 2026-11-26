# Changelog

All notable changes to Baritone Bedrock. Versions follow [semantic versioning](https://semver.org/).

## Unreleased

- **Parkour under candles and ceilings.** Jumps with a block or a candle 3 blocks
  above them were refused (Baritone's head-bump rule). A bump there only lowers the
  top of the jump from 1.25 to 1.2 blocks, so flat jumps, up to a 3 block gap,
  are now planned under it. Jumps that climb a block still need the full height.
- **Candles** are 6 pixels tall (they were 7) and are parkour supports: the bot
  lands on them and jumps from them, using one candle's 2 pixel footprint or the
  footprint of a group. It prefers short, straight jumps onto such small targets.
- Baritone's grid jumps no longer start from, or land on, candles, end rods and
  panes: their real height made those jumps fail (a jump from a candle to a block
  4 away climbs 0.625). The simulated jumps handle them.
- A one block gap with a block at head height above it is crossed with a simulated
  low hop.
- After a failed jump, the bot waits for the landing before planning again. It used
  to plan from mid-air, find nothing three times in a row and give up the `#goto`.
- `#terrain dump` writes the blocks around the player with their pathfinding
  properties, and the `dev_replay_terrain_dump` test replays a path problem from
  such a file offline.
- **Ladders hanging in the air**: the bot jumps into a ladder one or two blocks ahead
  and one up (the block it hangs on stops it in its cell), or straight up into one
  starting at head height, then climbs it. A ladder at head height is a thin plate on
  its wall and no longer blocks the jumps that pass under it.
- **Honey blocks** keep their real collision, 1/16 smaller than the cell: the bot sneaks
  along the edge of the floor beside them (new `EdgeWalk` movement), the body over the
  void, when they stand in the way.
- Closed shulker boxes are full blocks to stand and walk on.
- **Shulker box facing**: the bot reads the side each box's lid opens to from its block
  entity (Bedrock keeps it out of the block state; `BlockSource` virtual 3 and the
  byte at `0x380` of the shulker block actor in 26.65, found by comparing boxes with a
  single free side). Lid climbs are only planned from that side, instead of trying
  both and replanning after a failure. Terrain dumps keep the facings.
- **Ladder tops and ladder jumps**: climbing pushes into the wall the ladder hangs on,
  so the bot ends on the thin top of a ladder. From there it jumps into another ladder
  up to 2 blocks away, at the same height or one up, also around the corner of the
  block they hang on (`LadderHop`): zigzags of single ladders, ladders on two faces
  of a pillar. Hanging in a row of ladders under a ceiling, it moves sideways
  (`LadderShimmy`, sneaking keeps it on the ladders).
- From the top of a ladder the bot can step up onto the next block (Baritone forbids
  any jump from above a ladder): the spiral of ladders around the pillar near
  `56 3 0` now leads to the floor above. It never jumps onto the thin top of a
  ladder (too easy to miss): it walks into a ladder beside it and climbs it, and
  walking onto ladder tops aims against their wall.
- Ladder jumps aim straight at the ladder with a sprint jump; around a corner the
  body slides along the block between. Aiming at the free corner first turned too
  late in game.
- **Strings**: a tripwire that no hook holds triggers nothing and is no longer
  avoided (`blocksToAvoid` still avoids armed tripwires).
- `#bactor <x y z> [index] [bytes]` (developer): calls a `BlockSource` virtual on a
  position and dumps the result, to find block entity fields after a game update.
- Baritone's grid jumps wait to be on the ground before starting (they used to walk
  off the edge right after a ladder or an ascend) and correct their sideways drift in
  the air.
- `#terrain dump` takes an optional center, and the replay test a
  `BARITONE_SWEEP="x0 x1 z0 z1"` that lists which standing places are reachable.
- **4 block gaps** (5 blocks from middle to middle, new `MomentumJump` movement). A
  sprint jump from the ground falls short of one by about a hundredth of a block. With
  5 or 6 free blocks of run-up behind the takeoff block, the bot backs up onto them
  (sneaking: run-ups are often one block wide), sprints, hops once on the run-up and
  jumps again the tick it lands: that jump adds its boost to the speed kept from the
  air. It picks the hop tick with the most margin from a model of the run, and stops
  to run again if the moment passes, instead of jumping short.
- **Ladder zigzags on one wall and spirals**: before jumping into the next ladder up, the
  bot moves along the thin top to its end nearest to the destination, and away from
  the wall to the outer edge of the plate. That lets the body pass beside the next
  ladder's plate, which used to stop the jump dead. In the air it no longer brakes
  towards the wall of the destination ladder (the wall stops it anyway), keeping the
  height the feet need when they reach it.
- Arrival on the top of a ladder waits for the climb out of it to finish; it used to
  cancel in the air, drop back into the ladder and start over.
- Offline simulator for the tests (`tests/SimPlayer.h`): the bot's inputs drive a
  player with the game's walking, sprinting, jumping, ladder and sneaking physics
  against the real collision boxes. `dev_simulate_terrain_dump` plays a `#goto` on a
  terrain dump (`BARITONE_TERRAIN`, `BARITONE_SIMULATE`, `BARITONE_START`,
  `BARITONE_GOAL`, `BARITONE_TRACE="from count"`) before anything is tried in game.
- In the simulator on terrain dumps of Dean's Terrible Towers: the spiral of ladders
  from `58 0 0` and its 4 block gap (`#goto 61 6 11`, 17 seconds), the zigzag of
  single ladders and the row of ladders from `48 7 -16`, the corner pillar from
  `51 58 14`, and the green tower climb to `60 -22 4`.
- **Jumping from the top of a ladder in Bedrock.** The first tries of the spiral in game
  fell at its second hop. Standing on a ladder's top with the feet above its cell, the
  Bedrock client takes the player for being on the ladder: it ignores the sneaking (it
  moved 0.3 to 0.5 of a block further than the server) and a jump becomes a climb (0.2
  up, no boost), while the server jumps; the server's corrections arrived in mid-air. The
  bot now takes off from the end of the plate, the feet past the ladder's cell: it sneaks
  sideways only (pushing into the wall climbs it), lets the sneaking stop it at the end,
  keeps pushing until the server is there too, then stands still 12 ticks before the
  jump (the server corrects a gap over 0.3 that lasts 500 ms). An `Ascend` from a ladder
  top onto a block does the same, with a sprint jump. The momentum jump also waits
  between backing up and running. The simulator jumps like the Bedrock client above a
  ladder, so the tests catch takeoffs from inside its cell.
- **Jumps that land a block lower** over a gap (Baritone has none): where walking off the
  edge falls into the gap, as in the corridor after the 4 block gap (`60 6 12` to
  `58 5 12`). A step down onto the next block stays a descend.
- After being pushed off the path, the bot goes back to the most recent movement that
  holds its position (Baritone takes the first). In game it went back from the top of the
  spiral's pillar to the hop below it, jumped from there and fell back, over and over. A
  ladder hop's zone also stops a block above its higher end.
- **Staircases of ladders** (single ladders on one wall, each one along and one up, near
  `58 18 7`): the next one's plate stands beside the top of each, so no jump can start
  from the end of the plate there and ladder hops are no longer planned. The bot climbs
  from inside instead (`LadderShimmy` one up): it slides to the edge while sneaking
  (which holds it), climbs, and steps across as its feet pass the next ladder's bottom,
  braking early (inside a ladder it coasts about ten times its speed). It can step down
  into a ladder hanging in the air from its top, and keeps climbing (jump held) when it
  leaves a ladder for the block beside it. In game the bot froze there: the takeoff could
  not move along the plate and restarted its push forever.
- Short ladder hops (same height, a block and a half at most) no longer sprint: from
  standing still the Bedrock client starts sprinting a tick late, the server at once, and
  the server's correction put the player on top of the spiral's pillar, where the hop
  started over and fell. A hop that ends on the ground anywhere else (or finds the player
  at another height before jumping) gives up and the bot plans again from there.
- Tested in game on Dean's Terrible Towers after that: the spiral `#goto 53 6 8` from
  `58 0 0` in 16 seconds (one cancelled move recovered at the row of ladders), then the 4
  block gap `#goto 61 6 11` in 10 seconds: the hop landed 0.54 into the takeoff block (the
  model said 0.49 to 0.63) and the jump made the gap. Then `#goto 52 5 12` from `56 -20 12`
  in 65 seconds: the rest of the green tower, the spiral, the 4 block gap and the corridor.
  Then the staircase of ladders, `#goto 61 22 12` from the top of its first ladder, in 5.5
  seconds.
- `#walk <ticks> [sprint|sneak|normal] [left|right]` (test): also sneaks and steps
  sideways, and prints the positions with decimals, to compare them with the server's
  corrections.
- Tested in Survival on Dean's Terrible Towers with breaking and placing disabled:
  `#goto 60 -22 4` (twice), `#goto 53 -15 7`, then the whole green tower from its
  floor to its exit, `#goto 61 0 10` (33 blocks up: candles, low hop, hanging
  ladder, honey edge, 4 block jumps) in 44 seconds without a fall.

## 1.10.2

- Experimental advanced parkour now plans curved jumps around obstacles,
  including landings on lower platforms beneath overhangs.
- Baritone can use the lids of one or two stacked shulker boxes to climb onto a
  platform. If an approach fails, it can replan from another side.
- Tested in Survival on Dean's Terrible Towers with breaking and placing disabled.
  Baritone reached `-50 -31 -34` using `#goto`. The remaining climb through
  offset shulker boxes and the finish at `-42 82 -37` are still unverified.

## 1.10.1

- `#mine wood` (also `log`, `logs`, `tree`) now mines every natural log, bark
  block and nether stem. It used to look for the old `minecraft:wood` block,
  which no longer exists in Bedrock, find nothing and dig a branch mine at
  Y=-59. `leaves` and `planks` are understood the same way.
- When no matching block is known, `#mine` only digs down to branch mine for
  underground blocks (ores, deepslate, stone...). Surface blocks such as logs
  or sand are searched by walking away on the surface instead.
- A species name means its logs: `#mine oak`, `#mine oak wood` and
  `#mine birch log` mine only that species instead of looking for blocks
  named `oak` and `wood`.
- Read the level's entity list once per game tick. Mining, survival and
  pathing asked for it several times per tick, and each read checked the
  memory of every dropped item. The cost grew with the items left behind by
  mining until the game slowed down after a few minutes.
- **Fix the game slowing down over time.** Every check of a game pointer asked
  Windows `VirtualQuery`, which measures the whole memory region around the
  address. In Minecraft's multi-gigabyte heap one call took milliseconds and
  got slower as the game used more memory: profiled in game, these checks were
  78% of the bot's tick (armor, hunger and inventory reads every tick), up to
  55 ms per tick while mining. Pointers are now checked by reading them under
  a structured exception handler, which restores guard pages it touches.
- `#perf` also reports free RAM and the commit charge, and warns when Windows
  is low on memory: the game then slows down while it pages, whatever the bot
  does. Without samples it explains that `#perf on` starts the capture.

## 1.10.0

- **Experimental advanced parkour** supports offset jumps, low ceilings, and
  narrow landing surfaces such as end rods.
- Skip falling-block entity scans while moving through clear terrain to reduce
  frame-time spikes.

## 1.9.1

Windows x64 patch release for **Minecraft Bedrock 26.65**, package `1.26.6501.0`.

- Use the recipe book to craft in Survival from a crafting table or the
  personal 2 × 2 grid. Baritone waits for the game's response before moving
  the crafted item into the inventory.
- Check that the inventory can hold the ingredients before clearing the grid.
  Verify that the crafted stack fits before taking it, and report when more
  space is needed.

Verified in a local Survival world with a full inventory, an existing stack
that could accept the result, and a recipe change with ingredients in the grid.

## 1.9.0

Windows x64 release for **Minecraft Bedrock 26.65**, package `1.26.6501.0`.
This is the only active native profile in this release.

- Use `#schematic issues` after verification to page through recorded missing,
  wrong, extra or unloaded positions.
- Use `#rail inspect` with a station name or coordinates to check the stopping
  rail and dismount exits. Route and trip commands report the same cause when
  the destination fails inspection.

See the [Windows validation matrix](https://github.com/miked06/baritone-bedrock/blob/v1.9.0/docs/WINDOWS_NEXT.md#validation)
for the completed checks and remaining native scenarios.

## 1.8.0

Windows x64 release for **Minecraft Bedrock 26.65 and 26.63** (packages
`1.26.6501.0` and `1.26.6301.0`).

- Preview a route and its expected block actions with `#calc`.
- Load local resource-pack PNGs for in-game schematic previews with `#ghostpack`.
- Preserve more imported schematic states and resume construction from verified
  world-specific checkpoints after a reload.
- Plan production with optional smelting or crop harvesting. Withdraw supplies
  from supported vanilla shulker boxes with confirmed transfer and recovery.
- Register rail stations, preview connected routes and use experimental minecart
  travel. A complete in-game ride still needs validation.
- Keep selected destination coordinates visible across map refreshes. Report an
  empty inventory in chat and expose bounded timing diagnostics.

Validation on 26.65 covered 151 C++ tests (also under AddressSanitizer),
five Python importer tests, live command checks, route and construction previews,
schematic textures, and HTML and ZIP exports. The
[Windows validation matrix](docs/WINDOWS_NEXT.md#validation) records the
remaining live scenarios and villager trading work.

## 1.7.2

Windows x64 release for **Minecraft Bedrock 26.63** (package `1.26.6301.0`).
The optional `BaritoneControlPanel.exe` has a new dark interface.

- Sidebar sections for navigation, schematics, workflows and the console.
- Live world, player position and task progress across all sections.
- A command bar on every page with Enter to send and a persistent Stop button.
- Matching map and workflow editor styling with desktop DPI scaling.

In-game chat commands remain available through `BaritoneBedrock.dll`.

## 1.7.1

Windows x64 patch release for **Minecraft Bedrock 26.63** (package
`1.26.6301.0`). Update both the DLL and the control panel.

- Local saves with the same display name now have separate tasks, workflows,
  protections, waypoints, map destinations, storage and construction data.
  Older files remain in their original locations. Recreate applicable protected
  zones in the correct save before resuming automation.
- Replacement jobs preserve the original block until the replacement is available.
- Navigation waits for the player to settle within the destination before
  reporting arrival or disconnecting.
- Farming reports completion after the last eligible crop. Global storage scans
  skip registered workstations, and equip workflows can use items already held.
- History indicates when recent actions are still being verified.

The 142 C++ tests and 5 Python recipe importer tests passed. Live Windows
checks covered two same-name saves, six navigation runs, replacement and
clearing, wheat replanting, the equipment workflow and storage surveys.
See the [PC audit](https://github.com/miked06/baritone-bedrock/blob/v1.7.1/docs/PC_AUDIT_2026-11-14.md)
for the test scenarios and remaining coverage.

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
[validation roadmap](docs/archive/MODULE_ROADMAP.md) distinguishes implemented behavior
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

Stability/performance fixes for Minecraft Bedrock 26.60 (`1.26.6005.0`). See [the audit report](docs/archive/STABILITY_AUDIT.md) for the measured scope and remaining gameplay validation.

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

Compared against Baritone Java 26.3 at `25111daedf1d59e6a8dfb5a3e61885cdb8d953df`. See [the feature audit](docs/archive/JAVA_PARITY_AUDIT.md) for scope, verification and remaining differences.

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
