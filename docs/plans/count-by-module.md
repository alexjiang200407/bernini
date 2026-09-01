# count-by-module — implementation plan

## Context

`just count` answers "how much C++, how much Slang, how much of it is tests" and nothing else. The
question it cannot answer is the one asked more often — *how big is `bgl` next to the editor, what
did `assetlib_cli` actually cost* — and answering it today means running `find | wc -l` by hand with
a different set of exclusions than the script uses, so the two numbers disagree.

## Decisions

- **ADR-1 — a module is a path prefix, matched longest-first, from a table in the script.**
  `libs/assetlib/cli` → `assetlib_cli` before `libs/assetlib` → `assetlib`, which is what makes a
  target that lives inside another module's directory nameable at all. *Rejected: deriving modules
  from the CMake File API codemodel via `scripts/util/cmake_tools.py`, because it makes `just count`
  require a configured build dir where it currently requires nothing, and headers, `.slang` and
  `.py` appear in no target's source list so a path rule would be needed underneath it regardless.*

- **ADR-2 — this follows the standard, which is that the directory defines the group.** `cloc`,
  `tokei` and `scc` all group by language and leave component grouping to the caller passing paths
  (`cloc libs/bgl_extended`); none reads a build system to discover components. The table is that idiom
  written down once instead of retyped per invocation. *Rejected: a build-system-derived grouping,
  which no counter in wide use offers — see ADR-1 for the cost.*

- **ADR-3 — the module breakdown is a second table under the language one, not a replacement or a
  nesting.** Both tables stay flat and comparable down a column. *Rejected: nesting language rows
  under each module, because it triples the height and puts sub-rows between the two numbers a
  reader is comparing; and replacing the language table, which would drop the Slang-vs-C++ split the
  script exists for.*

- **ADR-4 — granularity is one row per library or app, with the example programs collapsed.**
  `bgl` stays whole — its `src/d3d12`, `src/metal`, `shaders` and `idl` subtrees fold in — and the
  five one-file example apps become a single `examples` row. *Rejected: splitting `bgl_d3d12` and
  `bgl_metal` out, because the roadmap tracks backend parity as a feature matrix rather than a line
  count, and five separate one-file example rows, which pad the table without answering anything.*

- **ADR-5 — every counted file belongs to a module, and an unmatched one gets an `(unclassified)`
  row.** The module totals therefore equal the language totals, and that equality is the test: a
  file dropped or double-counted shows up as two numbers that no longer agree. A hand-maintained
  table drifts, so the drift is given somewhere visible to land. *Rejected: covering only C++ and
  Slang, which leaves the two tables' totals disagreeing with no row accounting for the gap; and
  exiting non-zero on an unclassified file, which turns a stats command into something that refuses
  to print because somebody added a directory.*

## Non-goals

- No per-file or per-directory breakdown — the module row is the finest grain.
- No replacement of `cloc`/`tokei`; this counts lines, not blanks, comments or complexity.
- No JSON output. Nothing consumes `just count` programmatically.
- No CMake target discovery, and no new dependency on a configured build dir (ADR-1).

## Acceptance

- A `scripts_tests` pytest case pinning the classification: `libs/assetlib/cli/…` resolves to
  `assetlib_cli` and not `assetlib` (the longest-prefix rule is the part that can silently
  regress), `libs/bgl_extended/src/d3d12/…` resolves to `bgl`, and an unmatched path resolves to
  `(unclassified)`.
- A case asserting the module totals equal the language totals over the same file set, so no file
  is dropped or counted twice.
- `just test scripts` green; `just count` run and its output read.

## Commits

1. `docs(plans): plan a per-module breakdown for just count` — this file.
2. `feat(scripts): just count reports what each module cost` — the prefix table, the module tally
   and the second table, with the walk split from the report so both are callable on a directory.
   Gate: `just test scripts`, and `just count` read.
