# MineCPP roadmap

MineCPP has two connected but distinct tracks: an offline launcher/preparation tool and an experimental native voxel client. The native client is not yet Minecraft-compatible; compatibility must be earned by implementing and testing the formats and game rules.

## 0.1.0 — Offline JAR import foundation

- C++20 command-line launcher executable.
- Import an existing local Minecraft client JAR.
- Extract ZIP Store and DEFLATE entries without modifying the source JAR.
- Reject path traversal, absolute paths, symbolic links, duplicate paths, encrypted entries, unsupported methods, and oversized archives.
- Copy the matching version JSON file when it is adjacent to the JAR.
- Write a local file manifest with sizes and CRC-32 checksums.
- List imported versions and verify their extracted files.
- Automated ZIP fixtures for valid, corrupted, and malicious archives.
- Preserve the existing standalone voxel sandbox as an experimental engine target.

## 0.2.x — Minecraft installation metadata

- Parse the version JSON with a real JSON parser and validate its schema.
- Resolve the version's asset index, libraries, native classifiers, logging configuration, and Java requirements.
- Import only the assets and libraries referenced by the selected version.
- Add offline completeness checks and a clear report of missing files.
- Support multiple local game directories without searching the network.
- Add safe migration for changes to the local import manifest.

## 0.3.x — Native resource preparation

- Build a deterministic resource-preparation pipeline from imported client and server data supplied by the user.
- Implement version-specific data extractors and document the supported game versions.
- Verify source files against locally available official checksums where those metadata are present.
- Keep generated resources separate from original JARs and assets.
- Add resource-level tests and reproducible preparation reports.

## 0.4.x — Launcher experience

- Add a graphical C++ launcher UI.
- Show imported versions, resource readiness, verification status, and storage paths.
- Add profile settings, logs, crash reports, and launch configuration.
- Keep all normal operations local and offline.
- Do not add account or server authentication unless a separate, explicit requirement is agreed.

## Later — Native client compatibility

- Upgrade the experimental renderer and world model.
- Implement game data formats, terrain, block states, lighting, entities, inventory, UI, and world persistence.
- Add deterministic tests against user-provided reference worlds and known vanilla behaviour.
- Treat multiplayer, mod loaders, and compatibility with third-party servers as separate milestones.
- Do not describe the client as Minecraft-compatible until parity is demonstrated by tests.

## Non-goals

- Do not bundle Minecraft's proprietary code or assets.
- Do not download game files or contact Mojang/Microsoft during offline import or verification.
- Do not silently modify or delete the user's source JAR or original game directory.
- Do not claim that extracted Java classes can run as a native C++ client.
