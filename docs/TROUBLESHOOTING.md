# Troubleshooting

First, check `%LOCALAPPDATA%\BaritoneBedrock\log.txt`: every command, error and warning is there. Attach it when you [report a bug](https://github.com/miked06/baritone-bedrock/issues/new/choose).

## The DLL does not load / nothing happens

- **Check your game version.** Only Minecraft Bedrock **26.52** (package `1.26.5203.0`) is supported. Other versions move the functions the DLL hooks: the console shows which signatures were not found and the bot refuses to start. You can see your version at the bottom of Minecraft's main menu.
- **Inject into the right process**: `Minecraft.Windows.exe`.
- **Antivirus**: DLL injectors and injected DLLs are often flagged. Build the DLL yourself if you prefer (see [BUILDING.md](BUILDING.md)).
- **Admin rights**: if Minecraft runs as administrator, the injector must too. Running both normally is best.

## The bot stops when I switch windows

Turn off **"Pause game when focus is lost"** in Minecraft's settings. Otherwise the world, and the bot, stop as soon as the game loses focus. The bot itself works fine with the game in the background.

## The game freezes

- Clicking inside the *Baritone Bedrock* console used to freeze the game (Windows' text selection mode blocks the console, and the game waits on it). The console's selection mode is now disabled; if you still see this, press `Esc` in the console.
- If the game freezes for more than 5 seconds, the watchdog writes the game thread's call stack to `log.txt` (look for `Watchdog`). Please include it in a bug report.

## "No path found"

- The goal may be unreachable with the current settings: `allowBreak`, `allowPlace` and blocks in `acceptableThrowawayItems` decide whether the bot may dig or build its way.
- A goal in the air needs throwaway blocks to pillar up (dirt, cobblestone, netherrack, stone, cobbled deepslate by default — not obsidian).
- Falls higher than `maxFallHeightNoWater` (3) need a water bucket, and at most `maxFallHeightBucket` (20) blocks.
- Very long trips are computed in segments; the bot retries with more time before giving up.

## The bot walks on the surface instead of mining

`#mine` needs a pickaxe good enough for the ore in your hotbar (or inventory with `allowInventory`), otherwise breaking stone looks too expensive. The bot warns you in the chat. In creative mode everything breaks instantly.

## The bot does not break or place blocks

- Check `allowBreak` and `allowPlace` (`#modified` lists what you changed, `#reset all` restores the defaults).
- After changing the held item, the bot waits a few ticks before clicking, so the server knows about the new item.
- Bedrock's inventory is server-authoritative: moving items (with `allowInventory`) is a request the server must accept.

## The elytra flight does not start

- Wear an elytra and carry fireworks. With 5 fireworks or less (`elytraMinFireworksBeforeLanding`), or a nearly broken elytra (`elytraMinimumDurability`), the bot refuses to take off, like Java Baritone.
- By default you take off yourself (jump, then jump again in the air); set `elytraAutoJump true` to let the bot do it. Taking off from flat ground can fail a few times; starting from a high place is more reliable.
- In creative, two jump presses toggle creative flight instead of opening the elytra.

## The path is not drawn

Check `renderPath`, `renderGoal` and `renderSelectionBoxes`. Lines are hidden behind blocks (there is no "through walls" rendering on Bedrock yet).

## Commands go to the chat

Only messages starting with `prefix` (`#` by default) are handled; anything else is sent to the server as usual. If you changed `prefix` or turned `prefixControl` off and can no longer type commands, delete that line from `%LOCALAPPDATA%\BaritoneBedrock\settings.txt` and inject again.
