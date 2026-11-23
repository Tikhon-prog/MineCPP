# Roadmap

## 0.1.0 — Playable native prototype

- Native window and 3D first-person camera.
- Deterministic block terrain and chunk streaming.
- Background terrain generation and chunk meshing.
- Collision, jumping, sprinting, mining, and block placement.
- Save/load edited blocks.
- Linux, Windows, and macOS CI builds with world tests.

## 0.2.x — Engine foundations

- Replace compatibility-mode drawing with OpenGL 3.3 shaders and GPU vertex buffers.
- Greedy meshing and frustum/distance culling.
- Better worker scheduling, chunk lifetime management, and profiling.
- Robust player collision, step-up logic, and configurable controls.
- Block definitions and procedural texture-atlas support without bundled game assets.
- Full world-save corruption checks and versioned migrations.

## 0.3.x — World simulation

- Water and other transparent blocks.
- Lighting propagation and ambient occlusion.
- Caves, ore distribution, biome parameters, and more tree types.
- Inventory and hotbar UI, pause menu, settings, and world selector.
- Basic crafting and item drops.

## Later — Native game client

A Minecraft-like game client is a much larger project than a launcher. Before claiming compatibility, the engine needs independently implemented data pipelines, simulation, entities, rendering, sounds, world persistence, and (if desired) a multiplayer protocol. Support for proprietary game formats, server authentication, and third-party assets must be treated as explicit, separately tested features.
