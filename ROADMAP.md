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
- **RHI stays API-agnostic — among APIs with bindless resource access and mesh shaders.** All D3D12
  lives in `bgl_d3d12`, all Metal in `bgl_metal`. Every feature added to `bgl_extended` must be expressible
  without leaking backend types, so Vulkan stays viable. An API that clears that bar is a backend;
  one that does not is a second renderer above `bgl`'s public interface, not under the RHI. `bgl`
  names that interface and nothing else; a renderer under it is named for what it is built on, and
  `bgl_extended` is the tier that assumes the bar above.
- **IDL is the single source of truth** for structs shared by C++ and Slang (`libs/bgl_extended/idl`). New
  GPU-visible data (materials, lights, bones, LOD info) goes through the IDL, not hand-mirrored.
- **Data-Oriented Design (DOD)** traditional Object-Oriented Programming (OOP) will decimate your CPU cache at scale update unit gameplay states (health, status effects) in tight memory arrays.


## Module 1: Graphics Pipeline

- [ ] RHI
  - [x] DirectX 12
  - [ ] Vulkan
  - [x] Metal
  - [x] GPU Ring Buffer
  - [ ] Readback ring — N buffers, persistently mapped, fenced; never map a buffer written this frame.
  - [ ] `ExecuteIndirect` / `DispatchIndirect` plumbing so counts never leave the GPU.
- [x] Static Geometry
  - [x] FrameGraph: pass ordering, auto barrier derivation, resource namespaces, multi-queue,
    dead-pass culling (`libs/bgl_extended/src/fg`)
  - [x] Slang shader pipeline + IDL codegen for shared C++/Slang structs (`libs/bgl_extended/idl`)
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
  - [x] Per-instance material override — one mesh, a different material per instance, resolved into
    the cached `SubmeshInstance` so the draw pays nothing and an instance may change PSO bucket. For
    tens of hand-placed instances; crowd kit variation is the atlasing line under Crowd Variation,
    not this. No editor surface yet (see Level Editor for Battles).
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
    hands the pixel stage both clip positions, which is the seam the skinned path extends.
  - [ ] Skinned motion vectors (needs double-buffered bone palette) — hero and near tiers only.
  - [x] Animated motion vectors — the pose re-evaluated at `prevTime` through the previous
    view-projection, substituted at the mesh-shader seam; real velocity from the first playback PR.
  - [ ] Corpses use the static MV path — the palette is unique but constant, so camera motion only.
  - [ ] TAA
    - [ ] Hashed (dithered) alpha — stochastic alpha test resolved by TAA
- [ ] Animation
  - [ ] Animation Asset Import (clips, skeleton, etc)
    - [x] `.bskel` / `.banim` containers, and skin binding (`JOINTS_0` / `WEIGHTS_0`) on the `.bmesh`.
    - [x] Resample all clips to a fixed rate (30/60 Hz) — no runtime keyframe search.
    - [x] Topological bone sort (`parent(i) < i`), validated at import.
    - [x] Per-clip metadata: authored locomotion speed, root motion delta, duration, loop flag.
    - [x] Ground each clip at cook — 28 of the 29 source rigs are authored against their own ground
      plane rather than ours, so `groundClips` moves each clip's root track to rest its mesh on
      `y = 0` and the `.bimport`'s `clipFloor` overrules it where the lowest frame is not the
      standing one. See [docs/skinning.md](docs/skinning.md).
    - [x] Skeleton signature, so a clip set cooked against a since-reordered rig is caught.
    - [ ] Rotation compression (quat+translation, 16 B/bone) — matters most for permanent corpse palettes.
    - [ ] Per-LOD bone sets as index-compatible subsets, with weight-collapse validation.
    - [ ] State machine authoring → flat table export, rejecting graph features the GPU path lacks.
    - [ ] Separate humanoid and equine skeletons and clip sets, both exporting to the same table
      format — the rigs are separable already (one file is one rig, and a glTF with two skins is
      rejected rather than half-imported), but the shared table export waits on the line above.
    - [x] Editor import writes the rig beside the mesh — the skeleton always, the clips behind the
      *Import animations* box, both rolled back with a failed import.
  - [ ] Crowd tier — instances that own no palette, drawing a pose their rig computed once
    - [x] Bone anim table — a rig's every frame posed once on the GPU into a buffer the whole rig
      shares, filled the first time an instance asks for it. Replaced Vertex Animation Textures,
      which is retired: the table holds bone matrices rather than baked vertices, so a modular unit
      draws as several slot meshes on one shared pose, and memory scales `bones x frames` per rig
      rather than `verts x frames` per mesh. See [docs/skinning.md](docs/skinning.md).
    - [ ] **In-place bake policy** — a clip authored with travel in the joints carries that travel
      (the coyote's box spans ~130 units), but ground contact and locomotion are the game's: decide
      whether the cook subtracts root translation and hands it to gameplay as metadata
      (`AnimationClip::rootMotion` / `locomotionSpeed` already exist), or authoring simply requires
      in-place, ground-relative clips. The *vertical* half is settled — clips are grounded at cook,
      above — and what remains is the horizontal travel.
    - [ ] **Bake transitions instead of blending them** — explicit idle→run, run→attack clips as
      ordinary states with exit-time transitions; better motion than a crossfade and memory is cheap.
    - [ ] Phase-matched crossfade for unbaked transitions — offline pose-distance table picks matching
      entry frames, since position lerp only holds below ~30–40° of joint difference.
    - [ ] Constraints: no additive layers, no look-at, no IK, no per-unit bone-level variation; hit
      reactions must be full-body baked clips. The table is the rig's and no instance may write it,
      which is what forecloses all of them.
    - [ ] Tier boundary policy — anything needing those features must sit on the per-instance source.
    - [ ] Editor viewport playback — crowd instances placed and playing in the level viewport, which
      needs a clock.
  - [ ] Skinned Meshes & Animation — hero tier and the near-distance tier of rank and file
    - [x] Bone palette buffer, GPU-resident, per-instance indexed.
    - [ ] Pose sampling — fixed clip count at compile time, unused slots weighted to zero.
      *One* clip per instance ships, sampled with nlerp between the two frames it falls between;
      the weighted multi-slot form waits on the crossfade below.
    - [ ] Cross fade blending — slerp local rotations then walk the hierarchy, never blend model space.
    - [x] Local→model hierarchy walk — workgroup per unit, thread per bone, barrier per depth level,
      group size 64.
    - [ ] GPU skinning (compute) to a transient vertex buffer — hero tier only; everything else skins
      in the vertex shader.
    - [x] Skinned motion vectors — **not** by double-buffering the palette, which is what this line
      used to call for. The pose pass writes two palettes per instance in one dispatch, at `time`
      and at `prevTime`, and the mesh shader skins both: correct on the first frame and on an
      instance spawned mid-frame, where a history buffer holds garbage. It holds only while time is
      the sole input to a pose — a clip switched between frames reprojects through the wrong clip,
      which is the state machine's problem to own. See [docs/skinning.md](docs/skinning.md).
    - [ ] Animation preview + playback at different LODs, including the pose-source swap.
      The editor's Animation panel previews either source and switches between them by respawning;
      the runtime LOD swap is what remains.
    - [ ] Bone mask — small per-bone weight array, needed by additive flinch on the skinned tier.
  - [ ] State Machine — flat tables, tiny per-unit interpreter, ticked for all units regardless of
    tier and regardless of visibility.
    - [ ] Crowd units resolve state to a clip index and phase instead of a set of skeletal clips.
    - [ ] Mounted handling — rider and mount share clip index and phase.
  - [ ] Skinned-tier only
    - [ ] Look-at (head + torso, angle clamp) — best liveliness cue per instruction, unavailable to a
      shared pose.
    - [~] Foot planting — analytic two-bone IK, and the ankle turned onto the ground under it: a
      planted foot matches the surface's *orientation* as well as its height, clamped so a cliff
      edge does not break an ankle. Done against `IScene::SetGround`'s single plane; the heightfield
      that replaces the sampler is what is left, and it is what breaks on stairs and siege
      structures. Note none of it is what grounds a clip: the standard solve preserves a foot's
      animated height relative to the root, so on flat ground it corrects by zero. That is
      cook-side, and done. See [docs/skinning.md](docs/skinning.md) § Foot planting.
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
- [ ] Crowd Variation — a few meshes and one clip set means sameness is the primary visual risk; all
  of it must be deterministic from unit ID so nothing changes at a LOD boundary or on death.
  - [ ] Per-unit animation phase offset from an ID hash — non-negotiable, or a formation reads as one
    organism; offset clip time and preserve it across state transitions.
  - [ ] Per-unit `playRate` jitter (±3–5%) so units that synchronise don't stay synchronised.
  - [ ] Per-unit uniform scale (±3–4%) and small formation yaw jitter.
  - [ ] Per-instance submesh mask for small toggles on one mesh — a cape, a quiver — bucketed by
    mask alongside LOD. The *wardrobe* is not this: a swappable kit is a slot mesh of its own on the
    shared rig, which is what the crowd tier was built for.
  - [ ] Attachment variation as separate instanced draws off the rig's bone anim table, which is
    addressable by (clip, frame, bone) from any consumer for exactly this.
  - [ ] Per-instance material variation — kit index into a texture array plus hue/value jitter, reusing
    the blood parameter struct.
  - [ ] Grime/wear float, ID-hashed, reusing the blood dissolve-mask machinery.
  - [ ] Texture atlasing — a slot mesh carries its own material, so a kit already varies by
    construction; this is what is left for variation *within* a slot, and is no longer the only
    place kit variation can come from.
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
    - [ ] Rig strategy — separate rigs sharing phase plus the saddle transform keeps kits
      orthogonal; a combined horse+rider rig is simpler but combinatorial.
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
  - [ ] **Crowd→hero handoff** — a dying unit switches to a palette of its own, which the solver
    needs somewhere to write. The table is the rig's and shared, so it cannot be posed into.
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
  - [ ] **Role and distance are separate axes** — drive the pose-source switch from screen size with a
    top-K budget so near units are posed per instance regardless of rank.
  - [ ] Hysteresis (~10–20% gap) against per-unit stored LOD, especially at the pose-source boundary.
  - [ ] Dithered LOD crossfade resolved by TAA; also the mechanism for the pose-source swap.
  - [ ] Per-tier compaction → indirect args; fixed `maxPerLOD` regions hold until submesh-mask
    variation multiplies the bucket count.
  - [ ] Separate mesh LOD and animation LOD tables driven from the same screen-size value.
  - [ ] Animation ticking and tagging LODs.
  - [ ] Compute skinning bandwidth — measure palette writes, palette reads, bone anim table fetches,
    and the permanent corpse palette read before optimising ALU.
  - [ ] Test motion vectors across LODs, both pose sources, corpses, mounts, and both transitions.
- [ ] Light and Shadow
  - [ ] Async Compute
  - [ ] Directional Lighting
  - [ ] Point Light
  - [ ] Ambient / Sky Light
  - [ ] Cascaded Shadow Maps
  - [ ] Static vs. Dynamic Shadow
  - [ ] Shadow LODs — bias 1–2 tiers coarser, but a unit posed per instance for the camera must not
    read the shared table for a cascade.
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
  - [x] Outline Shader
  - [ ] Render Axis
- [ ] Decals
  - [ ] Material parameters for regular units — a packed `uint32` (amount 8b / dryness 8b /
    direction 16b) that works identically on both pose sources and the corpse tier, with no atlas or
    projection.
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
    available to a crowd unit.
- [ ] Weather
  - [x] Sky box (for interiors and editor)
  - [ ] Procedural Atmosphere Shader
  - [ ] Rain & Snow
  - [ ] Wetness (material modification) — one system with two inputs shared with blood; rain should
    also wash blood off.
  - [ ] Vertex-shader wind on cloth/hair — stateless, works on crowd units and corpses.
- [ ] Debug & Tooling — **build before the GPU implementations, not after.**
  - [x] GPU-Side Assertions
  - [ ] Focus-unit trace — `g_debugUnit` uniform, tagged ring buffer, pretty-printer; gives one agent's
    linear narrative across every pass.
  - [x] Buffer poisoning (`0x7FBADBAD` — sNaN and an impossible index) before a pass that declares
    a scratch output, in debug builds.
  - [ ] NaN/Inf scan pass — more important now that procedural settling is a solver.
  - [ ] Append buffer high-water marks — clamp writes, record attempted counts, assert above 80%.
  - [ ] Per-pass buffer hashing as a FrameGraph feature, to bisect to the first wrong pass in one run.
  - [ ] Determinism diffing as a race detector — run twice, compare, and perturb the schedule between
    runs to surface races that hide at one configuration.
  - [ ] Optional: a CUDA port of one or two kernels purely for `compute-sanitizer --tool racecheck`.
  - [ ] DRED & Aftermath / Radeon GPU Detective, paired with monotonic breadcrumb markers.
- [ ] Profiling
  - [ ] GPU timestamp per pass with on-screen breakdown — FrameGraph feature, same as hashing. The
    RHI has no timestamp query at all today, so nothing in the tree can attribute a cost to one
    stage — the crowd tier's frame-interpolation trade is one measurement queued behind this line.
  - [ ] Live counters: agents alive/dying/corpse split by type, visible per tier, per-instance vs
    table against the top-K budget, events vs capacity, slots in use, cells at cap, corpse palette
    memory.
- [ ] Capacity policy — one table, with clamp-and-report behaviour defined for every entry.
  - [ ] Max agents, max per cell, event buffer size, flow fields resident.
  - [ ] Top-K skinned budget.
  - [ ] Concurrent dying units and solver slots → overflow falls back to canned death clips.
  - [ ] Corpse cap and palette memory ceiling → overflow triggers oldest-first fadeout.
  - [ ] Bone anim table memory — a table is `bones x frames x 48 B` per rig and holds every clip
    whether or not anything plays it, so the ceiling is authoring plus, later, quantization. A crowd
    rig under 100 bones and 3,000 frames is under 15 MB; the project's 663-bone hero rig would be 68
    MiB, which is why a rig gets a table only when a crowd instance spawns on it.
- [ ] Misc
  - [ ] Texture Atlasing
  - [ ] Editor Build Texture Atlas


**Optional Features**

- [ ] Diffuse GI
- [ ] Flowmap baking
- [ ] Forward++ Shading
- [ ] Screen space reflections
- [ ] Gpu Virtual Memory
- [ ] `bgl_wgpu`, the baseline tier — **not** an RHI backend. Tried once as one and abandoned: WebGPU
  has no bindless heap, no descriptor indexing and no mesh stage, so the whole port went on emulating
  what the renderer already had. It is a second renderer above `bgl`'s public interface, where the
  engine's binding model is not the thing being translated. It serves the browser, and natively it is
  the fallback for a device that misses the bar `bgl_extended` assumes — so the two ship side by side
  and neither one is "the native one". What that costs is a renderer, not a device layer: no meshlet
  pipeline, no GPU-driven material lookup, its own culling and its own shaders. Choosing WebGPU as the
  substrate buys writing that device layer once instead of once per API; it does not buy the
  architecture. Whether the tier is picked at configure time or probed at runtime is undecided.
- [ ] Texture-space decals - Render decals into the mesh's UV/texture space, not screen space for heroes
- [ ] Analytic heightfield occlusion — march the height texture from camera to unit, useful only if
  occlusion is needed before the depth prepass or on a separate timeline.
- [ ] Multi-group radix sort — scale-up for transparent depth ordering, and needed if the spatial grid
  moves to a sparse hash table. Onesweep over the classic three-kernel build.
- [ ] Corpse pose clustering — cluster settled palettes to K representatives at distance, the escape
  hatch if corpse memory binds.
- [ ] Saliency-driven LOD - allocate fidelity by attention, not absolute distance. A smoothed autofocus
  anchor (nearest large object, from an HZB center min-depth reduction) rides the pose-source switch
  distance so an empty foreground upgrades the nearest far subject. Salience-ranked budget (screen size +
  proximity-to-anchor + velocity + center weight) picks the top-K for hero-tier skinning; heavy temporal
  smoothing + dithered LOD crossfades avoid global popping. Shares its focal distance with DOF if added.

## Module 2: Game Logic

- [ ] Level Editor for Battles
  - [ ] Terrain Gen. using Noise + inputs: hilly, flat, mountainous etc
  - [ ] Navmesh Gen.
  - [ ] Weather Editor
  - [ ] Drag and Drop Buildings & Meshes
    - [ ] Per-instance material override in the details panel — which submeshes wear an override
      against the geom's default, and setting one. The mechanism ships (see Materials); this needs
      the placement and selection above it first.
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
- [ ] Asset Streaming Pipeline — the archive and the read seam under it shipped, see
  [docs/archives.md](docs/archives.md)
  - [x] `core::file::IFileSystem` — one read seam (`Exists`/`Stat`/`Read`/`ReadRange`/`Enumerate`/
    `IsReadOnly`) over a directory (`LooseFileSystem`), an archive (`assetlib::PakFile`) or an
    ordered search path over both (`LayeredFileSystem`, first hit wins). Concurrent by contract.
  - [x] `.bpak` — header, 16-byte-aligned payloads, entry table, string pool. `PakWriter` streams to
    a temp and renames, so a project is not bounded by RAM and an interrupted pack leaves the
    previous archive intact; `pack` walks sorted, so one tree packs to identical bytes on any
    machine.
  - [x] `assetlib::AssetStore` — the read mount and the writable data root as one object, with every
    loader and staleness predicate on it. `gamelib::AssetManager` takes one, so a scene loads out of
    an archive through exactly the paths it loads a directory through.
  - [x] `assetlib_cli pack` / `list` — the exclusion rule is derived from the registered asset types,
    not a list, and unclaimed extensions are counted rather than dropped silently.
  - [x] Ranged reads preserved through the seam — a chunk-table survey reads a few hundred bytes per
    container, not the whole project.
  - [x] A derived container under a read-only mount is trusted rather than regenerated; `pack`
    resolves stale ones through the seam so a shipped archive is correct by construction.
  - [ ] Per-entry compression — the format reserves the flag; picking a codec wants a measurement.
  - [ ] `mmap`-backed reads — the alignment is there for it; the seam does not hand out a borrowed
    span yet.
  - [ ] Partial residency: load a mip range or a meshlet range rather than a whole entry. `ReadRange`
    is the hook, and this is the line the rest of this milestone actually means.
  - [ ] Patch archives over a base — mount order gives override and `SetMask` gives removal, but
    authoring, versioning and validation do not exist.
- [ ] CPU Spatial Partitioning
- [ ] Game Serialization
