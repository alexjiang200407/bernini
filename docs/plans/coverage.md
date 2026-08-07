# Coverage as a diagnostic, on macOS

The tree has 52k lines of source against 25k lines of test across five Catch2 suites, and no way to
ask which of the source those tests actually execute. This feature adds one: a separate build mode
that instruments the code, and a `just coverage` that runs the suites under it and reports what was
never reached.

The deliverable is **not a percentage**. A number invites a threshold, a threshold rewards tests that
execute a line without asserting anything about it, and that is precisely the failure mode a
heavily AI-assisted workflow already trends toward. The output this feature exists to produce is the
narrow one: *these lines of the diff you just wrote are executed by no test*. That is checkable, hard
to game, and is work a human reviewer currently does by eye.

Scope is macOS only, by instruction and by what the toolchain supports.

## What the survey found

**A per-target flag seam exists, but it is compile-only.** `enable_strict_compiler(<target>)`
([cmake/enable_strict_compiler.cmake](../../cmake/enable_strict_compiler.cmake)) is called at 12
sites across **four** `CMakeLists.txt` — `libs/core` (`:37`, `:129`), `libs/gamelib` (`:39`, `:82`),
`libs/bgl` (`:77`, `:114`, `:139`, `:148`, and `:246`/`:325` for the D3D12 and Metal variants of
`bgl_tests`), and `libs/assetlib` (`:53`, `:144`). It issues `target_compile_options(... PRIVATE ...)`
and nothing else (`:19`, `:52`).

**There is no `enable_strict_compiler` call anywhere under `apps/` or `examples/`.** The strict
warning set is a `libs/` convention; `editor_lib` (`apps/editor/CMakeLists.txt:65`), `editor_tests`
(`apps/editor/tests/CMakeLists.txt:15`), `assetlib_cli` (`libs/assetlib/CMakeLists.txt:65`) and the
five example binaries carry none of it.

**Library kinds decide how a flag has to travel.** `bgl` is the tree's only shared library —
`add_library(bgl SHARED ...)` in each of the three backend branches (`libs/bgl/CMakeLists.txt:87`,
`:93`, `:99`), absorbing `$<TARGET_OBJECTS:bgl_objects>` plus the backend object library. `core`
(`libs/core/CMakeLists.txt:18`), `assetlib` (`:19`) and `gamelib` (`:16`) are STATIC. `editor_lib` is
an OBJECT library consumed through `target_link_libraries` (`apps/editor/CMakeLists.txt:129`,
`apps/editor/tests/CMakeLists.txt:34`), not through `$<TARGET_OBJECTS:>`.

**`bgl` absorbs its backend by objects, not by linking.** The Metal branch adds only frameworks —
`target_link_libraries(bgl PRIVATE "-framework Metal" ...)` (`libs/bgl/CMakeLists.txt:146`) — and
never links `bgl_metal`, whose objects arrive through `$<TARGET_OBJECTS:>`, which carries no usage
requirements. (The D3D12 branch does link `bgl_d3d12`.) So on the backend this feature targets,
nothing `bgl_metal` declares can reach `bgl` by any path.

**Third-party source is built inside this tree.** `FetchContent` pulls `metalcpp`
(`libs/bgl/src/metal/CMakeLists.txt:4`) and `QtNodes` (`apps/editor/CMakeLists.txt:17`) in as
subdirectories, and the root `CMakeLists.txt:115-120` already documents that their contents "are not
ours to order or to build". Anything applied at directory scope reaches them.

**The test runner is discovery-driven and shards.** `scripts/run_tests.py` finds every `EXECUTABLE`
target ending `_tests` through the CMake File API codemodel (`find_suites`, `:135`), runs each with
cwd set to its own output directory (`:265`, and `:122` on the sharded path), and by default splits
each suite across four concurrent processes (`run_sharded`, `:97`). It builds by invoking
`scripts/build.py` with **only** `--config` forwarded (`:202-205`) — never a preset or a build dir. It
passes no `env=` to any child, so subprocesses inherit `os.environ`.

**Diff line-range parsing already exists.** `scripts/tidy.py:109` `changed(ref)` returns
`{abs path: [(first, last), …]}` from `git diff --unified=0 --diff-filter=ACMR {ref}...HEAD` — merge-base
semantics, already filtered to C/C++ extensions.

**Machine-local tool resolution has a shape, and it does not extend to the coverage tools.**
`scripts/util/config.py` resolves each tool from `config.json` then falls back to auto-detection, and
derives `clang++` as clang's neighbour in the same directory (`find_clang`, `:222-235`). On this
machine `config.json` records `clang` as `/usr/bin/clang`, but `/usr/bin/llvm-cov` and
`/usr/bin/llvm-profdata` do not exist; both live under `/Library/Developer/CommandLineTools/usr/bin/`
and are reachable only through `xcrun`.

**`build*/` is already git-ignored**, so any artifact written under a coverage build directory needs
no `.gitignore` change.

### What was verified on the toolchain

On Apple clang 21.0.0 with Apple LLVM 21.0.0, before this plan was written and again after the first
review of it:

* `-fprofile-instr-generate -fcoverage-mapping` compiles, and three *concurrent* processes writing
  through an `LLVM_PROFILE_FILE` pattern containing `%p` produce three distinct `.profraw` files that
  `llvm-profdata merge -sparse` combines.
* A single `%p` profile correctly captures counters from an instrumented executable **and** an
  instrumented dylib loaded into the same process. `%m` is not needed.
* `llvm-cov` reports only the images it is handed. `llvm-cov report ./app -instr-profile=…` omits an
  instrumented `liblib.dylib` **entirely and silently** — exit 0, no warning; the dylib's rows appear
  only with `-object ./liblib.dylib`. `export --format=lcov` behaves the same way, emitting one `SF:`
  record instead of two.
* Passing several images that each embed the same static library does **not** double-count it.
  `llvm-cov` dedupes identical function records: four objects sharing one static library report that
  library's regions, lines and branches identically to a single-object run, and emit one `SF:` record.
* An `-object` carrying no coverage mapping at all is tolerated — exit 0, no warning. Both facts are
  what make "enumerate every executable and shared library" safe rather than merely convenient.
* `-fprofile-instr-generate` is a **link** flag as well as a compile flag. An instrumented static
  library linked into an uninstrumented executable fails with
  `Undefined symbols: "___llvm_profile_runtime"`.
* `target_link_options(<lib> PUBLIC ...)` propagates the flag to consumers through all three shapes
  this tree uses — STATIC → executable, OBJECT → executable, and across a SHARED boundary — built and
  checked against a project mirroring them.
* Clang emits no coverage mapping for functions reached through `-isystem`, and does emit them for
  plain `-I`. This is what decides the report filter.
* `llvm-cov export --format=lcov` emits `DA:<line>,<count>` with absolute `SF:` paths.

The first round of verification exercised concurrency but never a shared library, which is exactly
why the `-object` requirement was missed the first time through. `bgl` is a shared library, and it is
the largest instrumented thing in the tree.

## Decisions

**macOS and clang only.** MSVC has no source-based coverage; OpenCppCoverage would be a second,
unrelated tool producing PDB-based line counts with no region or branch data. `windows-clang-dx12-debug`
*could* carry it through clang-cl, but that preset is not what CI compiles (`ci.yml` builds
`windows-ninja-msvc-dx12-debug`) and not what most Windows work uses, so a coverage mode available on
exactly one Windows preset is a trap rather than a feature. Rejected both; the doc says plainly that
coverage is a macOS-host capability.

**A separate preset and build directory, not a flag on the debug build.** Rejected: instrumenting
the default developer build. Three reasons, in order of weight. The dev loop here *is* the engine —
the editor and examples are run out of the debug build and judged on how the frame looks and how
long it takes, and current work (TAA, disocclusion) depends on those timings being real. Instrumented
binaries dump a profile on every exit into their cwd, and `just run` sets cwd to the output
directory, so `bin/` would silently accumulate profiles from every editor launch and stale ones would
pollute the next merge. And it could not be universal anyway: no MSVC preset can have it, so
"always on while developing" would mean on for some contributors and impossible for others.

**`enable_coverage(<target>)` sets compile *and* link options, and is not a mirror of
`enable_strict_compiler`.** Rejected: copying that function's shape — compile options, PRIVATE, at the
same 12 sites. It does not link. Instrumented objects emit a reference to `__llvm_profile_runtime`
that only the driver's profile runtime resolves, so every executable that links an instrumented
library needs the flag too, and `assetlib_cli`, `editor`, `editor_tests` and the five example binaries
have no call site to put it at. So `enable_coverage` issues
`target_compile_options(<target> PRIVATE …)` **and** `target_link_options(<target> PUBLIC …)`, and the
`PUBLIC` half is the point: it rides the link graph to every consumer, which is what keeps the call
sites confined to the libraries. It reaches `editor` and `editor_tests` because they consume
`editor_lib` by link rather than by `$<TARGET_OBJECTS:>`; C1's gate is what proves it.

**`bgl` needs no call of its own — corrected during C1.** This plan first claimed
`enable_coverage(bgl)` was load-bearing, arguing from the Metal branch never linking `bgl_metal`.
That overlooked `target_link_libraries(bgl PUBLIC bgl_objects glm::glm)`
(`libs/bgl/CMakeLists.txt:113`): `enable_coverage(bgl_objects)`'s `PUBLIC` link option rides that
edge to the dylib's link line and on to every consumer, which C1's precheck verified empirically in
a project mirroring the shape. `bgl`'s own sources are only `BGL_SHARED_HEADERS`, so a call on it
would instrument nothing and add no link flag that is not already inherited. There are 13 call
sites, not 14.

**Per-target, not directory-scoped.** Rejected: `add_compile_options` at the root guarded by
`BUILD_COVERAGE`, which is one line instead of thirteen. It would instrument `QtNodes` and `metalcpp`,
inflating build time and putting third-party files in every report. Per-target calls keep the vendored
trees out by construction rather than by a filter someone has to maintain.

**The editor is instrumented, so the call-site set deliberately differs from
`enable_strict_compiler`'s.** `enable_coverage` is called at 13 sites across six files: 11 of the
12 above (all but `bgl`, per the correction), plus `editor_lib` and `editor_tests` under
`apps/editor`. Rejected: keeping the two sets identical for
symmetry, which would leave `editor_tests` — the third-largest suite — running under `just coverage`
for its full wall-clock while reporting nothing about the editor. The sets differ because the
constraint differs: strict warnings stay out of `apps/` because Qt and moc-generated code trip them,
and instrumentation is not a warning.

**The report is filtered by an allowlist of source roots, not a denylist of generated output.**
Clang emits no coverage mapping for functions reached through `-isystem`, so most third-party code is
absent for free: `metalcpp` is `SYSTEM PRIVATE` (`libs/bgl/src/metal/CMakeLists.txt:35`), and the
vcpkg imported targets (glm, Catch2, Qt) get SYSTEM interface include directories by default. Two
things escape that. `QtNodes` arrives through `FetchContent_MakeAvailable`
(`apps/editor/CMakeLists.txt:22`) as an ordinary non-imported target, so its interface include
directories are *not* SYSTEM and any inline or template definition in its headers lands in the report
under `build/_deps/qtnodes-src/`; and the autogen directory is added with a plain
`target_include_directories(editor_lib PUBLIC ...)` (`apps/editor/CMakeLists.txt:89`), so `ui_*.h`
appears too. Rejected: a `moc`/`ui`/`qrc` denylist, which catches the second and misses the first. The
allowlist already exists — `SOURCE_ROOTS = ("libs", "apps", "examples")` at `scripts/tidy.py:62`,
under the comment that everything else "is somebody else's to name" — and moves into `scripts/util/`
with `changed()`.

**Reporting passes every instrumented image as its own `-object`.** Rejected: reporting against the
suite executable alone. `bgl` is SHARED, so nearly all of `libs/bgl` lives in `libbgl.dylib` and would
be omitted from every report and every lcov record without a word of warning — and the "out of scope"
note below, which says `bgl`'s number is low and misleading, would make the absence look intended.
The image list is enumerated from the File API codemodel (`ct.load_targets`,
`scripts/util/cmake_tools.py:82`, which already returns per-target `type` and `artifacts`) rather than
hardcoding `bgl`, so a second shared library added later is picked up without touching this code.

**No `-fprofile-update=atomic`.** `libs/assetlib/src/envmap_bake.cpp:683` runs a real `std::thread`
pool (and a second at `:795`), so counter updates there do race. Atomic counters were rejected because
the race cannot affect what this feature consumes: the increment is load/add/store on an unsigned
counter that starts at zero, so every store writes `loaded + 1` and no store can ever write zero. A
region that executed can lose increments but can never report **uncovered**. Execution *counts* under
that pool are therefore unreliable while the covered/not-covered verdict is sound — and
covered/not-covered is the entire output. The doc records that counts are indicative, not exact.

**`LLVM_PROFILE_FILE` is an absolute path containing `%p`, under the coverage build directory.**
Without `%p` the four concurrent shards of a suite overwrite one file and the merge sees one shard's
data. Relative, the profiles land wherever each suite's cwd happens to be — scattered through `bin/`
next to the binaries. Absolute and under the build dir, they are contained, already git-ignored, and
disposable as a unit.

**`llvm-cov` and `llvm-profdata` come from `xcrun`.** Rejected: new `tools` keys in `config.json`,
and deriving them from the configured clang's directory the way `clang++` is derived. The version
constraint here is hard — a profile written by one clang is unreadable to a mismatched `llvm-profdata`
— and both rejected options let the two drift silently. `xcrun` resolves against the active developer
directory, the same toolchain the build used, which makes the match structural. The neighbour rule is
additionally just false on macOS, where clang is in `/usr/bin` and the coverage tools are not.

**Diff coverage reuses `tidy.py`'s `changed()`, lifted into `scripts/util/`.** Rejected: parsing
`@@` hunk headers a second time inside `coverage.py`. `changed()` already returns merge-base line
ranges keyed by absolute path and already filters to C/C++ sources, which is exactly the input the
`DA:` intersection needs. It moves to `scripts/util/gitdiff.py` and `tidy.py` imports it from there,
so there is one implementation. It is not self-contained: it reads `SOURCE_EXTS` (`tidy.py:58`) and
calls `is_generated` (`:69`), so both move with it — as does `SOURCE_ROOTS` (`:62`), which is what
gives `coverage.py` the report allowlist above. Leaving any of them behind makes the two modules
import each other.

**Diff coverage is parsed from `--format=lcov`, not `--format=text`.** The text (JSON) export gives
`segments` as `[line, col, count, …]` tuples that have to be reassembled into per-line state; lcov
gives `DA:<line>,<count>` directly. Same data, one less thing to get wrong.

**Coverage is a report, never a gate.** No threshold, no CI job, and nothing added to the pre-commit
hook. Failing a build on a percentage is what turns the signal into a target. It is run when someone
— or an agent — wants to know what a change left untested.

## What changes

| File | Change |
|---|---|
| `cmake/enable_coverage.cmake` | new; compile + `PUBLIC` link options, a no-op unless `BUILD_COVERAGE` |
| `CMakeLists.txt` | `include()` it beside the other `cmake/` modules |
| `libs/{core,bgl,assetlib,gamelib}/CMakeLists.txt` | `enable_coverage(<target>)` at 11 of the 12 `enable_strict_compiler` sites (all but `bgl`) |
| `apps/editor/CMakeLists.txt`, `apps/editor/tests/CMakeLists.txt` | `enable_coverage` for `editor_lib` and `editor_tests` — 2 sites with no strict-compiler counterpart |
| `CMakePresets.json` | `macos-clang-metal-coverage`, configure + build preset |
| `scripts/util/gitdiff.py` | new; `changed()`, `is_generated`, `SOURCE_EXTS`, `SOURCE_ROOTS` moved out of `tidy.py`, which imports them |
| `scripts/format.py` | its own copy of `is_generated`/`GENERATED_BANNER` removed; imports from `gitdiff.py` |
| `scripts/coverage.py` | new; build, run, merge, report |
| `justfile` | `coverage` recipe |
| `docs/coverage.md` | new; created by C1, extended by C2 and C3 |
| `CLAUDE.md` | documentation-index entry, in C1 with the page |

**What could break.** The safety property the whole design rests on is that `enable_coverage` expands
to nothing unless `BUILD_COVERAGE` is set, so every existing preset compiles byte-identical command
lines. C1's gate proves that rather than asserting it. Beyond that: the coverage preset inherits
`debug`, so `BUILD_TESTS` and `BERNINI_BUILD_EXAMPLES` are both ON and the examples must link — they
are the check that the `PUBLIC` link half works. `bgl_tests` is already the slow suite and
instrumentation makes it slower, though far less than the ~30% figure suggests, since its wall-clock
is dominated by per-test device creation and GPU work that instrumentation does not touch. And
`editor_tests` exists only where `find_package(Qt6)` succeeded, so the suite set is host-dependent and
the tooling must not assume a fixed list.

One sharp edge to document rather than fix: `cfg.build_dir(None)` returns `None` when the configured
preset's directory has no File API reply (`scripts/util/config.py:173-174`), and
`ct.find_build_dirs(None)` then globs every `build/*` (`scripts/util/cmake_tools.py:49`). On a clone
where only the coverage directory has been configured, a plain `just test` would run the instrumented
binaries. Narrow, but it belongs in the doc's troubleshooting section.

## Tasks

**C1 — the build mode.** `cmake/enable_coverage.cmake`, the `BUILD_COVERAGE` cache variable, the
`macos-clang-metal-coverage` preset, the 13 call sites, `docs/coverage.md` covering the mode and how
to drive `llvm-cov` by hand — including the `-object` argument, which is what makes manual use
correct — and the `CLAUDE.md` index entry. Adds no script.

*Gate:* `just build --preset macos-clang-metal-coverage` builds the suites **and the examples and
`assetlib_cli`**, which is what proves the link half propagates. Running `core_tests` by hand with
`LLVM_PROFILE_FILE` set produces a profile that `llvm-profdata merge` and `llvm-cov report` accept
with non-trivial numbers. Running `bgl_tests` by hand and reporting **with** `-object` on
`libbgl.dylib` shows `libs/bgl` rows that are absent without it. **And** `compile_commands.json` from
`macos-clang-metal-debug` contains no `-fprofile` or `-fcoverage` flag, proving the default build is
untouched.

**C2 — `just coverage`.** `scripts/coverage.py`: build the coverage preset, set an absolute `%p`
profile pattern in the environment, delegate the run to `run_tests.py --no-build --build-dir <coverage
dir>` (which inherits that environment), merge every `.profraw`, and emit both an HTML report for a
human and a per-file summary on stdout. The report and export steps enumerate every `EXECUTABLE` and
`SHARED_LIBRARY` artifact from the codemodel and pass each as `-object`. Takes the same suite-name
filters as `just test`. Stale `.profraw` from a previous run is cleared before the suites start, or
the merge silently mixes runs.

*Gate:* `just coverage core assetlib bgl` produces a merged profile, an HTML report and a printed
summary in which **at least one `libs/bgl/**` file appears with a non-zero region count** — the
assertion that the `-object` enumeration works, and the one thing a plumbing-only gate would miss. A
second run with no source change reuses the warm build directory rather than recompiling.

**C3 — `--diff`, and the docs.** Move `changed()` from `scripts/tidy.py` into `scripts/util/gitdiff.py`,
with `is_generated`, `SOURCE_EXTS` and `SOURCE_ROOTS`, and have `tidy.py` import them. `just coverage
--diff` intersects the lcov `DA:` records with those line ranges and prints the lines the diff added
that no test executed — human-readable by default, `--json` for an agent. Finishes `docs/coverage.md`.

*Gate:* a scratch commit adding a deliberately untested function makes `--diff` name exactly its
lines; reverting it leaves the output empty. Run once against a `libs/core` file and once against a
`libs/bgl/src` file, since only the second exercises the dylib path. `just tidy --changed` still works
after the move.

C1 → C2 → C3 in order; each rests on the last, and each leaves the tree complete.

## Deliberately out of scope

**No CI job.** Both `ci.yml` jobs are compile-only — the runners have no GPU and no Qt — so `bgl_tests`,
`gamelib_tests` and `editor_tests` cannot run there at all. `core_tests` and `assetlib_tests` are
headless (`assetlib` is forbidden from linking `bgl`) and *could* carry a diff-coverage job on the
macOS runner. That is a coherent follow-up and deliberately not part of this feature: the stated
motivation is feedback while a change is being written, which is a local loop.

**No Windows coverage**, per the decision above.

**No shader coverage.** The 2.6k lines of Slang are invisible to `llvm-cov`, and much of `bgl`'s C++
is thin RHI translation whose correctness lives on the GPU rather than in a branch. `bgl`'s number
will be low and will not mean what it appears to mean; `core`, `assetlib` and the editor's material
graph are where it is informative. The doc says so, so nobody optimises the wrong figure — and C2's
gate exists to make sure a low number is a real measurement rather than a missing `-object`.
