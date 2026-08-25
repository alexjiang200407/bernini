# bmesh-skeleton-signature — implementation plan

## Context

[skinning.h:28](../../libs/assetlib/include/assetlib/skinning.h) states the invariant this change
makes true:

> A clip set **and a skinned mesh** both address a skeleton by bare index, and nothing about a wrong
> index is detectable from the pose it produces. So **each records the signature** of the rig it was
> built against.

A clip set does — `AnimationSet::skeletonSignature`, checked by `animationsMatchSkeleton`. A `.bvat`
does — `BVat::skeletonSignature`. **A skinned mesh does not.** `BMesh` carries `std::string skeleton`
and nothing else, and no mesh↔rig comparison exists anywhere in the tree.

The consequence is silent. Each container's cache key holds only its **own** bake token
(`AssetStore::GeometryIsStale` dispatches on the extension and compares `AssetCodec<T>::c_BakeToken`),
so bumping the skeleton's token regenerates the `.bskel` and leaves every `.bmesh` current. When that
re-cook changes bone ordering, naming or hierarchy — which is what #413's armature-node fix did to all
28 rigs — the clip set is refused with a message naming the rig, the VAT bake is refused, and the mesh
skins to the wrong bones with no error at all.

The same gap covers a second case that was never checked either: a mesh and a clip set naming
*different* rigs. `AcquireSkinnedGeom` loads the skeleton the **clips** name and never asks whether
the mesh agrees — its comment says *"The clip set names its own rig, so the pair cannot be mismatched
by a caller"*, and the mesh was simply not in that sentence.

## Decisions

- **ADR-1 — `BMesh` records `skeletonSignature`, in its own chunk.** *Rejected: deriving it at check
  time by loading the named `.bskel` and hashing it, which `rebake_bounds.cpp:114` already does for
  pairing. That is equivalent to no record at all: it hashes whatever rig is on disk **now**, so it
  agrees with itself by construction and can never witness the drift. The value has to be what the
  joint indices were cooked against, which only the cook knows.*
- **ADR-2 — One predicate, `meshMatchesSkeleton`, beside `animationsMatchSkeleton` in
  `skinning.h`.** *Rejected: comparing the two integers inline at each pairing site — three sites
  today, and it is how two of them start disagreeing about what "matches" means. The rule lives
  where its sibling already lives.*
- **ADR-3 — A zero signature is a mismatch, not an exemption.** This is the rule
  `animationsMatchSkeleton` already applies: `AnimationSet::skeletonSignature` defaults to 0 and
  compares unequal like any other value. *Rejected: reading 0 as "never recorded, therefore fine",
  which reinstates the exact silent hole for any file this build did not write. It is unreachable in
  practice anyway — ADR-4's token bump makes every pre-change `.bmesh` a cache miss that regenerates
  with a signature.*
- **ADR-4 — Bump `AssetCodec<BMesh>::c_BakeToken` and re-pin `TokenCanary_test`.** A chunk is a
  layout change, and the canary exists to fail one made without the bump.
- **ADR-5 — The check goes at the pairing sites, not inside `Deserialize`.** A `.bmesh` is a valid
  file on its own; the mismatch exists only when a mesh and a rig are brought together. *Rejected:
  refusing at load, which would make the mesh unopenable in the editor — the one place you need to
  see it in order to fix it.*
- **ADR-6 — On regeneration the signature comes from the freshly imported rig**
  (`group.import.skeleton`), not from the `.bskel` the mesh names. *Rejected: hashing the on-disk
  rig, which papers over a stale `.bskel` by definition — the regenerated mesh would agree with
  whatever is there rather than with what it was cut from.*
- **ADR-7 — This fixes the mesh↔rig edge only; cross-container bake tokens stay out of cache keys.**
  *Rejected: folding `AssetCodec<Skeleton>::c_BakeToken` into a skinned `.bmesh`'s key — Unity's
  import-dependency hash, and the more complete answer. It makes a bump **cascade**: one skeleton
  revision re-cooks every skinned mesh in the project, which is the wholesale re-import this
  project is deliberately moving away from. A loud refusal naming the mesh is the behaviour wanted,
  and it is what this delivers.*

## Non-goals

- **No cascade of bake tokens into dependent containers' cache keys** (ADR-7). Bumping the skeleton
  still leaves a `.bmesh` current; it now says so instead of skinning wrongly.
- **No change to `.banim`, `.bskel`, `.bvat`, or any authored document.** Only `.bmesh` gains a
  chunk, and only its token moves. A second bumped token in this diff is a bug.
- **No `bgl` change.** `idl::Clip`, `SkinnedState` and the pose pass are untouched.
- **Not the duplicate-`.bskel` import bug.** A mesh-on import of a second source writes its own
  `.bskel` rather than reusing a signature-matching one, which then makes `FindMatchingSkeleton`
  refuse every later clips-only import. Real, adjacent, and its own change.
- **No retargeting and no skeleton remap.** [skeleton_append.md](../specs/skeleton_append.md) stays
  a spec.
- **Not the derived-data commit rule.** Whether `.bmesh` and its siblings stay committed or become
  gitignored is a project-layout decision, and it neither blocks this nor is unblocked by it: a
  gitignored cache changes what a fresh clone starts with, while the drift here accumulates in a
  working tree git never saw either half of.

## Acceptance

- A test that cooks a mesh against one rig, reorders that rig's bones, and asserts the pair is
  **refused** — the negative that pins the hole this closes. Nothing today fails if the check is
  deleted, which is the point.
- A test that a mesh and a clip set naming different rigs are refused when acquired together.
- `TokenCanary_test` passes with `.bmesh`'s new token pinned to its new output hash, and **no other
  token moved**.
- `just test` green across all suites.

## Commits

1. `docs(plans): plan the mesh's record of the rig it addresses` — this file.
2. `feat(assetlib): a skinned mesh records the rig its joint indices address` — the `BMesh` field,
   the chunk, the token bump, the value set at import (`WriteImportedRig`) and at regeneration
   (`LoadRegenMesh`), and `meshMatchesSkeleton` beside its sibling. Docs in the same commit.
   Gate: `just test assetlib`, including the re-pinned `TokenCanary_test` and a round-trip that the
   signature survives serialize→deserialize.
3. `fix(gamelib,assetlib,editor): refuse a mesh posed by a rig it was not cooked against` — the
   check at the three sites that first bring a mesh and a rig together: `AcquireSkinnedGeom`,
   `bakeVat`, and the editor's animation preview.
   Gate: `just test gamelib editor` and the two acceptance tests above.
