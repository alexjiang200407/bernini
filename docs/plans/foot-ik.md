# foot-ik — a foot that meets the ground it is standing on

Feature branch `feat/foot-ik`. Written 2026-08-30 from the spec of 2026-08-28 (the day #491 merged),
grilled against the code as it stands; the spec is superseded by this plan and is not committed.

## Context

`groundClips` (`libs/assetlib/include/assetlib/skinning.h:259`) fixes **where a rig stands**: one
constant per clip, applied to the root track, so the lowest point the mesh reaches rests on `y = 0`.
It never touches a joint, so it cannot make a sole lie flat. Measured on the Dog by skinning each
clip's frame 0 and fitting a plane to the underside of each foot, a planted sole on flat ground is
off the ground normal by an *authored* 2–17° (`Idle` −2.3°, `Walk` −5.5°, `Run` +7.2° / −2.2°,
`Carry_Move` +16.9°), and the bind pose itself is −1.2°. No cook-side translation removes any of
it; the target is the ground normal, not the bind pose.

Today the world is flat and that residual is visible only on a hero close-up. It stops being cheap
when terrain lands — `ROADMAP.md:306` names foot planting as one of the five things the heightfield
feeds, and on a slope every foot is wrong by the whole slope angle — and on any camera that puts a
foot within a few hundred pixels. The flat-plane and slope cases are buildable and testable now,
against a ground plane a heightfield later replaces.

`ROADMAP.md:175-177` already carries the feature and says the standard solve "corrects by zero" on
flat ground. That is right about position and silent about orientation; the line is amended in the
task that lands the solve.

## Decisions

- **ADR-1 — The solve runs on the GPU, inside `SkinnedPosePass`.** `PoseSkinned.slang:151-155`
  is the one place a rig's **model-space** bone transforms exist — after the depth-level walk, before
  the inverse-bind multiply — and the solve is inserted there, one thread per leg. *Rejected: a CPU
  solve.* `docs/skinning.md` already decided the pose is computed on the GPU because the CPU
  evaluation "does not survive contact with a crowd"; a CPU foot solve drags the palette back across
  the bus per planted instance and reverses that ADR for a feature that does not need it.

- **ADR-2 — Ground is one plane held by `IScene`, sampled through one function.** `SetGround` /
  `GetGround` on `IScene`, a `{point, normal}` with the default `y = 0`, up. The shader asks a single
  `SampleGround(xz) -> {height, normal}`; when terrain lands that function reads the heightfield where
  the scene has one and the plane elsewhere, so the plane becomes the no-terrain fallback rather than
  being deleted. On the scene, not the view: the terrain that replaces it is scene geometry and every
  view of a scene stands on the same ground, where lighting was made per-view because two views may
  be exposed differently. *Rejected:* flat `y = 0` with no scene input — fixes the table but the
  sole-normal half of the solve is only ever exercised at normal = up and the slope case cannot
  exist; a minimal heightfield now — a terrain task inside foot IK, and the spec says the feature is
  not blocked on it.

- **ADR-3 — Leg chains are authored by bone name in a new authored document, `.bavatar`, one per
  rig.** Bone names are pack conventions (`Dog L Foot` / `Coyote L Foot` / whatever the next pack
  says), so a name-sniffing heuristic is a silent mis-solve the first time a pack disagrees. The
  document is the rig's authored half the way a `.bmaterial` is a mesh's: Unity's *Avatar* is the
  same object. Found **by convention** from the `.bskel` key — `Derived/Skeletons/<x>.bskel` ↔
  `Authored/Skeletons/<x>.bavatar`, `importDocumentKeyFor`'s rule for a source and its
  `.bimport` plus the swap of halves that rule never makes, since a `.bimport` sits beside its
  source — so a packed project reaches it and the reference scan derives the edge the way it
  derives `kImportedSource`. Chains resolve to indices **at load** (`findBone` already exists; the
  `.bskel` keeps its names), so the `.bskel` is untouched and the avatar enters no cache key.
  *Rejected:* a `legChains` parameter on the producing `.bimport` — unambiguous, since one import
  writes each `.bskel`, but rig metadata in a slot that means "how this source is cooked", and it
  cannot move to another source on re-export; cooking indices into the `.bskel` — a second copy of
  the names, a token bump, and a cache key the avatar would then have to reach. The document is
  built to take masks and any future per-rig authoring; blending is per rig too (a blend is an
  operation on one skeleton's poses, and a body and a cloak share it), so nothing here forecloses it.

- **ADR-4 — The sole plane is derived from the mesh, never authored, and measured at load.** Fitted
  to the underside of the vertices weighted to a leg's ankle and toe at bind pose, expressed in
  ankle-local space. It is a property of a (mesh, skeleton) pairing — the vertices are the mesh's —
  so it belongs in no rig container; it costs one pass over the vertex influences, which
  `boneBoxesFor` already makes at load on a cache miss. *Rejected:* an authored plane per foot (the
  chore that would leave the feature unused); storing it in the `.bskel` (wrong owner) or `.bmesh`
  (a per-mesh entry keyed by the avatar, for a number that costs milliseconds).

- **ADR-5 — Plant weights are derived at cook from the frame walk and stored in the `.banim`,
  self-keyed like `posedBoxes`.** A foot is planted at frame `f` when its sole is within ε of the
  grounded floor and its horizontal displacement over `f−1..f+1` is below ε. Stored as **one byte
  per leg per frame** — a weight, ramped over three frames at each end of a planted run at cook, so
  a foot enters and leaves the plant without a pop and the shader reads a weight rather than
  reconstructing one — in a chunk keyed by a signature over the resolved chains, the skeleton and
  the meshes the sole was fitted on; a load that finds no matching entry measures, exactly as
  `findPosedBounds` / `posedBounds` do. *Rejected: the standard.* Unreal makes this an animation
  notify and Unity a curve on the clip; 21 clips × 4 feet × 40 frames on 29 purchased rigs is
  authoring nobody will do. A wrong derivation on some clip gets a per-clip override in the avatar,
  not an authoring surface. *Also rejected:* the spec's one *bit* per frame — a one-frame lerp at
  30 Hz is a 33 ms pop, and the ramp has to live somewhere.

- **ADR-6 — The `prevTime` palette solves against the instance's one transform; the "time is the
  sole input" invariant holds.** An instance's transform is fixed for its lifetime: `ISceneView` has
  no mutator, movement is destroy + recreate (`docs/taa.md`), and `static_vertex.slang:19-21`
  passes one world position through both cameras and names the missing previous transform. So both
  palettes solve against the same placement and — while the ground stands, ADR-10 — the same ground, and the pose that `prevTime`
  produces is the pose that was drawn. *Rejected:* a previous transform on `Mesh` now — 64 B an
  instance that nothing would ever set to anything but the current transform; double-buffering the
  palette — reverses the re-evaluation ADR in `docs/skinning.md` and loses the first-frame and
  mid-frame-spawn correctness it bought. Whoever adds instance movement owes a previous transform to
  static and skinned meshes alike, and this ADR is the line they amend.

- **ADR-7 — The instance transform reaches the pose pass through `SkinnedState`.** The pass reads
  only the playback record (`SkinnedState.slang`); the world transform lives on `Mesh` and is applied
  in the mesh shader. The solve needs the foot's world `xz` (for the ground sample) and the ground
  back in model space, so the record gains the instance's `transform`, written at spawn like every
  other field. *Rejected:* binding the mesh buffer to the pose pass and an instance index into it —
  the index is not known until `WritePlacement` returns, after the record is written, and it couples
  the pose pass to the placement record's layout for 60 bytes.

- **ADR-8 — The solve is the standard one: analytic two-bone with pelvis drop and a clamped ankle.**
  Law of cosines on hip–knee–ankle toward the ankle target, the current knee direction as the pole;
  where the target is out of reach the deficit is accumulated and the whole rig is lowered by the
  largest deficit across its legs before every leg re-solves (Unreal's Foot Placement does this, and
  skipping it is what makes a leg visibly snap straight); then the ankle is rotated to bring the sole
  normal onto the ground normal, clamped to 30° so a cliff edge does not break an ankle; every step
  scaled by the plant weight. Descendants of a solved bone follow it rigidly. No deviation.

- **ADR-9 — The VAT tier gets none of it, permanently.** `ROADMAP.md:146` lists "no IK" among VAT's
  constraints and the reason is structural: that tier has no skeleton at runtime. A unit that needs
  planted feet sits above the VAT boundary, which is the roadmap's own tier policy.

- **ADR-10 — `SetGround` moves the scene's temporal epoch.** From task 3 on, the ground is a second
  scene-level input to a pose, and ADR-6's argument covers only the transform: a ground moved between
  frames has both palettes solved against the new one, so `prevTime` describes a pose that was never
  drawn — the clip-switch failure `docs/taa.md` spells out. `Scene::GetTemporalEpoch` counts exactly
  "a change no motion vector describes", and its rule admits discrete rebinds and refuses anything
  moved every frame; the ground is a rebind — set once with the scene, or by an editor slider — and
  so it bumps the epoch, and the resolve takes that frame whole. Terrain, when it replaces the
  sampler, is static for the same reason. *Rejected:* leaving the ground out of the epoch and taking
  a wrong motion vector on every planted foot for one frame after a change; a per-frame previous
  ground, which is ADR-6's rejected previous transform under another name.

## Non-goals

- Stairs and siege structures (`ROADMAP.md:175` concedes the heightfield solve breaks on them), and
  IK against arbitrary collision, ragdoll, anything on the CPU hero-tier line (`:188`).
- A heightfield. The plane is the seam; terrain replaces the sampler.
- Hand IK, prop attachment, look-at. Same solver, different targets and authoring; the chain format
  is not generalised for them.
- An editor panel for authoring chains. The `.bavatar` is hand-edited, as `clipFloor` is.
- Authored plant windows. If ADR-5's derivation is wrong on a clip, the fix is a per-clip override
  in the avatar, and that override is not built until a clip needs it.
- Instance movement and the previous transform it needs (ADR-6).
- Masks, blending, or anything in the `.bavatar` beyond legs — the document leaves room, no more.
- Fusing the plant-window walk with the posed-bounds and grounding walks. The three still walk the
  clip set separately, as `docs/plans/pose-bounds-perf.md` § Non-goals records; a shared walk is the
  measured next step there, not here.
- Rigs whose leg is not a direct `hip → knee → ankle → toe` parent chain (a twist bone between). Such a
  chain is refused with a message naming the bone; supporting it is a solver change.

## Acceptance

- The spec's table re-measured: every planted foot within ~0.5° of the ground normal on a flat plane
  at frame 0 of every clip that plants it, on the Dog and the Coyote in `./test-project`, by the same
  plane-fit probe that produced the table.
- `[skinned][pose]` palette-readback cases pinning the solve against a hand-computed two-bone
  solution on a synthetic leg: flat ground lowered, sloped ground, weight 0 untouched, the 30° clamp,
  descendants following, pelvis drop on an unreachable target, and the `prevTime` palette equal to a
  `time` palette at the same time.
- A `[skinned][render]` golden image of a synthetic leg on a 15° slope, and the motion-vector case
  extended: a planted, held instance on a slope writes zero velocity; an animating one writes some.
- `[avatar]` cases: the `.bavatar` round-trips canonically with unknown keys preserved, resolves
  names to indices and refuses an unknown one naming it, and the reference scan sees its skeleton.
- `[skinning][plant]` cases on a synthetic two-bone leg: a foot on the floor and still is planted, a
  lifted or sliding one is not, the ramp is three frames, the chunk round-trips through the `.banim`,
  `TokenCanary_test` re-pinned beside the bump, and a signature mismatch measures at load.
- `[skinnedacquire]` cases: a rig with no avatar acquires as before; one with an avatar hands bgl
  its legs; a stale plant signature is measured rather than trusted.
- `just run bgl_tests -- --gpu-validation` on the pose-pass tasks (Metal:
  `METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1`).

## What the survey found

**The pose pass.** `libs/bgl/shaders/src/PoseSkinned.slang`: one group of `cPoseGroupSize = 64`
threads per instance, striding over bones. Local TRS is seeded into the palette slot (`:126-129`),
the hierarchy is walked one depth level at a time with a `DeviceMemoryBarrierWithGroupSync` per
level (`:139-151`), then each slot is multiplied by its inverse bind in place (`:155-160`). Between
`:151` and `:155` every slot holds a model-space transform; nothing else in the frame has them.
`main` calls `PoseInto` twice, at `time` and `prevTime`, into two back-to-back slices
(`:180-185`); the slice is allocated at spawn (`SceneView.cpp:297-299`). The pass reads `SkinnedState`
only (`libs/bgl/idl/src/SkinnedState.slang`: `geom, clip, phase, rate, palette`) and has no mesh
buffer bound (`SkinnedPosePass.cpp:78-84`). Every barrier is group-uniform by construction and the
comment at `:114-117` states the rule a new loop must keep.

**What the pass knows about the world: nothing.** `Mesh.slang` is `transform, submeshes, playback`;
the transform is applied in `skinned_vertex.slang:87` and `:95`, the same one for both palettes. No
previous transform exists anywhere, and none is needed: there is no transform mutator on
`ISceneView`, and `static_vertex.slang:19-21` says so in a comment.

**Per-view constants** reach a pass through reflected cbuffer mirrors (`docs/uniforms.md`); the
pose pass writes its own `gUniforms` (`SkinnedPosePass.cpp:77-87`), so a ground plane is two more
members there. `DrawData` carries the view, and `SceneView::GetScene()` reaches the scene.

**Scene-level state.** `IScene` holds geometry and materials; environment, skybox and exposure are
per view (`ISceneView.h:136-175`, "lighting is a per-view concern"). No ground concept anywhere;
`AddPlaneGeom` is ordinary geometry.

**What a bone carries.** `assetlib::Bone` (`Skeleton.h:16-24`): `bindPose`, `inverseBind`, `parent`,
`nameOffset`; names are in the `.bskel`'s string pool and `findBone` (`skinning.h:54-56`) resolves
one. GPU side `SkinnedBone.slang` is `inverseBind, parent, depth`; `SkinnedGeom.slang` is
`bones, samples, clips, boneCount, maxDepth`. No spare range, no tag; `docs/skinning.md` records that a
bone tag would need a `.bskel` format change — which ADR-3 avoids by resolving at load. `Clip.slang`
is `firstFrame, frameCount, sampleRate, loop`, shared with VAT, "so per-clip metadata can grow in one
place"; frames are dense in the pool, so a per-frame side table addresses by `firstFrame + f` with no
new field.

**Upload and acquire.** `Scene::AttachSkinnedRecords` (`Scene.cpp:818-870`) derives depth, packs
samples and clips into `m_SkinnedBones / m_BoneSamples / m_Clips` under a `GeomRollback`;
`ValidateSkinnedRig` (`:750-810`) is where a refusal is thrown. `AssetManager::AcquireSkinnedMesh`
(`AssetManager.cpp:596-720`) reads the clip set, the skeleton it names and the mesh, checks both
signatures, reads or measures the posed box, and calls `AddSkinnedMeshGeom`. `gamelib` links both
sides and is the only place a name can be resolved against a rig before upload.

**Authored documents.** One codec per container in `codecs.h`, listed in `Containers`
(`container_table.cpp`) under a static assertion against `AssetType::kCount` (`asset_refs.h:9-27`).
An authored codec declares neither token nor magic (`AssetCodec<BMaterial>`, `codecs.h:83-94`).
Per-type switches with no `default:` live in `migrate.cpp:131`, `reimport.cpp:155`,
`asset_rename.cpp:118`, `pak_pack.cpp:182` and `:326`. The reference scan derives a `.bimport`'s
source edge by convention (`asset_refs.cpp:110`, `importedSourceKeyFor`). Required directories are
the 11 in `project_layout.h:60-72`; `Authored/Skeletons` is not among them. The `.bmaterial`
precedent is `bmaterial_io.cpp` over `json_doc.h`: canonical JSON, unknown keys kept.

**The cook.** `groundClips` is called from four places, each with the meshes in hand:
`asset_import.cpp:432` (import with a mesh), `bakeBoundsForRig` `:549` (clips-only import, every
`.bmesh` naming the rig), `AssetStore_Regen.cpp:351` (load-time regen), `reimport.cpp:128-144`
(`Reimport`). `measureClipFloors` (`skinning.cpp:794-830`) prunes frames and keeps no poses;
`sweepPoses` (`:357-372`) is the one walk of every frame, shared across mesh entries by
`bakePosedBounds` (`:723`). `posedBoxes` is the precedent for a derived, self-keyed, optional
`.banim` chunk (`banim_io.cpp:81-82`, `Animation.h:59-65`, `findPosedBounds`), with the
value-initialisation hazard at `skinning.cpp:740-742`. The `.banim` token is
`0x107bc43fdbd09c69` (`codecs.h:169`); any new chunk bumps it and re-pins `TokenCanary_test`.
Nothing per-frame exists in the `.banim` today.

**Tests.** `SkinnedPose_test.cpp` reads the palette back (`ReadPalette`, `:194`) and checks
hand-computed points on a `MakeChain` rig; `SkinnedRender_test.cpp:564` is the motion-vector case
(`ReadMotionVectors`, magnitudes deliberately unpinned); goldens are `assets/golden/*.got.png` via
`util/GoldenImage.h`. `ClipFloor_test.cpp:11-135` is a one-bone rig with hand-assembled frames —
the fixture the plant cases extend to a leg. `gamelib/tests/src/SkinnedAcquire_test.cpp` builds a
project through `util/RigFixture.h`. No test can reach the real Dog: no rig lives under `assets/`.

**The rigs.** Both test rigs are quadrupeds from one pack family, four legs each, every leg a direct
parent chain: Dog `Thigh → Calf → Foot → Toe0` and `UpperArm → Forearm → Hand → Finger0`, Coyote the
same words under its own prefix. The Dog is not in `./test-project` yet (Coyote, angelica,
cha800_00 are); `Dog.glb` is at `../../resources/glb-specular/Dog.glb`.

## What changes

| Where | What | What could break |
|---|---|---|
| `bgl_intfc/IScene.h`, `Scene.h/.cpp` | `GroundPlaneDesc { point, normal }`, `SetGround` (throws on a non-finite or zero normal, or one with `normal.y <= 0` — no height exists under a point; bumps the temporal epoch, ADR-10), `GetGround` | nothing existing reads it until task 3; from then it is a pose input |
| `idl/SkinnedState.slang`, `SceneView.cpp` | `float4x4 transform` written at spawn | playback arena record size grows 64 B; the arena is raw bytes and sized per record |
| `idl/SkinnedLegChain.slang`, `SkinnedGeom.slang`, `Scene.cpp` | per-geom leg table `{hip, knee, ankle, toe, solePoint, soleNormal}` and a packed plant-weight range; `FootPlantDesc` on `AddSkinnedMeshGeom` (defaulted empty); validation of chain parent links, bone range, weight count = frames × legs | a rig with an indirect chain is refused at upload; an empty desc changes nothing |
| `PoseSkinned.slang`, `SkinnedPosePass.cpp` | ground uniforms; `SampleGround`; per-leg solve between the walk and the inverse bind; groupshared old/new for solved bones; descendants-follow pass; pelvis drop | every new barrier must be group-uniform (`:114-117`); a rig with no legs must pay one skipped branch and no barrier count change |
| `forward` shaders | none — the palette is already the whole interface | |
| `assetlib_structs/Avatar.h`, `assetlib/avatar.h`, `src/bavatar_io.cpp` | `Avatar { legs[] {hip, knee, ankle, toe} }`, codec, `c_AvatarExtension`, `AssetType::kAvatar`, `Authored/Skeletons`, `avatarKeyFor` / `avatarSkeletonKeyFor`, `resolveLegChains` | every per-type switch gains a case or does not compile; `c_RequiredDirectories` grows to 12 |
| `asset_refs.cpp`, `asset_rename.cpp` | `kAvatarSkeleton` edge by convention; a `.bavatar` cannot be renamed alone (the `.bimport` rule, `asset_rename.cpp:186-190`), and a renamed `.bskel` — or a renamed directory under `Derived/Skeletons` — moves its avatar with it, because the pair straddles the two halves and no directory rename can carry both | a project deleting a rig under an avatar is blocked by the edge |
| `skinning.h/.cpp`, `Animation.h`, `banim_io.cpp`, `codecs.h` | `solePlanes`, `measurePlantWeights`, `bakePlantWeights`, `findPlantWeights`; `AnimationSet::plantWeights { signature, legCount, weights }`; new chunk; token bump | every project's `.banim` re-cooks once; `TokenCanary_test` re-pinned |
| the four `groundClips` call sites | `bakePlantWeights` after grounding, when the rig has an avatar | a cook without an avatar is unchanged |
| `assetlib_cli describe` | prints the legs a rig authors and how many frames each plants | |
| `gamelib/AssetManager.cpp` | acquire reads the avatar by convention, resolves, fits soles, finds or measures windows, fills `FootPlantDesc` | an unresolvable name refuses the acquire naming it |
| `apps/editor` `AnimationPreviewWindow` | a ground-slope control that sets the scene ground and tilts a plane geom under the rig | the panel is untestable (`docs/skinning.md`); `PlanAnimationLoad` is untouched |
| `docs/skinning.md`, `docs/taa.md`, `ROADMAP.md:175-177` | a Foot IK section; the ground listed among the rebinds the epoch counts; the roadmap line says position *and* orientation | |
| `./test-project` | `Dog.glb` imported; `Dog.bavatar` and `Coyote.bavatar` authored | a re-bake of every `.banim` in the project, discarded by `ws done` |

## The tasks, in order

Bottom-up: `bgl` against hand-built rigs, then `assetlib`, then the `gamelib` seam, then the editor
and the project. Each task is one PR into `feat/foot-ik`; each names its gate.

1. **`feat(bgl): a scene ground plane, and the instance transform in its playback record`** —
   ADR-2, ADR-7. `SetGround` / `GetGround`, the two uniforms written by the pose pass, and
   `SkinnedState::transform`. Two ADRs in one PR on purpose: they are the pose pass's two new
   inputs, each a few dozen lines, and the same re-run cases prove both inert. Nothing consumes
   either yet; said so in the PR body. *Gate:*
   `[skinned]` cases for `SetGround`'s refusals and the normalised read-back; every existing
   `[skinned][pose]` case re-run with a tilted ground set and still matching, which is what proves
   the scaffolding is inert.

2. **`feat(bgl): leg chains and plant weights upload with a skinned geom`** — the `FootPlantDesc`,
   the IDL leg record, the two scene buffers, and `ValidateSkinnedRig`'s new refusals. Dead until
   task 3; the tests call it. *Gate:* `[skinned]` cases reading the leg table back, refusing an
   out-of-range bone, an indirect chain, and a weight span of the wrong length; an empty desc
   uploads exactly what it did before.

3. **`feat(bgl): plant a foot on the ground in the pose pass`** — ADR-1, ADR-6, ADR-8 without the
   pelvis drop: the plant weight sampled at the fractional frame, `SampleGround`, the two-bone solve,
   the ankle rotation with its clamp, descendants following. `docs/skinning.md` gains its Foot IK
   section, `docs/taa.md` lists the ground among the rebinds, and `ROADMAP.md:175-177` is amended
   here. *Gate:* the `[skinned][pose]` readback cases
   in Acceptance minus the pelvis drop; the slope golden and the extended motion-vector case;
   `--gpu-validation`.

4. **`feat(bgl): drop the pelvis when a leg cannot reach`** — the rest of ADR-8: deficits into
   groupshared, the largest lowers every bone along the ground's up, both legs re-solve. *Gate:* a
   readback case with one target beyond reach: the root drops by the deficit and both soles rest;
   `--gpu-validation`.

5. **`feat(assetlib): the .bavatar document`** — ADR-3: struct, codec, type, extension, directory,
   the per-type switches, the reference edge, the rename rule, `resolveLegChains`, `describe`.
   *Gate:* the `[avatar]` cases in Acceptance; `[refs]` and `[rename]` cases for the edge and the
   move; `Container_test` and the codec table assertion compile.

6. **`feat(assetlib): sole planes and plant weights from the clip walk`** — ADR-4, ADR-5:
   `solePlanes`, `measurePlantWeights`, the `.banim` chunk, `bakePlantWeights` at the four call
   sites, `findPlantWeights`, the token bump. The stage opens a `core::logging::ScopedStage` like
   its two siblings (`skinning.cpp:684`, `:842`), with bones, legs, clips and frames as its
   dimensions — it is a third, unprunable walk of every frame, on the cook's largest stage.
   *Gate:* the `[skinning][plant]` cases in Acceptance; `[grounding]` cases unchanged;
   `just run assetlib_tests -- "[perf]" --no-lock` unchanged; and the cook delta measured on
   `cha800_00` (2254 frames, 663 bones, debug and release) in the PR body, against the 14 s
   grounding and 3.5 s bounds figures in `docs/plans/pose-bounds-perf.md` — the `[perf]` cases
   assert scaling, not wall clock, so they cannot see a whole extra walk.

7. **`feat(gamelib): the acquire reads the avatar and plants the rig`** — the seam: avatar by
   convention, resolve, fit, find-or-measure, `FootPlantDesc`. *Gate:* the `[skinnedacquire]` cases
   in Acceptance.

8. **`feat(editor): a ground slope in the animation preview`** — the control and the plane; the
   Dog imported into `./test-project` with `Dog.bavatar` and `Coyote.bavatar` authored on the
   project branch; the table re-measured on both rigs with the plane-fit probe and recorded in the
   PR body. *Gate:* the Acceptance table; `editor_tests` unchanged.

9. **`docs(skinning): move what outlives foot IK into the doc and drop the plan`** — whatever of
   this file describes how the code now behaves goes to `docs/skinning.md` (bcp-docs shape); the
   plan is deleted so the landing PR carries the deletion. *Gate:* the docs index in `CLAUDE.md`
   still resolves every link; nothing in `docs/` names this plan.
