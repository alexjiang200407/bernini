# Build performance

Three mechanisms, each answering a different question: **precompiled headers** decide how much a
translation unit parses, **ccache** decides whether it is compiled at all, and the **jobserver**
decides how many compile at once across every checkout on the machine. Measurement decides whether a
change to any of them did anything, and it is not optional here — a PCH change is as likely to cost
as to save.

Measure before changing anything — `just build --time`.

## Measuring

```bash
just build --time              # build, then report where the time went
python3 scripts/build_timing.py --top 30   # report the last build again, without rebuilding
python3 scripts/build_timing.py --all --json
```

It reads `.ninja_log` in the build directory, which ninja writes for free — one line per edge, with
its start and end. Nothing is instrumented and no wrapper is involved, so the numbers are the build
that actually ran.

**Wall and CPU are different claims.** Wall is how long the build took and parallelism decides it;
CPU is the sum of every edge's duration, which is the work the machine did. A change that removes
work moves CPU. On an idle 12-core machine a clean build is around a minute of wall against twelve
minutes of CPU, so wall is nearly all parallelism — and CPU is what matters, because CPU is what
several checkouts contend for.

The rollup is **by target as well as by module**, and the target grain is the one that matters: a
module is several targets, and a change that helps a library while hurting its test suite nets out to
nothing at module grain. That is not hypothetical — it is how a PCH regression hid here once.

Only object files and PCHs count as compile time. moc, IDL codegen, asset staging and linking are
real build time but move for their own reasons, and a total that mixed them would hide the thing
being measured.

**A measurement on a loaded machine is not a measurement.** Sibling checkouts building at the same
time inflate everything by half again or more. Certify a comparison against a target the change did
not touch — if `core` and `examples` moved, the machine moved, not the code.

### Where the parsing actually goes

`.ninja_log` says what each edge cost; ninja's dependency database says *why*. This lists what a
target's TUs read that its PCH does not already cover, which is the number a PCH change moves:

```bash
ninja -C build/<preset> -t deps > /tmp/deps.txt
```

Each `.o` entry lists every header that TU read, and the target's `cmake_pch.hxx.pch` entry lists
what the PCH covers; the difference is what is being re-parsed per file.

## Precompiled headers

Every target compiles through `PCH/pch.h` — the standard library — and most add a subsystem header
of their own (`libs/<lib>/src/pch.h`, `apps/editor/src/pch.h`) for the third-party libraries that
subsystem leans on.

**No PCH may be written against, the root one included.** A precompiled header makes an include
free; it does not stand in for one. So a source file includes the standard, Qt, glm and json headers
it uses, and the PCH only decides what that costs.

That is the same line Unreal draws — the engine moved to it in the 4.15/4.16 era under the name IWYU
while keeping its shared and per-module PCHs — and the one Qt's `-no-pch` configure switch exists to
prove. It is enforced here by `misc-include-cleaner`, which a subsystem's own `.clang-tidy` switches
on ([docs/naming.md](naming.md)), and shown live by [`.clangd`](../.clangd).

The reason is not tidiness. A PCH is invisible to everything that reads one file at a time — a
person, clangd, clang-tidy — so a file that leans on one is a file none of them can judge. And the
subsystem PCHs do not even reach every target that compiles their headers: `libs/assetlib/src/pch.h`
is `PRIVATE` while assetlib's public headers are compiled by `gamelib`, `editor_lib` and
`assetlib_cli` without it, and `MetalImpl.cpp` and `MetalSurface_mac.mm` carry
`SKIP_PRECOMPILE_HEADERS ON` because Objective-C++ cannot consume a C++ PCH.

MSVC editor SDK builds disable the PCH for `core`, `assetlib` and `gamelib`. CMake's automatic DLL
export scan otherwise mistakes MSVC's `__@@_PchSym` bookkeeping records for an export named `__`;
the scan reaches `core` through the static archive as well as the two DLLs' own object files.

### A PCH is not free per translation unit

**The trap, and the reason to measure rather than reason about it.** A precompiled header is
deserialized into *every* TU of a target, including the ones that need none of it. So the win is a
header's cost times the fraction of sources that include it, and the loss is its cost times all of
them. A big header that a minority reach is a net loss.

That is not hypothetical here. `nlohmann/json.hpp` is ~900 KB and 7 of assetlib's 59 sources reach
it; putting it in the PCH cost **28%**. `core/glm.h`, reached by 40 of the same 59, saved **16%**.
`QtCore`, reached by 56 of the editor's 60, saved 37%. Same mechanism, opposite signs, decided
entirely by that fraction.

Counting bytes re-parsed (above) finds the candidates but cannot settle them: it measures what a PCH
avoids and is blind to what it adds. Build the target three times each way and take the minimum.

### And it is paid in memory, per parallel job

The other axis, and the one that bites the machine rather than the build. A PCH is loaded by every
compile, so its size is multiplied by the job count — and then again by the number of checkouts
building at once. On this workspace that is twelve jobs across three checkouts: a 12 MB difference
in one PCH is ~430 MB resident, and the machine pays it in swap rather than in compile time. The
symptom is a laggy desktop during a build, which nothing about the build itself would point at.

So a PCH addition that measures *no faster* is not free — it is a straight loss. The editor's PCH
carries `QtCore` alone for exactly this reason: adding the `QtGui` and `QtWidgets` umbrellas on top
measured no faster (71.0s against 68.1s, inside the noise) and took the PCH from 52 MB to 64 MB.
Check the size alongside the time:

```bash
find build/<preset> -name 'cmake_pch.hxx.pch' -exec ls -lh {} \;
```

### What may not go in one

- **A header whose behaviour depends on a macro defined before it.** `tiny_gltf.h` and `stb_image.h`
  put their implementation *outside* the include guard, and `bmesh_gltf.cpp` defines
  `TINYGLTF_IMPLEMENTATION` before including them — a PCH would get there first and guard the
  definitions out. `MetalImpl.cpp` skips the PCH for the same reason.
- **`<glm/*>` directly.** `core/glm.h` sets `GLM_FORCE_DEPTH_ZERO_TO_ONE` and friends first, and glm
  reaching a TU unconfigured builds a projection matrix for the wrong depth range. Put `core/glm.h`
  in the PCH and glm is configured everywhere, deterministically rather than by include order.
- **Our own headers that change.** A PCH is rebuilt when anything in it changes, and everything
  behind it with it — so a churning header in a PCH turns a one-file edit into a whole-target
  rebuild. Third-party and standard headers qualify; ours generally do not.

### What cannot come out of one

`libs/bgl/src/pch.h` is load-bearing, not an optimisation. bgl's internal headers are written
against it: `ViewportState.h` uses `core::ensure`, `Shader.h` uses `slang`, `Srv.h` uses
`DescriptorHandle`, and none of them includes what it uses. So every target
that compiles bgl's internals — `bgl_objects`, `bgl_metal`, `bgl_tests` — must carry that header,
however it reaches them. `bgl_tests` gets it by `target_force_include`; removing it there to make
the suite cheaper does not compile.

`libs/bgl/src/metal/pch.h` is the same case and is documented as such in
[libs/bgl/CLAUDE.md](../libs/bgl/CLAUDE.md): `metal_cpp.h`, the slang headers and the two error
checkers are all used with no `#include` at the use site.

That is the cost of writing against a subsystem PCH, which is why [CLAUDE.md](../CLAUDE.md) says not
to. Both are being unpicked by the include sweep; until `libs/bgl` carries
`misc-include-cleaner` in its `.clang-tidy`, this section is still the truth about it.

## ccache

Detected, never required: `cmake/enable_compiler_cache.cmake` looks for it and the build compiles
uncached when it is absent. `just init` offers to install it.

ccache keys an object on the preprocessed source, the compiler and the flags, so it returns one
built before a `git checkout`, a rebase, or a wiped build directory — none of which ninja survives,
because its incremental build is mtimes and a dependency graph inside one build directory.

The settings ride in a wrapper script generated into `build/<preset>/compiler-cache/` rather than in
the environment, because this build is driven by `scripts/build.py`, by ninja directly and by an
IDE, and a variable exported by only one of them would leave the others missing every time.

`sloppiness = pch_defines,time_macros` is **not optional** with a PCH on every target: ccache cannot
tell whether a PCH used `__TIME__`, nor see the defines a PCH already resolved, so without it every
TU that uses one is declined. clang additionally stamps a PCH with a timestamp that moves on every
rebuild, so the PCH is compiled `-Xclang -fno-pch-timestamp` — otherwise nothing behind it can ever
hit.

The store is gigabytes of small files rewritten constantly, so `just init` drops a
`.metadata_never_index` marker in it on macOS — the same thing `ws init` does for every checkout's
`build/`, and for the same reason. A cache that speeds the compiler up while feeding a content
indexer is not a saving.

```bash
ccache --show-stats     # hit rate, size
ccache --zero-stats     # before a measurement
cmake -DBERNINI_COMPILER_CACHE=OFF ...   # opt out for one build dir
```

**An entry belongs to one build tree; nothing is shared between worktrees.** There is no
`CCACHE_BASEDIR`, so every path is hashed absolute. With one, the paths under the checkout were
rewritten relative — `build/<preset>/vcpkg_installed` included — and a clang PCH generated in another
worktree came back as a hit. But a `.pch` records the absolute paths of the headers it read, so the
TUs behind it compiled against the other worktree's headers, and a TU that included one of them
again (through its own copy, a different file) failed on redefinitions. Sharing could not work for
a TU behind a PCH anyway, and every TU here has one: it hashes the PCH's bytes, which carry those
paths. A debug build hashes its working directory besides. The within-tree win — a checkout, a rebase, a wiped build directory — is the
one this is for.

Two configurations get no cache by default, and say so at configure time rather than pretending.
Visual Studio and Xcode ignore compiler launchers entirely. **MSVC is refused on the compiler rather
than the generator**, because the `windows-ninja-msvc-*` presets drive `cl.exe` through Ninja, which
*does* honour a launcher — ccache's support for MSVC precompiled headers is an open issue with a
reported false *hit*, a wrong object returned rather than a miss, and every target here carries a PCH.

**`BERNINI_MSVC_COMPILER_CACHE=ON` (`just build -D BERNINI_MSVC_COMPILER_CACHE=ON`) is the way in, and it removes the
hazard instead of trusting ccache around it:** `target_precompile_headers` is replaced by a
force-include (`/FI`) of the same headers in the same order, so the translation unit sees what it
saw through the PCH but no compile ccache sees uses one — turning PCHs off outright does not build,
because sources lean on what their PCHs include. `/Zi` becomes `/Z7`, because a `/Zi` object names a
PDB the compiler writes as a side effect and no cache can replay that; `/MP` is dropped, which ccache
refuses and Ninja makes redundant; and the compiler is keyed by its content rather than its mtime.
And ccache runs in **depend mode**, keying an object on the source and the bytes of every header
`/showIncludes` reports, with its preprocessor fallback never taken. The fallback hashes preprocessed
text, which carries no comments, while a `/Z7` object's CodeView records a checksum of every file the
compile read — so a header edited only in a comment hit the entry made from the old header and came
back with the old header's checksum, a wrong object by the definition below. A comment edit now
misses, which costs a compile and is right.
It is off by default — a developer's build keeps its real PCHs, which are faster uncached — and
Windows CI turns it on, since CI compiles every file from nothing on every run.
`scripts/verify_compiler_cache.py` is the proof CI runs: it has the cache serve a seeded sample of
objects and compares each byte for byte with a fresh compile.

## The shared job budget

Several checkouts share one machine, and each build sizes itself against the whole of it: ninja with
no `-j` takes cores+2. Three checkouts building at once is three machines' worth of compilers on one
machine's cores, and all three slow down together.

A lock would be worse than the problem — a checkout building alone should still get the whole
machine. What is wanted is a budget the builds *share*, decided continuously rather than at launch,
and ninja 1.13 already speaks the standard for it: the GNU make jobserver. `scripts/util/jobserver.py`
holds the pool (a fifo on POSIX, a named semaphore on Windows) and points ninja at it through
`MAKEFLAGS`.

```bash
just build                     # shares the machine's budget
just build --jobs 4            # a smaller budget
just build --no-jobserver      # opt out; ninja takes the machine as before
```

The pool holds one token per core. ninja keeps one implicit token per instance on top of what it
draws, so N concurrent builds run up to cores+N compilers — bounded by the number of builds, and the
price of never idling a core.

**What it actually buys is memory.** Measured on `editor_lib` with the cache off, peak resident
memory across the compilers scales linearly at ~160 MB each, while wall time saturates at `-j 8`:

| `-j` | 2 | 4 | 6 | 8 | 12 | 20 | 28 |
|---|---|---|---|---|---|---|---|
| peak RAM | 624 MB | 929 MB | 1.2 GB | 1.4 GB | 2.0 GB | 3.5 GB | 4.2 GB |
| wall | 43s | 13s | 11s | 7s | 8s | 7s | 8s |

Past eight jobs the extra memory buys nothing at all. Uncapped, that waste multiplies by the number
of checkouts building — five of them is 70 compilers and roughly 11 GB, against 17 and 2.8 GB behind
a twelve-token pool. Reproduce it with the numbers above rather than trusting them:

```bash
find build/<preset> -path '*<target>.dir*' \( -name '*.o' -o -name '*.pch' \) -delete
ninja -C build/<preset> -j<N> <target>   # sample `ps -eo rss,comm` for clang while it runs
```

**It fails open.** A pool that cannot be created or joined produces a warning and an uncapped build,
which is what happened before it existed. A wrong cap would be a hang, and a hung build is a far
worse failure than a loud machine. A build killed while holding tokens leaks them until every build
exits, at which point the next one primes a fresh pool.

This is not the suite lock. [`scripts/util/lock.py`](../scripts/util/lock.py) lets **one** test suite
run at a time, because a suite holds a graphics device and two of them oversubscribe in a way no
budget divides; builds share a budget instead, because dividing them is exactly what works.
