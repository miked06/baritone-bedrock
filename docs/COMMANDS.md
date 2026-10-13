# Commands

Type commands in the Minecraft chat, starting with the prefix `#` (setting `prefix`). They are handled by the bot and never sent to the server. `#help` lists them in game and `#help <command>` shows one.

Conventions used below:

- `<x y z>` are block coordinates. `~` means "relative to the player": `#goto ~10 ~ ~-5`.
- `[...]` is optional, `a | b` means "one of".
- `<block>` is a block name, with or without `minecraft:` (`diamond_ore`, `minecraft:oak_log`).

You can also type a setting name directly: `#allowBreak false` is the same as `#set allowBreak false`, and `#allowBreak` alone toggles it.

## Contents

- [Moving](#moving)
- [Mining and gathering](#mining-and-gathering)
- [Building and selections](#building-and-selections)
- [Elytra](#elytra)
- [Exploring and farming](#exploring-and-farming)
- [Waypoints](#waypoints)
- [Long-distance cache](#long-distance-cache)
- [Control](#control)
- [Settings](#settings)
- [Developer commands](#developer-commands)

## Moving

| Command | Description |
|---|---|
| `#goto <x y z>` | Walk to a block. |
| `#goto <x z>` | Walk to a column, at any height. |
| `#goto <y>` | Go up or down to a height. |
| `#goto <block>` | Walk to the closest block of that type (`#goto crafting_table`). Looks in loaded chunks and in the long-distance cache. |
| `#goal [x y z \| x z \| y \| clear]` | Set the goal without moving (no argument: your position). `#goal clear` removes it. |
| `#path` | Start walking to the current goal. |
| `#thisway <distance>` (`#forward`) | Walk straight in the direction you look. |
| `#invert` | Run away from the current goal. |
| `#come` | Walk to the camera's position. |
| `#click` | Walk to the block you are looking at, up to 512 blocks away. |
| `#surface` (`#top`) | Go up to the surface. |
| `#axis` (`#highway`) | Go to the closest axis or diagonal (at `axisHeight`). |
| `#follow players` | Follow every player. |
| `#follow player <name>` | Follow one player. |
| `#follow entities` / `#follow entity <type>` | Follow entities, or entities of one type (`#follow entity zombie`). |

**Examples**

```text
#goto ~ 12 ~             dig down to Y=12 under you
#goto 0 0                walk to the world's center at any height
#goal 500 64 500
#path
```

## Mining and gathering

| Command | Description |
|---|---|
| `#mine [amount] <block> [block...]` | Mine these blocks and pick up the drops. Stops after `amount` blocks if given. |
| `#pickup [item...]` | Pick up items on the ground (all of them, or only these). |
| `#tunnel` | Dig a 1×2 tunnel straight ahead. |
| `#tunnel <height> <width> <depth>` | Clear a box of that size in front of you, using the builder. |
| `#blacklist` | During `#goto <block>` or `#mine`: ignore the closest target. |
| `#find <block> [block...]` | List the known positions of these blocks. |

`#mine` understands ore families:

- An ore includes its deepslate variant: `diamond_ore` also mines `deepslate_diamond_ore`.
- An item name means its ores: `diamond`, `iron`, `gold`, `coal`, `lapis`, `redstone`, `emerald`, `copper`, `quartz`, `netherite` (ancient debris).
- In the nether, `gold` also mines `nether_gold_ore`. Java names like `nether_quartz_ore` work too.

When no ore is known, the bot goes down to a mining level and branch-mines: `legitMineYLevel` (-59, diamonds) by default, or the best level of the ore (iron 16, gold -16, lapis 0, coal/copper/emerald 48; in the nether 15, quartz and nether gold 30). In survival you need a good enough pickaxe in your hotbar; in creative everything breaks at once.

```text
#mine 64 diamond
#mine iron coal
#mine ancient_debris
```

## Building and selections

| Command | Description |
|---|---|
| `#build` | List the files in the `schematics` folder. |
| `#build <file> [x y z]` | Build a schematic at your position, or at `x y z`. |
| `#sel pos1 [x y z]` / `#sel pos2 [x y z]` | Set the corners of a selection (your position by default). Each `pos2` adds a selection: you can have several. |
| `#sel clear` / `#sel undo` | Remove all selections / the last corner or selection. |
| `#sel set <block>` | Fill the selections. |
| `#sel walls <block>` | Build the walls of the selections. |
| `#sel shell <block>` | Build walls, floor and ceiling. |
| `#sel sphere <block>` / `#sel hsphere <block>` | Build a sphere (filled / hollow) inside the selection. |
| `#sel cylinder <block> [x\|y\|z]` / `#sel hcylinder <block> [x\|y\|z]` | Build a cylinder along an axis (default `y`). |
| `#sel cleararea` | Empty the selections. |
| `#sel replace <from...> <to>` | Replace blocks in the selections. |
| `#sel copy` / `#sel paste [x y z]` | Copy the selection and build a copy somewhere else. |
| `#sel expand\|contract\|shift [a\|newest\|oldest] <direction> <n>` | Grow, shrink or move one or all selections (`north`, `south`, `east`, `west`, `up`, `down`). |

Supported schematic formats, put in `%LOCALAPPDATA%\BaritoneBedrock\schematics\`:

| Format | Origin |
|---|---|
| `.mcstructure` | Bedrock structure blocks, Bedrock editors |
| `.schem` | WorldEdit / Sponge v1–v3 |
| `.litematic` | Litematica |
| `.schematic` | MCEdit (before 1.13, numeric ids) |

Java block names are converted to Bedrock names. The builder reproduces the orientation of stairs, logs and pillars, slabs, furnaces, chests and pumpkins by clicking the right face from the right spot. Blocks come from your hotbar, or from your whole inventory with `allowInventory`.

Useful builder settings: `buildIgnoreExisting`, `buildIgnoreBlocks`, `buildSubstitutes`, `buildInLayers`, `layerOrder`, `layerHeight`, `buildRepeat`, `buildRepeatCount`, `mapArtMode`, `buildOnlySelection`, `schematicOrientationX/Y/Z`, `buildSchematicRotation`, `buildSchematicMirror`. See [SETTINGS.md](SETTINGS.md).

```text
#sel pos1
#sel pos2 ~10 ~5 ~10
#sel hsphere glass
#build castle.schem 100 70 -40
```

## Elytra

| Command | Description |
|---|---|
| `#elytra` | Fly to the last goal set with `#goal` / `#goto` (a column or a block). |
| `#elytra <x z>` / `#elytra <x y z>` | Fly to these coordinates. |
| `#elytra reset` | Plan the current flight again from scratch. |
| `#elytra repack` | Read the loaded chunks again. |
| `#elytra supported` | Tell whether elytra flight is available. |

You need an elytra on and fireworks in your inventory. By default you take off yourself (jump, then jump again in the air) and the bot takes over as soon as you glide; with `elytraAutoJump` it takes off by itself. The bot fires rockets when it needs speed, flies around obstacles, then spirals down and lands, and walks the rest of the way.

It lands early if the elytra is almost broken (`elytraMinimumDurability`) or fireworks run low (`elytraMinFireworksBeforeLanding`), on solid ground away from water, and swaps a worn elytra for a better one from the inventory (`elytraAutoSwap`). The first `#elytra` shows a short notice; set `elytraTermsAccepted` to hide it.

## Exploring and farming

| Command | Description |
|---|---|
| `#explore [x z]` | Visit chunks never loaded before, in a spiral around you or `x z`. |
| `#explorefilter <file.json> [invert]` | Chunks to treat as already explored: a JSON list `[{"x": 0, "z": 0}, ...]` in the data folder. |
| `#farm [radius]` | Harvest ripe crops, replant, use bone meal and pick up the drops. |

## Waypoints

Waypoints are saved per world. The bot adds `death` when you die and `bed` when you sleep (`doDeathWaypoints`, `doBedWaypoints`).

| Command | Description |
|---|---|
| `#wp list [tag] [page]` | List waypoints. |
| `#wp save [tag] [name] [x y z]` | Save a waypoint (your position by default). |
| `#wp info <name\|tag>` | Show a waypoint. |
| `#wp goto <name\|tag>` / `#wp goal <name\|tag>` | Walk to a waypoint / set it as the goal. |
| `#wp delete <name\|tag>` | Delete a waypoint. |
| `#wp clear <tag>` | Delete every waypoint with a tag. |
| `#wp restore <n>` | Bring back deleted waypoints. |
| `#sethome` / `#home` | Save / go to your `home` waypoint. |

`#waypoint` and `#waypoints` are aliases of `#wp`. When several waypoints share a name, add `@ <date>` to pick one.

## Long-distance cache

Every chunk the bot sees is remembered in 2 bits per block (air, water, avoid, solid), plus the position of interesting blocks (chests, portals, spawners, rare ores...). The pathfinder plans through these chunks even when they are not loaded, and `#mine`, `#goto <block>` and `#find` search them.

| Command | Description |
|---|---|
| `#repack` (`#rescan`) | Put the loaded chunks into the cache again. |
| `#saveall` / `#reloadall` | Write the cache to disk / read it back. |
| `#render` | Read every loaded chunk again. |
| `#gc` | Free memory (cache regions far from the player). |

Setting: `chunkCaching` (on by default).

## Control

| Command | Description |
|---|---|
| `#stop` (`#cancel`) | Stop everything. |
| `#forcecancel` | Stop everything, even in the middle of a movement. |
| `#pause` (`#p`) / `#resume` (`#r`) | Pause the current task / continue it. |
| `#paused` | Tell whether the bot is paused. |
| `#proc` | Show the process in control and its goal. |
| `#eta` | Estimated time to the end of the segment and to the goal. |
| `#version` | Show the version. |
| `#help [page\|command]` (`#?`) | List commands / show one. |
| `#unload` | Unload the DLL cleanly. |
| `#unfault` | Turn the bot back on after it caught an internal error. |
| `#litematica`, `#schematica` | These Java mods do not exist on Bedrock: use `#build <file>`. |

## Settings

| Command | Description |
|---|---|
| `#set <name> <value>` | Change a setting. |
| `#set <name>` | Show a setting. |
| `#set toggle <name>` | Toggle a boolean. |
| `#set reset <name\|all>` | Back to the default. |
| `#set list [page]` / `#set modified [filter] [page]` | List all / changed settings. |
| `#set save` / `#set load` | Write / read `settings.txt`. |
| `#modified` (`#mod`) | List the changed settings. |
| `#reset [all\|name]` | Reset settings. |

The full list is in [SETTINGS.md](SETTINGS.md).

## Developer commands

These read the game's memory and help when updating the bot to a new game version. They are safe to run but print raw data to `log.txt`.

| Command | Description |
|---|---|
| `#debug` (`#info`) | What the bot reads from the game: position, block under the feet, held item, armor, hunger, effects, the game's target. Run it first after a game update. |
| `#world` | State of the world snapshot. |
| `#calc <x> [y] <z>` | Compute a path without moving and print it. |
| `#cost` | Cost of every movement from the player's position. |
| `#inv [all]` (`#hotbar`) | Hotbar (or whole inventory) contents. |
| `#entities` (`#ent`) | Loaded entities. |
| `#nbt [dx dy dz]` | Block states of a block near the player. |
| `#ecs <Component...>` / `#ecsdump <Component> [bytes]` | Does the player have these EnTT components / raw content of one. |
| `#attrs` | Player attributes (health, hunger, speed...). |
| `#vt <object> [count]` / `#hex <object> [size]` / `#hexat <address> [size]` | Dump a vtable or memory to the log. |
| `#ism [slot...]` / `#swap <slot> <slot> [local\|server]` | Inventory request manager state / swap two slots. |
| `#slot <0-8>`, `#walk <ticks> [sprint]`, `#look <yaw> <pitch>`, `#break dx dy dz`, `#place dx dy dz`, `#grab <0\|1>` | Low-level tests of input and interaction. |
