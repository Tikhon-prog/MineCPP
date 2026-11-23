# MineCPP architecture

MineCPP currently has two independent executables:

- minecpp-launcher: an offline C++ command-line importer and integrity checker.
- minecpp: an experimental voxel sandbox used to develop native rendering and world-simulation foundations. It is not yet compatible with Minecraft Java Edition.

## Offline launcher pipeline

~~~text
Existing Minecraft installation
  └─ versions/<id>/
       ├─ <id>.jar
       └─ <id>.json (optional at import time)
              |
              v
minecpp-launcher import
  ├─ inspect ZIP central directory
  ├─ validate paths, flags, sizes and compression methods
  ├─ extract files to a staging directory
  ├─ decompress DEFLATE entries with zlib
  ├─ verify CRC-32 for every extracted file
  ├─ copy adjacent version JSON when present
  └─ write manifest.mcpp and atomically publish the import
              |
              v
minecpp-data/versions/<id>/
  ├─ client/
  ├─ metadata/version.json
  └─ manifest.mcpp
              |
              +── list
              +── verify
~~~

The import command performs no network calls. The original JAR and the original Minecraft installation are read-only inputs. Extracted files are first written to a staging directory; only after extraction and manifest creation succeed is that directory moved into the selected version location.

The ZIP reader uses the central directory for authoritative compressed sizes and offsets. It currently supports standard ZIP Store and DEFLATE entries, rejects ZIP64 and multi-disk archives, rejects encrypted entries and symlinks, checks CRC-32, and enforces archive/entry/expanded-size limits. Archive paths are normalized and validated before filesystem writes. Absolute paths, drive-qualified names, empty path components, dot components, and parent traversal are rejected. Duplicate paths are rejected, including case-only collisions for portability to Windows.

Each manifest records the version ID, source filename, extracted file count, total uncompressed bytes, and the size and CRC-32 of every extracted file. The verify command recomputes these values from disk. CRC-32 detects accidental corruption; it is not a cryptographic authenticity signature.

When a matching <id>.json file is next to the source JAR, it is copied to metadata/version.json and included in the manifest. The first release does not yet parse the JSON or resolve libraries and assets. Those are explicit next milestones, not assumed to be included in a JAR.

## Native voxel prototype: frame and thread ownership

The main thread owns the GLFW window, OpenGL context, player state, loaded-chunk map, and input. Worker threads never call OpenGL and never mutate the live chunk map.

~~~text
Main thread
  ├─ read input and move player
  ├─ request nearby chunk coordinates
  ├─ drain completed worker results
  ├─ take immutable chunk snapshots for meshing
  ├─ issue OpenGL draw calls
  └─ save player edits

Worker pool (2..8 threads)
  ├─ generate seeded block arrays for requested chunks
  └─ build triangle meshes from immutable snapshots
       └─ post results back to the main thread
~~~

The snapshot/revision approach avoids worker threads reading mutable chunk arrays. If a neighbouring chunk loads or a block changes while a mesh is being built, the chunk revision changes; stale mesh results are discarded.

## World coordinates

- Horizontal chunk dimensions: 16×16 blocks.
- Vertical world dimension: 96 blocks.
- Chunks are keyed by signed integer X/Z coordinates.
- Floor division and positive modulo are used for negative world coordinates.
- Terrain is a deterministic function of the world seed and absolute X/Z coordinates.
- The save file stores only player block edits plus the world seed. Generated terrain is recreated on demand.

## Mesh generation

Each solid block contributes only faces whose adjacent block is air. Each visible quad is expanded into two triangles. Per-face vertex colours provide basic directional shading. The current renderer uses the OpenGL 2.1 compatibility pipeline with client-side vertex arrays and one batched draw per chunk. This reduces per-vertex API calls without requiring an extra OpenGL function loader; GPU VBOs and shaders remain a later performance milestone.

## Current boundaries

- The importer is not a complete Minecraft installation manager: libraries and the assets index/object store are not resolved yet.
- The importer does not launch Java, authenticate accounts, download files, or communicate with servers.
- The native voxel prototype is not a reimplementation of Minecraft and does not claim world or server compatibility.
- No Minecraft proprietary code, textures, sounds, fonts, or other assets are included.
- The native prototype has no greedy meshing, water/transparent render queue, chunk lighting, caves, fluids, or biome data.
- Mesh data is kept in CPU memory and submitted through OpenGL client arrays; persistent GPU vertex buffers are not implemented yet.
