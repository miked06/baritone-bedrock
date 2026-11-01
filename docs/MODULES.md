# Module commands (1.5 development)

These additions target Bedrock Windows package **1.26.6005.0**. See
[MODULE_ROADMAP.md](MODULE_ROADMAP.md) for the distinction between implemented
behavior, offline regression coverage and live gameplay validation. This working
version is released as 1.5.0 with the validation limits recorded below.

## Schematic comparison, materials and repair

Load a `.mcstructure`, `.schem`, `.schematic` or `.litematic` with
`#schematic load file [x y z]`. Inspect/move/rotate/mirror/layer the preview before
using `#schematic build`.

- `#schematic verify` counts matching, missing, wrong, extra, ignored and unknown positions.
- `#schematic requirements` compares the placement and allocates inventory materials without counting a substitute twice.
- `#schematic verify status` or `#schematic requirements status` shows scan progress.
- `#schematic repair` starts the difference builder for the current placement.

Comparisons are spread across ticks. Unloaded chunks are unknown and are excluded
from material counts. Material estimates account for supported doors, beds and
double slabs; warnings identify legacy variants that cannot be mapped reliably.
The builder still obeys the existing build settings and protected areas.

## Named schematic placements

- `#schematic store name` stores the current file, position, rotation, mirror, layers and visibility.
- `#schematic placements` lists saved placements.
- `#schematic use name` selects a resident placement or reloads its file in the background.
- `#schematic drop name` removes a saved placement.

Up to eight stored placements remain resident; up to 64 presets persist per
world/dimension. The current placement and resident snapshots share a maximum of
512 difference boxes. With stored placements, previews cover eight blocks around
the player; a single active placement covers sixteen. Presets retain local file
paths, so moving a schematic file requires storing the placement again. Stored
snapshots are independent: edit the active placement, then store its name again
to update that snapshot. These are wireframe difference previews; textured block
rendering is not included.

## Storage logistics

`#storage add supplies x y z` registers a chest, barrel or supported shulker box.
Use `#storage list` and `#storage remove name` to manage locations.

```
#storage deposit supplies cobblestone 64
#storage withdraw supplies oak_planks 32
```

Transfers walk to the container, open it normally, wait for server receipts,
close the owned screen and return to the starting position. They can temporarily
interrupt another process. Empty destination slots are required; partial stacks
are preserved rather than merged. `#storage inspect` lists nonempty slots of an
already open supported container. Locations are saved per world/dimension.

## Crafting and smelting

Register a crafting table or furnace with `#storage add`.

```
#craft workbench stick 8
#smelt furnace raw_iron iron_ingot 8 coal 1
```

Crafting targets **additional output items** and uses one normal recipe action at
a time. Recipe batch sizes can produce more than the requested minimum. It reads
Minecraft's current recipe entries instead of retaining recipe network IDs
across sessions. Clear the game's recipe search before starting and check recipe
filters/unlocks if an output is unavailable. Ingredients must already be present;
recursive ingredient crafting is not implemented.

`#craft list [item]` reads recipes from an open crafting interface. `#craft once item`
submits one action in that interface; `#craft state` reports its state (0 idle,
1 pending, 2 accepted, 3 rejected). Creative interfaces use Minecraft's normal
Creative item action. Survival interfaces require their normal ingredients and
permissions. Completion requires an accepted server response and observed output.

Smelting accepts one-to-one recipes, a batch of 1..64, and an explicit fuel item
and quantity. Use an empty furnace and keep the input batch and fuel in separate
single stacks. It waits for the expected output, collects it, returns remaining
fuel when possible and closes the screen. On refusal, timeout or interruption,
inspect the furnace before restarting: already submitted items can remain there.
The game decides whether the input/fuel combination is valid. An iron-smelting
cycle has been checked live; broader recipes and Survival crafting remain part
of gameplay validation.

## Persistent tasks and diagnostics

```
#task add goto 100 64 100
#task add mine 64 iron_ore
#task add withdraw supplies oak_planks 32
#task add build house.mcstructure 100 64 100
#task add wait 5
#task add inventory torch 16
#task add verify
#task resume
```

`#task list`, `#task status`, `#task pause`, `#task retry`, `#task clear` and
`#task configure index seconds retries` manage the workflow. `#queue` is an alias.
Mining targets a total inventory quantity. Storage tasks checkpoint their final
inventory target to avoid repeating a partly finished transfer after recovery.
Relative coordinates are resolved when a task is added.

Checkpoints use background serialized writes. Recovery is always paused and is
bound to a world/dimension. Failures stop following actions; `#why` shows recent
concrete failure reasons. Crafting/smelting remain explicit workstation commands
and are not checkpointed task kinds.

## Survival assistant

`#survival on` enables safe-food eating, nearby-hostile retreat and supply/tool
checks. `#survival home x y z` sets a return point. `#survival threshold food health`
sets the eating and retreat thresholds. `#survival off` or `#stop` disables it.
The assistant skips Creative mode and open containers, restores the previous
selected slot after eating and stops for attention at home when supplies are
missing. It does not automatically deposit valuables or change equipment.

## Protected areas

Select an area with `#sel pos1` and `#sel pos2`, then use
`#protect add name entry|modify|both`. `#protect list` and `#protect remove name`
manage saved zones. Policies apply to path segments, block changes, building,
farming, container interaction and supported vehicle routes. Changes stop the
current activity so subsequent calculations use the new policy. An unreadable
policy file blocks actions until resolved.

## Tree and animal farming

Select the complete trees and canopy, keep matching saplings, then use
`#trees [count]`. The bounded tree detector requires soil, connected unstripped
logs and nonpersistent leaves. Supported species are oak, spruce, birch, jungle,
acacia, dark oak, cherry and pale oak. It removes the selected logs and confirms
replanting. Detection is a conservative heuristic, not proof that a tree is
unmodified; choose the selection accordingly. Mangrove roots/propagules and
nether fungi are not supported by this tree process.

```
#animals list
#animals breed cow 2 16
#animals harvest chicken 3 4 16
```

Animal tasks support cows, mooshrooms, sheep, chickens, pigs and rabbits. Optional
radius is 1..64. Breeding counts adults confirmed in love, not babies born; local
cooldowns prevent repeated feeding. Harvesting preserves the specified minimum
of at least two known adults and skips babies/unknown ages. A disappearing animal
is not counted as a confirmed kill. A selection restricts eligible animals.

## Quarry manager

Select the inclusive excavation volume, then use `#quarry plan` to check it.
`#quarry start [lights]` excavates from the top down while leaving a solid
descending perimeter ramp. `#quarry status` reports preflight results.
`#quarry unload supplies` enables unloading a block stack when inventory fills;
`#quarry unload off` disables it.

Width/length are 5..128 blocks; volume is at most one million blocks; depth is at
most one perimeter lap. Every ramp step needs existing solid support. Preflight
rejects unloaded areas, boundary liquids, unbreakable excavation blocks and
protected positions. The builder cannot break the preserved ramp for a path.
Lighting adds supported torches on selected ramp steps and needs torches in
inventory. A quarry requires compatible build settings; a rejected command
explains which settings to change. Newly exposed liquids still require attention.

## Portal route planner

`#portal add name [dimension x y z]` registers a portal (without coordinates it
uses the current position/dimension). Dimensions are 0 Overworld, 1 Nether,
2 End. `#portal link a b [oneway]` records an actual connection; default links
are bidirectional. `#portal unlink a b`, `#portal remove name` and `#portal list`
manage the graph, saved per world across dimensions.

`#portal route destination` finds the cheapest registered route from the current
position, including walking legs and portal transitions. `#portal go destination`
walks to its next portal. Run it again after changing dimension to continue.
Costs are distance estimates; they do not model terrain or loading delays.
`#portal scale dimension` shows the Overworld/Nether coordinate estimate and does
not invent or discover a portal link.

## Boats and mounts

```
#vehicle board boat
#vehicle board horse
#vehicle goto boat x y z
#vehicle goto horse x y z
#vehicle dismount
```

Mounts must be tamed, saddled known adults (horses, donkeys or mules). Routing
uses a conservative three-block-wide water or ground corridor with headroom;
it does not reuse player-width walking paths. Segments are limited to 128 blocks
per horizontal axis. Unknown chunks, hazards, protected areas, route changes,
loss of riding state and a stuck vehicle stop navigation. It does not break or
place terrain, sail ocean-wide routes, jump gaps or handle every mount type.
End-to-end steering and dismount validation remain pending.

## Schematic export

Select loaded blocks and use `#export name.mcstructure`. Export refuses unknown
chunks and existing destination files. It writes blocks and supported state
types to the schematics directory. It does not export entities, container
contents, block-entity data or secondary waterlogged fluid layers.

## Control panel and gameplay validation

`BaritoneControlPanel.exe` connects to the running injected DLL. It provides live
status, navigation/mining controls, schematic file selection, preview/build/repair
actions, task controls and a command field for every module. Stop is prioritized;
status requests and command submissions use separate workers.

```
python tools/validate_gameplay.py --report re/gameplay-report.json
python tools/validate_gameplay.py --scenario scenario.json --allow-actions --report re/scenario-report.json
```

The default smoke checks read an **existing** world, inventory and module status.
Custom scenarios accept named steps with `command`, regular-expression `expect`
and `reject`, `status` key/regex assertions, and `timeout` (up to 300 seconds).
`cleanup` contains final commands; the report records their submission separately
from verified test outcomes. Stop at the first failed step. Reports include log
evidence, status and timings; they are test observations, not a guarantee of
zero bugs or lag. Scenario authors must include explicit restoration assertions
when modifying a fixture.

The adaptive performance controller requested to be excluded is not added.
