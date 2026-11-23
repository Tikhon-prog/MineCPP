# MineCPP roadmap

This roadmap separates the launcher from the much larger goal of a native C++ Minecraft-compatible client.

## 0.1.0 — Local vanilla launcher

- Discover installed Java Edition versions from disk.
- Inspect and safely extract a client JAR.
- Import a version into a MineCPP profile without modifying the original installation.
- Build a classpath from already-present vanilla libraries.
- Extract local native classifiers and launch using a local Java runtime.
- Support offline player identity, dry-run command preview, and offline diagnostics.
- Build and test on Linux, Windows, and macOS.

## 0.2.x — Reliable version and instance management

- Test against more real vanilla version metadata (legacy and modern).
- Improve JSON rule handling, library path validation, and native extraction filters.
- Verify local library and asset hashes where the version metadata provides them.
- Detect Java runtime versions and show clear compatibility errors.
- Add isolated game directories, per-instance settings, memory allocation, and log capture.
- Add robust error reporting and recovery for interrupted imports.

## 0.3.x — Desktop launcher

- Add a desktop UI without moving game logic out of the C++ core.
- Allow folder/JAR selection, version cards, profiles, and launch settings.
- Display import progress, validation status, and game logs.
- Keep every operation local; no automatic downloading or online account requirement.

## Native-client research milestone

Horizon is not merely a launcher: its native client is a separate implementation of the game. Reaching that goal in MineCPP requires a new engine, not just unpacking a JAR.

Before starting that implementation, define and prototype:

1. Resource and data formats that can be read from the locally supplied game files.
2. A renderer backend (for example, SDL plus OpenGL or Vulkan) and a window/input layer.
3. Texture atlas, model, block-state, biome, sound, and localization pipelines.
4. Chunk storage, meshing, lighting, world streaming, and entity simulation.
5. A protocol/client layer for multiplayer, with explicit boundaries around supported servers and authentication.
6. Compatibility tests against locally supplied vanilla assets and worlds.

The native milestone must be treated as a separate engine project. The Java launcher in 0.1.0 is useful as a working baseline and import/inspection tool, but it is not the native client.
