# Bernini Engine Roadmap

A 3D engine targeting a **battle game**: many skinned, instanced units under a single
directional sun, forward-rendered, PBR now and an ink/toon path later, with a dedicated
authoring editor. The game ships cross-platform (Windows / Linux / Xbox); the editor is
Windows-only.

This roadmap is a living checklist. Legend:

- `[x]` done / in place
- `[ ]` not done

Ordering within a milestone is roughly dependency order. Milestones are prioritized to
unblock the game's core loop first (units on screen, animated, lit, culled) before polish
and portability.

---

## Guiding Constraints (design rules the roadmap must respect)

- **GPU-driven by default.** The instance pipeline already buckets by `PsoType` and emits
  indirect dispatch args. New systems (culling, shadows, skinning) should stay on the GPU
  and extend this pipeline rather than adding CPU-side per-object work.
- **One dominant light.** Forward rendering with a single sun keeps shading cheap. Do *not*
  invest in clustered/tiled many-light infrastructure; spend the budget on shadow quality
  and instance count instead.
- **Instances are the unit of scale.** Thousands of units means per-instance data must be
  compact and GPU-resident; per-unit CPU updates are the enemy.
- **RHI stays API-agnostic.** All D3D12 lives in `bgl_d3d12`. Every feature added to `bgl`
  must be expressible without leaking backend types, so the Vulkan/Metal backends stay viable.
- **IDL is the single source of truth** for structs shared by C++ and Slang (`libs/bgl/idl`). New
  GPU-visible data (materials, lights, bones, LOD info) goes through the IDL, not hand-mirrored.
- **Data-Oriented Design (DOD)** traditional Object-Oriented Programming (OOP) will decimate your CPU cache at scale update unit gameplay states (health, status effects) in tight memory arrays.


## Module 1: Graphics Pipeline

- [ ] RHI
  - [x] DirectX 12
  - [ ] Vulkan
  - [ ] Metal
  - [x] GPU Ring Buffer
- [x] Static Geometry
  - [x] FrameGraph: pass ordering, auto barrier derivation, resource namespaces, multi-queue,
    dead-pass culling (`libs/bgl/src/fg`)
  - [x] Slang shader pipeline + IDL codegen for shared C++/Slang structs (`libs/bgl/idl`)
  - [x] GPU instance render
  - [x] Verification: golden-image comparison + structured error logging
  - [x] Submesh schema
  - [x] Static Mesh Asset Import
- [ ] Materials (PBR)
  - [x] Implement PBR IBL
  - [x] ktx2 textures
  - [x] Implement Alpha Test alpha mode
  - [x] Texture Asset Import
  - [ ] Editor Material Graph
    - [ ] Choose Material Type: PBR only for now
    - [x] Choose Material Options: e.g. Alpha Mode
    - [x] Link Textures to output nodes (BRDF for PBR)
- [ ] Culling
  - [ ] Frustum Culling
  - [ ] Geometry Cluster Culling
  - [ ] Two-pass HZB occlusion culling
  - [ ] Culling verification
- [ ] Ink Shader Proof of Concept
- [ ] Light and Shadow
  - [ ] Directional Lighting
  - [ ] Point Light
  - [ ] Ambient / Sky Light
  - [ ] Cascading Shadow Mapping
  - [ ] Static vs. Dynamic Shadow
- [ ] Animation
  - [ ] Animation Asset Import (clips, skeleton, etc)
  - [ ] Skinned Meshes & Animation
    - [ ] GPU skinning (compute): write deformed verts to a transient buffer for the mesh path
    - [ ] Bone palette buffer, GPU-resident, per-instance indexed
    - [ ] Animation runtime: pose sampling, blend/cross-fade, state machine / blend tree
    - [ ] Double-buffer the bone palette — required for skinned motion vectors
  - [ ] Vertex Animation Textures (VAT)
- [ ] LODs
  - [ ] Editor LOD Generator for Static and Skinned
  - [ ] LOD Selection for Skinned and Static meshes
  - [ ] Animation Ticking & Taging LODs
  - [ ] Compute Skinning Bandwidth
  - [ ] Shadow LODs
- [ ] Motion Vectors
  - [ ] TAA
  - [ ] Test Motion Vectors for all geometry types: LODs, Skinned meshes during animation
- [ ] Scene Representation
- [ ] Alpha Blending
  - [ ] WBOIT (weighted, blended order-independent transparency — no per-draw sorting)
  - [ ] Depth test against opaque, no depth write
  - [ ] Exclude from MV / TAA history (transparents ghost otherwise)
  - [ ] Verify: overlapping colored quads composite identically regardless of draw order
- [ ] Foliage
  - [ ] Grass
  - [ ] Trees
- [ ] Water
- [ ] Screen-space / Volume Decal Pipeline
- [ ] FX
  - [ ] GPU Compute Particle System
  - [ ] HZB-based Particle Collision
  - [ ] Outline Shader
  - [ ] Render Axis
- [ ] Post Processing
  - [ ] LUT
  - [ ] Color Grading
  - [ ] Ambient Occlusion. (Maybe not)
- [ ] Weather
  - [ ] Sky box (for interiors and editor)
  - [ ] Procedural Atmosphere Shader
  - [ ] Rain & Snow
  - [ ] Wetness (Material modificatio)
- [ ] Misc
  - [ ] Texture Atlasing
  - [ ] Editor Build Texture Atlas
  - [x] GPU-Side Assertions
  - [ ] DRED & Breadcrumbs are post-mortem forensics

**Optional Features**

- [ ] Diffuse GI
- [ ] Flowmap baking
- [ ] Forward++ Shading
- [ ] Ink Shading
- [ ] Screen space reflections
- [ ] Gpu Virtual Memory

## Module 2: Game Logic

- [ ] Level Editor for Battles
  - [ ] Terrain Gen. using Noise + inputs: hilly, flat, mountainous etc
  - [ ] Navmesh Gen.
  - [ ] Weather Editor
  - [ ] Drag and Drop Buildings & Meshes
- [ ] In-game UI
  - [ ] Adopt UI runtime e.g. Noesis
  - [ ] Controller/focus
- [ ] Input Engine
  - [ ] Character Controls and Movement
  - [ ] Horse Controls and Movement
- [ ] Combat
- [ ] Integrate Scripting Into Engine e.g. Lua
- [ ] Campaign Map Editor
  - [ ] Economy
  - [ ] Quests
  - [ ] Timeline. Order events
  - [ ] Weather and Day night cycle
- [ ] Cutscene Editor
- [ ] In-game AI
  - [ ] Battle AI
  - [ ] Campaign AI
- [ ] Physics
  - [ ] Hair Physics
  - [ ] Hit Stop
  - [ ] Ballistics e.g. Arrows, Boulders
- [ ] Crowd Simulation & Pathfinding
- [ ] Asset Streaming Pipeline
- [ ] CPU Spatial Partitioning
- [ ] Game Serialization

