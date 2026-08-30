# Profiling

CPU timing here is [Tracy](https://github.com/wolfpld/tracy). It is compiled into **every** preset,
debug and release — not gated the way `BUILD_COVERAGE` is, because a zone costs a few nanoseconds
where coverage changes the performance of everything it instruments.

This page is about **load and cook time**: what a bake cost, where a start-up went. GPU per-pass
timing is a separate, unbuilt thing (`ROADMAP.md` § Profiling), and `docs/gfx_debug.md` is where a
*wrong* frame is diagnosed rather than a slow one.

## Taking a capture

The client listens; the viewer dials in. Nothing is recorded to disk by the application itself.

```bash
# Record headlessly. Start this FIRST -- it waits for the process to appear, which is the only
# way to catch a start-up.
tracy-capture -o start.tracy -f &
just run editor

# Turn a capture into per-zone totals.
tracy-csvexport start.tracy > start.csv
```

Both tools come from the `tracy` port's `cli-tools` feature, so vcpkg has already put them in
`build/<preset>/vcpkg_installed/<triplet>/tools/tracy/`. The **GUI** profiler is not vendored —
install it once per machine from the Tracy releases and point it at the running process for a
timeline instead of a table.

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

Zones nest by scope and are separated by thread automatically, so neither the containment nor the
thread has to be passed anywhere.

## Where they are

| Layer | What is bracketed |
|---|---|
| `assetlib` | the glTF parse, tangents, posed bounds, clip floors, the VAT bake, the prefilter, the whole-project bounds rebake |
| `apps/editor` | an import, split into its worker and UI halves |

## Not zones

Do not bracket the frame loop. Nothing here samples per frame, and that is what keeps an
unconnected client's memory flat. `AssetThumbnailCache::Advance`'s slow-tick warning stays a
`qWarning` for the same reason its comment gives: a path that runs every frame wants a report that
costs nothing until it has decided to complain.
