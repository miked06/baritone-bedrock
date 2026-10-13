# Building

## Requirements

- Windows 10/11 x64
- [Visual Studio 2022 or newer](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload (MSVC with C++23 support)
- [CMake](https://cmake.org/) 3.25 or newer (Visual Studio ships one)
- Git (CMake downloads the dependencies)

The dependencies are fetched automatically by CMake (`cmake/Dependencies.cmake`):

| Library | Use |
|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) 1.3.4 | Function hooks |
| [libhat](https://github.com/BasedInc/libhat) 0.6.0 | Signature scanning |
| [EnTT](https://github.com/skypjack/entt) (pinned commit) | The game's entity-component registry layout. It must match the revision Minecraft is built with. |

## Build

From a *Developer PowerShell* or any shell with CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config RelWithDebInfo
```

To pick a generator explicitly: `-G "Visual Studio 17 2022"` or `-G "Visual Studio 18 2026"`.

Outputs:

| File | |
|---|---|
| `build\bin\RelWithDebInfo\BaritoneBedrock.dll` | The DLL to inject |
| `build\bin\RelWithDebInfo\BaritoneBedrock.pdb` | Its symbols (keep it next to the DLL: crash logs use it) |
| `build\bin\RelWithDebInfo\baritone_tests.exe` | The offline tests |

> [!IMPORTANT]
> Always build `Release` or `RelWithDebInfo`, never `Debug`. The game is built in release mode and the bot reads the game's `std::string`, `std::vector`... directly: their layout differs in debug builds.

## Tests

The pathfinding core (`src/baritone/`) does not depend on the game. `tests/` runs it on simulated worlds: costs, every movement, falls with and without water, parkour, the long-distance cache on disk, schematic loaders, orientation rules, the elytra physics, mining filters, masks and rotations, and a performance check.

```powershell
build\bin\RelWithDebInfo\baritone_tests.exe
```

or `ctest --test-dir build -C RelWithDebInfo`. Every change to the core should keep them green, and new logic should come with a test.

## Continuous integration

`.github/workflows/build.yml` builds the DLL and runs the tests on every push and pull request. Pushing a tag `v*` publishes a GitHub release with the DLL and its PDB.

## Regenerating the settings documentation

`docs/SETTINGS.md` is generated from `src/baritone/Settings.h`:

```powershell
python tools/gen_settings_doc.py
```
