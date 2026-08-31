# bone-animation-texture — the crowd tier skins from a rig's bone anim table

## Context

The crowd tier is VAT: every vertex of a mesh CPU-skinned at every frame of every clip into a
texture pair, one `.bvat` per (mesh, clip set) ([docs/vat.md](docs/vat.md)). It welds geometry to
the bake. A modular unit — a body cut into slots (torso, hands, feet) with swappable armor meshes
skinned to one rig — needs a bake per mesh per clip set, its memory scales `verts × frames`
("hundreds of megabytes on a dense rig", [docs/vat.md](docs/vat.md)), and no two parts can share a
pose. The workaround this was heading for — a few armor kits, layered so one VAT can be reused —
is a constraint on the game imposed by the bake's shape.

Nothing is broken today. The state machine and the crowd-variation work
([ROADMAP.md](ROADMAP.md) § Animation, § Crowd Variation) are about to stack on the crowd tier's
shape, which is when that shape becomes expensive to change.

The roadmap already names this tier: "**Baked-palette middle tier** — skin from the side-channel
palette instead of fetching VAT … the data already exists" ([ROADMAP.md](ROADMAP.md) `:390-391`).
This feature delivers it, and finds that once it exists the tier below it has no job left.

The survey found the replacement mostly built. The skinned tier already uploads a rig's bones,
clip table and sample pool and poses every instance on the GPU
([docs/skinning.md](docs/skinning.md)); `BVat::palettes` — the "skeletal side-channel" the roadmap
reserved for attachments, baked and never read — is the same `model × inverseBind` per bone per
frame that walk produces. The crowd tier this plan builds is the skinned tier's mesh shader reading
the rig's bone anim table the walk fills once, on first use, instead of a per-instance slice it fills every
frame.

## Decisions

- **ADR-1 — The crowd tier skins from a per-frame bone palette shared by every instance of a rig.**
  Unreal's AnimToTexture *Bone* mode (the City Sample crowd) and Unity's Animation Instancing — the
  standard for modular crowds. *Rejected: separate VATs per armor piece sharing phase — memory
  scales pieces × frames and every layer re-bakes with the clip set; layered kits reusing one VAT —
  the geometry is still welded to the bake.*
- **ADR-2 — VAT is retired inside this feature, as its last task, with the crowd-scene timing on
  both tiers recorded in that PR.** *Rejected: keeping both — no rule says when VAT would be
  chosen, and two paths for one thing is what the library bar forbids; a follow-up — never
  scheduled. Our VAT reads a rig, not a point cache, so no sim use is lost: a Houdini-style VAT is a
  new importer either way, and git history keeps this draw path.*

  **Frame time is not the case, and task 5 should not argue as though it were.** VAT skips both the
  pose walk and the table's per-vertex fetches for a texture read, so a fair measurement may well
  put it ahead of the table on a single static unit. What VAT cannot do is share one pose across a
  modular unit's slots, and its memory scales `verts × frames` per mesh against the table's
  `bones × frames` per rig. The retirement rests on that, on the library bar, and on there being no
  rule that says when VAT would be chosen. Task 5's timing is recorded, not relied on.
- **ADR-3 — The palette belongs to the rig: keyed on (skeleton, clip set), never on a mesh.**
  *Rejected: per-mesh, which `.bvat` and `AddSkinnedMeshGeom` both do today — a five-slot kit
  uploads five identical rigs.*
- **ADR-4 — A modular unit is one skinned mesh per slot on one shared skeleton, one instance per
  slot sharing `{clip, phase, rate}`.** Slots replace rather than stack; border loops match by
  construction because every variant is cut from one base body. *Rejected: one mesh holding every
  variant as submeshes behind a per-instance mask — the wardrobe re-cooks the mesh; stacked layers —
  clipping and hidden geometry.*
- **ADR-5 — The palette is a GPU buffer in the skinned tier's three-rows-a-bone layout, not a
  texture.** A deviation from the standard's texture storage, made because `skinned_vertex.slang`'s
  palette read already exists and exact rows gain nothing from a sampler. *Rejected: a texture, as
  Unreal stores it.*
- **ADR-6 — The table is addressable by (clip, frame, bone) from any consumer.** Attachments are a
  non-goal, but a table private to one mesh shader would foreclose them. *Rejected: nothing; this
  is the shape ADR-5 gives for free, stated so a task cannot narrow it.*
- **ADR-7 — The table is filled on the GPU, in one dispatch of the existing hierarchy walk over
  every frame of the clip set, from the sample pool the rig already uploads.** *Rejected: an offline CPU bake kept in a
  stripped `.bvat` — the standard's offline bake, which Unreal and Unity do because they have no
  GPU walk at load. Here it would keep a container, its staleness rule, bake-on-demand, `pack`'s
  re-bake and the editor's bake dialogs (~1,500 lines, [survey](#what-the-survey-found)) to cache
  what one dispatch regenerates — its cost is a number task 2 measures, not a guess — plus a CPU
  pose evaluator that must agree with the GPU one. The cost: no CPU-readable palette on disk — `assetlib::poseModelTransforms` still serves CPU
  needs.*
- **ADR-8 — The tier is a property of the instance, not a geometry family.** A skinned geom is a
  mesh cook plus a rig; what differs between the hero and crowd tiers is where an instance's pose
  comes from, so `SkinnedInstanceDesc` names its pose source (`kPerInstance`: the pose pass fills a
  slice per frame; `kBoneAnimTable`: the mesh shader reads the rig's table) and one mesh shader
  branches on it, uniformly per meshlet. *Rejected: a fourth `GeomType` with its own PSO buckets
  and mesh shader — duplicates a hundred lines for a path that differs in one fetch, and makes a
  LOD swap between tiers a second upload of the same cook.*
- **ADR-9 — A rig's table is filled when its first bone-anim-table instance is spawned, and lives
  with the rig.** The sample pool is resident, so the table is a dispatch away whenever it is first
  wanted, and a rig no crowd instance ever uses — the hero in the Animation panel — never pays for
  one. *Rejected: filling every rig's table at upload — `cha800_00` would hold 72 MB and run a
  1.49M-bone-pose dispatch for a tier it never draws through; a flag on the upload — a second
  parameter every acquire has to guess at.*
- **ADR-10 — A fractional frame is the lerp of the two frames' matrices.** Skinning is linear in
  the matrices, so `lerp(A, B, t)·v == lerp(A·v, B·v, t)` — VAT's blend of two texel rows, at half
  the cost of skinning twice. *Rejected: nlerp of local rotations then a walk, per vertex — that is
  the pose pass, and the reason the hero tier has one.*
- **ADR-11 — A rig is a scene object the caller owns, like a texture asset: added before the geoms
  skinned to it, deleted after them.** *Rejected: refcounting inside `bgl` — no handle in
  [docs/bgl_api.md](docs/bgl_api.md) is refcounted, and `gamelib` already refcounts every asset.*
- **ADR-12 — `AcquireSkinnedMesh` keeps its signature; the rig share is inside `AssetManager`,
  keyed on the normalized `.banim` path.** *Rejected: a public `AcquireRig` the caller threads
  through every skinned load — a second thing to hold and release for a share the manager can make
  itself. When attachments need the rig by handle, that door is added then.*
- **ADR-13 — The pose source is the *kind* of playback record a placement holds, not a field inside
  one.** A hero instance gets an `idl::SkinnedState`, a crowd one an `idl::SkinnedTableState` — the
  same `{rig, clip, phase, rate}` and no palette — and the mesh shader reads the arena's
  `RecordHeader` to know which, which is what that header exists for. Decided in review of task 3,
  and only available at all because `#543` landed the byte-addressed playback arena on `master`
  mid-feature. *Rejected: one record kind whose `palette` is left null on a crowd instance, with the
  shader branching on the absence — how task 3 was first built. The arena already answers "which
  kind of record is this", so reading the hole is a second convention for a fact it already carries,
  which is the two-ways-to-do-one-thing the library bar forbids; it also makes every crowd record
  carry a `Range<float4>` it never reads.*

## Non-goals

- **Rigid attachments** — a static mesh placed from one bone of the table. ADR-6 keeps the table
  reachable; nothing here reads it but the mesh shader.
- **The state machine**, clip switching or blending. `{clip, phase, rate}` at spawn, as today.
- **Per-unit culling.** A unit is N instances and culls N times.
- **The runtime LOD swap** between tiers. ADR-8 makes it a respawn on the same geom; nobody
  performs one.
- **Freeing a crowd-only rig's sample pool** once its table is filled. The bone-anim-table path reads
  only the table, so the pool is dead weight on such a rig; it stays resident because a table
  arena that grows re-fills from it (below), and because nothing yet says a rig is crowd-only.
- **Editor kit or slot UI.** The Animation panel previews one mesh through either tier, as today.
- **Palette quantization.** [docs/specs/animation_compression.md](docs/specs/animation_compression.md)
  stays a spec; the table is `float4` rows.
- **A sim / point-cache importer.** ADR-2.
- **Per-vertex masked layering**, and every other line under the roadmap's VAT constraints.
- **Deleting the pose pass.** The hero tier keeps it; it gains a sibling entry point, not a
  replacement.

## Acceptance

- **bgl** — one rig drawn through both pose sources is pixel-equal at integer frames and within
  tolerance between them, motion vectors included; the bone anim table read back matches
  `assetlib::skinningMatrices` at every frame of every clip; every existing skinned golden is
  unchanged by the rig refactor; `just run bgl_tests -- --gpu-validation` clean.
- **gamelib** — an end-to-end test: two slot meshes synthesized against one rig, acquired sharing
  one rig upload, drawn as one unit on the bone anim table, asserted on pixels.
- **assetlib** — no new container and no token bump. The `.bvat` codec, its `TokenCanary` pin, its
  reference-graph edges and `pack`'s re-bake are removed and every suite passes without them, no
  other token moved.
- **Retirement PR** — no VAT draw path, container or bake remains; the crowd-scene timing on VAT,
  the bone anim table and the per-instance tier is in its body.
- **GPU residency, owned here** — a rig with a table holds `bones × frames × 48 B` beside its
  same-sized sample pool. Nothing clamps it: a table is as large as its clip set, and the levers
  are authoring and, later, quantization. So the roadmap's "VAT texture memory under 50 MB"
  ceiling ([ROADMAP.md](ROADMAP.md) `:366`) is replaced by a *report and an authoring rule*: the
  reservation opens a Tracy zone carrying the table's size — `bgl`'s only one, since everything
  else instrumented is an `assetlib` cook or the editor's start-up, and
  [docs/profiling.md](docs/profiling.md)'s inventory gains the row — and a crowd rig is authored under 100 bones and 3,000
  frames, which is under 15 MB. The one rig the project holds today, `cha800_00` at 663 bones and
  2,254 frames, would hold 72 MB — a hero rig, which ADR-9 keeps table-less unless a crowd
  instance spawns on it, and the report is what says so when one does. Task 2's PR body carries
  the fill cost as a CPU-side number.

## What the survey found

The skinned tier, per [docs/skinning.md](docs/skinning.md) and the code:

- `IScene::AddSkinnedMeshGeom(mesh, meshIndex, materials, skeleton, animations, posedBounds)`
  ([libs/bgl_intfc/include/bgl/IScene.h](libs/bgl_intfc/include/bgl/IScene.h)) uploads the rig **per geom**:
  bones, clips and the sample pool land in scene buffers at
  [libs/bgl/src/scene/Scene.cpp](libs/bgl/src/scene/Scene.cpp) `:801-833`, one `idl::SkinnedGeom`
  per geom. Two meshes on one rig upload it twice.
- `idl::SkinnedGeom` ([libs/bgl/idl/src/SkinnedGeom.slang](libs/bgl/idl/src/SkinnedGeom.slang)) is
  "one rig as the GPU sees it" — bones, samples, clips, `boneCount`, `maxDepth` — and nothing of
  the mesh. `idl::SkinnedState` is `{geom, clip, phase, rate, palette}`, the last a per-instance
  `Range<float4>` of `2 × boneCount × cFloat4sPerBone` allocated at spawn
  ([libs/bgl/src/scene/SceneView.cpp](libs/bgl/src/scene/SceneView.cpp) `:293-302`) from the view's
  `BonePaletteBuffer` — GPU-only storage with a CPU offset allocator.
- `PoseSkinned.slang` poses one instance per workgroup at `time` and `prevTime` into that slice
  (`:176-181`); its `LocalTransform`, three-row pack/unpack and depth-level walk are the whole of
  a frame's pose. The clip table is one scene-wide `RangeBuffer<idl::Clip>` both tiers already share.
- `skinned_vertex.slang` reads `state.palette.GetStart()` and `base + boneCount × 3` for the two
  poses; `BoneMatrix`/`SkinMatrix` are the fetch ADR-5 reuses. Every `.bmesh` carries
  `joints0`/`weights0` (8 + 8 bytes; `bmesh_gltf.cpp:551-566`) and the decode is shared, so the
  crowd tier needs no re-cook.
- `SkinnedInstanceDesc` and `VatInstanceDesc` are already the same three fields by design
  ([libs/bgl_intfc/include/bgl/InstanceDesc.h](libs/bgl_intfc/include/bgl/InstanceDesc.h)).
- Motion vectors are the pose re-evaluated at `prevTime`; placement and deletion bump the view's
  temporal epoch ([docs/taa.md](docs/taa.md)), so a tier switch by respawn takes one unaccumulated
  frame rather than a ghost.
- `bgl_tests` links `assetlib` ([libs/bgl/CMakeLists.txt](libs/bgl/CMakeLists.txt) `:231`), so a
  GPU table can be checked against `assetlib::skinningMatrices` in-suite.
- No test draws one rig through two tiers and compares; the nearest shape is
  `SkinnedRender_test.cpp:227` (skinned at bind pose equals static). No GPU timestamp query exists
  in the RHI; timing is CPU frame time.

The VAT tier, to be removed:

- IDL `VatGeom`, `VatState`, `Mesh.vatState`, four `PsoType` buckets; `GeomType::kVatMesh`;
  `Forward_VatMesh.slang` (46), `forward/vat_vertex.slang` (138), `forward/VatData.slang` (17), the
  `Forward_AnyMesh` branch, `TransparentDepthKeys`, two `compile_shader` blocks; `Scene.cpp`
  `AddVatMeshGeom` ×2 + `AttachVatRecords` (~200 lines), `SceneView::CreateVatMeshInstance`,
  `c_VatBuffers` in `SceneBindings.h`, four PSO rows in `ForwardPass.cpp`, `OutlineMaskPass`,
  `util.cpp`; `IScene`/`ISceneView`/`InstanceDesc` declarations.
- assetlib: `BVat.h` (106), `vat_bake.h` (104) / `vat_bake.cpp` (439), `bvat_io.cpp` (324),
  `vat_tangent.h` (71), `AssetCodec<BVat>` and its token, `AssetType::kVat`, `RefKind::kVatSource`
  and its three edges, `DeletionPlan::derived`'s VAT case, `asset_rename.cpp`'s move to
  `vatPathFor`, `pak_pack.cpp`'s `rebakeStaleVats`, `migrate`/`reimport`/`describe`/
  `container_table` cases, `PackReport::vatsRebaked`; CLI `bakevat`, `describe`, `refs`, `pack`
  lines.
- gamelib: `vat_freshness.h/.cpp` (235), `AcquireVatMesh` (~130), `CreateVatInstance`, `VatMesh`.
- editor: the tier selector's "VAT (baked)" entry, the **Bake VAT** button, `AnimationLoadSteps`
  `{needsFreshBake, framedByBake}` and `PlanAnimationLoad`, the freshness gate and
  `OfferBakeForTier` (~90 lines in `AnimationPreviewWindow.cpp`) -- **not** `OfferBakeForRefusal`,
  which offers a *material* bake and is about the mesh's materials rather than the tier;
  `IsHiddenBuildProductFile` (`asset_paths.h`) and the Content Explorer's `.bvat` filter.
- Tests: 18 cases in five VAT-only suites (2,518 lines: `VatBake`, `VatAcquire`, `VatRender`,
  `VatNormalMap`, `VatPlayback`), 9 named cases and assertions in 16 mixed suites, the `[taa][vat]`
  animating-outline case, two goldens, `VatSynth` and `VatFixture`.
- Docs: [docs/vat.md](docs/vat.md) whole; sections of `skinning.md`, `archives.md` (the read-only
  `.bvat` rule), `passes.md`, `asset_containers.md`, `assetlib_api.md`, `taa.md`,
  `asset_standards.md`; the worked examples that name VAT in `idlgen.md` (`:67`, the `float3`
  layout rule) and `gfx_debug.md` (`:176`); `docs/specs/skeleton_append.md` (`:15`, `:97` — the
  signature rule and the consumer audit both name the bake); `.gitignore`/`.gitattributes`
  `*.bvat`.
- `ROADMAP.md`, which names VAT in some thirty lines. The ones that are design rather than text:
  § Animation's VAT block (`:111-146`, the side-channel line at `:118-120` and the constraints
  list at `:144-146` inside it); the LOD-preview line (`:163`); the Baked-palette middle tier
  (`:390-391`, delivered); the capacity ceiling (`:366`, replaced — see Acceptance); § Crowd
  Variation's submesh-mask line (`:194`, narrowed to small toggles on one mesh — a cape, a quiver —
  since the wardrobe is per-slot meshes under ADR-4) and its attachment line (`:195`, now "off the
  rig's bone anim table"); § Cavalry's bake-strategy line (`:243-244`); the VAT→skeletal death handoff
  (`:261`); the LOD module's skinned→VAT transition design (`:284-294` and the shadow-LOD rule at
  `:302` — switch axis, hysteresis, dithered crossfade — which survives as a swap between pose
  sources on one geom). The rest are one-word substitutions the task's grep finds.
- `libs/core`: the `IsReadOnly` contract in
  [libs/core/include/core/file/IFileSystem.h](libs/core/include/core/file/IFileSystem.h) `:95` and
  [libs/core/include/core/file/LayeredFileSystem.h](libs/core/include/core/file/LayeredFileSystem.h)
  `:66` state the read-only `.bvat` rule that `archives.md` is written from; they go together.

*Correction, from rebasing onto master.* Two changes landed on `master` while this feature was
being built, and both move parts of the inventory above. `#543` put every animated placement's
record in one byte-addressed arena, so `Mesh.vatState` no longer exists: a placement carries a
single `RawEntry<IPlayback>` and the tier is read from the record's `RecordHeader`. What task 5
removes there is `PlaybackType::kVat` and the `VatState` record, not a field and a state buffer —
and it should say whether a tag over one remaining kind still earns its place, rather than leaving
a constant behind. `#534` moved the public headers to `libs/bgl_intfc/include/bgl/`, which is where
the VAT declarations now are. Neither changes what the tier costs or what retiring it buys.

## What changes

| Where | What | What could break |
|---|---|---|
| `libs/bgl/idl` | `SkinnedGeom` becomes `Rig` and gains `Range<float4> boneAnimTable` (null until filled); `SkinnedState.geom` becomes `rig`, and `SkinnedTableState` joins it as the crowd record kind (ADR-13) with `PlaybackType::kSkinnedTable`; `VatGeom`, `VatState`, `Mesh.vatState`, four `PsoType`s go | Nothing in the layout: `gen_idl.py` emits the C++ and Slang sides from one module, so a removed field moves both together |
| `libs/bgl_intfc/include` | `RigHandle`, `IScene::AddRig`/`DeleteRig`, `AddSkinnedMeshGeom` takes a rig; `PoseSource` on `SkinnedInstanceDesc`, whose header comment is rewritten — the playback record stays the same three fields and a unit still moves between tiers without rewriting it; the source says where the pose comes from, not what plays; VAT declarations go | Every skinned golden — the refactor must be pixel-identical |
| `libs/bgl/src/scene` | Rig records; a second `BonePaletteBuffer` at scene level for the tables (the same GPU-only storage and offset allocator the per-view palette uses — not a new type; its header comment, which says "one view's" and "rewritten every frame", is rewritten to state the real precondition: whatever it holds is re-derivable after a growth); the pose pass's dense instance list excludes crowd instances, which own no palette to write into | `BonePaletteBuffer`'s growth **discards** its contents, safe per view only because every instance is re-posed every frame. A table is written once, so a growth must re-queue every rig holding one — the sample pool it fills from is resident, which is why this is a re-dispatch and not a loss. Also: a crowd instance reaching the pose pass would write through a slice it does not own |
| `libs/bgl/src/passes` | `PoseRigFrames`: one workgroup per frame, run for each rig whose table is wanted and unfilled (ADR-9) or discarded by a growth, ordered before every reader by the frame graph | Metal: a GPU-written scene buffer read by a mesh stage — the per-view palette already does this; the pass must not be culled as dead on the frame that fills a table before any instance on it is drawn |
| `libs/bgl/shaders` | `pose_walk.slang` shared by `PoseSkinned` and `PoseRigFrames`; `skinned_vertex.slang` branches on the record's kind and lerps rows across two frames at `time` and two at `prevTime`; VAT shaders go | 48 buffer loads a vertex on the crowd path, from a table shared by every instance on the frame |
| `libs/gamelib` | `AssetManager` holds one rig per `.banim`, refcounted, shared by every `AcquireSkinnedMesh` on it; `CreateSkinnedInstance` carries the pose source; VAT acquire and freshness go | Release order: geoms before the rig, on the unwind too |
| `libs/assetlib` | Deletions only (the inventory above); `TokenCanary` loses its `.bvat` row | A stale `.bvat` in a checkout is an unknown extension to the scan — must be skipped, not fatal |
| `apps/editor` | The selector offers "Skinned" and "Crowd" naming `bgl::PoseSource` directly (`editor::AnimationSource` goes -- one enum, not a mirror of it); both load alike, so `AnimationLoadSteps`, `PlanAnimationLoad` and `LoadMeshAs` all go, and a tier switch becomes a respawn on the same geom rather than a re-upload | The panel is untested through its window; what `editor_tests` can pin is the selector's mapping, and the two tiers look identical on screen |
| `docs` | `skinning.md` becomes the two tiers' doc — the rig, the table, the pose sources; `vat.md` deleted; the index and every VAT section updated; `ROADMAP.md` lines rewritten | — |

Memory, stated rather than deferred: a rig's table is `bones × frames × 48 B` beside the same-sized
sample pool — ~9 MB + 9 MB for a 60-bone crowd rig with 3,000 frames, the number Acceptance owns;
72 MB + 72 MB for `cha800_00` *if* a crowd instance ever spawns on it, which ADR-9 is what stops a
hero rig paying by default. [docs/specs/animation_compression.md](docs/specs/animation_compression.md)
is not the answer here — it compresses the `.banim` on disk and says so — so what the GPU holds
per rig is this plan's cost to carry.

## The tasks in order

1. **`refactor(bgl,gamelib): a rig is a scene object shared by the geoms skinned to it`** — ADR-3,
   ADR-11, ADR-12. `Rig` in the IDL, `AddRig`/`DeleteRig`, `AddSkinnedMeshGeom` by handle;
   `AssetManager` acquires one rig per clip set. No behaviour change.

   *Correction, from building it:* this row previously said the geom would validate its joint
   indices against the rig's bone count. It cannot. `bgl` links `assetlib_structs`, not `assetlib`,
   so it can read a submesh's vertex *layout* but has no decoder for the packed vertex bytes — the
   same reason `IScene::AddSkinnedMeshGeom` already documents for not measuring the posed box
   itself. Joint range stays where it was: a `dbg_assert` in the mesh shader under
   `BERNINI_GPU_DEBUG`. What the door does check is that the rig handle is live.
   *Gate:* every skinned suite and golden unchanged (`bgl_tests "[skinned]"`, `gamelib_tests`,
   `editor_tests`); a new case that two geoms on one rig upload one sample pool; a rig deleted
   under a live geom is refused; a refused add counts no use.

   *Correction, from building it:* this gate also asked that "the unwind releases the rig", and
   `gamelib` cannot assert it. A rig the acquire's unwind or `~AssetManager` forgot is invisible
   through `IScene` — nothing reports a rig, and `gamelib_tests` does not reach `bgl::Scene`. What
   is covered instead: the *ordering* (rigs after geoms, or `bgl` refuses and the destructor
   swallows the throw), and on the `bgl` side that a refused add leaves the rig deletable. Making
   the freeing itself assertable means putting bgl's private root on a gamelib test target, which
   is a change worth its own argument rather than a line in this one.
2. **`feat(bgl): a rig's every frame is posed once, on demand`** — ADR-7, ADR-9. `pose_walk.slang`
   extracted from `PoseSkinned`; the `PoseRigFrames` kernel and pass; `Rig.boneAnimTable` and a
   scene-level `BonePaletteBuffer` whose growth re-queues every filled rig; a test-facing door to
   request a rig's table ahead of task 3's instances; the reservation's Tracy zone, and
   `docs/profiling.md`'s inventory gaining it.
   *Gate:* readback of a synthesized rig's table equals `assetlib::skinningMatrices` at every frame
   of every clip within float tolerance; a
   growth of the arena leaves every table intact (asserted by readback after a forced growth);
   `--gpu-validation` clean; the pose pass's existing readback cases unchanged; the fill's
   CPU-side cost on the test project's largest rig in the PR body, with the `cha800_00`
   extrapolation beside it.

   *Correction, from building it:* the gate also asked that the table equal *the per-instance pose
   pass* at integer time. Dropped as redundant rather than skipped: both producers now run the same
   extracted walk, and both are pinned against `assetlib::skinningMatrices` — the table by this
   task's cases, the pose pass by `SkinnedPose_test`'s — so a divergence between them is a failure of
   one of those. The addressing risk a direct A/B would have caught, a global frame read as a
   clip-local one, is covered instead by the table's fixture carrying *two* clips. The cost is a mesh
   fixture duplicated to place an instance, which buys nothing the two references do not.
3. **`feat(bgl,gamelib): a skinned instance may draw from the rig's bone anim table`** — ADR-1, ADR-5,
   ADR-8, ADR-10, ADR-4. `PoseSource`, the shader branch, the pose pass skipping table instances,
   `CreateSkinnedInstance` passing the source through; `docs/skinning.md` gains the crowd tier.
   *Gate:* the parity case (both sources, one rig, pixel-equal at integer frames, tolerance
   between, motion vectors); the gamelib end-to-end two-slot unit; `--gpu-validation`; a hidden
   `[.posetiming]` case spawning N instances on the table and the per-instance tier, run by hand —
   its numbers go in this PR's body and ADR-2's.

   *Correction, from building it:* the gate named VAT as a third leg of that measurement. It is not
   one here. VAT geometry comes through a different door — a baked texture pair over a procedural
   quad — so timing it beside a skinned strip would compare two meshes and read like a comparison of
   two tiers. The leg moves to task 5, where a gamelib fixture can bake a `.bvat` from the very mesh
   the table is posed from and make it fair. What task 3 measures is the pair that *can* share a
   mesh: 2,000 instances of a 64-bone rig six levels deep, 1.22 ms/frame per-instance against 1.06
   on the table — the median of three runs.

   *A third correction, from review:* the pose source was first stored as a null `palette` on one
   record kind. It is now the record kind itself — ADR-13, which this task adds. Nothing above is
   reversed: no ADR had decided the representation, so this is a decision the plan was missing
   rather than one it got wrong.

   *A second correction, from measuring it three times:* the first number recorded here was 2.75
   against 1.83 and it was wrong twice over — taken while another suite had the machine, and on the
   two-bone fixture rig, which gives the pose pass almost nothing to remove. Re-measured on a
   64-bone rig it read ≈1.48 against ≈0.99; but that rig was a 64-deep *chain*, and the walk costs a
   barrier-synced level per depth, so it was the most expensive rig of its size that exists. As a
   binary tree — six levels, which is what a rig looks like — the honest figure is the pair above,
   about 13%, and it is an upper bound rather than a floor: the fixture's reads are as cache-hot as
   they get. Each correction made the tier look worse and the number more usable.
4. **`feat(editor): the Animation panel previews the crowd tier`** — the selector, the VAT load
   path and its bake dialog removed.
   *Gate:* `editor_tests` pins which pose source each selector entry names; an **Eyes** box: the
   coyote plays under the Crowd entry at all.

   *Correction, from building it:* three things this row got wrong.

   **"Both Bake Now dialogs" is one dialog.** `OfferBakeForTier` offers the VAT bake and goes.
   `OfferBakeForRefusal` offers to bake *materials* — it is reached when a mesh is shown in bind pose
   because its materials are unbaked, calls `BakeableMaterials`/`BakeMaterials`, and has nothing to
   do with the tier. It survives this feature. Taking the inventory literally would have deleted a
   working feature.

   **`PlanAnimationLoad` is not re-pinned, it is deleted.** `AnimationLoadSteps` holds exactly
   `needsFreshBake` and `framedByBake`, and both are "is this VAT". With one pose source per
   instance and one upload behind both, the struct is empty; the function and its test case go.

   **The Eyes box the row named cannot fail.** "Plays identically under both entries" is what task 3
   *proved* — the two sources are pixel-equal at a whole frame — so a selector wired to nothing would
   pass it looking perfect. The gate is now a test over the selector's index-to-`PoseSource` mapping,
   which is the only part a person cannot check by looking. Eyes keeps a box, for what it can catch:
   that the Crowd entry draws at all. Writing that test immediately paid: the mapping answered
   *crowd* for an out-of-range index, and now answers with the hero tier, as an unset
   `SkinnedInstanceDesc::source` does.
5. **`refactor!(bgl,assetlib,gamelib): retire the VAT tier`** — ADR-2. The inventory deleted, the
   docs and roadmap rewritten, `TokenCanary` re-pinned without the row, ignore rules dropped.
   *Gate:* `just test` green; `TokenCanary` shows no other token moved; a case-insensitive grep
   for `vat` across `libs/`, `apps/`, `docs/`, `ROADMAP.md`, `CLAUDE.md` and the ignore files,
   with only the English false positives excluded
   (`grep -riE vat … | grep -viE 'private|activat|derivat|conservat|reservat|innovat|motivat|elevat'`),
   finds nothing outside `docs/plans/` — an exclusion list fails loud on a spelling it did not
   foresee, where an inclusion list (`\bvat\b` misses `vat_bake`, `kVat`, `vatPathFor`) passes
   with the tier still in the tree; the roadmap's lines are rewritten, not left as history; the
   timing from task 3 in the body.
6. **`docs: fold the plan into docs/skinning.md and delete it`** — the decisions that outlive the
   feature move to the subsystem page; this file goes with the landing PR.
