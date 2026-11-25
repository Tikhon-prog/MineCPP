# MineCPP roadmap

MineCPP has two independent workstreams: an offline toolchain for files supplied from an existing Minecraft installation, and a native C++ voxel sandbox. The 1.0.0 release establishes foundations for both. It does **not** claim full Minecraft Java Edition compatibility.

## 1.0.0 — Offline preparation and voxel-engine foundation

- [x] C++20 JAR importer with ZIP Store/DEFLATE support, staging and integrity manifest.
- [x] Safe archive path validation, CRC verification and import listing.
- [x] Strict JSON parser and local version metadata inspection.
- [x] Asset-index parser, SHA-1 helper and local direct-library / asset-object preparation.
- [x] Virtual-asset and map-to-resources compatibility mappings.
- [x] Client logging configuration copy when declared.
- [x] Prepared-resource manifest and verification command.
- [x] Refuse to publish when required files are missing or unsupported inheritance/library requirements are present.
- [x] Bounded NBT reader, gzip/zlib decompression and Anvil region reader.
- [x] Decode 1.16+ padded block-state arrays and modern section palettes while preserving namespaced state IDs and properties.
- [x] Map a small explicit subset of common vanilla states to built-in sandbox blocks.
- [x] Separate player physics, fixed 60 Hz stepping, bounded collision substeps and voxel DDA targeting.
- [x] Greedy meshing and safer local world-save writes.
- [x] Dedicated source-level test targets and cross-platform CI workflow.

The checklist records implementation in source. It does not imply tests were executed or passed; consult CI for the commit's actual results.

## 1.1.x — Resource resolver completeness

- [ ] Resolve inherited version metadata with cycle detection and a bounded inheritance depth.
- [ ] Evaluate Mojang library allow/disallow rules for the current OS and architecture.
- [ ] Resolve Maven coordinate-only libraries against existing local library paths.
- [ ] Select and safely extract native classifiers for supported platforms.
- [ ] Report Java runtime requirements and verify a user-selected local runtime.
- [ ] Support additional asset-index and logging metadata variants.
- [ ] Add offline diagnostic mode that lists all missing files without copying.
- [ ] Add real version JSON and asset-index fixtures from user-owned installations.

## 1.2.x — Minecraft resource and world data

- [ ] Support the remaining Anvil chunk compression modes where their format is documented and testable.
- [ ] Add external .mcc chunk payload handling with bounded reads.
- [ ] Add legacy pre-1.16 palette packing and fixtures for the old section format.
- [ ] Parse block registries, block-state definitions and tags across explicitly supported data versions.
- [ ] Parse blockstate/model JSON, parents, variants and multipart definitions.
- [ ] Decode PNG textures and build a material/texture atlas.
- [ ] Build an importer that turns supported vanilla NBT/Anvil chunks into the native world representation without silently discarding unknown blocks.
- [ ] Compare decoded chunks against user-provided reference saves.

## 1.3.x — Renderer and simulation

- [ ] Add shader-based rendering and GPU vertex/index buffers.
- [ ] Add proper material, UV, transparency and render-layer handling.
- [ ] Add ambient occlusion, chunk lighting and dynamic light updates.
- [ ] Add full block collision shapes instead of a single solid/air rule.
- [ ] Add fluid simulation and biome-aware terrain generation.
- [ ] Add entities, hitboxes, ticking, animation and sound.
- [ ] Add inventory, item stacks, crafting and survival systems.
- [ ] Add crash-safe, versioned world persistence and migration.

## Later — Native client compatibility

- [ ] Reproduce the version-specific data preparation performed by the official client without running Mojang's Java client.
- [ ] Implement the required client lifecycle, menus, settings and resource loading in C++.
- [ ] Add reference tests for world generation, blocks, entities, physics and rendering.
- [ ] Design protocol support as a separate milestone with explicit security and compatibility tests.
- [ ] Publish a supported-version matrix and incompatibility list.
- [ ] Claim Minecraft compatibility only after a reproducible conformance suite passes.

## Non-goals

- Do not bundle Minecraft's proprietary code, assets or libraries.
- Do not download files or contact Mojang/Microsoft during offline import, preparation or verification.
- Do not modify or delete the user's original game installation.
- Do not execute the imported Java bytecode as a substitute for the native client.
