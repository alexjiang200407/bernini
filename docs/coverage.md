# Coverage — the instrumented build mode

A separate build mode that compiles the tree with clang's source-based coverage
(`-fprofile-instr-generate -fcoverage-mapping`), so a test run can be asked which source lines it
actually executed. macOS/clang only: MSVC has no source-based coverage, so this is a capability of
the macOS host, not of the project. The output is a diagnostic — there is no threshold, no CI job,
and nothing in the pre-commit hook, deliberately: a coverage number that gates a merge rewards
tests that execute lines without asserting anything about them.

**This document is a map, not a mirror.** [cmake/enable_coverage.cmake](../cmake/enable_coverage.cmake),
[CMakePresets.json](../CMakePresets.json) and [scripts/coverage.py](../scripts/coverage.py) are the
source of truth; when this doc disagrees, trust them, then fix this doc.

## just coverage

```bash
just coverage                     # build the coverage preset, run every suite, report
just coverage core assetlib      # only suites matching these names, like `just test`
just coverage --no-build         # run what is already built
just coverage -- "[materialgraph]"  # forward a Catch2 filter to every suite
```

One command does the whole pipeline: build `macos-clang-metal-coverage`, run the suites through
the same runner as `just test` (same discovery, same sharding, cwd per suite), merge the per-process
profiles, and report — a per-file summary on stdout and an HTML report under
`build/macos-metal-coverage/coverage/html/`. Every executable and shared library from the codemodel
is passed to `llvm-cov` as an `-object`, and the report is filtered to `libs/`, `apps/` and
`examples/`. Stale profiles are deleted before the suites start — a merge across runs would mix
them silently. A failing suite does not stop the report; the exit code carries the failure.

## Diff coverage

```bash
just coverage --diff                  # added lines of the staged diff that no test executed
just coverage --diff origin/master    # ... of everything this branch changed (merge-base)
just coverage core --diff --json      # machine-readable, running only the core suite
```

The output this feature exists for: *these lines of the diff are executed by no test* — checkable,
hard to game, and exactly what a reviewer otherwise does by eye. Each line of output is
`file:first-last`, clickable and greppable; `--json` emits
`{"uncovered": {"<file>": [[first, last], …]}, "no_data": […]}` for an agent — and stdout then
carries only the JSON object (everything else reports on stderr), so it pipes cleanly. The ref semantics
are `just tidy --changed`'s: staged diff by default, `REF...HEAD` merge-base with a ref.

Reading the answer right:

* A line is reported only if it is executable and its count is zero. Changed lines with no `DA:`
  record — comments, blanks, declarations — are not "covered"; they are not statements.
* A changed file that appears under `no_data` was compiled into no instrumented image (or only ever
  included through `-isystem`). That is a louder warning than an uncovered line: nothing measured
  it at all.
* Uncovered lines never fail the command. Coverage is a diagnostic, not a gate — a threshold here
  would reward tests that execute lines without asserting anything.
* Run the suites that could plausibly reach the change (`just coverage core --diff` while iterating
  on `libs/core`); the full default run is the honest final answer.

## Design Choices

* **A separate preset and build directory, never the debug build.** `macos-clang-metal-coverage`
  configures `build/macos-metal-coverage` with `BUILD_COVERAGE=ON`; it inherits everything else
  from the debug preset, so `BUILD_TESTS` and `BERNINI_BUILD_EXAMPLES` are on. The debug build
  stays uninstrumented because the dev loop is judged on real frame timings, and because an
  instrumented binary writes a profile into its cwd on every exit — `bin/` would silently
  accumulate stale profiles from every editor launch.
* **`enable_coverage(<target>)` is per-target, not directory-scoped.** FetchContent builds QtNodes
  and metalcpp as in-tree subdirectories; a directory-scoped flag would instrument both. The call
  sites sit beside `enable_strict_compiler`'s, plus `editor_lib` and `editor_tests`, which have no
  strict-compiler site — strict warnings stay out of `apps/` because Qt and moc output trip them,
  but instrumentation is not a warning.
* **The flag is a `PUBLIC` link option as well as a compile option.** Instrumented objects
  reference `___llvm_profile_runtime`, which only a `-fprofile-instr-generate` link resolves.
  `PUBLIC` rides the link graph, so `assetlib_cli`, `editor`, `editor_tests` and the examples get
  it from the libraries they link without call sites of their own.
* **`bgl` has no call site of its own.** Its only sources are headers, and the profile runtime
  reaches the dylib's link line through `target_link_libraries(bgl PUBLIC bgl_objects)` — the
  `$<TARGET_OBJECTS:>` absorption carries no usage requirements, but that link edge does.
* **Counts are indicative, not exact.** `-fprofile-update=atomic` is deliberately off. Counter
  increments race under real thread pools (`libs/assetlib/src/envmap_bake.cpp`), which can lose
  counts — but a raced non-atomic counter can never write zero for an executed region, so the
  covered/uncovered verdict is sound. Treat execution counts under threaded code as a floor.

## Driving a report by hand

The tools must come from `xcrun`, which resolves against the active developer directory — the same
toolchain that compiled the code. A mismatched `llvm-profdata` cannot read the profile. (They are
not beside the configured clang: `/usr/bin/clang` has no `llvm-cov` neighbour.)

The example keeps a profile directory of its own, apart from `coverage/` — which `just coverage`
deletes stale profiles from on every run.

```bash
just build --preset macos-clang-metal-coverage

BIN=build/macos-metal-coverage/bin
LLVM_PROFILE_FILE="$PWD/build/macos-metal-coverage/profiles/core-%p.profraw" \
    ./$BIN/core_tests   # cwd must be $BIN for suites that read assets/ -- use (cd $BIN && ...)

xcrun llvm-profdata merge -sparse build/macos-metal-coverage/profiles/*.profraw \
    -o build/macos-metal-coverage/merged.profdata
xcrun llvm-cov report ./$BIN/core_tests \
    -instr-profile=build/macos-metal-coverage/merged.profdata
```

Two things every manual invocation must get right:

* **`LLVM_PROFILE_FILE` needs `%p` and an absolute path.** Concurrent processes — `just test`
  shards each suite four ways — otherwise overwrite one file, and the merge sees a single shard.
  Relative patterns scatter profiles into whatever each process's cwd is.
* **`llvm-cov` reports only the binary images it is handed, and omission is silent** — exit 0, no
  warning. `bgl` is a shared library, so nearly all of `libs/bgl` lives in `libbgl.dylib`; a
  report for any suite that loads it (`editor_tests`, the examples) must pass
  `-object $BIN/libbgl.dylib`, or everything that lives in the dylib is missing — a few
  header-inline rows compiled into the executable still appear, which is exactly what makes the
  truncation easy to miss. Passing every executable and dylib
  as `-object` is safe: identical function records are deduplicated, and an uninstrumented image
  is tolerated.

`bgl_tests` is the exception that proves the rule: it embeds `bgl_objects`' and the backend's
object files directly rather than linking the dylib, so its own binary carries the `libs/bgl`
mapping.

## What the report can and cannot say

Slang shaders are invisible to `llvm-cov`, and much of `bgl`'s C++ is thin RHI translation whose
correctness lives on the GPU. `bgl`'s number will be low and does not mean what it appears to
mean; `core`, `assetlib` and the editor's material graph are where line coverage is informative.

## Troubleshooting

* **A suite aborts or reports zero coverage after a toolchain update** — the profile format is
  tied to the clang version; wipe `build/macos-metal-coverage` and rebuild.
* **`just test` unexpectedly runs instrumented binaries** — on a machine where only the coverage
  directory has been configured, the build-dir fallback globs every `build/*`. Configure the
  regular debug preset (`just build`) and the configured preset wins again.
* **Stale `.profraw` files** — profiles accumulate per PID; merging across runs mixes them.
  Delete the profile directory before a fresh run.
