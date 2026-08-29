# Profiling

CPU timing here is [Tracy](https://github.com/wolfpld/tracy). It is compiled into **every** preset,
debug and release — not gated the way `BUILD_COVERAGE` is, because a zone costs a few nanoseconds
where coverage changes the performance of everything it instruments.

It is still a switch, and the reason is not cost. Tracy's client opens a TCP socket and announces
itself over UDP from process start, so a profiler can find it — which is the whole mechanism, and is
not something a shipping binary should carry. `core` links it and everything links `core`, so the
switch has to exist before the tree grows more consumers rather than after.

`BERNINI_PROFILING` is declared at the root and defaults **off**; the `debug` and `release` presets
in `CMakePresets.json` turn it on, the same way they carry `BUILD_TESTS` and `BERNINI_BUILD_EXAMPLES`.
So every developer preset profiles, a build that asked for no preset carries none of it, and a
shipping preset would say so in the one place the other build toggles already live. To turn it off
for one configure, the command line still wins over the preset:

```bash
cmake --preset macos-clang-metal-debug -DBERNINI_PROFILING=OFF
```

Off is headers-only: without `TRACY_ENABLE` every zone macro expands to nothing, the client is not
linked, and no call site changes. The one thing that does not degrade on its own is naming a thread
— `tracy::SetThreadName` is an ordinary function, not a macro — which is why that goes through
`core::profiling::name_this_thread`
([thread_name.h](libs/core/include/core/profiling/thread_name.h)) and never through Tracy directly.
**A variable read only to feed a `ZoneTextF` needs `[[maybe_unused]]`**, or `-Werror` calls it dead
in that build; `bmesh_gltf.cpp`'s source size is the worked example.

This page is about **load and cook time**: what a bake cost, where a start-up went. GPU per-pass
timing is a separate, unbuilt thing (`ROADMAP.md` § Profiling), and `docs/gfx_debug.md` is where a
*wrong* frame is diagnosed rather than a slow one.

## Taking a capture

**Nothing is written to disk by the application, ever.** There is no Tracy log and no output
directory: the client holds the run in memory and serves it on TCP 8086, and a `.tracy` file exists
only because you asked for one at a path you chose. (`editor.log` / `bgl.log` beside the binary are
the *other* channel — spdlog and Qt, see [gfx_debug.md](gfx_debug.md) — and share nothing with this.)

The tools are not on `PATH`. vcpkg stages them per build directory:

```bash
TRACY=build/<preset>/vcpkg_installed/<triplet>/tools/tracy   # e.g. macos-metal-debug / arm64-osx
```

```bash
# Record headlessly. Start this FIRST -- it waits for the process to appear, which is the only
# way to catch a start-up.
$TRACY/tracy-capture -o start.tracy -f &
just run editor

# Turn a capture into per-zone totals.
$TRACY/tracy-csvexport start.tracy > start.csv
```

`tracy-csvexport` defaults to per-zone totals, which say nothing about what ran inside what. Add
`-u` and it emits one row per zone event with its thread, start and duration — enough to rebuild the
tree by interval containment on each thread, and the only way to check the shape of the
instrumentation without the GUI:

```bash
$TRACY/tracy-csvexport -u start.tracy > events.csv   # name, ns_since_start, exec_time_ns, thread, value
```

Both tools come from the `tracy` port's `cli-tools` feature, so vcpkg has already put them in
`build/<preset>/vcpkg_installed/<triplet>/tools/tracy/`.

## Looking at one

The **GUI** profiler is deliberately not vendored (it drags glfw, freetype, curl and imgui in for a
tool nobody builds from this tree), so it is installed once per machine:

```bash
brew install tracy          # macOS: 0.13.1, the version vcpkg vendors
```

On Windows, take the binary from the Tracy releases page. **Match the version.** Tracy version-locks
the client and the viewer, and a mismatch refuses to connect rather than degrading — the vendored
client is pinned by `vcpkg.json`, so read it before installing a viewer.

Two ways in, and only one of them works for a start-up:

* **Open a file.** `tracy` → *Open*, and pick a `.tracy` that `tracy-capture` wrote. This is the one
  to use for a cold start: the interesting window is over before anything could have connected, and
  a captured file has it.
* **Connect live.** The client announces itself over UDP, so a running editor appears in the
  discovery list and one click attaches. Good for a session already under way — a slow import, a
  panel that hangs — and no use for the thing that happened during launch.

There is no on-demand mode. The client buffers from process start, which is what makes a cold start
capturable; the cost is that a process left running for hours accumulates events, so a capture
session is a deliberate act rather than the way the editor is normally left open.

## Writing a zone

```cpp
#include <tracy/Tracy.hpp>

void
bakeSomething(const Mesh& mesh)
{
    ZoneScopedN("assetlib something");
    ZoneTextF("%zu submeshes, %zu vertex bytes", mesh.submeshes.size(), mesh.vertexData.size());
    …
}
```

**The name is a literal and the dimensions are text.** Tracy aggregates by name, so a name that
interpolates a mesh path produces one statistics row per asset and no total; the dimensions belong in
`ZoneTextF`, where they are visible on the zone that has them and summed nowhere. This is the one
rule worth stating, because the obvious spelling — a formatted name — is the one that ruins the
statistics view.

Tracy keys a zone on its **source location**, not on the string, so a zone in a function template
appears once per instantiation — `load<BMesh>` and `load<Skeleton>` are two rows both reading
"assetlib container load". That is usually what you want; it is only surprising if you expected the
name to be the key.

Zones nest by scope and are separated by thread automatically, so neither the containment nor the
thread has to be passed anywhere. A thread that does load work is worth naming once, at the top of
its body, so its track is legible rather than a number:

```cpp
core::profiling::name_this_thread("bgl-render");
```

The first name a thread is given is the one it keeps, so a pooled worker can call that at the top of
every task rather than needing a thread body to put it in. Three are named: `bgl-render`,
`assetlib cook` (every fanned-out cook, so `Reimport`'s stages and `Migrate`'s resave walk both) and
`editor thumbnails`.

## Where they are

| Layer | What is bracketed |
|---|---|
| `assetlib` cooks | the glTF parse, tangents, posed bounds, clip floors, the VAT sample, the prefilter, the whole-project bounds rebake |
| `assetlib` reads | a whole container through a mount, a selective chunk read, a KTX2 decode/transcode |
| `assetlib` doors | `Migrate`, `Reimport`, `RefreshImportedTextures`, `BakeVat`, and the two staleness scans a project pays on every open |
| `bgl` | reserving a rig's bone anim table — the only one here, because it is a device allocation of tens of megabytes rather than work the renderer does, and the dispatch that fills it has no timestamp query to measure it |
| `gamelib` | every `AssetManager::Acquire*`, and the bake-on-demand `EnsureVatBaked` behind the VAT one |
| `apps/editor` | the whole start-up, the device and pipeline build, the mount, each half of opening a project, the explorer root, the thumbnail pool, and an import split into its worker and UI halves |

**A cache hit and a cache miss share one zone name on purpose.** `LoadRegenMesh` and its two
siblings are the door where a container is either read or regenerated, and which of those happened
is legible from what nests *inside* — a hit contains a container load, a miss contains a glTF parse
and a tangent pass. Splitting the name would put the same door in two rows of the statistics view
and lose the total.

## What a start-up costs

Measured on `macos-clang-metal-debug`, the test project (3 glTF sources, 315 MB). Four scenarios,
because "load time" means a different thing in each and only the first is what a developer pays
daily:

| Scenario | `editor startup` | Where it goes |
|---|---:|---|
| Everything warm | **0.90 s** | `assetlib scan stale textures` 0.39 s (43%), `editor create graphics` 0.29 s (32%) |
| Cold shader cache | **5.14 s** | `editor create graphics` 4.43 s (86%) |
| Derived containers absent | **24.2 s** | `assetlib reimport` 22.6 s (94%), of which `assetlib glTF parse` ×7 = 10.1 s |
| Materials stale | **26.5 s** | `assetlib migrate resave walk` 22.0 s (83%) |

A project can be worse than any of these — a full cold rebuild with everything stale at once measured
**133.9 s** — but that run predates the phase zones, so the split above is measured and that number
is only a total.

### Where the resave walk goes

`Migrate`'s resave walk is the largest single phase there is, and it is nearly half texture encoding:

| | CPU across 4 threads | share |
|---|---:|---|
| `assetlib resave`, 73 files | 78.5 s | — |
| ⤷ `assetlib ktx2 encode`, 37 images | **36.1 s** | **46%** |
| ⤷ `assetlib ktx2 decode`, 49 images | 1.2 s | 2% |
| ⤷ everything else (mips, compositing, serialize, write) | 41.2 s | 52% |

Every one of those 37 encodes is a **UASTC** encode, and for a bake target it is immediately
transcoded to BC1/BC5/BC7 and the UASTC thrown away — `image_io.cpp` does it that way because libktx
exposes no direct BC encoder, not because anything wants UASTC. A direct BC encoder would attack 46%
of the dominant phase; changing what *source* textures are stored as would not, because the decode
side is 2%.

A **warm** start is nearly half staleness scanning: `GetStaleImportedTextureSources` walks every
import document on every launch of a project where nothing changed.

### The glTF re-parse is not the win it looks like

Read this before optimising the rebuild row, because the obvious target has already been tried.

The **parse count is one per output, not one per source**: 3 sources with 3 `.bmesh`, 2 `.bskel` and
2 `.banim` between them parse **seven** times, because `Reimport` cooks a stage at a time and
re-parses rather than holding a source's meshes resident across all three (`reimport.cpp`, whose
comment states that trade deliberately). Holding them instead was built — a bounded cache keyed on
the source, released as its last stage finished — and measured against a same-session control on the
same binary and machine:

| | cache off | cache on | |
|---|---:|---:|---|
| `assetlib glTF parse` | 10.2 s / 7 | 4.7 s / 3 | −5.5 s |
| `assetlib clip floors` | 9.3 s | 10.4 s | +1.1 s |
| `assetlib posed bounds` | 3.7 s | 5.4 s | +1.7 s |
| `assetlib reimport` | 23.2 s | 21.4 s | −1.8 s |
| `editor startup` | 24.6 s | 24.4 s | **−0.2 s** |

The mechanism works and the saving is real; it just does not survive the stages after it. Three
parsed sources resident make the memory-heavy skinning sweeps slower, 2.8 s of the 5.5 s comes
straight back, and at the whole-start-up level the result is inside the run-to-run spread. It was
not kept.

Two things that measurement also settled. A **source-major restructure cannot work at all** —
`Reimport_test.cpp`'s "Two sources rebuild together" pins a clip set reading *another* source's
`.bskel` from disk, so the stage barriers must stand and only a parse may cross them. And the real
target in that row is the sweeps rather than the parse: `clip floors` plus `posed bounds` is 13 s of
the 23 s, which `docs/plans/pose-bounds-perf.md` already names as the next thing.

`apps/editor/CLAUDE.md` describes a cold pipeline build as "tens of seconds". On macOS/Metal it is
**4.4 s**. That figure was never measured on this backend; it may still hold for DXIL on Windows,
which these numbers say nothing about.

## Not zones

Do not bracket the frame loop. Nothing here samples per frame, and that is what keeps an
unconnected client's memory flat. `AssetThumbnailCache::Advance`'s slow-tick warning stays a
`qWarning` for the same reason its comment gives: a path that runs every frame wants a report that
costs nothing until it has decided to complain.
