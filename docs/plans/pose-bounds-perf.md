# pose-bounds-perf — implementation plan

## Context

Rebuilding a project's derived geometry is slow enough to be noticed in the debug editor, and posed
bounds gets the blame because it is the only stage in that phase carrying a `ScopedStage`. It is not
the cost. Regenerating `Derived/{Meshes,Skeletons,Animations}` for `cha800_00.reduced.glb` (97 MB,
663 bones, 27 entries, 170k vertices, 5 clips / 2254 frames), debug build:

| | debug | release |
|---|---|---|
| glTF parse ×3, one per output kind | 13 s | 0.62 s |
| tangents ×2 | 0.77 s | 0.02 s |
| `groundClips` — no stage line, so invisible | **79 s** | **0.74 s** |
| `bakePosedBounds` | 3.6 s | 0.13 s |

The whole `migrate` of the test project — three sources, textures already baked — is 99 s of which
79 s is that one uninstrumented stage.

Inside `groundClips`, `sample` over the whole phase attributes 69,255 samples:

| | samples | share |
|---|---|---|
| `exactFloor` — skinning every vertex of a surviving frame (`skinning.cpp:778`) | 65,510 | 94.6% |
| `boundedFloor` — the bound sweep over all frames (`:763`) | 2,770 | 4.0% |
| `poseModelTransforms` for surviving frames (`:782`) | 709 | 1.0% |
| `skinningMatrices` (`:780`) | 254 | 0.4% |
| influence decode ×3 | ~100 | 0.15% |

`skinning.h:220` and `docs/skinning.md:88-92` both claim the prune leaves "a handful of frames per
clip actually skinned". Against `exactPosedBounds`' documented ~360 s for all 2254 frames, 82 s of
exact skinning is roughly 514 frames — about a quarter of the clip set. The frame-level prune stops
cutting because a bone box is 1.09–1.51× loose, so on a rig standing still the lower bound sits below
the true floor for hundreds of frames.

## Decisions

- **ADR-1 — Apply the convexity bound per vertex, not only per frame.** A skinned position is a
  convex combination of its bones' products with weights summing to one, so no vertex can fall below
  the lowest of its own bones' boxes — the values `boundedFloor` already computes each frame. A
  vertex whose every bone sits at or above the best floor found so far cannot lower it and is never
  skinned; on a standing character only the feet qualify. This is the *same* inequality the frame
  prune rests on, applied at finer granularity, so it introduces no approximation the cook did not
  already accept. *Rejected: capping frames per clip, or grounding on the conservative bound alone
  — both move the floor, and the bound's 1.09–1.51× slack would sink every clip below the plane.*

- **ADR-2 — Withdrawn: the source is still parsed once per output kind.** This was built and
  measured — source-major with a deferred second round, three parses down to one, ~9 s of the debug
  rebuild — and then #515 landed, restructuring `Reimport` into per-stage `parallelFor` cooking with
  a progress count fixed up front. Both halves of the decision died with the old shape: the
  stage-major loop the change replaced is now what the parallelism and the item count are built on,
  and the serial 14 s the change was justified by is now overlapped across threads and unmeasured.
  Redoing it against the new base is a different change with a different measurement, so it is not
  folded in here on the strength of a number that no longer describes the code. See the non-goals.

- **ADR-3 — `groundClips` gets a `ScopedStage`, and the docs get the number they are missing.**
  The stage line beside it is why the wrong stage was blamed for a year of this cost. The "handful of
  frames" claim in `skinning.h` and `docs/skinning.md` is false on a dense standing rig and is
  corrected; `docs/skinning.md`'s bake figures are labelled as the debug numbers they are, against a
  release measurement 27× cheaper. *Rejected: leaving the docs to be fixed by whoever next measures,
  which is how the claim survived.*

- **ADR-4 — The posed-bounds bake stays where it is.** It is the culling volume for every skinned
  geom: `AcquireSkinnedMesh` hands it to `AddSkinnedMeshGeom` (`AssetManager.cpp:688-703`) and
  `Scene.cpp:909` derives every submesh's sphere from it. It is 4.6% of the rebuild, and not baking
  it does not remove the walk — the acquire path falls back to `assetlib::posedBounds`
  (`AssetManager.cpp:698`), moving it onto a blocking load. *Rejected: dropping the bake, and
  replacing the sweep with a static box after Unreal's `PhysicsAsset` bounds or Unity's authored
  `localBounds` — both need an authoring pass this project has no home for, and `bgl` cannot measure
  a box itself (`docs/skinning.md:209`).*

## Non-goals

- Fusing the grounding sweep with the posed-bounds sweep. It was 4.0% of grounding when this was
  written, and sharing one walk means reshaping two public entry points that each have one obvious
  job — an ADR against the strict `libs` bar, taken against a projection rather than a measurement.

  Re-measured after ADR-1, it is the next thing: what is left of char800's cook is grounding's 14 s
  and the bake's 3.6 s, and both are now dominated by the same `poseModelTransforms` walk over the
  same 2254 frames — 48% of grounding, and most of the bake. Still out of scope here, and now backed
  by a number rather than a guess.
- Animation compression and quantization — [docs/specs/animation_compression.md](../specs/animation_compression.md).
- Parsing an import source once per rebuild — ADR-2, withdrawn above. It wants a fresh design and
  a fresh measurement against #515's parallel stages.
- Texture transcode, which is the actual majority of a full `migrate` wall clock (46 s of 46 s in
  release) and is untouched here.
- Changing what a posed box or a clip floor *is*. Both keep their current values.

## Acceptance

- `just test assetlib` — the existing `[grounding]` cases pin the floors, including the authored
  override and the idempotence case, and must not move.
- A new `[perf][grounding]` case: padding a rig with vertices that cannot be the lowest must not
  make measuring its floor cost more. That is the shape ADR-1 buys, and it fails on today's code.
- `just run assetlib_tests -- "[perf]" --no-lock` and the full suite.

## Commits

1. `docs(plans): plan a rebuild that stops skinning what cannot be lowest` — this file.
2. `perf(assetlib): a clip floor skins only the vertices that could be lowest` — ADR-1 and ADR-3.
   Gate: `just run assetlib_tests -- "[grounding]" --no-lock`, and the new `[perf][grounding]` case.
