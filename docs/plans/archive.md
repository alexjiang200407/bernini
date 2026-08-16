# Asset archives

One `.bpak` file in place of a tree of loose assets, mounted behind the data-root-relative path
every reference in the project is already written against. An archive is never written into, and
never read back into the editor: the editor authors the loose tree, `pack` turns that tree into an
archive, and a shipped game mounts it.

---

## The questions this answers

The four the feature was asked, with the short answer and where it is argued:

| | |
|---|---|
| **How does this affect the workflow?** | It does not. Drag-and-drop, import, save and delete all still write loose files, and the Content Explorer still lists the loose tree. `pack` is a step at ship time. See [the editor never sees an archive](#the-editor-never-sees-an-archive-pack-makes-one-to-ship). |
| **Can we still drag and drop and write, after the editor uses the archive?** | Yes — the editor does not use one. Nothing is ever written *into* an archive, and nothing is read out of one in the editor either: assets are version-tracked as separate files, so the loose tree stays the source of truth and the archive is the artifact built from it. |
| **How should the archives be organised?** | One `Data.bpak` per project by default, indexed by the same `normalizeRef` data-root-relative path everything already keys on. Splitting is an argument to `pack`, not a rule. See [one archive per project](#one-archive-per-project-by-default-databpak-mounted-as-one-layer). |
| **What shouldn't be archived?** | *An archive carries what the runtime reads and nothing that produces it* — out go `textures_src/`, unimported source art, the `.berniniproject` and the shader cache. See [the exclusion table](#what-should-not-be-archived). |
| **Should we support archived and non-archived?** | Yes, permanently, and loose always wins. It is one ordered `LayeredFileSystem`, and that single rule is the debug workflow, patching and modding. See [both, always](#both-archived-and-non-archived-always-and-loose-wins). |

---

## What the survey found

**There is no filesystem seam.** Every loader in `assetlib` takes a `std::filesystem::path` and
opens it — `load` ([bmesh_io.h:46](../../libs/assetlib/include/assetlib/bmesh_io.h)),
`loadMaterial` ([bmaterial_io.h:35](../../libs/assetlib/include/assetlib/bmaterial_io.h)),
`loadSky`, `loadEnvLighting`, `loadEnv`, `loadSkeleton`, `loadAnimations`, `loadVat`. Three
functions do the actual opening:

| what | where | shape |
|---|---|---|
| chunked containers (`.bmesh`, `.bskel`, `.banim`, `.bvat`) | [`chunk::readChunksFromFile`](../../libs/assetlib/src/chunk_io.cpp) | `std::ifstream` + **seek per chunk** |
| textures (`.ktx2`) | [`image_io.cpp`](../../libs/assetlib/src/image_io.cpp) | libktx `ktxTexture2_CreateFromNamedFile` |
| everything else | [`core::file::read_file_bytes`](../../libs/core/src/file.cpp) | whole file into a `vector<byte>` |

`readChunksFromFile` is a **ranged** reader on purpose: it reads the header, the chunk table and
only the requested chunks, "a few hundred bytes of a file of many megabytes"
([chunk_io.h](../../libs/assetlib/src/chunk_io.h)). `AssetRefGraph::Scan` walks every mesh and clip
set in a project through it. A seam that could only hand back whole files would quietly turn that
scan into a full read of every mesh on disk.

**Identity is already a mount-ready string.** `assetlib::normalizeRef`
([ref_paths.h](../../libs/assetlib/src/ref_paths.h)) reduces every stored reference to one
data-root-relative generic-form path, and `requireInsideDataRoot` rejects anything that escapes the
root. `AssetManager` resolves those against one `m_DataRoot`
([AssetManager.h:508](../../libs/gamelib/include/gamelib/AssetManager.h)). Nothing stores an absolute
path. That is the key that an archive can be indexed by, unchanged.

**`bgl` never reads an asset.** It touches the filesystem twice, and neither is a read that a mount
could serve: the shader cache
([ShaderCache_d3d12.cpp](../../libs/bgl/src/d3d12/shadercache/ShaderCache_d3d12.cpp),
[ShaderCache_metal.cpp](../../libs/bgl/src/metal/shadercache/ShaderCache_metal.cpp)), a per-machine
write-back cache of driver PSOs; and `WritePng`
([RenderContext.cpp:92](../../libs/bgl/src/gfx/RenderContext.cpp)) behind the public
`IGraphics::ScreenshotPng`, which writes captures outward for the golden-image suite. The layering
rule holds without effort: this feature does not touch `bgl`.

**Staleness is `stat`.** `stampOf` is size + last-write-time
([bmaterial_io.h:43](../../libs/assetlib/include/assetlib/bmaterial_io.h)), and `bakeIsStale`,
`drawsLoose`, `vatIsStale` and `envMapToDraw` all decide by comparing it or by
`std::filesystem::exists`. `drawsLoose` in particular is measured *per load* and decides which of
two material representations a scene gets. Inside an archive there is no `stat`, so this machinery
needs a home in the seam or every packed material silently changes representation.

**`AcquireVatMesh` writes to the data root at runtime.** A missing or stale `.bvat` is re-baked and
rewritten in place ([AssetManager.cpp:351-371](../../libs/gamelib/src/AssetManager.cpp)). That is
the one runtime path that assumes the data root is writable.

**The editor's data root is a shared mutable directory, by design.** `AssetRefGraph` refuses to
cache because "the data root is shared with the user's file manager"
([asset_refs.h](../../libs/assetlib/include/assetlib/asset_refs.h)); the Content Explorer is a
`QFileSystemModel` subclass ([AssetFileModel.h](../../apps/editor/src/Windows/ContentExplorer/AssetFileModel.h));
drag-and-drop imports through `ContentExplorerWindow::dropEvent`; `Project::Create` scaffolds ten
category directories and `IsRequiredDirectory` protects them
([Project.h](../../apps/editor/src/Project/Project.h)). Delete, rename and prune
([texture_prune.cpp](../../libs/assetlib/src/texture_prune.cpp),
[asset_rename.cpp](../../libs/assetlib/src/asset_rename.cpp)) all mutate the tree in place.

**The roadmap has one unelaborated line for this**: `Asset Streaming Pipeline`
([ROADMAP.md:394](../../ROADMAP.md)), under Module 2. There is no packaging or shipping milestone,
and the prioritized work (culling, animation, crowd) is elsewhere. This feature is infrastructure
bought ahead of its demand — worth saying plainly, because the honest justification is not the one
in the prompt.

**What an archive actually buys, measured against what is there today.** Loads are already
one-file-at-a-time, read fully and closed, so open handles are ~1 and nothing is gained there.
Fragmentation is a real but second-order cost. The wins that hold up:

- **one artifact to ship, version and checksum** — the game currently has no packaging step at all;
- **fewer syscalls and seeks on a cold cache**, which is where a directory of thousands of small
  `.bmaterial` and `.ktx2` files does cost measurably;
- **a mount order**, which is where patching, DLC and mod override live, and which cannot be
  retrofitted onto direct `path` opens;
- **a place to put alignment, compression and streaming later** — the entry table is the hook.

---

## Design decisions

### The editor never sees an archive; `pack` makes one to ship.

The editor opens a project as an ordered mount — the loose `Data/` tree first, `Data.bpak` behind it
— and every write it makes lands in the loose tree. Drag-and-drop still writes a `.bmesh` into
`Data/Meshes/`, saving a material still rewrites a `.bmaterial`. A loose file shadows its packed
twin, so editing a packed asset means writing a loose copy of it, and reading it back gets the
edit. **Nothing is ever written into an archive.** A *Sync* action repacks: archive ∪ overlay, minus
what was deleted, written to a new `.bpak`, after which the absorbed loose files are removed.

Copy-on-write, in other words, with the repack as the explicit commit. The mount ordering that does
it is the same one the runtime uses — the editor is not a special case, and the packed form is
exercised on every session rather than first on ship day.

**Rejected: a read-write archive the editor mounts and saves through.** It fails on four counts, any
one of which is disqualifying, and every one of them is what the overlay avoids:

1. Writing an entry either rewrites the whole container or patches in place with a free list. The
   first makes every material save proportional to the size of the project; the second is a
   filesystem, and one that then needs its own crash-safety, compaction and repair — for a problem
   the OS already solved. The overlay pays the whole-project cost once, at *Sync*, when the user
   asked for it.
2. The staleness machinery is `stat`. Bake-vs-loose, `vatIsStale`, the thumbnail cache's `FileStamp`
   ([asset_paths.h](../../apps/editor/src/util/asset_paths.h)) and the texture prune all read file
   metadata. Under the overlay an edited asset is a real file with a real `stat`, and an untouched
   one carries the stamp it was packed with — no metadata is ever mutated inside a container.
3. `AssetRefGraph` is explicitly a snapshot rebuilt per question, because the user's file manager can
   move things underneath it. That stays true of the overlay half, which is the half that moves; the
   archive half is immutable between syncs.
4. Every shipping engine with a pack format (`.pak`, `.bsa`, `.vpk`, `.pck`) is read-only at runtime,
   and the ones with editors — Bethesda's especially — do exactly this: loose files override the
   archive, and re-packing is a separate deliberate step.

**Rejected: the editor never sees an archive at all** (author loose, `pack` only at ship time). It is
less code — no tombstones, no union enumeration, no Content Explorer change — but it means the packed
form is never loaded until the day it has to work, and a project that has been packed can only be
edited by unpacking it wholesale. The archive would be a write-only artifact, which is how a format
rots.

**What it costs, said plainly.** This is the expensive half of the feature, and three things follow
from it that the loose-only design did not need:

- **Deletion needs a tombstone.** A packed entry cannot be unlinked. The overlay carries a deletion
  set, `LayeredFileSystem` treats a tombstoned path as absent, and *Sync* drops those entries from the
  repack. This is the same mechanism patch archives would want later, now with a consumer.
- **`AssetRefGraph::Scan` and the texture prune must walk the union**, not
  `recursive_directory_iterator` over the data root
  ([asset_refs.cpp:290](../../libs/assetlib/src/asset_refs.cpp),
  [texture_prune.cpp:40](../../libs/assetlib/src/texture_prune.cpp)). That is what
  `IFileSystem::Enumerate` is for.
- **The Content Explorer cannot stay a `QFileSystemModel`.** It browses a real directory
  ([AssetFileModel.h](../../apps/editor/src/Windows/ContentExplorer/AssetFileModel.h)), and it has to
  show packed entries alongside loose ones with the loose one winning. Its own task, and the largest
  single one in the feature.

**Reversed, 2026-08-16 — the rejected alternative above is the one taken.** The editor mounts
nothing. It reads and writes the loose `Data/` tree, as it always did, and `pack` turns that tree
into an archive to ship. Nothing reads one back into the editor.

The reason is version control, and it outweighs what the mount bought: a project's assets are
tracked as *separate files*. One packed blob is the wrong unit — it defeats diffing, it defeats LFS's
per-file dedup, and every edit rewrites the whole container. The loose tree is the source of truth
and the archive is a build artifact derived from it, so the arrow only ever points one way.

The objection that got this rejected the first time — that the packed form is never loaded until the
day it has to work — was the strongest argument for the mount, and it is answered by where the tests
ended up rather than by the editor. `AssetManager_test` acquires a mesh, its materials and its
textures out of a `.bpak`, `VatAcquire_test` acquires from a read-only mount, and `assetlib_cli list`
reads one back. The packed path runs on every test run; it does not need the editor to exercise it.

Everything the copy-on-write design existed to solve then dissolves rather than being solved:

- **Editing a packed asset.** There are none in the editor, so no shadowing, no overlay, no
  ordering rule.
- **Deleting one.** Likewise. Every asset the editor lists is a loose file it can unlink, which is
  what makes `Data/.overlay.json`, the tombstone manifest and its three rules unnecessary.
- **Sync.** There is nothing to fold back. The way to refresh an archive is to run `pack` again.
- **The Content Explorer.** It browses a real directory and now only ever has to, so it stays a
  `QFileSystemModel` — which gives it shell icons, directory watching and sorting for free. The
  largest single task in the feature is not made cheaper, it is deleted.

What this costs, stated plainly: a packed project cannot be opened in the editor to inspect what
shipped. That is a real capability given up, and the trade is deliberate — what it protects is that
an asset the editor lists is always one the editor can act on.

**What the reversal did not touch.** The seam is the feature, and it stands: every read in
`assetlib` goes through `AssetStore`, `gamelib`'s `AssetManager` takes one, and a *shipped game*
mounts a `.bpak` through exactly the same paths the editor uses on a directory. `pack`, `list`, the
format, the ranged reads and the read-only `.bvat` rule are all unaffected. What went was the
editor's half of the mount, and the tasks that existed only to serve it.

### The tombstone manifest: one JSON file, cleared by every Sync.

`Data/.overlay.json`, written by `assetlib` (which already links `nlohmann_json`
[CMakeLists.txt:35](../../libs/assetlib/CMakeLists.txt)) so that both the editor and a CLI `sync`
read the same file:

```json
{ "version": 1, "deleted": ["Materials/kirk.bmaterial", "Textures/kirk_orm.ktx2"] }
```

Paths in `normalizeRef` form, like every other reference. `LayeredFileSystem` takes the set as its mask;
`Sync` drops those entries from the repack and then deletes the manifest. Its lifetime is one
editing session's worth of deletions — created on the first delete of a packed asset, gone at the
next sync — so it is small, short-lived and hand-editable when something goes wrong.

Three rules that are not obvious and are each a bug if missed:

- **A tombstone is written only when the path still resolves after the loose copy is unlinked** —
  i.e. only when something packed is underneath. Deleting a loose-only asset just unlinks, as today.
  Tombstoning unconditionally would grow the manifest forever with entries that mask nothing.
- **Writing a path clears its tombstone.** Importing an asset at a path that was deleted earlier in
  the session must not leave the new file masked by its own tombstone.
- **It is written through `write_atomic`, and it is excluded from packing.** It is overlay state, not
  runtime data; a shipped archive that carried it would mask its own contents.

**Rejected: a marker file per deletion** (`Materials/kirk.bmaterial.deleted` beside the asset). It
cannot desync from the tree the way one central file can, and it merges better under version
control — but it scatters state through every category directory, needs a suffix reserved forever,
and puts an extension-filtering rule in front of every `Enumerate` in the project. For a set that is
emptied at each sync, one file is easier to reason about than N.

**Rejected: keeping the deletion set in the `.berniniproject` file.** It is editor metadata that
travels with the project; the overlay is working state that dies at the next sync. Different
lifetimes should not share a file.

**Reversed, 2026-08-16.** Nothing is built. The manifest existed so the editor could delete an asset
it could not unlink; with the editor never mounting an archive, every asset it lists is a loose file
and delete is `remove`. `LayeredFileSystem::SetMask` stays — it is the general mechanism, and patch
archives are the consumer that would want it — but nothing writes a mask today.

### What should not be archived

| excluded | why |
|---|---|
| `textures_src/` | authoring source; the bake reads it, the runtime never does |
| `.glb` / `.hdr` and anything else awaiting import | same |
| the `.berniniproject` file | editor metadata, not runtime data |
| the shader cache (`.bsc`, `pipelines.psolib`) | per-machine, write-back, disposable — an archive entry would be write-once and wrong |
| `Levels/` | **included** when they exist — no `.blevel` type is registered yet, so today the rule below would skip them; `pack` counts unclaimed extensions so that stays visible |
| `Textures/` (baked) | **included**, and it is most of the bytes |
| `.bvat` | **included**, and packed fresh — see below |

The rule is one line: *an archive carries what the runtime reads and nothing that produces it.*
`pack` derives that from the same `assetTypeFromExtension`
([asset_refs.h:61](../../libs/assetlib/include/assetlib/asset_refs.h)) the reference graph uses, plus
an explicit directory exclusion for `textures_src/`, so a new container type joins the archive by
being registered once rather than by editing a list here.

### One archive per project by default, `Data.bpak`, mounted as one layer.

**Rejected: one archive per category** (`Meshes.bpak`, `Textures.bpak`, …). It buys nothing — an
entry table lookup is a hash regardless of how many files the entries are spread over — and it costs
a rule about which archive a path lives in, which is a second identity scheme next to the one that
already works.

**Rejected: content-addressed entries.** The whole engine keys assets by path
(`AssetManager`'s `m_TextureByPath`, `normalizeRef`, every reference stored in every container).
Hashing content would mean a translation table and a second source of truth about what an asset is.

Splitting is still possible without a rule: `pack` takes an explicit file list or prefix, so a
project that wants `Base.bpak` + `DLC1.bpak` gets it by running `pack` twice and mounting both. The
default is one.

### A mount is an interface in `core`; the archive implements it in `assetlib`.

```
core::file::IFileSystem          Exists / Stat / Read / ReadRange / Enumerate / IsReadOnly
  ├── core::file::LooseFileSystem   a directory, what everything does today
  ├── core::file::LayeredFileSystem        an ordered list of mounts, first hit wins
  └── assetlib::PakFile             a .bpak — always read-only
```

`core` because `assetlib` (the loaders), `gamelib` (the seam) and `assetlib_cli` all link it and all
need it, while `bgl` links it and must not grow a dependency on an asset container. `PakFile` in
`assetlib` because writing one is a cook, and `assetlib` is the cook library.

**`ReadRange` is not optional.** Without it `readChunksFromFile` loses its partial read and
`AssetRefGraph::Scan` becomes a full read of every mesh in the project. The interface carries a
ranged read from the first commit, and `LooseFileSystem` implements it with the seek it already does.

**`IsReadOnly` is on the interface, not discovered later.** `AcquireVatMesh` needs it to decide
whether a `.bvat` is re-bakeable, and `LayeredFileSystem` reports it for the mount that answered rather
than for itself — a loose-over-archive search path is writable for one path and not for the next.
Six members, fixed at the first commit: a capability bolted on after `LooseFileSystem` and
`LayeredFileSystem` have landed with tests against a five-member shape is a rewrite of a merged task.

**`Stat` returns `core::file::FileStamp` — a size and an mtime, and *not* `assetlib::SourceStamp`.**
The two are structurally identical, which is the trap: `SourceStamp` lives in `assetlib_structs`
([SourceStamp.h](../../libs/assetlib_structs/include/assetlib_structs/SourceStamp.h)), which links
`core` ([CMakeLists.txt:13](../../libs/assetlib_structs/CMakeLists.txt)), so `core` returning one
would invert the dependency edge. `assetlib::stampOf` becomes the conversion and stays the only
place that knows the two are the same shape — `SourceStamp` is serialized into `.bmaterial`,
`.bsky` and `.benvl`, so it is a format type and could not follow `core`'s definition anyway.

That makes three stamps in the tree: `core::file::FileStamp` (the seam), `assetlib::SourceStamp`
(on disk, in containers), and the editor's `editor::FileStamp()`
([asset_paths.h:15](../../apps/editor/src/util/asset_paths.h) — a Qt-side millisecond mtime for the
thumbnail caches). They are deliberately not merged: the first is new and unavoidable, the second is
a file format, the third is a different resolution on a different string type.

`PakFile` stores the stamp each entry had when it was packed, so `drawsLoose` and `bakeIsStale`
return the same verdict against an archive that they returned against the tree it was packed from.
A packed project must not silently change which material representation it draws.

### `AssetStore` carries the read mount and the writable root together.

Every `load*` that names a single file gains an `IFileSystem&` overload beside the path-taking one,
because both callers are real and neither can be deleted. Every function that takes a `dataRoot`
instead has that parameter *replaced*: `bakeIsStale`, `drawsLoose`, `vatIsStale`, `envMapToDraw`,
`isSkyBakeStale`, `isEnvLightingBakeStale` and `describe` use `dataRoot` as `dataRoot / relative` and
for nothing else, at every site. A data root is already a mount spelled as a path, so there is no
second overload to add — the signature count stays at one and the resolution moves to the seam.

**Rejected: an `assetlib` loader class composing an `IFileSystem&`** — `AssetReader::LoadMesh(path)`
in place of `loadMesh(fileSystem, path)`, currying the mount once rather than threading it through
every call.

It would not replace the free functions, it would join them. Of 41 production call sites, 6 curry a
data root (`AssetManager` ×5, each spelled `m_DataRoot / x`); 11 hold an absolute path with no root
to mount — `assetlib_cli` on whatever was typed at the command line
([main.cpp:641](../../libs/assetlib/cli/main.cpp)), and the Material Editor on Qt paths off a file
dialog ([MaterialEditorWindow.cpp:1006](../../apps/editor/src/Windows/MaterialEditor/MaterialEditorWindow.cpp)).
A loader serves the first group and cannot serve the second; routing the second through a throwaway
`LooseFileSystem(path.parent_path())` plus `path.filename()` is worse at the call site than the
overload it replaced.

And the object it would be already exists one layer up. `AssetManager` holds `m_DataRoot` and is
what gamelib hands around to load a project's assets; task 8 swaps that one field for a
`LayeredFileSystem`. A loader would be a second composition object beneath a composition object over
the same mount, owning nothing — the state that would justify one is already placed elsewhere:
tombstone masking on `LayeredFileSystem`, the archive handle inside `PakFile`.

What would reverse this: a decoded-asset cache keyed by mount. A cache needs an owner with a
lifetime and a free function cannot be one — but that owner is `AssetManager`, which is also where
the question would first be asked.

**Reversed, 2026-08-15.** Not by the cache, and not by the argument above being wrong — a loader
class *with methods* over the free functions is still the thing that argument rejects, and the
path-taking overloads did stay. What it missed is that the question was never only about loaders.

The reviewer asked for an object holding **the filesystem and the data root**. By the end of task 7
that pair had been written out by hand in nine places: seven descs carrying a `dataRoot`
(`PackDesc`, `VatBakeDesc`, `TexturePruneDesc`, `EnvBakeDesc`, `AssetRefScanDesc`,
`MaterialBakeDesc`, `EnvImportDesc`), two of them grown a parallel `fileSystem` with a paragraph
each explaining which half meant what, `AssetManager`'s two members, and five call sites building a
throwaway `LooseFileSystem` from a root they already held. Nine hand-written copies of a pair is a
type, and the doc comment kept being rewritten instead of the type being named.

`assetlib::AssetStore` is that type, and the loaders came with it as methods since the object
exists anyway. `AssetRefScanDesc` disappears; `TexturePruneDesc` keeps only `textureDir` and
`PackDesc` only `target`. The free functions stay in two roles rather than three: path-taking for
arbitrary host files no project owns, and mount-taking as the primitive the methods forward to.

The `is_directory` guard that `AssetRefGraph::Scan` and `findUnusedBakedTextures` each kept for
themselves moves to the loose constructor. A mount over a directory that is not there enumerates
*empty* rather than failing, so with the guard nowhere a mistyped root reads as a project with
nothing in it — a scan reporting no assets, a prune reporting nothing to sweep.

### Reads are concurrent, so `PakFile` holds no seek position.

The editor's `AssetThumbnailCache` decodes on a `QThreadPool` with both workers "deep in KTX2
decodes" at once
([AssetThumbnailCache.h:235](../../apps/editor/src/Thumbnails/AssetThumbnailCache.h)), through the
same loaders task 4 routes into the seam. That is safe today only because `readChunksFromFile` and
`ktxTexture2_CreateFromNamedFile` each open their own handle per call.

A `.bpak` is the obvious place to keep one handle open across reads instead — and a shared handle
has a shared seek position, which two decode threads would race on. `IFileSystem::Read` and
`ReadRange` are therefore const and **safe for concurrent calls**. `PakFile`'s entry table is built
once at mount and immutable after; only the payload read touches the file.

**Rejected: a mutex around the handle.** It serialises exactly the case `TexturePrefetch` exists to
parallelise — two workers decoding two textures — and buys nothing a positional read does not.

**Corrected by task 2:** `PakFile` holds no handle at all. It opens a stream per payload read, the
way `LooseFileSystem` does, which satisfies the contract with no lock and no platform code. Holding
one handle and reading positionally (`pread`, `ReadFile` with an explicit `OVERLAPPED` offset) is
the optimization behind that contract, not the contract itself — it needs a positional-read
primitive in `core` on three platforms, and there is no measurement yet to justify one. The table
lookup, which is what happens most often, is already in memory. The concurrency test lands either
way and is what would catch a shared cursor being introduced later.

### Both archived and non-archived, always, and loose wins.

Yes, permanently, and not as a transitional state. Consumers that need loose forever: the editor,
the golden-image tests, and a standalone baked model directory
([AssetManager.h:508](../../libs/gamelib/include/gamelib/AssetManager.h)).

`LayeredFileSystem` resolves in order and takes the first hit, loose-first and archive-second, so an
unpacked file shadows its packed twin. That one rule is three features:

- the **debug workflow** — drop one `.bmaterial` next to the exe and re-run;
- **patching** — ship a later archive, or a loose fix, ahead of the base;
- **mods** — the same, authored by someone else.

Which is why it is a search path and not a boolean.

**Rejected: a build-time switch between archived and loose.** It makes the two configurations
diverge exactly where a bug would hide, and it forecloses override ordering.

### `.bpak` is its own format, not the chunk container and not the shader cache.

`chunk::Writer` builds the whole file in memory before writing
([chunk_io.h](../../libs/assetlib/src/chunk_io.h)), addresses chunks by a small `uint32` id, and has
no path strings. An archive is gigabytes, addressed by path, and must be readable without loading it.
Different problem, same *shape*: header, aligned payloads, table at the end. `.bpak` reuses the shape
and `core::io::ByteReader`/`ByteWriter`, not the code.

The shader cache is a keyed cache of derived, disposable artifacts whose invalidation is
content-hash-miss-and-recompile ([docs/shader_cache.md](../shader_cache.md)). An archive is
authoritative and versioned. Nothing to share — including its atomic write, which is
`bgl::WriteFileAtomic` in `bgl`'s **`src/`**
([shadercache/util.h:33](../../libs/bgl/src/shadercache/util.h)) and so unreachable from `assetlib`,
which must never link `bgl`.

`pack` needs one anyway, and needs it more than the shader cache does. A cache entry truncated by a
crash is a miss and recompiles; a `.bpak` truncated at the target path is the shipped artifact, and
the next run mounts it and fails to find half the project. So task 1 adds `core::file::write_atomic`
— write to a sibling temp, `fsync`, rename — and `bgl`'s copy is left where it is rather than
migrated, which is a cleanup this feature has no reason to carry.

```
+--------------------------------------------------+
| Header  magic 'BPAK', version, entryCount,        |
|         entryTableOffset, stringPoolOffset        |
+--------------------------------------------------+
| Payloads, each aligned to 16                      |   <- ReadRange lands here
+--------------------------------------------------+
| Entry table, sorted by path                       |   <- 48 B each; the mount builds a map from it
|   pathOffset u32 | pathSize u32 | offset u64      |
|   size u64 | stamp (16 B) | flags u32 | pad u32   |
+--------------------------------------------------+
| String pool: every path, NUL-terminated           |
+--------------------------------------------------+
```

Entries are stored **uncompressed** in this feature. Compression is a per-entry flag the format
reserves and a later task fills in; landing it now would mean choosing a codec before there is a
measurement to choose it with, and it defeats the `mmap` path the aligned layout is there to allow.

Paths are stored in `normalizeRef` form — the same string `AssetManager` keys on — so lookup is the
identity function, not a translation.

**Corrected by task 2.** The table was to be sorted by a stored path hash and binary-searched. It is
sorted by *path* instead, and there is no hash field: the mount reads the table once and builds a
`core::str::unordered_str_map`, which answers in O(1) and makes a stored hash dead weight. Sorting by
path still earns its place — packing one tree twice produces identical bytes, and `list` comes out
ordered.

The writer streams, too, rather than building the archive in memory the way `chunk::Writer` does:
each `Add` writes its payload to a temp straight away and only the table and pool are held. An
archive is the whole project's cooked output, and a writer that had to hold one would put a ceiling
on how large a project can be packed.

**Rejected: `.zip`.** It is the closest call in this plan, and the honest summary is that it costs
about as much code as it saves and gives up two properties the design rests on.

What it would buy is real: `unzip -l` inspects an archive with no tooling of ours, a per-entry CRC32
comes free, and the format is beyond argument. `.pk3` (idTech) and `.jar` are zips for exactly that
reason.

What it costs:

- **Compression and ranged reads are mutually exclusive.** A deflated entry cannot be read from the
  middle — it has to be inflated from its start. So a zip we can range-read is a zip stored with
  `STORE`, and `STORE` is most of why one would pick zip.
- **Zip does not align payloads.** Each entry's data follows a variable-length local header, so it
  lands wherever it lands. Android ships `zipalign` as a separate tool precisely for this, padding
  extra fields to force it. We would be writing that padding ourselves, which is to say writing a
  constrained zip only our writer produces correctly.
- **The reader would still be ours.** Concurrent positional reads with no shared seek position
  (above) is not what miniz or libzip offer — their extraction APIs own a handle and seek it. We
  would parse the central directory and read positionally by hand, so the library saves us the
  writer and not the reader.
- **Zip's own edges are not free**: locating the end-of-central-directory means scanning backwards
  for a signature that a file comment can contain, and anything past 4 GB needs Zip64. Neither is
  hard; both are more parsing than the table above.

So the zip we could actually adopt is `STORE`-only, alignment-padded, extended-timestamp, read by
our own positional reader — a zip in name, with the compression given up and the tooling benefit
reduced to `unzip -l`. `.bpak` is ~200 lines and answers to the three requirements exactly. `list`
(task 6) covers the inspection case.

This is a decision worth revisiting if compression becomes the priority, since at that point the
ranged-read constraint has to be renegotiated anyway.

### Under a read-only mount, `.bvat` is trusted, not re-baked.

`AcquireVatMesh` re-bakes a stale `.bvat` because it is a derived build product and the bake is
seconds of CPU. Packed, its inputs (the `.bmesh`'s skin, the `.bskel`, the `.banim`) may be present
but the write target is not, and the staleness question is meaningless: `pack` bakes every `.bvat`
fresh as part of packing, so what is in the archive is correct by construction. When the `.bvat`
resolves out of a mount that reports itself read-only, `AcquireVatMesh` uses it without asking
whether it is stale.

In the editor this costs nothing: a mount with any writable layer keeps checking staleness, so a
re-bake happens exactly as it does today and the fresh `.bvat` lands in the overlay like any other
edit. Only a shipped mount — archive alone — takes the trusting branch.

**Amended at task 8: the whole mount's answer, not the answering mount's.** This first said
`IsReadOnly` should be the answer of whichever mount resolved the `.bvat`, which turns out to break
the very case the paragraph above promises. Take a rig whose `.bvat` is still only in the archive
and whose `.bmesh` has just been edited into the overlay. Per-path, the `.bvat` resolves to the
archive, reads read-only, and is *trusted* — so the editor animates the edited mesh from a bake of
the old one. Silent, and a wrong picture rather than a failure, which is the class of bug this plan
says it is most exposed to.

Asking the whole mount instead — "is there anywhere at all to put a rebuilt derived file" — gets
both cases right: an overlay is writable even for a path only the archive carries, so that `.bvat`
is re-baked into the overlay; an archive alone is not, so it is trusted. It also needs nothing new
on the seam, where per-path would have wanted a seventh `IFileSystem` member.

**Unchanged by the 2026-08-16 reversal, though its example is gone.** The editor mounts nothing now,
so the edited-mesh-over-packed-`.bvat` case above can no longer arise there. The rule and the
amendment both stand as written: they are about *any* mount with a writable layer, which is still
what a loose data root, a modding search path and the standalone baked-model directory are. Only the
motivating story changed.

A `.bvat` *missing* from a read-only mount is the other half: `pack` only re-bakes the ones already
there, so a rig nothing acquired before packing ships without one. That is an error naming the file,
not a bake attempt that fails somewhere inside `saveVat` on a directory that was never there.

**Rejected: bake to a scratch directory beside the archive.** It reintroduces a writable data root
at runtime, which is the thing shipping an archive is meant to remove, and it would let a shipped
build spend seconds skinning on first load.

---

## What changes

| subsystem | change | risk |
|---|---|---|
| `bgl` | none | — |
| `core` | new `core::file::IFileSystem`, `FileStamp`, `LooseFileSystem`, `LayeredFileSystem`, `write_atomic` | none; additive |
| `assetlib` | new `AssetStore`, the read mount and the writable root as one, with the loaders as methods; every `load*` gains an `IFileSystem&` overload; `readChunksFromFile` → `readChunks`; KTX2 via `ktxTexture2_CreateFromMemory`; the staleness predicates have their `dataRoot` parameter replaced by a filesystem; `AssetRefGraph::Scan` and the texture prune walk the mount union instead of the data root; new `pak_io`; new `pack` CLI command | **highest.** The staleness predicates decide which representation a material draws; a wrong verdict is a silent visual change, not a failure |
| `gamelib` | `AssetManager` takes a `LayeredFileSystem`; the path-taking constructor stays and builds a loose mount, so every existing caller compiles unchanged; `AcquireVatMesh` respects a read-only mount | moderate |
| `apps/editor` | `Project` holds an `AssetStore` over the loose `Data/` tree instead of a bare data root, and hands it to `MainWindow`'s `AssetManager` and the texture prune. Nothing else: the Content Explorer stays a `QFileSystemModel`, delete stays `remove`, and there is no *Sync* — see the 2026-08-16 reversal | low, once the reversal took the mount out of it |
| docs | new `docs/archives.md`; `ROADMAP.md` line under Asset Streaming Pipeline | — |

The path-taking `load*` overloads are all kept, reading the file directly rather than through a
mount — a directory served through `IFileSystem::ReadRange` costs an open per range where a held
handle costs one for the whole container, so the two forms are two readers behind one parser, not a
wrapper over a default mount. Roughly a hundred call sites across the CLI and the test suites do not
move, and the diff stays readable.

---

## Tasks

Bottom-up by layer, one PR each.

**1. `core::file::IFileSystem`, `LooseFileSystem`, `LayeredFileSystem`, `write_atomic`.**
All six interface members — `Exists`, `Stat`, `Read`, `ReadRange`, `Enumerate`, `IsReadOnly` — plus
`core::file::FileStamp`; a directory-backed implementation; an ordered mount list that takes the
first hit and reports both which mount answered and whether *that* mount is read-only; a mask set on
the search path, so a masked path reads as absent (task 10 was to have persisted the set and was
dropped, so nothing sets a mask today — it stays as the mechanism patch archives would want); and
`write_atomic`, which task 6 needs and nothing in the tree offers below `bgl`.
Nothing calls any of it yet — dead scaffolding at the bottom layer, justified by its tests.
*Gate:* new `core_tests` cases — a `LooseFileSystem` over a temp tree round-trips whole and ranged
reads and reports stamps matching `std::filesystem`; a ranged read past EOF throws rather than
returning short; a two-mount `LayeredFileSystem` resolves to the first, falls through on a miss, enumerates
the union without duplicates, and reports the answering mount's `IsReadOnly` rather than its own; a
masked path reads absent from `Exists`, `Read` and `Enumerate` alike even when a mount carries it;
`write_atomic` leaves no partial file at the target when the write fails.

**2. `.bpak`: `PakWriter` and `PakFile`.**
The format above, in `assetlib`. `PakFile` implements `core::file::IFileSystem` and reports itself
read-only. Bounds-checked like `chunk::Reader` — every offset and size out of the file is verified
against the real size by subtraction before anything is allocated.
*Gate:* new `assetlib_tests` — pack a generated tree, read every entry back byte-identical through
both `Read` and `ReadRange`; stamps survive the round trip; `Enumerate` returns exactly what went in;
a truncated, a corrupt-magic and an offset-past-EOF archive each throw rather than read out of
bounds; and N threads reading N different entries off one mounted `PakFile` concurrently each get
their own bytes, which is the test that fails if the reader ever grows a shared seek position.

**3. Route the chunked containers through the seam.**
`readChunksFromFile(path, …)` → `readChunks(IFileSystem&, path, …)` using `ReadRange`, and
`.bmesh` / `.bskel` / `.banim` / `.bvat` loaders gain the overload. The ranged read is preserved,
which is the point of the task.
*Gate:* existing `assetlib_tests` pass unchanged through the loose path; new cases load the same
containers out of a `.bpak` and compare struct-for-struct against the loose load; a counting
`IFileSystem` asserts `AssetRefGraph::Scan` still reads only header, table and reference chunks.

**4. Route textures and the flat containers through the seam.**
KTX2 to `ktxTexture2_CreateFromMemory`; `.bmaterial`, `.benv`, `.bsky`, `.benvl` to the byte-taking
deserializers they already have.
*Gate:* `assetlib_tests` — a `.ktx2` loaded from a `.bpak` decodes to pixels identical to the loose
load, mip count and format included.

**5. Staleness through the seam.**
The predicates that take a `dataRoot` — `bakeIsStale`, `drawsLoose`, `vatIsStale`, `envMapToDraw`,
`isSkyBakeStale`, `isEnvLightingBakeStale`, and the file-local `routeIsStale`, `tripletIsOnDisk`,
`routesAreOnDisk` — have that parameter **replaced** by an `IFileSystem&` rather than overloaded,
per the decision above; `describe` follows for the same reason. `stampOf` gains an `IFileSystem&`
overload and keeps its path-taking form for the CLI. It stays the one place that knows
`core::file::FileStamp` and `assetlib::SourceStamp` are the same two numbers, and it keeps its
missing-file contract by mapping `Stat`'s empty optional onto the zeroed stamp that never compares
equal.
Replacing rather than overloading does ripple, and the ripple is the cost of this task: five
production call sites outside `assetlib` pass a `dataRoot` today — `AssetManager` ×3
([AssetManager.cpp:165,236,359](../../libs/gamelib/src/AssetManager.cpp)), `AssetThumbnailCache` and
`MaterialEditorWindow` one each — plus roughly forty-five in `assetlib_tests`. Each of the five
already holds a data root that tasks 8 and 9 convert to a mount regardless, so this task gives them
a `LooseFileSystem` member built from the root they hold and those tasks change only what it is
built *from*. The alternative is an overload pair carried across four merged tasks and deleted in
the fifth, which is more churn spread over more PRs.
*Gate:* `assetlib_tests` — a material that reads loose from a directory reads loose from an archive
packed out of that directory, and one that reads baked reads baked; a source touched after packing
flips the verdict on the loose mount and not on the archive; `stampOf` over a mount equals `stampOf`
over the same file by path, and a path absent from the mount yields the zeroed stamp rather than
throwing.

**6. `assetlib_cli pack` (and `list`).**
Walks a data root, applies the exclusion rule, bakes stale `.bvat` fresh, writes one `.bpak`. The
walk and the rule live in `assetlib` (`packProject`), not in the CLI, because the gate is an
`assetlib_tests` gate. (This also said the editor's *Sync* would call the same thing in task 10;
task 10 was dropped, and `packProject` remains the one place the rule lives.)

Not through `core::file::write_atomic`, as this plan first said: `PakWriter` landed in task 2 as a
*streaming* writer that already syncs a temp beside the target and renames, so an interrupted pack
leaves the previous archive intact without ever holding a whole archive in memory. `write_atomic`
would mean buffering hundreds of megabytes to gain nothing.

`pack` reports what it left out — extensions no asset type claims, and materials that still draw
from authoring routes the archive does not carry. Both are cases where the archive is *valid* and
what it names is missing, which is otherwise discovered as an untextured surface on someone else's
machine. `list` prints the entry table for debugging.
*Gate:* `assetlib_tests` — packing the test project's data root and enumerating the result excludes
`textures_src/` and the project file and includes every asset type; a round trip through
`pack` then `PakFile` reproduces every input byte-for-byte; a `pack` interrupted before it finishes
leaves the previous archive at the target intact.

**7. The reference graph and the texture prune walk the mount union.**
`AssetRefGraph::Scan` and `texture_prune` swap `recursive_directory_iterator` for
`IFileSystem::Enumerate`. The no-cache contract is unchanged — the overlay half is still what moves,
and the scan is still rebuilt per question.
*Gate:* `assetlib_tests` — the graph scanned over a loose tree equals the graph scanned over a
`.bpak` packed from it, edge for edge; an asset packed and then shadowed by an edited loose copy is
scanned once, from the loose copy; a prune over a mount union never proposes deleting a packed
texture that only a loose material references.

**7a. `AssetStore`.**
The read mount and the writable root as one value, with the loaders and staleness predicates as
methods. Inserted after task 7 on review; see the decision above for what it replaces and why the
original decision was reversed. Must precede task 8, or the editor and `AssetManager` get written
against the shape it removes.
*Gate:* `assetlib_tests` — a loose source reads and writes one directory; a source over a directory
that is not there is a caller error, and so is one with no mount; reads widen to the mount while
`GetDataRoot` stays on the writable layer; the staleness methods answer exactly as the free
functions they forward to.

**8. `AssetManager` mounts a `LayeredFileSystem`.**
New constructor taking a mount list; the existing path-taking one builds a `LooseFileSystem` and
delegates, so no caller changes. `AcquireVatMesh` skips the staleness check on a read-only mount.
*Gate:* `gamelib_tests` — the whole existing suite passes through the loose mount unchanged; a new
case acquires a mesh, its materials and its textures out of a `.bpak` and gets handles equal to the
loose acquire; a loose-over-archive `LayeredFileSystem` resolves an overridden `.bmaterial` to the loose
one; `VatAcquire_test` acquires from a read-only mount without re-baking.

**9. The editor holds an `AssetStore`.** *(Landed as #373, then narrowed.)*
`Project` holds a loose `AssetStore` over `Data/` and hands it to everything that took a data root;
`MainWindow` builds the `AssetManager` from it. Originally this task also mounted `Data.bpak` behind
the loose tree and took `AssetFileModel` off `QFileSystemModel` so the Content Explorer could list
the union. Both were reverted when the editor stopped seeing archives -- see the decision above. The
model went back to `QFileSystemModel`, which lists the real filesystem and does it better.
*Gate:* `editor_tests` -- a project reads and writes its loose tree, and an archive sitting beside it
changes nothing about what it reads.

**10. ~~Delete writes a tombstone, and *Sync* repacks.~~ Dropped.**
The tombstone manifest and *Sync* existed to let the editor edit and delete packed assets and fold
the result back. With the editor never mounting an archive there is nothing to shadow, nothing to
mask and nothing to fold: every asset it lists is a loose file it can unlink. `pack` is how an
archive is refreshed.

**11. Docs and roadmap.**
`docs/archives.md` — the format, the mount model, the exclusion rule, the read-only `.bvat` rule, and
where an archive sits in the workflow (built by `pack` from the loose tree, mounted by a shipped
game, never read back into the editor). `ROADMAP.md` gains the sub-bullets under Asset Streaming Pipeline. The
pieces of this plan worth keeping move there, and the plan is deleted.
*Gate:* the doc's every claim carries a file reference, and `just format` is clean.

---

## Explicitly out of scope

- **Per-entry compression.** The format reserves the flag; choosing a codec wants a measurement.
- **`mmap`-backed reads.** The 16-byte alignment is there for it; the interface does not expose a
  borrowed span yet.
- **Streaming / partial residency.** `Asset Streaming Pipeline` is the roadmap line above this one,
  and it is a different feature that this one is a prerequisite for.
- **Splitting one project across several archives from the editor.** `pack` takes a file list, so
  `Base.bpak` + `DLC1.bpak` is available from the CLI; the editor has no pack UI at all.
- **Patch archives shipped over a base.** The mechanisms exist — mount order for override,
  `LayeredFileSystem::SetMask` for removal — but a patch is authored, versioned and validated, and
  none of that is here.
- **Migrating `bgl`'s `WriteFileAtomic` onto `core::file::write_atomic`.** A cleanup this feature has
  no reason to carry.
