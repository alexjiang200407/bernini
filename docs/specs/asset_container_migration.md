# Asset container migration — the problem, and how we will solve it

**Status: deferred, deliberately.** Nothing in this document is built. It records a problem that is
currently cheap to ignore, the point at which it stops being cheap, and the design we settled on so
that whoever picks it up does not re-derive it. Written 2026-08-16, alongside the change that first
made the problem concrete ([stamp-content-hash](../plans/stamp-content-hash.md)).

---

## The problem

Bernini's asset containers — `.bmaterial`, `.bmesh`, `.bskel`, `.banim`, `.bvat`, `.bsky`, `.benvl`,
`.benv` — carry a major version, and **every reader refuses a major it does not recognise**:

```
bmaterial: unsupported version 9 (expected 10); re-bake the material
```

That refusal is deliberate and worth keeping. The change that prompted this document turned
`SourceStamp` from `{size, mtime}` into `{size, content-hash}` — sixteen bytes either way, at the
same offsets. **Nothing but the version could tell the two apart.** Without the guard, every reader
would have silently read an old mtime as a content hash and reported every bake fresh.

So a format change makes every container already on disk unreadable, and there is no reader for the
old shape anywhere in the tree — by choice, because keeping one means keeping all of them:
`deserializeMaterialV9` beside `deserializeMaterialV10` beside `V11`, each reader growing a version
parameter that only a migration path ever passes.

Today that costs almost nothing. One project exists, it is migrated by hand the same afternoon the
format moves, and everything it was baked from is still on disk beside it.

## When it stops being cheap

The cost is a function of **how far behind a project can fall**, and that is a function of how many
people and how many long-lived copies exist.

| Trigger | Why it changes the calculus |
|---|---|
| **Concurrent developers** | A branch cut before a format change and merged after it carries containers at the old major. The person who lands the change is no longer the only person who has to migrate. |
| **The engine is online / shipped** | Projects exist that we do not hold and cannot re-bake. A refusal to load is then a user-visible failure, not an afternoon's chore. |
| **Sources stop travelling with projects** | Recovery today leans on `Data/textures_src/` still being present. A shipped project carries baked output only, and then a lost stamp cannot be recomputed at all. |
| **More than one version behind** | With one project migrated promptly, a file is never more than one major old. With many, a project can arrive three versions behind, and the single-hop assumption below breaks. |

Until at least the first two hold, a format change is: bump the major, re-bake or hand-patch the one
project, move on.

## What we will do instead of a versioned reader

A migration is a **throwaway program**, not a maintained interface.

- One `.cpp` under a `migrations/` directory, globbed into its own executable, so adding a file
  creates a tool and `git rm`-ing it removes the tool. There is no CMake edit either way.
- **The old container is bytes.** The migration parses the old shape itself and is the only thing in
  the tree that knows it. What it *writes* goes through the **current** serializers.
- That last point is why a migration is C++ linking `assetlib` rather than a script: a script would
  have to reimplement the *new* layout in another language, and that is the copy that drifts.
- Shared machinery — the walk, `--dry-run`, the confirmation, the per-file report, per-file error
  isolation — belongs in `assetlib` as a pure `applyMigration`; the argv-reading, stdout-writing half
  is compiled into each migration, because a library does neither.
- **Returning "no change", or the bytes handed in, must not count as a rewrite.** The containers are
  LFS-tracked binaries; a migration that re-serialized every file it read would dirty a whole project
  for nothing, and re-running one would show every file modified. Enforcing that in the runner rather
  than in each migration is what makes migrations idempotent without their authors thinking about it.
- **The second walk is authoritative.** If the tool previews and then writes, the preview never
  attempts a write, so a read-only path or a full disk shows up only in the second pass. Reporting
  the preview's counts afterwards calls a half-migrated project a success.

All of the above was built and then removed before landing, in PR #374 — worth a look for the shape,
but this document is written to be sufficient on its own rather than to depend on that surviving.

## Multiple old versions: collapse, do not chain

The obvious model is Laravel's: one migration per hop, `v7 → v8 → v9 → v10`, replayed in order. **It
does not work here**, and the reason is worth stating plainly because it is not obvious.

Each step would have to emit *its own* target version. Ours write through the current serializer, so
given migrations A (9→10) and B (10→11) and a project at v9, A writes **v11**-shaped bytes carrying
only A's fix-up; B then reads the version, sees 11, and skips the file. B's fix-up is never applied
and nothing reports a problem.

Making the chain sound requires each step to carry a **frozen** copy of its era's writer — a snapshot
that can never be touched again without retroactively changing what that link means. That is the same
unbounded accumulation as a versioned reader, moved to the write side.

**Collapse instead.** One migration accepts a *set* of old majors and takes any of them straight to
current:

```
.from = { 7, 8, 9 }      // majors it accepts; anything else is reported, not skipped
.to   = current          // always

transform: dispatch on the file's own major, hand-parse that layout,
           fill the current struct, write with the current serializer
```

| | Chain | Collapse |
|---|---|---|
| Parsers | one per step | one per supported old version |
| **Writers** | **one frozen per step** | **one, always current** |
| Still correct after the next bump | only if every writer stayed frozen | yes — it writes current by construction |
| Retiring a version nobody has | breaks the chain | delete that branch |

Chaining is only *mandatory* when migrations are immutable historical records — Laravel's are,
because they have already run against production databases. Ours will not have, so a migration
remains editable and collapsing is strictly less code.

One flaw to fix when this is built: a migration must **report** a major it does not handle rather
than silently counting the file as unchanged. Otherwise a missing branch is invisible.

## Retention: keep the source, not the target

Once a migration has been run everywhere, delete its build target. Keeping it compiling is what makes
it rot: it links `assetlib` and calls the current serializers, so it keeps building while quietly
changing meaning under the next format change.

Move the source to an uncompiled archive instead — readable, copyable, and repairable against
whatever the API looks like when someone needs it. Git history is technically enough; an archive
directory only buys discoverability, which is worth something to a person who does not already know
the file once existed.

## The schema option, and what it is actually for

Describing the containers as data and generating readers, writers and migrations from the schema was
considered and deferred. Two findings worth keeping, because they point in opposite directions:

- **It will not auto-generate the migrations.** A differ sees the *shape* change and not the *value
  recovery*. On the stamp change it would have emitted `drop mtime, add hash = 0` — the lossy
  version, which re-bakes everything. What made that migration free was "hash the file this route
  names, but only where it still measures the size the old stamp recorded", which is domain knowledge
  no differ derives. Migrations stay hand-written.
- **It would make chains composable**, which is the strongest argument for it. A schema gives a
  version-independent intermediate — a field-name→value tree — so only the first step parses bytes
  and only the last serializes, and nothing is frozen. This was under-weighted when the idea was
  first assessed on auto-generation alone.

Its own merits are the better reason to do it: roughly 1,000–1,300 lines of hand-written
serialization across eight containers would go, the reader/writer field-order symmetry hazard would
go with them, and it matches the "IDL is the single source of truth" constraint in `ROADMAP.md`.

Costs to budget for: a schema language and a generator on the order of `bgl_idlgen` (1,296 lines);
eight formats ported one at a time; generated readers that must reproduce the **current byte layouts
exactly**, or every existing asset breaks — which is itself a migration. `image_io.cpp` (769 lines of
KTX2) is a foreign spec and cannot be generated at all, and `.bmesh`'s meshlet pools and validation
will not fall out of a field list. Weeks of work, and it should be decided on the serialization
argument rather than to unblock migrations.

## What holds in the meantime

- **Keep the version guard.** It is what turns a stale file into a clear error instead of silent
  misreading, and it costs one branch.
- **Additive change needs no migration at all.** `.bmesh`, `.bskel`, `.banim` and `.bvat` are one
  chunked container format (`src/chunk_io.h`); an absent chunk is not an error, so adding data is a
  **minor** bump and leaves what is on disk readable. Prefer a chunk over a layout change.
- **`.bvat` never needs migrating.** It is git-ignored and wholly derived, and `game::EnsureVatBaked`
  treats one it cannot parse as one that is not there — see [Vertex Animation Textures](../vat.md).
- **A format change is a one-off script until the triggers above hold.** The repo's own committed
  fixtures were carried across the stamp change exactly that way, and it took ten minutes.
