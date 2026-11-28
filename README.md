# MineCPP

**MineCPP 0.3.0 — an offline-first C++20 Minecraft preparation tool, native voxel prototype and engine foundations.**

MineCPP is an independent C++ project. Its first launcher milestone imports an existing Minecraft Java client JAR from disk, safely extracts its ZIP entries, preserves nearby version metadata, and writes a local integrity manifest. It never downloads Minecraft or contacts an account service.

The repository also contains an experimental native voxel sandbox (the minecpp executable). That sandbox is not yet compatible with Minecraft Java Edition. The long-term goal is a native C++ client that can use resources prepared from a player's own local Minecraft installation.

## Launching the real game (0.4.0)

```bash
./build/minecpp-launcher                 # interactive menu (terminal)
./build/minecpp-launcher versions        # installed versions
./build/minecpp-launcher launch latest --player Steve --memory 4096
./build/minecpp-launcher launch 1.21.4 --dry-run   # print the command only
```

The launcher uses what the official launcher installed in `~/.minecraft` (or `--minecraft-dir`). It does not download anything and cannot sign in: offline mode, so singleplayer and offline servers. Versions before 1.19 are refused (extracted natives not implemented). Settings are saved in `minecpp-launcher.cfg`.

## Textures and settings (0.3.0)

`minecpp` uses the block textures from **your own** Minecraft client jar; MineCPP ships no Mojang assets.

1. Install a version with the official Minecraft launcher (it creates `~/.minecraft/versions/<id>/<id>.jar`).
2. Run `./build/minecpp`. The first start imports the newest version into `minecpp-data/` (a few seconds) and then reuses it.
3. Pick another jar with `--jar PATH` or another folder with `--minecraft-dir PATH`. `--no-textures` keeps flat colours.

Settings live in `minecpp-settings.cfg` next to where you run the game (`key = value`, `#` comments): `render_distance`, `fov`, `mouse_sensitivity`, `vsync`, `fullscreen`, `window_width`, `window_height`, `textures`, `minecraft_dir`, `jar`, `data_dir`. Command-line options override the file. In game: F11 fullscreen, V vsync, `[` `]` field of view, `-` `=` mouse sensitivity; these are written back on exit.

## Engine foundations (0.2.0)

Independent C++20 rewrites of Minecraft Java Edition building blocks, structured after the MIT-licensed [Horizon](https://github.com/horizon-rust/horizon) project. Each module has tests under `tests/`.

| Area | Files |
| --- | --- |
| Chunk storage | `packed_storage.hpp`, `paletted_container.hpp`, `chunk_section.hpp`, `chunk_io.hpp`, `registries.hpp` |
| Formats | `nbt.hpp`, `region.hpp`/`region.cpp` (Anvil `.mca`) |
| Randomness and math | `random.hpp` (Java `Random`, xoroshiro128++), `mth.hpp` |
| Physics | `aabb.hpp`, `collision.hpp`, `player_physics.hpp` |
| Items | `items.hpp` (registry, stacks, inventory) |
| Network | `protocol.hpp`, `packet_frame.hpp` |

Not yet: packet registry, world generation parity, lighting, entities beyond the player, rendering of these chunks. See `docs/ROADMAP.md`.

## Launcher 0.1.0

The first release provides a command-line launcher/importer:

- Import an existing client.jar without internet access.
- Detect the version ID from the JAR filename, or specify it explicitly.
- Extract stored and DEFLATE-compressed ZIP entries.
- Reject absolute paths, parent-directory traversal, duplicate/case-colliding paths, symbolic links, encrypted entries, unsupported compression, and unsafe archive sizes.
- Verify each extracted file against its CRC-32 and recorded size.
- Copy the matching version JSON when it sits next to the JAR.
- Keep the original JAR unchanged.
- List imported versions and verify an import again later.
- Replace an existing import only when --force is supplied.

### Import a local Minecraft version

A normal Minecraft installation already has a structure like this:

~~~text
.minecraft/
└── versions/
    └── 1.20.4/
        ├── 1.20.4.jar
        └── 1.20.4.json
~~~

Import the JAR:

~~~bash
./build/minecpp-launcher import ~/.minecraft/versions/1.20.4/1.20.4.jar
~~~

The version ID defaults to the JAR filename (1.20.4). MineCPP extracts the JAR under:

~~~text
minecpp-data/
└── versions/
    └── 1.20.4/
        ├── client/                 # extracted JAR entries
        ├── metadata/
        │   └── version.json        # copied when present beside the JAR
        └── manifest.mcpp           # file sizes and CRC-32 checksums
~~~

The original JAR is not moved, modified, or deleted. The default output folder is minecpp-data in the current working directory.

Other commands:

~~~bash
# Choose the ID and destination explicitly
./build/minecpp-launcher import /path/to/client.jar --id 1.20.4 --data-dir ./game-data

# Show locally imported versions
./build/minecpp-launcher list

# Check every imported file against the manifest
./build/minecpp-launcher verify 1.20.4

# Replace an existing imported version
./build/minecpp-launcher import /path/to/client.jar --id 1.20.4 --force

# Show commands
./build/minecpp-launcher --help
~~~

--force builds a complete staging directory first and then replaces the previous import. A failed extraction does not replace the previous version.

### Current boundaries

This is the first preparation milestone, not a playable Minecraft launcher yet:

- It extracts the client JAR and copies an adjacent version JSON file.
- It does not yet collect the libraries/ and assets/ directories referenced by Minecraft metadata.
- It does not yet implement Mojang's resource exporters, the full native game engine, a graphical launcher, Microsoft authentication, or multiplayer.
- It does not execute the extracted Java classes.
- ZIP64 and multi-disk archives are not supported in this first importer.

No Minecraft code, assets, libraries, or other proprietary game files are bundled in this repository. Supply files from a Minecraft installation you already have.

## Build

MineCPP uses C++20, CMake, GLFW, OpenGL, threads, and zlib. The importer and its tests are C++ code; zlib is used for standard ZIP DEFLATE decompression.

### Linux / Lubuntu

To build only the offline launcher/importer (no OpenGL or GLFW required):

~~~bash
sudo apt update
sudo apt install build-essential cmake zlib1g-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMINECPP_BUILD_VOXEL_PROTOTYPE=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure

./build/minecpp-launcher --help
~~~

To build both the launcher and the experimental voxel prototype, install the graphics dependencies and leave the CMake option enabled:

~~~bash
sudo apt install libglfw3-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMINECPP_BUILD_VOXEL_PROTOTYPE=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

### Windows

Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload, CMake, and vcpkg. From Developer PowerShell:

~~~powershell
vcpkg install glfw3:x64-windows-static zlib:x64-windows-static
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

.\build\Release\minecpp-launcher.exe --help
~~~

### macOS

~~~bash
xcode-select --install
brew install cmake glfw zlib

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

./build/minecpp-launcher --help
~~~

## Native voxel prototype

The separate minecpp executable is an early C++20 voxel sandbox used to develop engine fundamentals:

- GLFW window and OpenGL renderer.
- First-person camera, mouse look, WASD movement, sprinting, jumping, and player collision.
- Deterministic seeded terrain with hills, grass, dirt, stone, sand, trees, and bedrock.
- 16×16 horizontal chunks with world streaming around the player.
- Background worker threads generate terrain and build chunk meshes; OpenGL work stays on the main thread.
- Visible-face meshing, block breaking and placement, a six-slot block selector, and local world saves.

It is a development prototype, not a claim of Minecraft compatibility. Textures, water, caves, lighting, crafting, inventory, mobs, audio, multiplayer, and mod support remain future work.

## Automated tests

CMake builds two test executables:

- minecpp_tests: deterministic world generation, negative chunk coordinates, block edits, saves, and mesh generation.
- minecpp_import_tests: JAR import, DEFLATE extraction, metadata copy, duplicate-import protection, CRC verification, tamper detection, and path-traversal rejection.

GitHub Actions builds and runs tests on Linux, Windows, and macOS.

## Roadmap

See docs/ROADMAP.md for the planned launcher, resource-preparation, and native-client milestones.
