# MineCPP engine architecture

## Frame and thread ownership

The main thread owns the GLFW window, OpenGL context, player state, loaded-chunk map, and input. Worker threads never call OpenGL and never mutate the live chunk map.

```text
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
```

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

## Current limitations

- No greedy meshing yet; adjacent coplanar faces are not merged.
- Chunks are rebuilt when neighbouring chunks arrive or a block is edited.
- No water/transparent render queue, chunk lighting, caves, fluids, or biome data.
- Mesh data is kept in CPU memory and submitted through OpenGL client arrays; persistent GPU vertex buffers are not implemented yet.
- There is no network protocol or account/session implementation.
- This is not an implementation of Minecraft's proprietary formats or assets.
