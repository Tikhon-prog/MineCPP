# MineCPP

**MineCPP 0.1.0 — native C++ voxel sandbox prototype.**

This is an independent, native C++20 block-world project. It does not load or run the Minecraft Java client, does not bundle Mojang assets, and does not depend on Java. The first milestone is a small playable 3D voxel sandbox that establishes the engine foundations before larger features are attempted.

## First version

- Native C++20 application with a GLFW window and an OpenGL renderer.
- First-person camera, mouse look, WASD movement, sprinting, jumping, and basic player collision.
- Seeded block terrain with hills, grass, dirt, stone, sand, trees, and bedrock.
- 16×16 horizontal chunks; the world streams around the player.
- Background worker threads generate terrain and build chunk meshes. OpenGL drawing stays on the main thread.
- Visible-face meshing: hidden faces between neighbouring solid blocks are not emitted.
- Break and place blocks, select six block types, and save edits to a compact local world file.
- CMake build and automated world/mesh tests; CI builds Linux, Windows, and macOS.

This is a foundation, not feature parity with Minecraft. Textures, water, caves, lighting, crafting, inventory UI, mobs, audio, multiplayer, mod support, and a native resource pipeline are future work.

## Build

### Linux / Lubuntu

Install the compiler and system dependencies:

```bash
sudo apt update
sudo apt install build-essential cmake libglfw3-dev libgl1-mesa-dev
```

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/minecpp
```

### Windows

Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload, CMake, and vcpkg. From Developer PowerShell:

```powershell
vcpkg install glfw3:x64-windows-static
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\minecpp.exe
```

### macOS

Install Xcode Command Line Tools and Homebrew, then:

```bash
xcode-select --install
brew install cmake glfw
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/minecpp
```

## Controls

| Input | Action |
|---|---|
| W / A / S / D | Move |
| Mouse | Look around |
| Space | Jump |
| Left Shift | Sprint |
| Left mouse button | Break the targeted block |
| Right mouse button | Place the selected block |
| 1–6 | Select grass, dirt, stone, sand, wood, or leaves |
| Tab | Release or recapture the mouse |
| Escape | Save and quit |

The window title displays the player's coordinates, selected block, loaded chunk count, and pending terrain-generation jobs.

## Worlds and options

By default, MineCPP saves edits to `worlds/Default.mcw` in the current directory. The world terrain is deterministic from its seed; the save file stores block edits, not a duplicate copy of every generated chunk.

```bash
# Start a new world with a chosen seed
./build/minecpp --world worlds/Test.mcw --seed 20261123

# Increase the loaded chunk radius (2..8)
./build/minecpp --world worlds/Test.mcw --render-distance 5

# See all options and controls
./build/minecpp --help
```

If a save file already exists, its saved seed is used unless `--seed` was supplied explicitly.

## Project layout

- `src/world.*`: deterministic terrain, chunk streaming, save files, and worker jobs.
- `src/mesh.*`: chunk snapshots and visible-face mesh generation.
- `src/renderer.*`: window, OpenGL drawing, player movement, and block interaction.
- `src/thread_pool.hpp`: bounded-size background worker pool.
- `tests/world_tests.cpp`: deterministic terrain, negative chunk coordinates, edits, and mesh tests.
- `docs/ENGINE_ARCHITECTURE.md`: current design and thread ownership.
- `docs/ROADMAP.md`: milestones toward a larger native engine.

## Design boundaries

The engine does not ship copyrighted Minecraft code, textures, sounds, fonts, or other game assets. The current renderer uses procedural geometry and vertex colours. Compatibility with Minecraft worlds or network servers is not claimed by this prototype.
