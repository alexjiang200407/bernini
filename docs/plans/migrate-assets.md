# migrate-assets — implementation plan

## Context

[stamp-content-hash](./stamp-content-hash.md) bumped four container majors and deliberately left
every existing file unreadable, its ADR-5 naming a migration ability as what would own the upgrade.
This branch built one: first with maintained legacy readers, then — after that was rejected as the
accumulation the mechanism exists to prevent — as throwaway per-change programs.

Both were removed before landing. With one developer, one project and nothing online, a format change
is cheaper to absorb by hand than to build machinery for, and the machinery would have to be kept
correct across every format change in the meantime.

What the branch delivers instead is the `.bvat` fix, which is needed *more* without a migration, and
a spec so the design is not re-derived when it is finally wanted.

## Decisions

- **ADR-1 — No migration system now.** The triggers that make one necessary — concurrent developers
  with long-lived branches, an engine that is online, projects whose sources do not travel with them
  — are all still false. Until then a format change is: bump the major, carry the one project across
  by hand, move on. *Rejected: landing the mechanism anyway "since it is written", which buys an
  unused subsystem that must stay correct through every format change the next months bring.*

- **ADR-2 — The design is written down rather than kept.**
  [docs/specs/asset_container_migration.md](../specs/asset_container_migration.md) records the
  problem, the trigger, and the conclusions this branch reached the expensive way: throwaway programs
  over maintained readers, collapse over chain, why frozen writers are the same trap as versioned
  readers, and what a schema would and would not buy. The working implementation stays recoverable
  from this branch's history. *Rejected: leaving it in the code as the record, which is a subsystem
  pretending to be a document.*

- **ADR-3 — `docs/specs/` is a new kind of document, and the index says how it differs.** A doc
  describes code that exists; a plan records a change that happened; a spec describes code that does
  not exist yet and the trigger that will call for it. It is deleted when the thing lands. *Rejected:
  filing it under `docs/plans/`, where every other file is the record of a change that shipped.*

- **ADR-4 — An unreadable `.bvat` is treated as absent and re-baked.** `EnsureVatBaked` called
  `loadVat` outside any `try`, so the parent change turned a `.bvat` at the old major into a thrown
  error — contradicting [docs/vat.md](../vat.md), which already stated a stale `.bvat` is "re-baked,
  never an error". It is git-ignored and wholly derived. This is independent of migration and is the
  one piece of the branch that lands. *Rejected: leaving it, which would make a bumped major an error
  for every project that had ever baked one.*

## Non-goals

- Any migration mechanism: the runner, the per-migration build rule, the legacy readers. All removed.
- Carrying `bernini-test-project` across the stamp change. A one-off script does that when wanted, as
  one already did for this repo's own committed fixtures.
- The schema work. Its case is in the spec; it should be decided on the serialization argument, not
  as a way to unblock migrations.

## Acceptance

- `just test gamelib` — a `.bvat` whose bytes cannot be parsed is re-baked rather than thrown from,
  for both a stale major and a truncated file.
- `just test` — nothing else moved.

## Commits

1. `docs(plans): plan the container migration` — this file.
2. `fix(gamelib): re-bake a .bvat that cannot be read` — ADR-4, with its test.
3. `docs(specs): the container migration problem, and how we will solve it` — ADR-1 to ADR-3, and the
   removal of the mechanism this branch had built.
