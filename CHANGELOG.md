# Changelog

## Unreleased

- Add `PackedStorage`: Minecraft-style palette index storage (padded 64-bit words, 1-32 bits per entry) with tests. First module ported with Horizon (MIT) as reference.

## 0.1.0 — Native voxel prototype

- Start the project as a native C++20 voxel sandbox instead of a Java launcher.
- Add deterministic terrain generation and 16×16 chunk storage.
- Generate chunks and chunk meshes on a background worker pool.
- Add first-person movement, collision, jumping, sprinting, and mouse look.
- Add block selection, block breaking, and placement.
- Persist world seed and block edits.
- Add CMake, native world tests, and Linux/Windows/macOS GitHub Actions builds.
