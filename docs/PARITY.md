# Differences with Baritone Java

Baritone Bedrock was originally ported from [Baritone Java](https://github.com/cabaletta/baritone) 1.21.11. Version 1.3.0 was compared with Java 26.3 at commit `25111daedf1d59e6a8dfb5a3e61885cdb8d953df`. The [feature audit](JAVA_PARITY_AUDIT.md) records added behavior, verification and remaining gaps.

For the 1.4 feature inventories and per-feature qualifications, see [FEATURE_COMPARISON.md](FEATURE_COMPARISON.md). The new [schematic workspace](SCHEMATICS.md) provides an open-placement workflow inside Bedrock.

## What matches

- **Commands**: every Baritone command and alias exists (`goto`, `goal`, `path`, `mine`, `follow`, `farm`, `explore`, `build`, `sel`, `wp`, `elytra`, `tunnel`, `axis`, `surface`, `invert`, `thisway`, `click`, `come`, `pickup`, `find`, `blacklist`, `eta`, `proc`, `pause`/`resume`/`paused`, `cancel`, `forcecancel`, `set`, `modified`, `reset`, `help`, `repack`, `saveall`, `reloadall`, `render`, `gc`, `version`...).
- **Settings**: all user-configurable Java setting names are present (see [SETTINGS.md](SETTINGS.md)). Platform adaptations and the remaining terrain-generator settings are listed below. `#set load [file]` supports named profiles.
- **Pathfinding**: the A* search with Baritone's cost model, the improvement coefficients, segment cutoff (`pathCutoffFactor`, `pathCutoffMinimumLength`), cutting at the loaded chunks, path splicing, backtrack favoring, avoidance of mobs and spawners.
- **Movements**: traverse, ascend, descend, fall (with water bucket), diagonal, pillar, downward, parkour, with Baritone 1.21's rules (magma blocks, head bonks, ladders, safe walk...).
- **Execution**: `PathExecutor` with its sprint logic, pauses, snipping and splicing.
- **Tools**: `ToolSet` (best tool, efficiency, silk touch, `itemSaver`, swords, haste / mining fatigue, hunger for sprinting).
- **Processes**: custom goal, get-to-block, mine (with legit mine, drops, branch mining), follow, explore, farm, builder (layers, repeats, substitutes, rotation and mirror, map art), backfill, elytra, inventory pauser.
- **Look behavior**: client / server modes, random looking, smooth look.
- **Chat**: prefix, prefix control, chat control, censored coordinates, short prefix, ran-command logging, verbose errors, notifications.
- **Rendering**: Baritone's `PathRenderer` — path colors, `fadePath`, `renderPathAsLine`, animated goal box, inverted goals, Y-level box, blocks to break / place / walk into, best path so far and most recent node, selections.

## Adapted for Bedrock

| Area | Difference | Why |
|---|---|---|
| `allowParkour` | On by default (off in Java). | The bot corrects its jump in the air to land in the middle of the block, which makes parkour reliable on Bedrock. |
| Look mode `NONE` | Behaves like `SERVER`. | There is no separate "no rotation" path in Bedrock's movement. |
| `#mine` without known ores | Goes down to the mining level first, with a level per ore. | Faster than Java's branch mining from the current height on Bedrock's terrain. |
| Builder orientation | The orientation of a block comes from how it is placed (clicked face, aimed half, look direction). Only orientation states are compared. | Bedrock has no "place this exact block state" action. |
| Water bucket fall | The bucket is selected and the camera looks down as soon as the fall starts; the click is armed every frame. A bucket in the main inventory is brought to the hotbar even without `allowInventory`. | A slot change needs a few ticks to reach the server, and a fast fall crosses the reach in one tick. |
| `elytraAutoSwap` | Uses the game's armor swap (use the elytra from the hand). | Bedrock has no inventory window clicks. |
| Main inventory moves | Briefly opens and closes the inventory, pausing bot actions during the transfer. An inventory opened by the player remains open. | Bedrock 26.60 validates main-inventory moves against the server's open screen. |
| Emergency elytra landing | Searches solid ground (no water, lava, leaves...) about 50 blocks ahead, then lands with the normal spiral. | — |
| `#click` | Aim with the crosshair, then left click for a goal, right click above it, or drag with left held to select a box. | Replaces Java's free-cursor `GuiClick` screen. |
| Completion | `#complete` suggests commands, settings, known blocks/states, nearby entities, waypoints and files. | Native chat Tab insertion and Java clickable/hoverable chat are not integrated. |
| `toastTimer` | Windows 10+ decides how long notifications stay. | Windows notification API. |
| `useMessageTag` | Only removes the prefix. | Bedrock chat has no message tags. |
| `chunkPackerQueueMaxSize` | Bounds a deduplicated packing queue processed within the frame budget. | Live world copying and packing stay on the game thread. |
| Flight cache culling | Uses the configured block radius and interval, evicting saved regions wholly outside the radius. | The shared cache uses 512-block regions; dirty data stays in memory. |
| `elytraCustomAllocator` | Selects a per-search C++ arena or ordinary allocation. | The C++ solver does not use Java's native pathfinder allocator. |
| Flight ray/hitbox diagnostics | Draws actual steering checks and swept gliding hitboxes, including collisions. | This solver evaluates complete flight trajectories. |
| `renderCachedChunks`, `cachedChunksOpacity` | Draws exposed cached surfaces as a translucent gray/blue/red ghost, with a bounded mesh built on a worker. | The two-bit cache retains pathing categories rather than original textures. |
| `renderGoalXZBeacon` | A filled beam with translucent crossed surfaces and a bright core. | Java's animated beacon texture is not used. |
| `pauseMiningForFallingBlocks` | Rarely triggers. | On Bedrock a falling block entity only exists for about one tick; the bot still digs through falling gravel and sand correctly. |
| `#goto` failures | Retries twice with more time before giving up. | Long trips through complex terrain. |
| `pathRenderLineWidthPixels`, `goalRenderLineWidthPixels`, `selectionLineWidth` | Lines wider than 1 pixel are drawn as two crossed ribbons, with a width in blocks (about 0.012 block per pixel): they look thinner far away and thicker up close. | The game's line primitive is always 1 pixel. |

## Java-specific features and remaining gaps

| Setting / feature | Why |
|---|---|
| `elytraNetherSeed`, `elytraPredictTerrain` | A Bedrock terrain generator is not implemented. Java's generator produces different terrain. Flight plans with observed/cached chunks and replans as chunks load. |
| `#litematica`, `#schematica` | Adapted in 1.4: load/preview files and build the open Bedrock workspace placement. Java mod binaries are not loaded. |
| `logger`, `notifier`, `toaster` | Java API callbacks, not user settings. |

## Additions

Things Bedrock Baritone has that Java does not:

- Diagonal parkour: Baritone only jumps along the four axes; Bedrock Baritone also jumps 2 blocks diagonally over a gap (3 when it can sprint), at the same height, never over lava (with `allowParkour`).
- `#build` reads `.mcstructure` (Bedrock) files and converts Java block names to Bedrock.
- `#complete` exposes completion suggestions through a command.
- Ore-family and item aliases such as `diamond` expand to the matching ore blocks.
- A watchdog writes the game thread's call stack to `log.txt` if the game freezes for more than 5 seconds.
- Developer commands to inspect the game's memory (`#debug`, `#ecs`, `#vt`, `#hexat`...).
