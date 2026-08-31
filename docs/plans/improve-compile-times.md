# improve-compile-times — implementation plan

## Context

A clean build is minutes and an incremental one is longer than it should be, on a machine shared by
several checkouts of this repo. Three things are true of the tree today and none of them was decided:

- **The PCH seam exists and is nearly empty.** `PCH/pch.h` (40 standard headers) is attached with
  `target_precompile_headers` to every compiled target there is. The per-subsystem PCHs beside it are
  not: `apps/editor/src/pch.h` holds two `core/ref` headers and **no Qt** while 95 editor files carry
  318 Qt include lines; `libs/assetlib/src/pch.h` is `#pragma once` and nothing, across 109 `.cpp`
  reaching nlohmann, tiny_gltf and glm. Only `libs/bgl/src/pch.h` is actually filled.
- **The suites bypass the PCH entirely.** `bgl_tests`, `assetlib_tests` and `gamelib_tests`
  *force-include* `PCH/pch.h` and `src/pch.h` (`-include`, textual, uncached, per TU) and separately
  precompile a `tests/src/pch.h` whose entire content is `#include <catch2/catch_test_macros.hpp>`.
  198 test files and 54,381 lines pay a full standard-library parse each, with no reuse.
- **Nothing caches a compile and nothing bounds one.** No ccache or sccache anywhere;
  `scripts/build.py` passes no `-j`, so ninja takes cores+2. Three checkouts building at once is 42
  compiler processes on 12 cores. The machine-wide coordination that exists (`scripts/util/lock.py`)
  covers test suites only, and a lock is the wrong shape for a build — serialising builds is worse
  than oversubscribing them.

## Decisions

- **ADR-1 — Fill the subsystem PCHs that already exist rather than adding a mechanism.** Qt into the
  editor's PCH, glm into assetlib's. The seam is wired on every target already; what is missing is
  content. `tiny_gltf.h` and `stb_image.h` stay out: `bmesh_gltf.cpp` defines their
  `*_IMPLEMENTATION` macros before including them and both put the implementation outside the include
  guard, so a PCH would get there first and guard the definitions out — the hazard `MetalImpl.cpp`
  already carries `SKIP_PRECOMPILE_HEADERS` for. *Rejected: unity builds (`CMAKE_UNITY_BUILD`), which are the larger raw
  win and what Unreal does — they merge translation units, so anonymous-namespace and file-static
  collisions become build breaks, ODR violations become silent, and include rot stops being visible;
  Chromium adopted jumbo builds and then removed them. Rejected: include-what-you-use pruning, the
  durable fix, but it touches hundreds of files and is a feature rather than a PR.*

- **ADR-2 — The root PCH may be written against; an `#include` may never be dropped for a subsystem
  PCH.** `PCH/pch.h` reaches
  every compiled target in the tree, and that universality is the condition CLAUDE.md's "do not
  `#include` standard c++ libraries" rule silently depends on. A subsystem PCH does not reach
  everything: `libs/assetlib/src/pch.h` is `PRIVATE`, while assetlib's public headers are compiled by
  `gamelib`, `editor_lib`, `assetlib_cli` and `assetlib_tests` without it, and two files carry
  `SKIP_PRECOMPILE_HEADERS ON` (`MetalImpl.cpp`, `MetalSurface_mac.mm`). So Qt and nlohmann stay
  explicitly included and the PCH only makes them cheap. *Rejected: extending the omission rule to
  the new headers, which is less to type and consistent with std on its face — but it cannot be
  stated as one rule (it becomes "omit in `src/`, never in `include/`"), and it fights the strict bar
  a library's public header is held to. The tree already follows the rule unstated:
  `apps/editor/src/Platform/MetalSurface.h` writes `#include <QtGui/qwindowdefs.h>` because its one
  consumer is compiled without a PCH.*

- **ADR-4 — ccache as a compiler launcher, detected and never required.** A content-keyed compiler
  cache is the one thing Unreal and Chromium agree on for this problem, and it is the only mechanism
  that survives a `git checkout` or a wiped build directory. Wired through
  `CMAKE_<LANG>_COMPILER_LAUNCHER` from a `find_program`, so a machine without ccache builds exactly
  as it does today; `just init` installs it. PCH forces `sloppiness = pch_defines,time_macros` and
  clang `-Xclang -fno-pch-timestamp`, without which ccache either misses everything or reports a
  false hit. *Rejected: sccache, which classifies MSVC's `/Fp` as non-cacheable, so with a PCH on
  every target it would cache nothing. Rejected: distributed compilation (Incredibuild, Unreal's UBA,
  reclient) — one machine, no farm.*

- **ADR-5 — One job budget shared across the workspace's builds, via ninja's jobserver — to bound
  memory, not CPU.** Measured on `editor_lib`: peak resident memory scales linearly at ~160 MB per
  concurrent compiler, while wall time saturates at `-j 8` (7s at 8 jobs, 8s at 28). So every job
  past the saturation point is pure memory for no speed, and uncapped it multiplies by the number of
  checkouts: five builds is 5 × 14 = 70 compilers and ~11 GB, against 17 and ~2.8 GB behind a
  12-token pool. That is the difference between a machine that swaps and one that does not, and the
  symptom is a laggy desktop rather than a slow build. ninja 1.13 is a GNU make jobserver client
  (fifo on POSIX, semaphore on Windows), so the pool lives outside any one build. Fail-open — any
  error setting it up warns and builds uncapped, because a wrong job cap must never be a hang. *Rejected: a fixed `-j` in
  `build.py`, which is wrong in both directions — it throttles a lone build and still oversubscribes
  four. Rejected: wrapping the compiler in a token-acquiring launcher, which works but pays a process
  spawn on every TU including cache hits. Rejected: a lock like `scripts/util/lock.py`, which
  serialises rather than shares.*

- **ADR-6 — Cross-worktree cache sharing is measured, not assumed.** ccache's certain win is within a
  checkout, across branch switches and build wipes. Sharing hits *between* worktrees additionally
  needs `base_dir` path rewriting *and* directory hashing disabled, which would leave a cached debug
  object's DWARF pointing at whichever worktree built it. So `base_dir` is set and `hash_dir` is left
  alone: worktrees share what they can, and correct debug info outranks a higher hit rate. The
  within-checkout win — a branch switch, a rebase, a wiped build directory — needs none of it.
  [docs/build_performance.md](../build_performance.md) carries the finding.
  *Rejected: claiming the cross-worktree number up front; rejected: `hash_dir=false` to force it.*

- **ADR-7 — A PCH's contents are decided by measurement, not by which headers look heavy.** A PCH is
  deserialized into every TU of a target, so its payoff is a header's parse cost times the fraction
  of sources that include it, minus its cost times all of them. Measured here: Qt at 56/60 editor
  sources saved 37%, `core/glm.h` at 40/59 assetlib sources saved 16%, and `nlohmann/json.hpp` at
  7/59 **cost 28%** and was removed. *Rejected: counting bytes re-parsed from ninja's dependency
  database as the deciding metric — it finds the candidates, but it measures only what a PCH avoids
  and is blind to what it adds, and by that metric the nlohmann regression looked like a win.*

- **ADR-8 — A PCH is judged on its size as well as its time.** It is loaded by every compile, so its
  size is multiplied by the job count and again by the number of checkouts building at once; twelve
  jobs across three checkouts turn 12 MB into ~430 MB resident, paid in swap and felt as a laggy
  desktop rather than a slow build. So an addition measuring *no faster* is a straight loss: the
  editor's PCH carries `QtCore` alone, because adding the `QtGui` and `QtWidgets` umbrellas measured
  inside the noise (71.0s against 68.1s) and grew it from 52 MB to 64 MB. *Rejected: the three-module
  umbrella, kept until this was measured on the grounds that it needed no list maintained.*

## Non-goals

- **CI is untouched.** sccache cannot cache a PCH'd MSVC TU and ccache's MSVC PCH support is an open
  issue with a known false-hit, so neither is safe on the Windows job; the macOS job alone was judged
  not worth the cache quota. The PCH work still speeds both jobs up.
- Unity builds, include-what-you-use, and C++20 modules.
- Link time — no lld/mold, no changes to what is static vs shared.
- Shader compilation time (`.slang` → DXIL/Metal), which is a different pipeline with its own cache
  ([docs/shader_cache.md](../shader_cache.md)).
- Reducing what a header includes. This change makes existing includes cheap; it removes none.
- **Replacing the suites' `target_force_include` with a real PCH.** Built and measured, then dropped.
  `target_force_include` is a second way to do what `target_precompile_headers` already does — its
  own documentation calls it the way to include headers "without using PCH" — and the suites are its
  only caller, so removing it was tempting on design grounds. But across two independent runs of
  three paired rounds the PCH was faster in five of six comparisons while the runs disagreed on the
  aggregate, so there is no measured speedup to justify the churn, and this change does not spend
  the tree's one mechanism-consolidation budget on something that buys nothing. The suites lose
  nothing by it: force-including `libs/assetlib/src/pch.h` with `core/glm.h` in it (ADR-1) measured
  47.3s → 37.0s on `assetlib_tests`, so the glm win reaches them either way.

## Acceptance

- `just build --time` reports per-target and total wall time from ninja's `.ninja_log`, and the PR
  body carries measured before/after for a clean build, an incremental build after touching one
  editor header, and a rebuild from a warm ccache.
- `just test scripts` covers the jobserver's token accounting and the timing parser.
- Every suite still builds and passes on `macos-clang-metal-debug`, which is what proves the PCH
  changes altered no semantics.

## Commits

1. `docs(plans): plan the compile-time work` — this file.
2. `build: report where compile time goes` — `scripts/build_timing.py`, `just build --time`.
   Gate: `just test scripts`.
3. `perf(build): put Qt and glm in their subsystem PCHs` — ADR-1, ADR-2, ADR-7, ADR-8, and the
   CLAUDE.md rule the tree was already following unstated. Gate: `just build`, measured.
4. `build: cache compiled objects with ccache when it is installed` — ADR-4, ADR-6.
   Gate: a second clean build is a cache hit; a build with ccache absent is unchanged.
5. `build: share one job budget across the workspace's builds` — ADR-5.
   Gate: `just test scripts`, and the peak-memory curve above.
