# MineCPP

**MineCPP 1.0.0** is an independent C++20 project with two local-only parts:

- an offline importer and resource-preparation utility for files from a Minecraft installation the user already has;
- a native OpenGL voxel sandbox used as the foundation for a future C++ game client.

MineCPP does not download Minecraft, bundle Mojang assets or libraries, authenticate accounts, or execute the supplied Java client. The native sandbox is its own game prototype and is **not yet Minecraft Java Edition-compatible**.

## What 1.0.0 includes

### Offline import and inspection

The minecpp-launcher utility can:

- import a locally supplied client JAR without moving or modifying the source;
- extract ZIP Store and DEFLATE entries into a staged directory;
- reject path traversal, absolute paths, symlinks, special ZIP entries, encrypted entries, unsupported compression, duplicate/case-colliding paths, and unsafe archive sizes;
- copy an adjacent version JSON file when one is present;
- create and verify an integrity manifest using file sizes and CRC-32;
- list imported versions;
- parse and inspect local version metadata, including the main class, declared Java version, asset index, direct library artifacts, logging configuration and unresolved library requirements.

Example layout:

~~~text
.minecraft/
└── versions/
    └── 1.20.4/
        ├── 1.20.4.jar
        └── 1.20.4.json

minecpp-data/
└── versions/
    └── 1.20.4/
        ├── client/
        ├── metadata/
        │   └── version.json
        └── manifest.mcpp
~~~

Import and inspect:

~~~bash
./build/minecpp-launcher import ~/.minecraft/versions/1.20.4/1.20.4.jar
./build/minecpp-launcher list
./build/minecpp-launcher verify 1.20.4
./build/minecpp-launcher inspect 1.20.4
~~~

The version ID is inferred from the JAR filename or its containing folder. Use --id to override it and --data-dir to select the output directory.

### Offline resource preparation

The prepare command uses only files already present in an existing game directory. It reads the imported version JSON, the declared asset index, the local content-addressed asset objects, direct library artifacts and an optional client logging configuration. It verifies declared sizes and SHA-1 values, then prepares a separate output tree.

~~~bash
./build/minecpp-launcher prepare 1.20.4 --game-dir ~/.minecraft
./build/minecpp-launcher verify-prepared 1.20.4
~~~

Prepared output is kept separate from the source installation:

~~~text
minecpp-data/
└── prepared/
    └── 1.20.4/
        ├── metadata/
        │   └── version.json
        ├── assets/
        │   ├── indexes/
        │   ├── objects/
        │   ├── virtual/       # only when requested by the asset index
        │   └── log_configs/   # when declared by version metadata
        ├── libraries/
        ├── resources/         # only when map_to_resources is requested
        └── resources.mcpp
~~~

Preparation is staged and only published when its required local inputs are present. Replacing an existing prepared tree requires --force. Legacy asset mappings use hard links from the staged object copy where possible, with a copy fallback.

**Important completeness boundary:** 1.0.0 does not resolve inherited version JSON, OS-specific library rules, Maven-only library entries, or native library classifiers. When metadata requires one of those unresolved cases, MineCPP reports preparation as incomplete instead of claiming the resource set is ready. It never silently substitutes network downloads.

### NBT and Anvil file readers

The format library includes:

- bounded NBT parsing for all standard tag types;
- gzip/zlib NBT decompression with an output limit;
- Anvil .mca region header and sector validation;
- reading region chunks stored with gzip, zlib or no compression;
- decoding modern section palettes and block-state packed arrays, preserving namespaced block IDs and string properties;
- explicit mapping of a small set of common vanilla block states to the sandbox's built-in block enum.

LZ4 region compression, external .mcc chunk payloads, and pre-1.16 continuous palette packing are not supported in this release. The reader is a format foundation: the graphical sandbox still generates its own procedural worlds and does not yet load a vanilla save into its renderer.

### Native voxel sandbox

The separate minecpp executable currently provides:

- a GLFW window and OpenGL 2.1 compatibility renderer;
- first-person mouse-look and a six-slot block selector;
- deterministic procedural terrain, 16×16 horizontal chunks and asynchronous chunk/mesh jobs;
- block breaking and placement;
- a standalone player-physics module with acceleration, friction, gravity, jumping, sprinting, axis-separated AABB collision and bounded movement substeps;
- a fixed 60 Hz simulation step;
- voxel DDA ray traversal for block selection;
- greedy meshing of coplanar faces with the same block material;
- local world saves with validated reads and staged/backup writes.

World saves contain MineCPP's own terrain seed and player block edits. They are not Minecraft .mca worlds. Rendering currently uses flat block colours; Minecraft texture atlases, model JSON, lighting, fluids, biome generation, entities, inventory/crafting, sound, multiplayer and mod support are not part of 1.0.0.

## Build

Requirements: C++20 compiler, CMake 3.20+, zlib. The native windowed client additionally needs GLFW 3.3+ and OpenGL.

### Linux / Lubuntu

~~~bash
sudo apt update
sudo apt install build-essential cmake zlib1g-dev libglfw3-dev libgl1-mesa-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

To build the offline tools and file-format readers without the graphical client:

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMINECPP_BUILD_VOXEL_PROTOTYPE=OFF
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
~~~

### macOS

~~~bash
xcode-select --install
brew install cmake glfw zlib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

## Tests and CI

CMake defines dedicated test targets for:

- ZIP/JAR importing and import-manifest verification;
- JSON and Minecraft version metadata validation;
- SHA-1, asset-index parsing, offline preparation and prepared-resource verification;
- NBT parsing, decompression and Anvil region reading;
- modern block-state palette decoding;
- deterministic world generation, persistence and greedy mesh output;
- player physics and voxel DDA ray traversal;
- CLI help, version and error-path smoke tests.

GitHub Actions is configured to build and run CTest on Linux, Windows and macOS. The test targets are included in this release source, but a successful run must be established by the CI results for the commit; source registration alone is not a test pass.

## Design rules

- C++20 implementation; the Java JAR is an input archive, not the runtime.
- No network activity for import, inspection, preparation or verification.
- Never alter the user's original JAR or game directory.
- Never report a resource set as complete when unsupported metadata requirements remain unresolved.
- Do not claim Minecraft compatibility until real reference worlds, resource packs and gameplay behaviour pass a documented compatibility suite.

See the architecture guide at docs/ENGINE_ARCHITECTURE.md and the roadmap at docs/ROADMAP.md.
