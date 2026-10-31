# Schematic preview and building

The built-in schematic workspace is the Bedrock alternative to an open Schematica/Litematica placement. It loads files, displays a ghost, adjusts the placement and passes that exact placement to Baritone's survival builder. Java mods themselves are not loaded into Bedrock.

## Preview first, then build

Put your files in `%LOCALAPPDATA%\BaritoneBedrock\schematics\`, enter a world, then run:

```text
#schematic list
#schematic load "My house.litematic" ~5 ~ ~
#schematic rotate 90
#schematic mirror none
#schematic move ~ ~ ~3
#schematic materials
#schematic build
```

Loading displays a preview and stops existing bot activity. It does not build or change blocks. `#schematic build` starts construction; `#pause`, `#resume` and `#stop` control it. Editing/unloading a placement stops a running build. Invalid files leave the previous placement intact.

The workspace holds one placement. `#schematica` and `#litematica` with no arguments build that placement. With a filename, either command loads a preview; their subcommands also work, for example `#litematica load house.litematic`. `#litematica 1` builds the single loaded placement. `#schem` and `#blueprint` are aliases of `#schematic`.

## Commands

| Command | Behavior |
|---|---|
| `#schematic list [page]` | List supported files, eight per page. |
| `#schematic load <file> [x y z]` | Load a file at your feet or an explicit origin; quotes support filenames with spaces. An omitted extension is resolved using `schematicFallbackExtension` and the supported formats. |
| `#schematic info` | Show dimensions, origin, rotation, mirror, preview state and active layers. |
| `#schematic move x y z` | Move the origin. `~` coordinates are relative to the **current placement**. |
| `#schematic move here` | Move the origin to your feet. |
| `#schematic rotate 0\|90\|180\|270` | Set the absolute clockwise rotation; negative multiples of 90 also work. |
| `#schematic mirror none\|x\|z` | Set the reflected coordinate axis, applied before rotation. |
| `#schematic layer all` | Preview/build every layer. |
| `#schematic layer y [yMax]` | Restrict both preview and construction to this zero-based local Y range. |
| `#schematic preview on\|off` | Show/hide the ghost; hiding it does not cancel a running build. |
| `#schematic materials [page]` | Count block positions by type in the active placement, plus air/ignored cells. |
| `#schematic build` | Build the previewed origin, rotation, mirror and layer mask. |
| `#schematic clear` | Stop its build and unload the placement. |
| `#complete schematic load <prefix>` | Suggest filenames. Quote a suggested filename containing spaces when loading it. |

Load coordinates are relative to your feet; move coordinates are relative to the placement. Origins are the minimum corner of the transformed bounding box. Loading another world or changing dimension clears the placement so it cannot be built accidentally in a different world.

## Reading the preview

- **Cyan:** missing block, or replaceable terrain where a block is wanted.
- **Red:** a different block or supported orientation occupies the position.
- **Orange:** the schematic wants air here, but a block exists.
- **Gray:** the chunk is not loaded, or the position is outside build height; the comparison is unknown.
- **Blue outline:** the complete placement bounds. Matching and ignored positions have no individual ghost.

The ghost uses translucent unit cubes and outlines, rather than original block textures/shapes. Up to 512 differences within 16 blocks of the player are displayed and refreshed about twice per second. Walk around a larger placement to inspect it. Cached chunk approximations are never counted as exact live block matches.

The workspace owns its spatial transforms and active layer mask. `buildSchematicRotation`, `buildSchematicMirror` and `schematicOrientationX/Y/Z` are not applied a second time when it builds. `mapArtMode` and `buildOnlySelection` are file-build wrappers for the older direct `#build <file>` workflow, not workspace transforms. Ordinary execution settings, including `allowBreak`, `allowPlace`, `allowInventory`, substitutes, ignored/skipped blocks, build layers and repeats, still control the builder. Material counts describe the source placement, before substitutions/skips; they are not remaining inventory requirements. Beds, doors and double slabs can consume different numbers of items than their block-position counts.

## Formats and limits

| File | Reads |
|---|---|
| `.mcstructure` | Bedrock little-endian NBT, first block layer, block names and state values. |
| `.schem` | Sponge v1/v2 and nested v3 block palettes/varint data, plain NBT or gzip/zlib. |
| `.litematic` | Litematica regions, negative region dimensions, multi-region gaps, bit-packed indices spanning long boundaries. |
| `.schematic` | Legacy MCEdit numeric IDs/metadata; Sponge files using this extension are also detected. |

Imports validate dimensions and palette references. NBT collections are checked against available input and an allocation budget before reservation. Input files and decompressed streams are capped at 256 MiB; schematic volumes at 64 million blocks. NBT allocation limits can reject a smaller but unusually expensive representation. Material counts use a palette histogram, including active layers, to avoid copying a block-state string for every position in a large file.

Java names and supported orientation properties are translated to Bedrock; not every Java block state has a matching Bedrock placement. Orientation support includes logs/pillars, stairs, slab halves and the supported furnace/chest/pumpkin directions. Redstone state, double-slab item handling, waterlogging, multi-block placement rules, containers/sign contents, entities and arbitrary block-entity data are not fully reproduced. The loader reports known discarded metadata, secondary fluid layers and unknown legacy IDs. A Java numeric mod block ID has no automatic Bedrock equivalent.

No external resource pack, Java mod or world-edit command permission is required for this preview/build workflow. Supply the materials in your hotbar or enable `allowInventory` for main-inventory retrieval; the builder uses normal player actions.

## Validation on Bedrock 26.60

The offline suite includes preview classifications and output caps, live-vs-unloaded comparisons, rotation/mirror/layer material counts, no duplicate builder transforms, invalid dimensions/palettes, quoted command paths, NBT collection validation and a nested Sponge v3 fixture, alongside existing format/pathfinding tests.

In the existing world, a gzip-compressed Sponge v3 file with spaces in its name loaded as a 2x2x1 preview. Its three differing cells and complete bounds were inspected visually after the native shader color correction. Move/rotation/mirror/layer controls, material counts, filename completion and invalid-file preservation were exercised. A separate one-block placement was built through `#litematica`: the log reported `Build complete`, and a native block-state read confirmed the expected stone at the previewed coordinate. The temporary block was removed and confirmed to be air; inventory counts, selected slot and saved settings were restored. No new world was created. Large builds, arbitrary complex Java state conversions and every renderer/graphics mode have not been validated by this small test.
