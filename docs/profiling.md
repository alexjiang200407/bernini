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

This page is about **load and cook time**: what a bake cost, where a start-up went -- and, in
§ Memory below, what a run *held*. GPU per-pass timing is a separate, unbuilt thing
(`ROADMAP.md` § Profiling), and `docs/gfx_debug.md` is where a *wrong* frame is diagnosed rather
than a slow one.

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
every task rather than needing a thread body to put it in. Four are named: `bgl-render`,
`assetlib cook` (every fanned-out cook, so `Reimport`'s stages and `Migrate`'s resave walk both),
`bgl pipeline build` (the renderer's start-up pipeline batch) and `editor thumbnails`.

## Where they are

| Layer | What is bracketed |
|---|---|
| `assetlib` cooks | the glTF parse, tangents, posed bounds, clip floors, the prefilter, the whole-project bounds rebake |
| `assetlib` reads | a whole container through a mount, a selective chunk read, a KTX2 decode/transcode |
| `assetlib` doors | `Migrate`, `Reimport`, `RefreshImportedTextures`, and the two staleness scans a project pays on every open |
| `bgl_extended` | reserving a rig's bone anim table — the only one here, because it is a device allocation of tens of megabytes rather than work the renderer does, and the dispatch that fills it has no timestamp query to measure it |
| `gamelib` | every `AssetManager::Acquire*`, and the UI runtime's load doors: a document, a font face, and each texture a document names or generates |
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
| Cold shader cache | **5.14 s** | `editor create graphics` 4.43 s (86%). Captured before the pipelines were built in parallel; since then a cold `CreateGraphics` measures 1.5 s against 3.7 s in `bgl_extended_tests`, and this row has not been re-captured |
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
the 23 s, and that is where the next reduction has to come from.

`apps/editor/CLAUDE.md` describes a cold pipeline build as "tens of seconds". On macOS/Metal it is
**4.4 s**. That figure was never measured on this backend; it may still hold for DXIL on Windows,
which these numbers say nothing about.

## Not zones

Do not bracket the frame loop. Nothing here samples per frame, and that is what keeps an
unconnected client's memory flat. `AssetThumbnailCache::Advance`'s slow-tick warning stays a
`qWarning` for the same reason its comment gives: a path that runs every frame wants a report that
costs nothing until it has decided to complain.


## Memory

Time and memory are measured by different machinery here, and the reason is the readout. Tracy's
`tracy-csvexport` exports **zones only** — its memory data is reachable through the GUI alone, so
nothing scripted, and nothing on a device, can ever be handed a number. So the tracker is ours and
Tracy is one of its sinks.

What it is modelled on is Unreal's **LLM**: bytes charged to a coarse subsystem tag, reported beside
what the OS says the process owns (`FPlatformMemory::GetStats` there, `core::process_memory` here).
Both halves are needed. A tag accounts for what *we* allocated; a low-memory killer counts what the
OS charges, GPU and driver and compressed pages included. The gap between them is the interesting
number, and it has a name below.

### Getting a report

Every binary reports what it cost, and where the report goes follows one rule: **it goes to the log,
and a binary whose log is a terminal arms it on request** rather than on every run.

| Binary | Reports |
|---|---|
| `editor` | into `editor.log` unless `config.json` sets `"memoryReport": false`; `--mem-report <path>` also writes JSON, and outranks the setting |
| `assetlib_cli` | on `--mem-report <path>` only, and the flag goes **before** the subcommand |
| `editor_tests` | on `--mem-report <path>` only |

```bash
just run editor -- --mem-report start.json
./assetlib_cli --mem-report bake.json bake -p Game.bproj model.glb   # flag first: it is global
just run editor_tests -- --mem-report suite.json
```

The log form is a table; the JSON is the same numbers for something that parses them, which is what
an agent reads:

```
memory report (peak / live, allocations)
  device buffer      48.6 MiB peak         0 B live  0 allocations
  device texture     15.2 MiB peak         0 B live  0 allocations
  tagged             63.8 MiB peak         0 B live
  process             1.4 GiB peak   592.1 MiB live
  untagged          592.1 MiB       (footprint the tags do not account for)
```

**A report is read for its peaks, not its live column.** It is written at the end of `main`, by
which point the subsystems are torn down and nearly everything reads zero — which is why a peak
survives the release that follows it. Live is only interesting mid-run, or as the sign of something
that was never given back.

### The tags

Nine, at the granularity of Unreal's `ELLMTag`: `mesh`, `animation`, `texture`, `material`,
`environment`, `shader`, `device buffer`, `device texture`, `editor`. Coarse on purpose — one label
per thing somebody can act on. A finer taxonomy is a set of labels nobody maintains, and an
unmaintained tag reports a number nobody trusts.

**The list is the engine's, in `bgl_common/MemoryTag.h`; the machinery is `core`'s.** `core` is
shared by every target and has no business knowing what a mesh is, so `core::profiling::TaggedBytes`
is templated on a tag enum and asks only for a count and a name, both found by ADL beside it. The
enum sits at the lowest point that everything charging memory can see — the renderer's resources and
gamelib's container cache — so `bgl_wgpu` reaches it from there too. A report never names a tag enum
at all: each instantiation registers its table on first use and the report walks what registered,
which is what lets `assetlib_cli` write one without linking a renderer.

**`untagged` is not an error, it is the mechanism.** It is `footprint - tagged live`: memory the OS
charges us for that no tag claimed. It is how a missing tag announces itself, and on a unified-memory
device it is the only warning there is. A tag is added when that column says one is worth adding, not
in advance.

### Writing a tag

`core::profiling::TaggedBytes` charges bytes for as long as it lives. Hold one **as a member of
whatever owns the buffer**, so the release cannot be forgotten on a path that throws:

```cpp
#include <bgl_common/MemoryTag.h>

struct CachedThing
{
    std::vector<std::byte> bytes;
    bgl::TaggedBytes       tracked;
};

thing.bytes   = load(key);
thing.tracked = bgl::TaggedBytes(bgl::MemoryTag::kMesh, thing.bytes.size());
```

Assigning a fresh one releases the charge it replaces, which is what a container that grew wants —
never adjust a charge, re-seat it.

**A charge is move-only, so whatever holds one is too** — and MSVC builds with `/Wall /WX`, where an
implicitly deleted copy constructor is an *error* (C4625/C4626, and C5026/C5027 for the moves). So
declare all five special members on the holder explicitly rather than letting them be deduced. The
cost of forgetting is a Windows-only compile failure no macOS build reproduces; `Cached<T>` in
`AssetManager.cpp` is the worked example, and note that declaring any of them also stops the type
being an aggregate, so it needs a constructor of its own.

**Tag doors, never elements.** A charge is two relaxed atomics plus, where profiling is compiled in,
a Tracy pool event: right for a container, wrong for anything in a per-vertex or per-bone loop. It
is the same rule zones follow, for the same reason.

The charge is identified by a minted id rather than by the address it accounts for. A container that
reallocates would otherwise hand Tracy a free for an address it never saw allocated, and there is
no second spelling for a call site to get wrong.

### What is measured, and what is not

| Layer | Tagged |
|---|---|
| `bgl_extended` | every buffer and texture created through the RHI's `ResourceManager` — the buffer at the size asked for, the texture at the size the driver reports — in both backends. A swap-chain texture is adopted rather than allocated, so it is deliberately not charged |
| `gamelib` | `AssetManager`'s stamped container cache — the `.bmesh` under `mesh`, the `.banim` and `.bskel` under `animation` |

Nothing else, and the gaps below are what the residual is currently made of:

- **`assetlib`'s cook is untagged**, and it is the largest consumer in the tree. The measurements
  below say so plainly.
- **The editor's thumbnail cache does not read through `AssetManager`**, so the `.bmesh` files it
  loads are not charged to `mesh`.
- **Device memory taken outside `ResourceManager` is untagged.** Three paths ask the device
  directly: D3D12's `UploadManager` chunk pool (which already counts its own bytes in
  `m_AllocatedMemory` and simply never hands them over), `ReadbackBuffer_d3d12`, and Metal's
  staging buffers in `CommandList_metal`. So `device buffer` is the RHI's buffers, not the
  process's.

### What a run costs

Measured on `macos-clang-metal-debug`, against the test project.

| Run | Process peak | Tagged peak | Where the tagged bytes are |
|---|---:|---:|---|
| `editor_tests`, whole suite | **1.48 GiB** | 63.8 MiB | device buffers 48.6 MiB, device textures 15.2 MiB |
| `assetlib_cli bake`, the 663-bone reference rig | **1.22 GiB** | 0 B | nothing on this path is tagged |

Two things to take from that. A cook of one character costs **1.2 GiB of resident memory**, which no
document previously stated because nothing could measure it — and `ROADMAP.md`'s Capacity policy
section is written entirely in bytes of exactly this kind. And the residual is doing its job on both
rows: the suite's peak is 96% untagged and the cook's is **entirely** untagged, which is what names
`assetlib`'s cook as where the next tag goes — with a number rather than a guess.

Neither row is an interactive editor start-up. This checkout cannot drive a GUI to a clean exit, and
a report is written when `main` returns — so an editor figure needs somebody to launch it, open the
project and close the window.

### Tracy, when a peak is not enough

A peak says how big; it never says what shape. When the question is a leak or a growth curve rather
than a total, the same tags are Tracy memory pools in any build with `BERNINI_PROFILING` on — one
pool per tag, on the same timeline as the zones above, so a spike lines up with the zone that caused
it. Take a capture exactly as § Taking a capture describes; the pools are in the GUI's *Memory*
window.
