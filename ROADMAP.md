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
  - [ ] WebGPU
  - [ ] Vulkan
  - [ ] Metal
  - [x] GPU Ring Buffer
  - [ ] Readback ring — N buffers, persistently mapped, fenced; never map a buffer written this frame.
  - [ ] `ExecuteIndirect` / `DispatchIndirect` plumbing so counts never leave the GPU.
- [x] Static Geometry
  - [x] FrameGraph: pass ordering, auto barrier derivation, resource namespaces, multi-queue,
    dead-pass culling (`libs/bgl/src/fg`)
  - [x] Slang shader pipeline + IDL codegen for shared C++/Slang structs (`libs/bgl/idl`)
  - [x] GPU instance render
  - [x] Verification: golden-image comparison + structured error logging
  - [x] Submesh schema
  - [x] Static Mesh Asset Import
- [x] Materials (PBR)
  - [x] Implement PBR IBL
  - [x] ktx2 textures
  - [x] Implement Alpha Test alpha mode
  - [x] Implement Alpha Blend alpha mode
  - [x] Transparent depth pre-pass for self occlusion (per-material `occlude`)
  - [x] GPU sort for transparent depth ordering — replaces the per-frame CPU sort. Bitonic, one
    workgroup, capped at 1024 transparent instances; a multi-group radix sort is the scale-up.
  - [x] Texture Asset Import
  - [x] Editor Material Graph
    - [x] Choose Material Type: PBR only for now
    - [x] Choose Material Options: e.g. Alpha Mode
    - [x] Link Textures to output nodes (BRDF for PBR)
- [ ] Culling
  - [x] Frustum culling — 6 plane/sphere dots, runs first as the cheapest test.
  - [ ] HZB build (FidelityFX SPD) — single dispatch; reduce with **min** under reversed-Z, and
    handle non-power-of-two mips explicitly or the odd row/column drops the far sample.
  - [ ] HZB occlusion test — screen AABB, mip where it spans ≤2 texels, `GatherRed` 2×2, take farthest.
  - [ ] Single-phase HZB for the crowd — units are occludees, never occluders, so build from this
    frame's static depth prepass
  - [ ] Terrain must render to the depth prepass at unbounded range, or distant mountains occlude
    nothing.
  - [ ] Density culling — deterministic hash-selected fraction past a distance; cavalry culls later.
  - [ ] Per-view culling — camera and each shadow cascade get their own pass and indirect args.
  - [ ] Culling verification — CPU reference cull, diff the visible sets, assert.
- [ ] Motion Vectors
  - [x] Static geometry — an `RG16_FLOAT` velocity buffer written as MRT slot 1 by the forward and
    skybox passes. Instance transforms are immutable, so this is camera motion only; the mesh shader
    hands the pixel stage both clip positions, which is the seam the skinned and VAT paths extend.
  - [ ] Skinned motion vectors (needs double-buffered bone palette) — hero and near tiers only.
  - [ ] VAT motion vectors — sample previous frame's VAT UV; not optional since VAT is the majority path.
  - [ ] Corpses use the static MV path — the palette is unique but constant, so camera motion only.
  - [ ] TAA
    - [ ] Hashed (dithered) alpha — stochastic alpha test resolved by TAA
- [ ] Animation
  - [ ] Animation Asset Import (clips, skeleton, etc)
    - [ ] Resample all clips to a fixed rate (30/60 Hz) — no runtime keyframe search.
    - [ ] Topological bone sort (`parent(i) < i`), validated at import.
    - [ ] Per-clip metadata: authored locomotion speed, root motion delta, duration, loop flag.
    - [ ] Rotation compression (quat+translation, 16 B/bone) — matters most for permanent corpse palettes.
    - [ ] Per-LOD bone sets as index-compatible subsets, with weight-collapse validation.
    - [ ] State machine authoring → flat table export, rejecting graph features the GPU path lacks.
    - [ ] Separate humanoid and equine skeletons and clip sets, both exporting to the same table format.
  - [ ] Vertex Animation Textures (VAT)
    - [ ] Bake pipeline: resampled clip → position texture (+ normal/tangent), unorm-packed in the
      mesh bounding box.
    - [ ] Use one global bounding box across all clips of a rig, or blended samples are meaningless.
    - [ ] **Per-frame skeletal side-channel** — baked bone palette alongside each VAT frame; required
      for the death handoff, the cavalry saddle transform, and attachments.
    - [ ] Motion vectors (see above).
    - [ ] Free inter-frame interpolation — vertex index along U at exact texel centre, frame along V
      fractional, linear sampler; pad each clip with a duplicate end frame to stop bleed.
    - [ ] **Bake transitions instead of blending them** — explicit idle→run, run→attack clips as
      ordinary states with exit-time transitions; better motion than a crossfade and memory is cheap.
    - [ ] Per-vertex masked layering for upper/lower split — a baked vertex mask, near-free, and the
      one blend VAT does well; align roots at bake time or the upper body floats.
    - [ ] Phase-matched crossfade for unbaked transitions — offline pose-distance table picks matching
      entry frames, since position lerp only holds below ~30–40° of joint difference.
    - [ ] Constraints: no additive layers, no look-at, no IK, no per-unit bone-level variation; hit
      reactions must be full-body baked clips.
    - [ ] Tier boundary policy — anything needing those features must sit above the VAT boundary.
  - [ ] Skinned Meshes & Animation — hero tier and the near-distance tier of rank and file
    - [ ] Bone palette buffer, GPU-resident, per-instance indexed.
    - [ ] Pose sampling — fixed clip count at compile time, unused slots weighted to zero.
    - [ ] Cross fade blending — slerp local rotations then walk the hierarchy, never blend model space.
    - [ ] Local→model hierarchy walk — workgroup per unit, thread per bone, barrier per depth level,
      group size 64.
    - [ ] GPU skinning (compute) to a transient vertex buffer — hero tier only; everything else skins
      in the vertex shader or fetches VAT.
    - [ ] Double-buffer the bone palette — required for skinned motion vectors.
    - [ ] Animation preview + playback at different LODs, including the skinned→VAT swap.
    - [ ] Bone mask — small per-bone weight array, needed by additive flinch on the skinned tier.
  - [ ] State Machine — flat tables, tiny per-unit interpreter, ticked for all units regardless of
    tier and regardless of visibility.
    - [ ] VAT units resolve state to a clip index and phase instead of a set of skeletal clips.
    - [ ] Mounted handling — rider and mount share clip index and phase.
  - [ ] Skinned-tier only
    - [ ] Look-at (head + torso, angle clamp) — best liveliness cue per instruction, unavailable on VAT.
    - [ ] Heightfield foot planting — analytic two-bone IK; breaks on stairs and siege structures.
  - [ ] Hit reaction
    - [ ] Directional reaction clips (4–8 variants) — works on both tiers, so build this first.
    - [ ] Additive flinch over locomotion (skinned tier) — one fixed slot, upper-body mask, ~0.3 s envelope.
    - [ ] Spring reaction pool (skinned near tier) — ~1k pooled slots, not per-unit; shares its solver
      with procedural death settling.
    - [ ] Knockback — impulse into velocity, resolved by existing avoidance; tier-independent.
  - [ ] Per-clip metadata and events
    - [ ] Notify detection on the GPU — wave-ballot + one `atomicAdd` per wave into the event buffer.
    - [ ] **Notifies are never authoritative** — combat decides when damage lands, animation depicts it.
  - [ ] Root motion — cosmetic nudge only, never authoritative for position.
  - [ ] Stays on the CPU (hero tier): ragdoll against arbitrary collision, IK against non-heightfield
    geometry, layers beyond the one additive slot, montages, facial/lip sync, variable-depth graphs.
- [ ] Crowd Variation — one mesh and one clip set means sameness is the primary visual risk; all of it
  must be deterministic from unit ID so nothing changes at a LOD boundary or on death.
  - [ ] Per-unit animation phase offset from an ID hash — non-negotiable, or a formation reads as one
    organism; offset clip time and preserve it across state transitions.
  - [ ] Per-unit `playRate` jitter (±3–5%) so units that synchronise don't stay synchronised.
  - [ ] Per-unit uniform scale (±3–4%) and small formation yaw jitter.
  - [ ] Per-instance submesh mask (helmet, cape, quiver, shield) — bucket by mask alongside LOD.
  - [ ] Attachment variation as separate instanced draws off the skeletal side-channel bone transform.
  - [ ] Per-instance material variation — kit index into a texture array plus hue/value jitter, reusing
    the blood parameter struct.
  - [ ] Grime/wear float, ID-hashed, reusing the blood dissolve-mask machinery.
  - [ ] Texture atlasing — a single mesh means a single material, so kit variation has nowhere else to
    come from; now on the critical path.
  - [ ] Not recommended: X-mirroring, since reversed handedness is visible on armed units.
- [ ] Crowd Simulation & Pathfinding
  - [ ] **Shared-source kernel harness** — one kernel body per pass, compiled as both a Slang entry
    point and a C++ loop; the IDL codegen is already half of this.
    - [ ] Type shims (`float3`/`clamp`/`lerp`/`saturate`) + macro layer for genuine divergences.
    - [ ] Wave intrinsics, LDS, and atomics have no shared form — write those references semantically
      and sort both sides before comparison.
    - [ ] Differential test harness — exact hash for integer and fixed-point state, epsilon comparison
      for float state, bisecting to the first diverging pass.
    - [ ] CPU path runs single-threaded for debugging and `parallel_for` for hero-tier production.
  - [ ] Group orchestration interface — the one contract between CPU AI and GPU simulation.
    - [ ] CPU → GPU per group: flow field index, formation shape/origin/facing, stance, engagement
      rules, target group.
    - [ ] GPU → CPU per group: **aggregate reduction only** — headcount, casualties, mean position and
      facing, cohesion, melee contact fraction, morale, fatigue.
    - [ ] Group ID as a first-class per-unit field, reassigned by CPU-issued rewrite.
  - [ ] Navigation
    - [ ] Navmesh or nav-grid bake (offline).
    - [ ] Flow field generation (GPU Eikonal/wavefront, one field per group destination).
    - [ ] Flow field cache + eviction — LRU by group; resolution and count bound memory and dispatches.
    - [ ] Static obstacle rasterization into the field.
    - [ ] CPU path queries for heroes and distinct goals (Detour or equivalent).
    - [ ] Terrain sampling: height, normal, grounded test, per-type slope cost.
  - [ ] Spatial grid — cell hash → count → prefix sum → scatter; shared by avoidance, combat, queries,
    and corpse placement, so it is its own node.
    - [ ] Per-cell agent cap with clamped writes and high-water reporting.
  - [ ] Simulation passes (shared source, compiled both ways)
    - [ ] Velocity Planning
      - [ ] Per-type kinematic limits — max speed, acceleration, turn rate.
      - [ ] Non-holonomic constraint for mounts — no strafing, minimum turn radius, speed-dependent
        turn rate.
    - [ ] Dynamic Constraints
      - [ ] Agent Overlap
        - [ ] Melee Overlap
        - [ ] Ranged Overlap
        - [ ] Asymmetric mass — cavalry displaces infantry, as a mass term rather than a special case.
      - [ ] Static Obstacles
      - [ ] Long Range Interaction — anticipated collision, weighted much higher for mounts.
      - [ ] Group Locomotion
  - [ ] Cavalry / mounted units
    - [ ] One agent per mount; the rider is an attachment with no nav, avoidance, or grid entry.
    - [ ] Rider and mount share clip index and phase.
    - [ ] VAT bake strategy — separate VATs sharing phase plus the saddle transform keeps kits
      orthogonal; a combined horse+rider bake is simpler but combinatorial.
    - [ ] Charge impact — a proximity event gated on relative velocity, with an impulse term.
    - [ ] Agent **type mutation** on mount or rider death (riderless horse, unhorsed rider) — design
      the mutable type index in now, it is painful to retrofit.
    - [ ] Larger radius, larger corpse, later density-cull distance, separate reaction clip set.
  - [ ] Combat Interaction
    - [ ] Proximity / target selection — one-sided, nearest enemy, tie-broken by lowest unit ID.
    - [ ] Damage accumulation — fixed-point `atomicAdd` into a separate buffer, which keeps
      determinism-diffing and the differential harness usable.
    - [ ] Apply + death detection as a separate pass with one owning thread per unit, so exactly one
      death event is emitted.
    - [ ] Double-buffered agent state — read A, write B, swap.
    - [ ] Event append buffer with wave-aggregated atomics; carries VFX and audio triggers only.
  - [ ] Hero units — simulated on the CPU, uploaded as read-only obstacles and targets.
- [ ] Death & Corpses
  - [ ] Three-stage retirement `alive → dying → corpse`; a dying unit leaves grid, nav, avoidance, and
    combat immediately but still runs a solver.
  - [ ] **VAT→skeletal handoff** — read the pose from the side-channel and switch to the skinned path,
    since bone transforms cannot be recovered from baked vertices.
  - [ ] Procedural settle solver — shared angular spring-damper or fixed-iteration PBD, colliding
    against the terrain heightfield only.
    - [ ] Hard timeout (~2 s) with forced settle, or the dying tier becomes unbounded.
    - [ ] Canned death clip fallback inside structures, where heightfield-only collision fails.
  - [ ] Dying-tier budget — a few thousand concurrent; overflow falls back to canned clips.
  - [ ] Unique-but-constant corpse palette at the corpse bone LOD — 288 B each, ~26 MB at 90k corpses,
    re-quantized once at settle.
  - [ ] Corpses stay on the skinned draw path — one instanced draw per (variant, LOD) with per-instance
    palette indexing.
  - [ ] **Corpse budget with oldest-first fadeout — mandatory**, since unique palettes grow
    monotonically with no sharing to fall back on.
  - [ ] Ground conformance — fit a plane from 3–4 height samples along the body's long axis.
  - [ ] Sink 2–4 cm over 1–2 s, instance transform only.
  - [ ] One-shot depenetration on retirement against a coarse corpse grid, sinking deeper with local
    density; larger radius for horses.
  - [ ] Corpse density field — coarse world-space texture read by velocity planning as slowdown and
    weak repulsion; do not put corpses back in the agent grid.
- [ ] LODs
  - [ ] Editor LOD generator for static and skinned.
  - [ ] LOD selection on **projected screen size**, not distance, with thresholds authored in pixels.
  - [ ] True Euclidean distance to camera, not view-space Z, or panning makes edge units pop.
  - [ ] **Role and distance are separate axes** — drive the skinned→VAT switch from screen size with a
    top-K budget so near units are skinned regardless of rank.
  - [ ] Hysteresis (~10–20% gap) against per-unit stored LOD, especially at the skinned→VAT boundary.
  - [ ] Dithered LOD crossfade resolved by TAA; also the mechanism for the skinned→VAT swap.
  - [ ] Per-tier compaction → indirect args; fixed `maxPerLOD` regions hold until submesh-mask
    variation multiplies the bucket count.
  - [ ] Separate mesh LOD and animation LOD tables driven from the same screen-size value.
  - [ ] Animation ticking and tagging LODs.
  - [ ] Compute skinning bandwidth — measure palette writes, palette reads, VAT fetches, and the
    permanent corpse palette read before optimising ALU.
  - [ ] Test motion vectors across LODs, skinned animation, VAT, corpses, mounts, and both transitions.
- [ ] Light and Shadow
  - [ ] Async Compute
  - [ ] Directional Lighting
  - [ ] Point Light
  - [ ] Ambient / Sky Light
  - [ ] Cascaded Shadow Maps
  - [ ] Static vs. Dynamic Shadow
  - [ ] Shadow LODs — bias 1–2 tiers coarser, but a unit skinned for the camera must not be VAT for a
    cascade.
- [ ] Terrain — **missing entirely and load-bearing**: the heightfield feeds the grounded test, foot
  planting, corpse settling, slope cost, and the ground blood field.
  - [ ] Heightfield representation + GPU-sampleable height/normal.
  - [ ] Terrain rendering + LOD, with unbounded range in the depth prepass.
  - [ ] Terrain material layers.
- [ ] Scene Representation
- [ ] Foliage
  - [ ] Grass
  - [ ] Trees
- [ ] Water
- [ ] Screen-space / Volume Decal Pipeline
- [ ] FX
  - [ ] WBOIT — order-independent transparency; supersedes the alpha-blend CPU sort (see Materials)
  - [ ] GPU Compute Particle System
  - [ ] HZB-based Particle Collision
  - [ ] Outline Shader
  - [ ] Render Axis
- [ ] Decals
  - [ ] Material parameters for regular units — a packed `uint32` (amount 8b / dryness 8b /
    direction 16b) that works identically on VAT, skinned, and corpse tiers with no atlas or projection.
    - [ ] Dissolve threshold against a cavity/AO mask, not a uniform tint.
    - [ ] Bias by world normal (upward faces accumulate) and by hit direction.
    - [ ] Drive roughness and normal, not just albedo; wet→dry via the dryness byte.
    - [ ] Write from the combat pass and splatter onto neighbours from the proximity list already in hand.
    - [ ] Per-unit rate variation by ID hash, or the field goes flat.
    - [ ] Far-tier fallback: drop the mask sample and lerp albedo toward dark red.
  - [ ] Ground blood — second channel of the corpse density field, sampled by the terrain shader.
  - [ ] Hero units keep a real per-unit damage-mask render target for recognisable shapes.
- [ ] Post Processing
  - [ ] LUT
  - [ ] Color Grading
  - [ ] Ambient Occlusion — cost is independent of unit count, and it is the main grounding cue
    available on the VAT tier.
- [ ] Weather
  - [x] Sky box (for interiors and editor)
  - [ ] Procedural Atmosphere Shader
  - [ ] Rain & Snow
  - [ ] Wetness (material modification) — one system with two inputs shared with blood; rain should
    also wash blood off.
  - [ ] Vertex-shader wind on cloth/hair — stateless, works on VAT and corpses.
- [ ] Debug & Tooling — **build before the GPU implementations, not after.**
  - [x] GPU-Side Assertions
  - [ ] Focus-unit trace — `g_debugUnit` uniform, tagged ring buffer, pretty-printer; gives one agent's
    linear narrative across every pass.
  - [ ] Buffer poisoning (`0xDEADBEEF` / sNaN) before each dispatch in debug builds.
  - [ ] NaN/Inf scan pass — more important now that procedural settling is a solver.
  - [ ] Append buffer high-water marks — clamp writes, record attempted counts, assert above 80%.
  - [ ] Per-pass buffer hashing as a FrameGraph feature, to bisect to the first wrong pass in one run.
  - [ ] Determinism diffing as a race detector — run twice, compare, and perturb the schedule between
    runs to surface races that hide at one configuration.
  - [ ] Standalone replay harness — serialize one frame's inputs, run headless, dump output; also what
    makes GPU unit tests possible in CI.
  - [ ] `printf` — `debugPrintfEXT` on Vulkan, Slang `printf` elsewhere; wave-guard it.
  - [ ] GPU-based validation (D3D12 GBV / Vulkan GPU-AV) in nightly CI.
  - [ ] Optional: a CUDA port of one or two kernels purely for `compute-sanitizer --tool racecheck`.
  - [ ] DRED & Aftermath / Radeon GPU Detective, paired with monotonic breadcrumb markers.
- [ ] Profiling
  - [ ] GPU timestamp per pass with on-screen breakdown — FrameGraph feature, same as hashing.
  - [ ] Live counters: agents alive/dying/corpse split by type, visible per tier, skinned vs VAT against
    the top-K budget, events vs capacity, slots in use, cells at cap, corpse palette memory.
- [ ] Capacity policy — one table, with clamp-and-report behaviour defined for every entry.
  - [ ] Max agents, max per cell, event buffer size, flow fields resident.
  - [ ] Top-K skinned budget.
  - [ ] Concurrent dying units and solver slots → overflow falls back to canned death clips.
  - [ ] Corpse cap and palette memory ceiling → overflow triggers oldest-first fadeout.
  - [ ] VAT texture memory — under 50 MB at one humanoid and one equine rig, so budget generously.
- [ ] Misc
  - [ ] Texture Atlasing
  - [ ] Editor Build Texture Atlas


**Optional Features**

- [ ] Diffuse GI
- [ ] Flowmap baking
- [ ] Forward++ Shading
- [ ] Screen space reflections
- [ ] Gpu Virtual Memory
- [ ] Texture-space decals - Render decals into the mesh's UV/texture space, not screen space for heroes
- [ ] Analytic heightfield occlusion — march the height texture from camera to unit, useful only if
  occlusion is needed before the depth prepass or on a separate timeline.
- [ ] Multi-group radix sort — scale-up for transparent depth ordering, and needed if the spatial grid
  moves to a sparse hash table. Onesweep over the classic three-kernel build.
- [ ] Corpse pose clustering — cluster settled palettes to K representatives at distance, the escape
  hatch if corpse memory binds.
- [ ] Baked-palette middle tier — skin from the side-channel palette instead of fetching VAT, for
  correct blending without pose evaluation; the data already exists.
- [ ] Saliency-driven LOD - allocate fidelity by attention, not absolute distance. A smoothed autofocus
  anchor (nearest large object, from an HZB center min-depth reduction) rides the skinned→VAT switch
  distance so an empty foreground upgrades the nearest far subject. Salience-ranked budget (screen size +
  proximity-to-anchor + velocity + center weight) picks the top-K for hero-tier skinning; heavy temporal
  smoothing + dithered LOD crossfades avoid global popping. Shares its focal distance with DOF if added.

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
- [ ] Asset Streaming Pipeline
- [ ] CPU Spatial Partitioning
- [ ] Game Serialization
