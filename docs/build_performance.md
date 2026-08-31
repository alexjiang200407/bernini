# Build performance

Two mechanisms: **precompiled headers** decide how much a translation unit parses, and measurement
decides whether a change to them did anything. The second is not optional here — a PCH change is as
likely to cost as to save, and only a measurement tells you which.

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

**The root PCH may be written against; a subsystem PCH may not.** `PCH/pch.h` reaches every compiled
target in the tree, which is what makes the rule in [CLAUDE.md](../CLAUDE.md) — never `#include` a
standard header — safe. A subsystem PCH reaches only some: `libs/assetlib/src/pch.h` is `PRIVATE`
while assetlib's public headers are compiled by `gamelib`, `editor_lib` and `assetlib_cli` without
it, and `MetalImpl.cpp` and `MetalSurface_mac.mm` carry `SKIP_PRECOMPILE_HEADERS ON` because
Objective-C++ cannot consume a C++ PCH. So a source file still includes the Qt, glm and json headers
it uses; the PCH only makes them free.

The exception is a name a PCH *defines* rather than includes, which no `#include` could reach:
`libs/bgl/src/pch.h` declares the `bgl::logger` alias every bgl source logs through
([docs/gfx_debug.md](gfx_debug.md)). That is why `MetalImpl.cpp`, which skips the PCH, does not log.

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
against it: `ViewportState.h` uses `gassert`, `Shader.h` uses `slang`, `Srv.h` uses
`DescriptorHandle`, `Uniforms.h` uses `glm`, and none of them includes what it uses. So every target
that compiles bgl's internals — `bgl_objects`, `bgl_metal`, `bgl_tests` — must carry that header,
however it reaches them. `bgl_tests` gets it by `target_force_include`; removing it there to make
the suite cheaper does not compile.

`libs/bgl/src/metal/pch.h` is the same case and is documented as such in
[libs/bgl/CLAUDE.md](../libs/bgl/CLAUDE.md): `metal_cpp.h`, the slang headers and the two error
checkers are all used with no `#include` at the use site.

That is the cost of writing against a subsystem PCH, which is why [CLAUDE.md](../CLAUDE.md) says not
to. Both predate this change; fixing them is include hygiene and a change of its own.
