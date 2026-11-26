# Changelog

## Unreleased

- Add `PackedStorage`: Minecraft-style palette index storage (padded 64-bit words, 1-32 bits per entry) with tests. First module ported with Horizon (MIT) as reference.
- Add `PalettedContainer`: palette + packed indices that grow with the palette, uniform containers without storage, and validated save/load.
- Add `ChunkSection`: 16x16x16 blocks and 4x4x4 biome cells on `PalettedContainer`, vanilla YZX index order, non-air count.
- Add `Aabb`/`Vec3d` and `move_with_collision`: double-precision entity collision with vanilla axis order (Y, then larger of X/Z) and step-up. Full-cube blocks only.
- Add `ItemRegistry`, `ItemStack` and `Inventory`: stack limits, merge/split, pickup order. Data components not yet modelled.
- Add NBT reader/writer (`nbt.hpp`): all 12 tag types, big-endian, nameless network root, depth and length limits against hostile input, modified UTF-8 conversion.
- Add Anvil region reader/writer (`region.hpp`): header tables, sector validation, gzip/zlib/none chunk data, decompression-bomb limit; LZ4 and external `.mcc` chunks are reported as unsupported.
- Add `BlockStateRegistry`/`NameRegistry` and `chunk_io.hpp`: parse and write 1.18+ chunk NBT sections into `ChunkSection` with unknown tags (light, heightmaps, block entities) preserved; malformed palettes and data are rejected.

## 0.1.0 — Native voxel prototype

- Start the project as a native C++20 voxel sandbox instead of a Java launcher.
- Add deterministic terrain generation and 16×16 chunk storage.
- Generate chunks and chunk meshes on a background worker pool.
- Add first-person movement, collision, jumping, sprinting, and mouse look.
- Add block selection, block breaking, and placement.
- Persist world seed and block edits.
- Add CMake, native world tests, and Linux/Windows/macOS GitHub Actions builds.
