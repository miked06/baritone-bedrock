# Differences with Baritone Java

Baritone Bedrock was written by reading [Baritone's Java code](https://github.com/cabaletta/baritone) (version 1.21.11) and reproducing it in C++. This page lists what matches, what had to be adapted for Bedrock, and what cannot be done.

## What matches

- **Commands**: every Baritone command and alias exists (`goto`, `goal`, `path`, `mine`, `follow`, `farm`, `explore`, `build`, `sel`, `wp`, `elytra`, `tunnel`, `axis`, `surface`, `invert`, `thisway`, `click`, `come`, `pickup`, `find`, `blacklist`, `eta`, `proc`, `pause`/`resume`/`paused`, `cancel`, `forcecancel`, `set`, `modified`, `reset`, `help`, `repack`, `saveall`, `reloadall`, `render`, `gc`, `version`...).
- **Settings**: all of Baritone's settings, with the same names, types, defaults and meaning (see [SETTINGS.md](SETTINGS.md)). `#set` behaves like Java's `SetCommand`.
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
| Emergency elytra landing | Searches solid ground (no water, lava, leaves...) about 50 blocks ahead, then lands with the normal spiral. | — |
| `disconnectOnArrival` | Shows a notification instead of disconnecting. | There is no safe way to leave the world from the DLL. |
| `toastTimer` | Windows 10+ decides how long notifications stay. | Windows notification API. |
| `useMessageTag` | Only removes the prefix. | Bedrock chat has no message tags. |
| `chunkPackerQueueMaxSize` | Not used. | Chunks are packed within a time budget each tick, there is no queue. |
| `extendCacheOnThreshold` | Always on in practice. | Loaded chunks are always scanned exactly. |
| `pauseMiningForFallingBlocks` | Rarely triggers. | On Bedrock a falling block entity only exists for about one tick; the bot still digs through falling gravel and sand correctly. |
| `#goto` failures | Retries twice with more time before giving up. | Long trips through complex terrain. |
| `pathRenderLineWidthPixels`, `goalRenderLineWidthPixels`, `selectionLineWidth` | Lines wider than 1 pixel are drawn as two crossed ribbons, with a width in blocks (about 0.012 block per pixel): they look thinner far away and thicker up close. | The game's line primitive is always 1 pixel. |

## Not possible

| Setting / feature | Why |
|---|---|
| `elytraNetherSeed`, `elytraPredictTerrain` | Java uses a native library that generates nether terrain from the seed. The Bedrock port plans with the chunks it has seen. |
| `elytraCacheCullDistance`, `elytraTimeBetweenCacheCullSecs`, `elytraCustomAllocator` | Settings of Java's native elytra pathfinder. |
| `elytraRenderRaytraces`, `elytraRenderHitboxRaytraces` | The flight solver works differently (it simulates whole trajectories); the chosen trajectory is drawn with `elytraRenderSimulation`. |
| `renderPathIgnoreDepth`, `renderGoalIgnoreDepth`, `renderSelectionBoxesIgnoreDepth`, `renderSelectionIgnoreDepth` | No material without depth test was found for lines; lines are hidden by blocks. |
| `renderCachedChunks`, `cachedChunksOpacity` | Would need to draw cached chunks as world geometry. |
| `renderGoalXZBeacon` | Drawn as a thin column instead of a beacon beam. |
| `#litematica`, `#schematica` | These mods do not exist on Bedrock. `#build` reads their files directly. |
| `logger`, `notifier`, `toaster` | Java API callbacks, not user settings. |

## Additions

Things Bedrock Baritone has that Java does not:

- Diagonal parkour: Baritone only jumps along the four axes; Bedrock Baritone also jumps 2 blocks diagonally over a gap (3 when it can sprint), at the same height, never over lava (with `allowParkour`).
- `#build` reads `.mcstructure` (Bedrock) files and converts Java block names to Bedrock.
- `#sel` keeps several selections at once.
- A watchdog writes the game thread's call stack to `log.txt` if the game freezes for more than 5 seconds.
- Developer commands to inspect the game's memory (`#debug`, `#ecs`, `#vt`, `#hexat`...).
