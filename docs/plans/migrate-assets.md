# migrate-assets — implementation plan

## Context

[stamp-content-hash](./stamp-content-hash.md) replaced `SourceStamp`'s mtime with a content hash and
bumped four container majors, deliberately leaving every existing file unreadable — its ADR-5 named
this ability as what would own the upgrade. Until it lands, `bernini-test-project` cannot be opened:
27 of its containers (23 `.bmaterial` at v9, 2 `.bsky` and 2 `.benvl` at v1) fail with
`unsupported version … re-bake`.

The sources are all still there under `Data/textures_src/`, so the upgrade does not have to be lossy.
A stamp can be recomputed from the file it named, and a bake that was fresh before stays fresh.

This is stacked on `feat/stamp-content-hash` — the new `SourceStamp` has to exist for any of it to
compile — and retargets to `master` when #371 merges.

## Decisions

- **ADR-1 — Migration recomputes each stamp from the live source, and zeroes it only when the size
  disagrees.** The recorded size is the one piece of the old stamp that still means something, so it
  is the evidence: same size, and the source is almost certainly the one the bake read, so the fresh
  hash keeps the bake fresh and nothing re-bakes. Different size, and the source genuinely changed
  since, so a zeroed stamp reads stale — which is the truth. *Rejected: zeroing every stamp, which
  would re-bake all 27 and reintroduce exactly the churn the parent change removed; and recomputing
  unconditionally, which would bless a source edited since the bake and leave the triplet claiming a
  texture it was never built from.*

- **ADR-2 — A legacy reader per major that exists, beside the current one in that format's own
  file.** `.bmaterial` v9, `.bsky` v1, `.benvl` v1. The old and new layouts differ only in how the
  stamp's second 8 bytes are read, so the parse is shared and the major is a parameter — a second
  copy of `readPbr` is how two readers start disagreeing about a format. *Rejected: a general
  version-upgrade registry chaining arbitrary majors, which is machinery for hops nobody has
  designed.*

- **ADR-3 — A CLI subcommand, run by hand.** `assetlib_cli migrate <dataRoot> [--dry-run]`, walking
  the tree. *Rejected: migrating on project open in the editor, because it would turn a read into a
  bulk rewrite of LFS-tracked binaries with no one having asked for it.*

- **ADR-4 — An unreadable `.bvat` is treated as absent and re-baked, not migrated.**
  `EnsureVatBaked` (`libs/gamelib/src/vat_freshness.cpp`) calls `loadVat` outside any `try`, so the
  parent change turned a `.bvat` at the old major into a thrown error — contradicting
  [docs/vat.md](../vat.md), which already states a stale `.bvat` is "re-baked, never an error". It is
  git-ignored and wholly derived, so catching the load failure is both the fix and the reason it
  needs no legacy reader. *Rejected: giving `.bvat` a legacy reader like the rest, which upgrades a
  file that re-bakes in seconds anyway.*

## Non-goals

- Any format other than the three with a legacy reader. `.benv` carries no stamp and no version
  bump; `.bmesh`, `.bskel` and `.banim` were untouched by the parent change.
- Running the migration over `bernini-test-project`. The ability lands here; pointing it at that repo
  is a separate, deliberate act against a repo this PR does not own.
- Backing up what it rewrites. The projects it runs on are in git, which is the backup.
- An editor-side prompt when an open fails on version.

## Acceptance

- `just test assetlib` — the suite synthesizes v9/v1 containers, migrates them and asserts:
  - the result loads at the new major, with the stamp the live source actually hashes to;
  - a source whose size no longer matches lands zeroed rather than blessed;
  - `--dry-run` reports every file and writes none;
  - a file already at the current major is left untouched, not rewritten;
  - a corrupt or unknown-major file is reported as failed without aborting the run.
- `just test gamelib` — a `.bvat` whose bytes cannot be parsed is re-baked rather than thrown from.

## Commits

1. `docs(plans): plan the container migration` — this file.
2. `fix(gamelib): re-bake a .bvat that cannot be read` — ADR-4, with its test. Gate:
   `just test gamelib`.
3. `feat(assetlib): migrate containers written before the content stamp` — the legacy readers,
   `migrateAssets`, and the CLI subcommand. Gate: `just test assetlib`.
