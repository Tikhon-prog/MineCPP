# Changelog

## 1.0.0 — Offline preparation and native voxel foundation

- Consolidate the offline JAR importer, metadata inspector and local resource preparer into the 1.0.0 C++20 release line.
- Add strict JSON parsing, version metadata inspection, asset-index parsing and streaming SHA-1 verification.
- Prepare direct library artifacts, asset objects, legacy virtual/map-to-resources mappings and logging configuration from local files only.
- Add a verifiable resource manifest and a verify-prepared command.
- Reject preparation when version inheritance, library rules or native classifiers cannot be resolved.
- Add bounded NBT parsing, gzip/zlib decompression, Anvil region-sector validation and modern block-state palette decoding.
- Separate player physics from the renderer, introduce a fixed-step simulation and DDA block targeting.
- Add greedy chunk meshing and safer deterministic world-save publication.
- Expand CMake test targets for metadata, resources, NBT, Anvil, physics and ray traversal.
- Keep vanilla Minecraft launch, full resource-exporter compatibility, the full Java client renderer, networking and gameplay parity outside this release's claims.

## 0.1.0 — Native voxel prototype

- Start the project as a native C++20 voxel sandbox.
- Add deterministic terrain generation and 16×16 chunk storage.
- Generate chunks and chunk meshes on a background worker pool.
- Add first-person movement, collision, jumping, sprinting and mouse look.
- Add block selection, block breaking and placement.
- Persist world seed and block edits.
- Add CMake, native world tests and Linux/Windows/macOS GitHub Actions builds.
