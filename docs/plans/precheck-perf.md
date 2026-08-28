# precheck-perf — implementation plan

Give `bcp-precheck` a cost lens — *is this feasible for AAA assets?* — and give `assetlib` the stage
logging that makes a slow load attributable.

## Context

`bcp-precheck` has four lenses — duplication (§ 2), roadmap/standard (§ 3), plan boundaries (§ 4),
style (§ 5) — and none of them asks what a change costs at content scale. The bottleneck in this
engine is not render speed: bindless + GPU-driven forces the fast path. It is asset structure and the
offline and load work over it. AdaWong, a AAA character added to the test project, took a long time
to load (posed bounds) and its VAT bake was infeasible — **reported, not measured**, and that is the
point: there is no number to cite because nothing in the cook emits one. Slice 1 is what closes that
gap, and the first thing it should produce is the figure this paragraph cannot give.

History says this recurs, and that it is always found after landing: `2e22adf5` (#401) introduced the
posed-bounds walk, `0cb8003b` (#421) and `96b9419a` (#457) had to fix it (~6 min → 3.5 s, and 740 ms
→ 29 ms per read-back), and `42fc4987` (#447) hid the remainder behind a loading screen.

And the subsystem where the cost lives is the one that logs nothing: `assetlib` carries a single
spdlog line in the whole offline cook ([libs/assetlib/src/env_import.cpp:189](../../libs/assetlib/src/env_import.cpp)),
so a slow load cannot be attributed from a log at all.

## Decisions

- **ADR-1 — `assetlib` brackets its cook stages in the log, not the editor.** The stages that cost
  sit inside one `assetlib` call, and `assetlib_cli` bypasses the editor entirely. *Rejected:
  editor-side timing, because `apps/editor/src/Import/import_pipeline.cpp:311` reports two numbers —
  worker ms and UI ms — and neither names a stage, so it says which thread froze but never what it
  froze on. It also leaves the CLI baker dark. Also rejected: returning timings as data, which widens
  every cook entry point's API for a diagnostic.*

- **ADR-2 — one `core` scoped stage timer, `core::logging::ScopedStage`, logging on scope exit.**
  *Rejected: a per-call-site ad-hoc timer, which is two ways to do one thing and is what the strict
  `libs/` bar forbids.*

  **Amended by the survey.** The consensus had this consolidating three existing `steady_clock`
  copies. They are three different mechanisms, and only one is a stage timer:

  | Site | What it actually is | Disposition |
  |---|---|---|
  | `libs/assetlib/src/envmap_bake.cpp:827` | fills `PrefilterStats::seconds`, an out-param **read by nothing**, passed non-null only by `EnvmapBake_test.cpp:331` | converts; the dead field goes |
  | `apps/editor/src/Thumbnails/AssetThumbnailCache.cpp:478` | warns only above `c_SlowTickMs`, over three hand-measured sub-durations | converts only the outer tick, which is why `ScopedStage` takes a threshold |
  | `apps/editor/src/Import/import_pipeline.cpp:186` | splits worker ms from UI ms across a loading screen | outer bracket converts; the worker/UI split stays — it names *which thread froze*, which no stage timer knows |

- **ADR-3 — every stage line carries its own elapsed ms.** *Rejected: relying on timestamp diffs,
  and rejected: unifying the editor's two log sinks.*

  **Amended by the survey**, which found the premise wrong. The editor writes **two** log files with
  two clocks and two formats — `editor.log` through a Qt message handler
  (`apps/editor/src/main.cpp:19`, `FileLog.cpp:41`, ISO-8601 with ms) and `bgl.log` through spdlog
  (`libs/core/src/log.cpp:27`, `[%H:%M:%S:%e]`). An import's timeline therefore straddles both, and
  subtracting a Qt timestamp from an spdlog one is the correlation problem, not the fix for it. A
  line that states its own duration needs no diff and is immune to the split. Timestamps stay useful
  for the *gaps between* stages, which is what they are good at.

- **ADR-4 — the yardstick lives in `ROADMAP.md`, as a rule *and* a table, which are not the same
  entry.** Guiding Constraints gains one architectural rule — an import, bake or load path must stay
  feasible at the scale the roadmap names, and cost that grows superlinearly in one of those
  dimensions is a design defect rather than a tuning problem. The **dimensions themselves** go in a
  separate `## Content scale` section the rule points at, seeded with the reference rig
  (`cha800_00.glb` — 663 bones, 27 mesh entries, 170k vertices, 2254 frames;
  [docs/skinning.md](../skinning.md)). *Rejected: a new `docs/asset_budgets.md`, another file to keep
  true; and inlining the numbers in the agent prompt, since an engine fact stored in an agent's
  prompt is the copy nobody updates when terrain lands.*

  **Rule and table are split because precheck treats them differently.** Every Guiding Constraint is
  something [`.claude/agents/bcp-precheck.md`](../../.claude/agents/bcp-precheck.md) § 3 reads as
  "a change that breaks one is a finding even when it works" — true of a design rule, and a category
  error for a number that goes stale when a bigger asset lands. So: **the rule** is a constraint and
  breaking it is a finding; **the table** is a reference, and a dimension missing from it is not a
  licence. Where the table is silent, precheck says so and reasons from the nearest named type
  rather than treating the silence as a pass — the same discipline as ADR-10.

- **ADR-5 — the cost lens covers any per-asset-element path** — import, bake, load, reimport — in
  whatever layer it sits. Steady-state GPU/render work is explicitly out: the roadmap's
  GPU-driven-by-default constraint already forces that path. *Rejected: an `assetlib`-only lens,
  because the read-back that cost 740 ms is `AcquireSkinnedMesh` in `gamelib` and the import walk is
  in `apps/editor`.*

- **ADR-6 — a cost finding is `block` only when the diff makes an asset path superlinear in a
  dimension the table names**; everything else is `revise`. *Rejected: blocking on any stated budget
  exceeded, which fires on cost paid once offline that nobody minds.*

- **ADR-7 — the instrument is a committed `[perf]` case set in `assetlib_tests`, not a test precheck
  writes.** The cases synthesize their rigs in memory — `assetlib_tests` already assembles
  `Skeleton`, `BMesh` and `AnimationSet` as PODs by hand
  (`libs/assetlib/tests/src/Skinning_test.cpp:104`, `:491`) — so nothing is committed to `assets/`
  and nothing enters LFS. *Rejected: precheck writing a throwaway case each run, which reverses its
  "edits no file" rule, dirties the tree precheck itself reports as blocking, and makes a
  Sonnet-written benchmark a new source of an unchecked number.*

  **Amended by the survey**: the cases run at a **reduced** scale, not at the ROADMAP dimensions. A
  663-bone, 2254-frame `bakeVat` is minutes, and a suite `just test` runs cannot be minutes. What the
  cases assert is scaling *shape*, which a ratio between two sizes proves at any base size. The full
  dimensions live in the ROADMAP table, which is what precheck reasons against.

- **ADR-8 — the cases assert counted work, never an absolute wall-clock ceiling.** Counted reads
  through the existing `libs/assetlib/tests/src/CountingFileSystem.h` where the work crosses the
  `IFileSystem` seam; cost *ratios* between two problem sizes where it does not. Wall-clock may be
  printed, never asserted. *Rejected: time budgets — loose enough to survive a loaded debug machine
  is loose enough to miss a 3x regression.*

- **ADR-9 — precheck runs the cases with `--no-lock`.** `scripts/util/lock.py:41` matches the
  `_tests` suffix, so an unqualified run waits behind another checkout's full `just test`;
  `assetlib` links no graphics device, so the reason the lock exists does not apply, and an unbounded
  wait inside a gate that runs before every PR is worse. *Rejected: taking the lock; and a separate
  non-suite target, which would be a second way to run `assetlib`'s tests.*

- **ADR-10 — precheck never reports a number it did not observe.** When the suite cannot be built or
  run, it says so in one line and falls back to the static reading. *Rejected: silence, which reads
  as a pass.*

## Non-goals

- **Making AdaWong load.** Precheck reads diffs; the posed-bounds and VAT costs that exist today
  appear in no diff and this feature will not surface them.
- **A CI performance gate.** `.github/workflows/ci.yml` runs no suite at all; adding one is its own
  change.
- **A cost lens on GPU or render work.**
- **Committing an AAA asset** to `assets/` or to LFS.
- **Optimising any path this instruments.** The feature measures and gates; it does not fix.
- **Unifying `editor.log` and `bgl.log`.** See ADR-3 — worth doing, and not this.

## Acceptance

- **Slice 1** — an AAA-class import writes one line per cook stage, each stating its own elapsed ms,
  and those lines account for the import total. `just test core assetlib editor` green.
- **Slice 2** — the new precheck run against `2e22adf5` (#401, introduced the posed-bounds walk) and
  `7863479f` (#340, introduced the VAT bake) raises the cost finding on both, and stays silent on a
  diff with no per-asset cost content. `just run assetlib_tests -- "[perf]" --no-lock` green, and
  each `[perf]` case proven to fail when its invariant is temporarily broken.

## What the survey found

- **`assetlib` is unobservable.** One `spdlog::warn` in the whole subsystem
  (`libs/assetlib/src/env_import.cpp:189`). No stage boundary is logged anywhere in the cook.
- **`core::logging` is one function.** `init_file_logger` (`libs/core/include/core/log/log.h`),
  called only from the two graphics backends (`Graphics_metal.cpp:105`, `Graphics_d3d12.cpp:180`).
  So `assetlib_cli` has no file sink and its lines go to spdlog's default console sink — correct for
  a CLI, and a decision rather than an accident.
- **Two editor log files, two clocks.** `editor.log` (Qt handler, `main.cpp:19`) and `bgl.log`
  (spdlog). See ADR-3.
- **No benchmark infrastructure exists.** Zero Catch2 `BENCHMARK` in the tree; three ad-hoc
  `steady_clock` sites, tabulated in ADR-2.
- **`CountingFileSystem` already exists** (`libs/assetlib/tests/src/CountingFileSystem.h`) and counts
  `reads` and `bytesRead` through the `IFileSystem` seam. Its own docstring makes this feature's
  argument: *"Nothing about the returned structs would show that guarantee being lost, so the test
  measures the reads themselves."* Slice 2 reuses it; it does not write a second one.
- **The invariants worth pinning are already stated in prose**, which is what makes them testable:
  - `rebake_bounds.cpp:60` — a rig's meshes are loaded once for the run, not once per clip set.
  - `docs/skinning.md` — all mesh entries share one walk of the clip set, so a rig drawn as 27
    meshes evaluates each pose once (2.4 s of the 3.5 s bake is that walk).
  - `docs/vat.md` and `vat_bake.h` — `vatBakeSize` lays a bake out **without filling it**, so a
    caller can be offered the cost before paying it.
- **`assetlib_tests` builds rigs procedurally**, never from a file (`Skinning_test.cpp:104`, `:491`,
  `:564`), which is why slice 2 needs no fixture.

## What changes

| File | Change |
|---|---|
| `libs/core/include/core/log/ScopedStage.h`, `libs/core/src/log/ScopedStage.cpp` | new: a named stage that logs its own elapsed ms on scope exit, with an optional threshold below which it stays quiet. The flat `src/log.cpp` stays where it is: `core` already puts a module's free functions in `src/<name>.cpp` and its classes in `src/<name>/`, which is `src/file.cpp` beside `src/file/LooseFileSystem.cpp` and `src/str.cpp` beside `src/str/str.cpp`. No move is wanted |
| `libs/core/tests/src/ScopedStage_test.cpp` | new: the stage logs once, states a duration, and honours the threshold |
| `libs/assetlib/src/skinning.cpp`, `vat_bake.cpp`, `asset_import.cpp`, `rebake_bounds.cpp` | bracket the posed-bounds bake, the VAT bake and the import walk |
| `libs/assetlib/src/envmap_bake.cpp`, `libs/assetlib/include/assetlib/envmap.h` | drop the dead `PrefilterStats::seconds`; the stage logs instead |
| `apps/editor/src/Import/import_pipeline.cpp`, `Thumbnails/AssetThumbnailCache.cpp` | outer brackets converted; the worker/UI split and the three sub-durations stay |
| `ROADMAP.md` | a **Content scale** Guiding Constraint and its per-asset-type dimension table |
| `libs/assetlib/tests/src/Perf_test.cpp` | new: the `[perf]` cases |
| `.claude/agents/bcp-precheck.md` | the cost lens, as a new section |
| `CLAUDE.md`, `docs/skinning.md`, `docs/vat.md` | the `[perf]` tag, and the stage lines a cook now writes |

**What could break.** `PrefilterStats::seconds` is public API, so removing it is a breaking change to
a struct — safe only because nothing reads it, which the survey checked. Stage logging inside
`assetlib` means a library decides its caller's output policy; the repo has already accepted that
(`env_import.cpp:189`), and the threshold in ADR-2 is what stops it becoming noise on a per-frame
path.

## The tasks, in order

**Slice 1 — make a cook attributable.**

1. `feat(core): a stage times itself and says how long it took` — `core::logging::ScopedStage`.
   Gate: `just test core`, with a case per behaviour (logs once, states a duration, stays quiet under
   the threshold).
2. `feat(assetlib): every cook stage names its own cost` — bracket the posed-bounds bake, the VAT
   bake and the import walk; drop the dead `PrefilterStats::seconds`.
   Gate: `just test assetlib`, and a case that captures the stage lines through an spdlog sink and
   asserts the bake emits one.
3. `refactor(editor): the import and the thumbnail tick time themselves through core` — the two
   outer brackets. Small, and last in the slice so it can be dropped if it reads as churn.
   Gate: `just test editor`.

**Slice 2 — gate the next one.**

4. `docs(roadmap): the content scale every asset path is held to` — the Guiding Constraint and its
   table. Gate: the dimensions match `docs/skinning.md` and
   `docs/specs/animation_compression.md`; no number is invented here.
5. `test(assetlib): pin the work a cook does as its inputs grow` — the `[perf]` cases:
   - a rig's mesh is read once per run, not once per clip set (`CountingFileSystem`);
   - all mesh entries share one walk of the clip set (cost ratio, 1 entry vs many);
   - `vatBakeSize` does not skin (cost ratio, few frames vs many).
   Gate: `just run assetlib_tests -- "[perf]" --no-lock`, and each case proven to fail when its
   invariant is temporarily broken.
6. `feat(claude): precheck asks whether a change is feasible at AAA scale` — the cost lens section,
   and the `[perf]` tag in `CLAUDE.md`'s Tests notes.
   Gate: the replay in Acceptance — `2e22adf5` and `7863479f` raise it, a cost-free diff does not.
