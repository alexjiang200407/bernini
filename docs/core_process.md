# core_process

`core` is a static library, so every binary that links it gets a copy: the executable, the renderer
DLL where the build makes one, and every editor plugin. For a function a copy costs nothing. For state it is a bug. A second
log flag opens a second log file, a second tag registry keeps the renderer's GPU bytes out of the
executable's memory report, and a second id counter hands Tracy colliding allocation ids.

So `core` is split along that line. **What is wrong when doubled lives in `core_process`, which is
shared; everything else stays in `core`, which is static.** Source does the same with `tier0` (one
DLL holding the allocator, logging, asserts and the profiler) beside static `tier1` and `mathlib`.
A consumer links `core` as before, and `core` links `core_process` `PUBLIC`.

| Held once, in `core_process` | Where |
|---|---|
| the memory tag registry, the whole-process total, the allocation id sequence | [src/process/memory.cpp](../libs/core/src/process/memory.cpp) |
| spdlog's registry, and with it the default logger | compiled spdlog, linked through `core_process` |
| Tracy's client, under `BERNINI_PROFILING` | `Tracy::TracyClient`, linked through `core_process` |

**The directory decides.** A source under `libs/core/src/process/` is compiled into `core_process`
and nowhere else. Its declarations stay in `core`'s public headers, where a reader already looks,
and are marked `CORE_PROCESS_API` ([process_api.h](../libs/core/include/core/process_api.h)). The
marker says which binary defines the symbol and is the Windows export. A header template may still
keep a `static` (`table_for<Tag>()` does) as long as it only caches something `core_process` hands
back the same way every time.

A state-free rule belongs in `core`, not here. `init_file_logger` stays in `core` because the
"already installed" answer it needs is spdlog's registry. `write_atomic` names its temp file by
thread, so it has no counter to share.

## Linkage

**Shared exactly where the renderer is.** The root [CMakeLists.txt](../CMakeLists.txt) makes one
decision, `BERNINI_RENDERER_LIBRARY_TYPE`, and `bgl` and `core_process` are both built as
it says:

| Build | `bgl` | `bgpu` | `crowdlib` | `core_process` |
|---|---|---|---|---|
| default — a game embedding the engine, a build without Qt | `STATIC` | `STATIC` | `STATIC` | `STATIC` |
| `BERNINI_SHARED_RENDERER=ON` | `SHARED` | `SHARED` | `SHARED` | `SHARED` |
| `BERNINI_EDITOR_SDK` on — a top-level build with Qt, by default | `SHARED` | `SHARED` | `SHARED` | `SHARED` |
| `RENDERER_BACKEND=NONE` | not built | the compiler and the RHI's interfaces, no backend, `STATIC` | not built | `STATIC` |
| `RENDERER_BACKEND=VULKAN` | not built | the same and the Vulkan context ([bgpu.md § Vulkan](bgpu.md#vulkan)), `STATIC` | not built | `STATIC` |

The default is the game that ships as one binary. The editor SDK implies the shared renderer:
its plugins are DLLs that reach the renderer through shared `gamelib`, and the editor links the
renderer as well, so a static one would be linked into two binaries. A static `core_process` beside
anything shared would be two copies again, so it is derived and is never an option of its own.
`BERNINI_SHARED_RENDERER` with a backend that has no renderer, `NONE` or `VULKAN`, is a configure
error.

The rule is not the renderer's alone: any engine library that holds process-wide GPU state is built
as the renderer is and lands in the same binary it does. `bgpu` is the one that holds the
device and the Slang sessions themselves ([bgpu.md](bgpu.md)), and `crowdlib` shares them
([crowdlib.md](crowdlib.md)); their `BGPU_API` and `CROWD_API` follow the same pattern.

`BGL_API` follows `CORE_PROCESS_API`: it exports and imports only under `BGL_SHARED`, which
`bgl`'s interface carries when the renderer is shared, and is empty otherwise.

Shared, it is one more library a binary loads. In the build tree it sits next to `bgl`: the
runtime directory on Windows, `lib/` on macOS, found through the build rpath. `just install` stages
it next to `assetlib_cli` ([libs/assetlib/CMakeLists.txt](../libs/assetlib/CMakeLists.txt)).

**A dependency with state of its own is linked by `link_once_per_process`**
([libs/core/CMakeLists.txt](../libs/core/CMakeLists.txt)):

- **Already shared** (the `x64-windows` triplet builds `spdlog.dll` and Tracy's DLL): linked
  `PUBLIC`, since it is once per process already.
- **A static archive** (`arm64-osx`): linked into `core_process` with `WHOLE_ARCHIVE`, so every
  symbol a consumer can reach is exported from it. Consumers get the archive's headers, definitions
  and dependencies, but never the archive itself. Given the archive, a binary would take its own
  copy of whatever `core_process` did not export.

This is why spdlog is the compiled target and not `spdlog_header_only`. Header-only spdlog puts its
registry in a function-local static of an inline function, which means one per binary on Windows.

## What proves it

`core_tests` `[process]` loads a fixture library at runtime
([tests/fixture/process_fixture.cpp](../libs/core/tests/fixture/process_fixture.cpp)) that links
`core` the way a renderer does. A `dlopen`ed image binds to its own copy of anything `core` still
holds, on every platform. The cases check that its charges reach this binary's report, its ids come
from this binary's sequence, its default logger is this binary's, and its `init_file_logger` opens
no second file.

The editor alone proves nothing on macOS. There, `libbgl.dylib` exports every `core` symbol
it links, and ld64 binds the executable to those exports before looking in `libcore.a`, so even a
split `core` looks single. `nm -m build/<preset>/bin/editor` shows where each symbol came from: after
this change, `register_table` and `tracy::GetProfiler` read `(from libcore_process)`.
