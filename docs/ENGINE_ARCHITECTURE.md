# MineCPP 1.0.0 architecture

MineCPP contains a local-only preparation toolchain, a binary-format library and a separate native voxel sandbox. The native sandbox does not yet execute the imported Minecraft client or load a vanilla world into its renderer.

## Components

- minecpp-launcher: import, list, verify, inspect, prepare and verify-prepared commands.
- minecpp_metadata: strict JSON parser and Minecraft version / asset-index metadata.
- minecpp_resources: SHA-1 validation, offline preparation, staging and resource manifest verification.
- minecpp_formats: NBT, compressed NBT, Anvil region and modern block-state palette readers.
- minecpp_world: deterministic custom terrain, chunk state, worker pool, world persistence and mesh scheduling.
- minecpp: GLFW/OpenGL native voxel sandbox with player physics and block interaction.

## Offline import flow

~~~text
User-owned Minecraft installation
  └── versions/<id>/
        ├── <id>.jar
        └── <id>.json (when available)
                |
                v
minecpp-launcher import
  ├── parse the ZIP central directory
  ├── reject unsafe flags, methods, paths and entry sizes
  ├── extract to a staging directory
  ├── decompress Store / DEFLATE entries
  ├── verify CRC-32
  ├── copy adjacent version JSON
  └── publish the import and manifest
                |
                v
minecpp-data/versions/<id>/
  ├── client/
  ├── metadata/version.json
  └── manifest.mcpp
~~~

The original installation is an input only. --force replaces an imported version only after the staging extraction has completed. ZIP64 and multi-disk archives are not supported. CRC-32 detects accidental damage; it is not a cryptographic signature.

## Metadata and offline resource flow

~~~text
minecpp-launcher inspect <id>
  ├── strict JSON parsing
  ├── version ID / main class / Java major version
  ├── asset index metadata
  ├── direct library artifacts
  ├── logging configuration
  └── unresolved inheritance / library requirements report

minecpp-launcher prepare <id> --game-dir <existing-install>
  ├── read local assets/indexes/<id>.json
  ├── verify index size / SHA-1 when declared
  ├── resolve direct libraries already on disk
  ├── resolve assets/objects/<hash-prefix>/<hash>
  ├── create legacy virtual / resources mappings when requested
  ├── copy optional logging configuration
  ├── write a checksummed manifest
  └── publish a separate prepared/<id> directory
          |
          v
minecpp-launcher verify-prepared <id>
  └── verify every manifest-listed file and checksum
~~~

URLs in version JSON are parsed as metadata, never fetched. No network downloader exists in this pipeline. Preparation refuses to publish a new output when required files are absent or metadata asks for features the resolver does not yet understand, including inherited versions, OS library rules and native classifiers.

The preparation manifest records the version, copied metadata and index fingerprints, direct library artifacts, content-addressed asset objects, legacy mapping flags and optional logging configuration. Verification walks the manifest and checks file existence, sizes and SHA-1 values.

## NBT and Anvil

minecpp_formats reads big-endian NBT tags with configurable caps for input size, nesting, tag count and array lengths. The decompressor accepts gzip/zlib streams and enforces a maximum expanded size.

The region reader validates the 8 KiB location/timestamp header, sector allocation bounds and overlap, then reads a chunk's length-prefixed payload. Compression types gzip, zlib and uncompressed are supported. LZ4 and external .mcc chunks are rejected explicitly.

The Anvil chunk decoder accepts the 1.16+ padded block-state palette representation and the modern lowercase sections/block_states shape as well as the earlier Level/Sections/Palette/BlockStates field names. It preserves unknown namespaced block identifiers and their string properties. Only a small explicit set of common states is mapped to the sandbox's simplified Block enum. It does not yet migrate all vanilla blocks or connect these decoded chunks to the graphical world renderer.

## Native world and frame ownership

The main thread owns GLFW, OpenGL, player input and the live chunk map. Workers generate immutable chunk snapshots and meshes, then return results through a mutex-protected queue. Revision numbers discard stale mesh results.

~~~text
Main thread
  ├── poll input
  ├── run player simulation at a fixed 60 Hz
  ├── update chunk streaming
  ├── apply completed worker results
  ├── issue OpenGL draws
  └── save MineCPP world edits

Worker pool
  ├── generate deterministic terrain arrays
  └── build greedy meshes from immutable snapshots
~~~

The player physics solver is independent of GLFW. It uses feet position, velocity, grounded state, acceleration, friction, gravity, sprint and jump-edge input. Axis-separated AABB collisions use bounded movement substeps and binary refinement. Unloaded chunks are treated as solid by the renderer adapter to prevent moving into terrain that has not arrived yet.

Ray selection uses Amanatides-Woo voxel traversal. It returns the first solid cell and previous empty cell, so placement does not depend on coarse fixed-distance sampling.

Greedy meshing merges adjacent coplanar faces with the same block material into rectangles. Rendering still uses the OpenGL 2.1 compatibility pipeline and flat vertex colours; there is no Minecraft model/texture pipeline or modern shader renderer yet.

## Save format and safety

MineCPP's .mcw save stores a MineCPP seed and sparse block edits, not vanilla Anvil chunks. Reads validate the whole file before applying edits. Writes use a temporary file and backup/restore publication path so a failed replacement can preserve the previous save.

## Explicit 1.0.0 boundaries

- No Minecraft Java launcher or Java bytecode execution.
- No account login, download manager or network protocol.
- No complete Mojang library-rule / native-classifier / inheritance resolver.
- No LZ4 or external .mcc support; no pre-1.16 continuous block-state packing.
- No full vanilla block registry, blockstate/model JSON, textures, lighting, biomes, fluids, entities, inventory, sound or multiplayer.
- The graphical voxel sandbox creates its own procedural worlds; the Anvil reader is a data-format foundation, not a vanilla-world renderer.
