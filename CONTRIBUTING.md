# Contributing

Thanks for your interest in Baritone Bedrock! Bug reports, fixes, ports of missing Baritone behavior and updates to new game versions are all welcome.

## Reporting a bug

[Open an issue](https://github.com/miked06/baritone-bedrock/issues/new/choose) with:

- your Minecraft version (bottom of the main menu) and the bot version (`#version`);
- what you did (the commands) and what happened instead;
- `%LOCALAPPDATA%\BaritoneBedrock\log.txt` from that session. For a movement problem, run `#set logMovement true` before reproducing it: the log then has one line per tick.

Please check [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) first.

## Proposing a change

1. Fork the repository and create a branch.
2. Build and run the tests (see [BUILDING.md](docs/BUILDING.md)). They must pass.
3. Test in game when the change affects behavior, and say in the pull request what you tested.
4. Open a pull request describing the change and why.

Small, focused pull requests are easier to review than large ones.

## Guidelines

- **Follow Baritone.** Most logic is a port of [Baritone's Java code](https://github.com/cabaletta/baritone). Keep the same structure, names, constants and settings; when Bedrock forces a difference, explain it in a comment and add it to [PARITY.md](docs/PARITY.md).
- **Keep the core independent of the game.** `src/baritone/` must not include game headers; game access goes through `PlayerContext` and the world snapshot.
- **Add tests** in `tests/` for new core logic.
- **Be careful with game internals.** Prefer emulating player input. A new internal call must use the exact signature and calling context the game uses — wrong calls freeze or crash the game. See [DEVELOPMENT.md](docs/DEVELOPMENT.md).
- **Style**: C++23, 4 spaces, the existing naming (`camelCase` functions, `trailing_` members, `kConstants`). Comments explain *why*, briefly.
- **Settings**: after adding or changing one in `Settings.h`, run `python tools/gen_settings_doc.py`.
- **Never commit game files** (dumps of `Minecraft.Windows.exe`, game assets) or third-party binaries.

## Updating to a new Minecraft version

This is the most valuable contribution after each game update. Follow [Updating to a new game version](docs/DEVELOPMENT.md#updating-to-a-new-game-version) and include in your pull request the signatures and offsets you changed and how you checked them.

## License

By contributing, you agree that your contributions are licensed under the [LGPL-3.0](COPYING.LESSER), like the rest of the project.
