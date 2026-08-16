# stamp-content-hash — implementation plan

## Context

`SourceStamp` is `{size, mtime}`
([libs/assetlib_structs/include/assetlib_structs/SourceStamp.h](../../libs/assetlib_structs/include/assetlib_structs/SourceStamp.h)),
and it is persisted into four formats: `.bmaterial` route stamps, `.bvat` mesh/skeleton/animations
stamps, and the `EnvMapRoute` stamps inside `.bsky` and `.benvl`.

A `git pull` or `git checkout` in `bernini-test-project` rewrites source mtimes without changing a
byte. Every bake then reads stale, re-bakes, and rewrites a container that is tracked in git — the
working tree shows `M Data/Materials/coyote_skinned/Coyote.bmaterial` today from exactly that, and
`.bmaterial` is an LFS-tracked binary, so the churn is not free to carry.

A persisted mtime is also machine-local: the same bake run on two machines produces byte-different
output, so a re-bake can never be reproducible.

## Decisions

- **ADR-1 — `SourceStamp` becomes `{uint64_t size, uint64_t hash}`.** Staleness is a question about
  content, so a checkout cannot fabricate one and a bake becomes byte-reproducible across machines.
  *Rejected: keeping mtime as a persisted fast path alongside the hash, because after a checkout the
  mtime never matches again — so it hashes on every check anyway, while keeping a machine-local
  field inside a committed file.*

- **ADR-2 — This reverses the rejection recorded in `SourceStamp.h`.** That comment rejected content
  hashing on cost ("hashing megabytes of texture … would cost more than the bake it is trying to
  avoid"). ADR-4 is what makes the cost affordable. The comment is amended here rather than left to
  contradict the code.

- **ADR-3 — Hash with `core::hash_bytes`, streamed in fixed-size chunks.** FNV-1a already lives in
  [libs/core/include/core/hash.h](../../libs/core/include/core/hash.h) and is seeded so calls chain,
  so a 100 MB texture is hashed without ever being resident. *Rejected: xxhash from vcpkg — roughly
  ten times faster, but a new dependency and a second hash function doing what core already does.*

- **ADR-4 — `stampOf` memoizes, keyed by `(path, size, mtime)`.** mtime is demoted from a persisted
  field to a runtime cache key. A file edited externally changes its mtime, misses the key and is
  re-hashed, so nothing goes stale-blind. *Rejected: hashing on every call, because `asset_describe`
  walks every material in a project and would re-read a shared texture once per material that routes
  it.*

- **ADR-5 — Bump the format majors and let existing files fail to load.** `.bmaterial` 9→10, `.bvat`
  1→2, `.bsky` 1→2, `.benvl` 1→2. `SourceStamp` stays 16 bytes, so the byte layout is unchanged and
  only the *meaning* of the second field moves — which is precisely the case a version guard exists
  to catch, since a silent reinterpretation would read an mtime as a hash and call every bake fresh.
  *Rejected: widening the readers to accept both layouts, because the assetlib "modify existing
  assets" ability is coming and will own the upgrade properly.*

- **ADR-6 — The two make-style "output newer than input" checks keep their mtime ordering, via a new
  `mtimeOf`.** `material_bake.cpp`'s `isUpToDate` and `env_bake.cpp`'s `bakeRoute` skip a re-encode
  when the target's mtime beats the source's. They read `SourceStamp::mtime`, so ADR-1 forces the
  question; they now call `mtimeOf` (`src/fs_util.h`) directly. Mtime is the right tool here and a
  stamp is not: the target records nothing about what produced it, so "is this map current" is a
  question about *when*, not *what*. *Rejected: comparing `stampOf(source)` against the material's
  `routeStamps` — tried, and it broke map reuse across materials. `routeStamps` is empty for a bake
  composed from a graph, so every such bake re-encodes a map another material had just written;
  `MaterialBake_test.cpp`'s "nothing changed: the existing map is reused" catches it.* The residual
  cost is a needless re-encode after a checkout, which produces a byte-identical file and so dirties
  nothing.

- **ADR-7 — This repo's own committed bakes are migrated with zeroed stamps, not re-baked.**
  `assets/Sky/forest.bsky`, `assets/EnvLighting/forest.benvl` and the two
  `assets/Materials/apples/*.bmaterial` are fixtures the suites load, so ADR-5's break would take CI
  with it — and their sources are not in this repo, so they cannot be re-baked. A one-shot script
  bumped each major and zeroed the stamps. Zero is the honest value: it reads as never-stamped,
  which is the verdict a missing source already produced, so no test's meaning moves. *Rejected:
  reinterpreting the old mtime bits as a hash, which would leave a committed file asserting a
  provenance that was never measured.*

## Non-goals

- The assetlib migration/upgrade ability for existing assets. It comes later, and until it lands
  `bernini-test-project` will not open.
- Re-baking `bernini-test-project`.
- Caching the stale *verdict* in `CachedMaterial`. It stays re-derived per call, for the reason its
  own header gives.
- `.bvat` LFS tracking in `bernini-test-project` — wanted, but that is a separate repository and
  lands as its own commit there.

## Acceptance

- `just test assetlib`, chiefly the negative case: bake, advance a source's mtime without changing a
  byte, and assert `bakeIsStale`, `vatIsStale` and the env staleness check all return **false**.
  That is the bug, stated as a test.
- `just test core` — a file hashed in chunks equals the same bytes hashed in one call, and a file
  whose mtime moved but whose content did not hashes the same.
- Round-trip serialization at the new major for all four formats.
- `just test` in full: `assets/Sky/forest.bsky`, `assets/EnvLighting/forest.benvl` and the apple
  materials are loaded by the bgl, gamelib and assetlib suites, so ADR-7's migration is gated by all
  of them rather than by an assertion of its own.

## Commits

1. `docs(plans): plan stamping bake sources by content` — this file.
2. `feat(core): hash a file without making it resident` — `core::file::hash_file`, streaming
   FNV-1a over fixed chunks. Gate: `just test core`.
3. `feat(assetlib): stamp bake sources by content, not mtime` — `SourceStamp` becomes
   `{size, hash}`, `stampOf` streams and memoizes, the four serializers and their majors follow,
   `renameAsset` re-stamps a `.bvat` whose inputs it rewrote, and the docs that describe the old
   shape are corrected. Gate: `just test assetlib`.
4. `chore(assets): migrate the committed bakes to the content stamp` — the four in-repo fixtures at
   their new majors (ADR-7). Gate: `just test`.
