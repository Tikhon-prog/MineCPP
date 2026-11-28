# Changelog

## 0.4.0 — Launcher starts the real Minecraft

- `minecpp-launcher launch [VERSION|latest]` builds the same java command line as the official launcher from a version it already installed (`versions/`, `libraries/`, `assets/`) and starts the real game with an offline player. Merges `inheritsFrom` profiles (Fabric style), evaluates `rules` (os, arch, custom resolution), substitutes all `${...}` placeholders, supports `-Xmx`, window size, game folder and extra JVM/game arguments. `--dry-run` prints the command.
- Offline UUID is vanilla's `MD5("OfflinePlayer:" + name)` (own MD5 in `md5.hpp`); own strict JSON parser in `json.hpp`.
- `minecpp-launcher` without arguments in a terminal opens an interactive menu: play, choose version, full settings (folder, version, name, Java, memory, game folder, window size, JVM/game args), dry run. Settings live in `minecpp-launcher.cfg`.
- `minecpp-launcher versions` lists installed versions (releases first, newest first).
- Limits: no downloads, no Microsoft sign-in (singleplayer and offline servers only), versions before 1.19 with separately extracted natives are refused with a clear message, `os.version` rules are ignored, Windows spawning is untested. Not compiled by the author; first build is the first real test.
- New tests: `minecpp_json_tests`, `minecpp_launch_tests`.

## 0.3.0 — Textures from your own jar, settings

- The sandbox now draws with the real block textures from the player's own Minecraft client jar. On start it looks for the newest version in `~/.minecraft/versions` (or `--minecraft-dir`/`--jar`), imports it once into `minecpp-data/`, and packs `assets/minecraft/textures/block` into an atlas. Nothing is bundled or downloaded. Without a jar it falls back to the old flat colours.
- Add a PNG decoder (`png.hpp`, uses zlib) and `texture_atlas.hpp`: grass top and leaves get the plains tint baked in, the grass side composites the overlay, animated strips use their first frame, larger resource-pack textures are resampled.
- Mesh vertices carry UVs (`MeshVertex` gained `u`, `v`); `set_mesh_uv_table` selects the atlas. The hotbar shows textured icons.
- Add a settings file `minecpp-settings.cfg` (`render_distance`, `fov`, `mouse_sensitivity`, `vsync`, `fullscreen`, window size, `textures`, `minecraft_dir`, `jar`, `data_dir`) plus CLI overrides, and in-game hotkeys: F11 fullscreen, V vsync, `[` `]` FOV, `-` `=` mouse sensitivity. Hotkey changes are saved on exit.
- Fix two tests that were wrong, not the code: `chunk_section_tests` passed `get_block` arguments in the wrong order, and `nbt_tests` kept a reference into a destroyed temporary.

## 0.2.0 — Native engine foundations

First version of the C++ rewrite's building blocks, with Horizon (MIT) as the reference for structure. Everything is header/test level: the voxel sandbox is unchanged and this is not Minecraft-compatible yet. Nothing here has been compiled or run in CI by the author of these modules; treat the first build as the first real test.

- Add `PackedStorage`: Minecraft-style palette index storage (padded 64-bit words, 1-32 bits per entry) with tests. First module ported with Horizon (MIT) as reference.
- Add `PalettedContainer`: palette + packed indices that grow with the palette, uniform containers without storage, and validated save/load.
- Add `ChunkSection`: 16x16x16 blocks and 4x4x4 biome cells on `PalettedContainer`, vanilla YZX index order, non-air count.
- Add `Aabb`/`Vec3d` and `move_with_collision`: double-precision entity collision with vanilla axis order (Y, then larger of X/Z) and step-up. Full-cube blocks only.
- Add `ItemRegistry`, `ItemStack` and `Inventory`: stack limits, merge/split, pickup order. Data components not yet modelled.
- Add NBT reader/writer (`nbt.hpp`): all 12 tag types, big-endian, nameless network root, depth and length limits against hostile input, modified UTF-8 conversion.
- Add Anvil region reader/writer (`region.hpp`): header tables, sector validation, gzip/zlib/none chunk data, decompression-bomb limit; LZ4 and external `.mcc` chunks are reported as unsupported.
- Add `BlockStateRegistry`/`NameRegistry` and `chunk_io.hpp`: parse and write 1.18+ chunk NBT sections into `ChunkSection` with unknown tags (light, heightmaps, block entities) preserved; malformed palettes and data are rejected.
- Add `random.hpp`: bit-exact `java.util.Random`, Stafford mix13 and xoroshiro128++ with seed expansion. Java Random and mix13 are tested against reference values; xoroshiro seeding is not yet verified against vanilla.
- Add `mth.hpp` and `player_physics.hpp`: vanilla's table-based `Mth.sin/cos`, and one tick of on-foot player movement (gravity, ground and air friction, jump, sprint jump, 0.98 input scaling, collision). Expected values: jump height 1.2522, walk 0.2159 and sprint 0.2806 blocks per tick. Water, ladders, flying, sneaking and potion effects are not modelled.
- Add network primitives: `protocol.hpp` (VarInt/VarLong, strings with UTF-16 length limits, packed positions, UUIDs, bounds-checked reader) and `packet_frame.hpp` (length-prefixed frames with compression threshold, incremental decode, size limits). No packet registry yet.

## 0.1.0 — Native voxel prototype

- Start the project as a native C++20 voxel sandbox instead of a Java launcher.
- Add deterministic terrain generation and 16×16 chunk storage.
- Generate chunks and chunk meshes on a background worker pool.
- Add first-person movement, collision, jumping, sprinting, and mouse look.
- Add block selection, block breaking, and placement.
- Persist world seed and block edits.
- Add CMake, native world tests, and Linux/Windows/macOS GitHub Actions builds.
