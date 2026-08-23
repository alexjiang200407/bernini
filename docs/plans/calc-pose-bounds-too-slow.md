# calc-pose-bounds-too-slow — implementation plan

Three costs found while making the posed-bounds bake usable on an AAA rig, none of which the bake
itself was responsible for. The feature is what stands between a 663-bone character and an editor
that can open it.

## Context

`resources/cha800_00.glb` — 663 bones, 27 skinned mesh entries, 170,131 vertices, 2,254 frames at
30 Hz — is the asset that started this. Two commits already on `feat/calc-pose-bounds-too-slow` took
the posed-box bake from ~6 minutes to 3.5 s and the read from 740 ms to 29 ms. Opening the rig is
still unusable, and for three reasons that live elsewhere:

- **`AcquireSkinnedMesh` re-reads its containers on every acquire.** The Animation panel acquires
  one geom per animated mesh entry, so this rig deserializes the 60 MB `.banim` and the 14 MB
  `.bmesh` 27 times: **26.5 s measured**, on the render thread, and every one of those acquires then
  throws on the bone ceiling below.
- **`cMaxBonesPerRig` is 192** and the rig has 663, so `AddSkinnedMeshGeom` refuses and the panel
  shows a bind pose. 337 of those bones carry skin weight and 545 are needed once dead subtrees are
  pruned, so no import-side cleanup reaches 192. The ceiling has to go.
- **`.banim` stores raw resampled float TRS.** 663 bones × 2,254 frames × 40 bytes is 59.7 MB, which
  is the file exactly. **75% of its tracks never change** — `blink_loop` is 97% constant, a
  six-second clip storing every bone at every frame so that 53 tracks can move.

All timings are `macos-clang-metal-debug`.

## Decisions

- **ADR-1 — `AssetManager` caches the containers it reads, keyed by mount key.** It is already the
  caching layer: it keys geoms by path in `m_GeomByPath`. A container cache belongs beside that, and
  it fixes every caller rather than the one panel that happens to hurt. *Rejected: passing the
  already-loaded containers into `AcquireSkinnedMesh`, because it inverts the asset layer — the
  caller would own reads the asset layer exists to own — and only helps callers that already read.*

- **ADR-2 — the cache re-stamps on every get and reuses only an unchanged stamp.** The editor writes
  `.banim`/`.bmesh` through `assetlib` directly, so the cache cannot see writes and must not trust
  itself. `AssetStore::StampOf` already exists. Of the 810 ms a `.banim` get costs, only ~20–50 ms is
  the read and hash; the rest is the schema-converting deserialize, which is what the cache skips.
  *Rejected: explicit `Invalidate(path)` from the write paths, because correctness would rest on
  every present and future writer remembering, and a missed one serves a stale rig with no symptom.
  Rejected: a stat-only size+mtime stamp, because `AssetStore` already owns one staleness concept and
  mtime lies across a `git checkout`.*

- **ADR-3 — the pose is composed in the palette buffer, not in groupshared.** A bone's model
  transform is affine, so it fits the three `float4` rows (`cFloat4sPerBone`) the palette already
  reserves for its skin matrix. Write each local transform into its own palette slot, compose depth
  level by level in place, then multiply by the inverse bind in place. The bone count stops being
  bounded by anything but memory the palette already owns. *Rejected: a per-instance scratch device
  buffer, which costs VRAM sized against a worst case for the same result. Rejected: packing
  `gModel` to three rows and raising the constant to the ~682 the 32 KiB groupshared limit allows,
  because it is a bump rather than a fix, it collapses occupancy to one group per core, and the next
  asset breaks it again.*

- **ADR-4 — posing stays on the GPU, against the standard.** Unreal and Unity both evaluate the pose
  on CPU worker threads and upload a finished palette; almost no engine walks a skeleton hierarchy in
  a compute shader. `ROADMAP.md` § Guiding Constraints rules that out here — "GPU-driven by default"
  and "per-unit CPU updates are the enemy" — and the skinned tier exists to carry thousands of
  instances. *Rejected: CPU posing, which is the industry standard and contradicts a constraint the
  repo made knowingly.*

- **ADR-5 — compression collapses constant tracks and nothing else.** A track whose TRS does not
  change across a clip stores one sample. 75% measured on this rig, and it cuts the pose walk and the
  bounds bake by the same fraction with no precision loss anywhere. *Rejected: quantized rotations
  and translations (~10x rather than ~4x), whose error has to be bounded and tested per bone and is
  most of the work. Rejected: ACL-style per-track variable bit rate — the actual standard, what
  Unreal ships as its default codec — which is a subsystem, not a task, and would dwarf this
  feature.*

- **ADR-6 — the `.banim` major bumps and every clip set is re-baked, so an un-re-baked file fails
  loudly.** Not because carrying both shapes costs anything: a constant-track table is an *additive*
  field, and since the asset-schema feature the converter defaults an absent one for free
  ([docs/asset_schema.md](docs/asset_schema.md) — "shape changes cost no code"), with "absent" already
  meaning "nothing is constant". The reason is that the alternative's failure mode is silence — a
  project that missed the re-bake keeps loading, keeps paying the full sample pool, and nothing says
  so. Re-baking is scripted and idempotent. *Rejected: the additive default, which is free and is
  what the schema system exists for, because a clip set that is quietly still four times its size is
  worse than one that refuses to load. Note this is not the `.bmaterial` 9→10 precedent, which bumped
  for a change of `SourceStamp`'s **meaning** and does not apply to an added field.*

- **ADR-7 — `cha800_00` is licensed for education only: it is never committed and never enters a
  test.** Its imported data already sits untracked-but-unignored in `bernini-test-project`, where the
  re-bake ADR-6 requires would sweep ~75 MB of it into a commit. *Rejected: fixtures built from the
  real asset, which is what makes a >192-bone golden image convenient.*

## Non-goals

- **Animation quantization of any kind**, and anything ACL-shaped. ADR-5 draws the line at constant
  tracks.
- **Pruning weightless bones at import.** Measured: only 118 of 663 have no weighted descendant, so
  it saves 10 MB of a 60 MB file and moves 663 to 545 — it neither fixes the ceiling nor competes
  with ADR-5.
- **The glTF parse**, 29.4 s of this asset's import and the dominant step once this feature lands.
- **The VAT bake**, which needs every vertex at every frame by construction.
- **LODs, and any change to what the skinned tier draws.** This feature changes how a pose is
  computed and how containers are read, never what appears.
- **A general asset cache.** ADR-1 caches the three container types `AcquireSkinnedMesh` reads —
  `BMesh`, `Skeleton`, `AnimationSet` — and nothing else. A task-1 diff that starts caching
  materials, textures or environments is outside this feature.

## Acceptance

- A `>192`-bone **synthetic** rig poses correctly, proven headlessly by a golden image in
  `bgl_tests`, and every existing skinned golden image is unchanged.
- The new palette-composition path is measured against the current groupshared one on an existing
  small rig, and the cost is written into the task's PR body. Moving the hierarchy out of groupshared
  will cost something; landing it without knowing what is not acceptable.
- A second acquire of the same clip set performs no second deserialize, proven by a counting
  `core::file::IFileSystem` — the read seam `docs/archives.md` already defines — and a rewritten file
  is still picked up.
- A clip set with constant tracks round-trips to bit-identical poses, and the test project's
  re-baked `.banim` files are materially smaller.
- The user confirms `cha800_00` animating in the editor. It cannot be committed or tested against
  (ADR-7), and screenshots are unavailable in this environment, so this gate is theirs alone.

## What the survey found

- `AssetManager::AcquireSkinnedMesh` (`libs/gamelib/src/AssetManager.cpp:466`) checks
  `m_GeomByPath` for `"{path}#{meshIndex}#skinned"`, and on a miss calls `m_Store.LoadAnimations`,
  `m_Store.LoadSkeleton` and `m_Store.LoadMesh` before anything else. Each key is distinct per mesh
  entry, so every entry is a miss.
- `AssetStore::LoadAnimations` (`libs/assetlib/src/AssetStore_Containers.cpp:35`) is a straight
  `loadAnimations(*m_Files, path)` — no cache anywhere beneath it. Measured on the imported rig:
  `.banim` 810/781/761 ms on three consecutive loads, `.bmesh` 246/218/213 ms. The page cache does
  not help, because the cost is the deserialize.
- `PoseSkinned.slang:42` holds `groupshared float4x4 gModel[cMaxBonesPerRig]` — 192 × 64 B = 12 KiB.
  `PoseInto` fills it with local transforms, composes with one `GroupMemoryBarrierWithGroupSync` per
  depth level up to `geom.maxDepth`, then writes three rows a bone into `bonePalettes`. The bound is
  group-uniform and nothing returns early, which is what makes the barriers legal — a property any
  replacement has to keep.
- `SkinnedState.slang:23` shows the palette holds **two** poses an instance, current and previous
  (`docs/skinning.md`: the previous pose is re-evaluated, not remembered), so `PoseInto` runs twice
  and each run has its own three-rows-a-bone half to compose in.
- `Scene.cpp:727` is where the ceiling is enforced, and the message in the editor's dialog comes
  from there.
- `AnimationPreviewWindow.cpp` reports `"Reading the pose bounds..."` over a block whose real cost is
  `store.LoadAnimations`; the bounds read inside it is now 29 ms of ~810 ms. The label is wrong and
  is why this was reported as slow bounds.

## What changes

| Where | What |
|---|---|
| `libs/gamelib/include/gamelib/AssetManager.h`, `src/AssetManager.cpp` | a stamp-checked container cache; `AcquireSkinnedMesh` reads through it |
| `libs/bgl/idl/src/Constants.slang` | `cMaxBonesPerRig` retires; `cFloat4sPerBone` stays |
| `libs/bgl/shaders/src/PoseSkinned.slang` | `PoseInto` composes in the palette slice instead of `gModel` |
| `libs/bgl/src/scene/Scene.cpp` | the bone-count refusal goes; its test goes with it |
| `libs/bgl/include/bgl/IScene.h`, `src/scene/Scene.h` | two `@throws`/`ValidateSkinnedRig` comments name the retired constant |
| `libs/assetlib_structs/.../Animation.h`, `libs/assetlib/src/banim_io.cpp`, `gltf_skin.cpp` | a per-clip constant-track table; the importer writes it |
| `libs/assetlib/src/skeleton.cpp` | `poseModelTransforms` resolves a constant track to its one sample |
| `apps/editor/.../AnimationPreviewWindow.cpp` | the progress label says what it is doing |

**What could break.** The palette is read by the mesh shader and now written *and read* by the pose
pass within one dispatch, so the depth-level barriers become device-memory barriers; getting that
wrong is a race that a golden image may pass by luck. GPU validation is the gate. Retiring
`cMaxBonesPerRig` removes a refusal that `SkinnedGeom_test.cpp:407` asserts. Constant tracks change
what `poseModelTransforms` indexes, which is the function the bounds bake, the VAT bake and the CPU
reference all walk — a wrong index there is silently wrong everywhere.

## Tasks

Bottom-up would put `bgl` first. Task 1 leads anyway: it is a plain defect, it is the cheapest thing
standing between the user and an editor that opens this asset, and nothing below depends on it.

1. **`perf(gamelib): read a clip set once per acquire, not once per mesh entry`** — ADR-1, ADR-2.
   Gate: a `gamelib_tests` case counting reads through a stub `core::file::IFileSystem` proves the
   second acquire opens no file, and a case proving a rewritten file is picked up.
2. **`feat(bgl): compose a pose in the palette, so a rig may have any number of bones`** — ADR-3.
   Gate: `just run bgl_tests -- --gpu-validation`, a golden image of a synthetic rig past the old
   ceiling, every existing skinned golden image unchanged, and the measured cost against the
   groupshared path in the PR body.
3. **`feat(assetlib): store a clip's unchanging tracks once`** — ADR-5, ADR-6. Gate:
   `assetlib_tests` proves a constant-track clip round-trips to bit-identical poses and that the
   pool shrinks; the test project is re-baked and committed, `cha800_00` excluded (ADR-7).

Ahead of all three, and not a task because it is in another repository: **git-ignore `cha800_00` in
`bernini-test-project`** so task 3's re-bake cannot commit it.

## How this branch differs from the skill

`feat/calc-pose-bounds-too-slow` is not an empty integration branch: it already carries two commits
that arrived directly, and `#457` is open against `master` from it. Rather than rewind reviewed work,
`#457` becomes the feature's landing PR and each task lands as its own PR *into* this branch, so the
tasks accumulate in `#457`. Its body is updated as they land.
