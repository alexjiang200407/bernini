# Asset archives

One `.bpak` file in place of a tree of loose assets, mounted behind the data-root-relative path
every reference in the project is already written against. The editor keeps authoring loose files;
the archive is a cook output that the runtime mounts, and a project may be loaded either way.

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
([AssetManager.h:78](../../libs/gamelib/include/gamelib/AssetManager.h)). Nothing stores an absolute
path. That is the key that an archive can be indexed by, unchanged.

**`bgl` never reads an asset.** Its only filesystem use is the shader cache
([ShaderCache_d3d12.cpp](../../libs/bgl/src/d3d12/shadercache/ShaderCache_d3d12.cpp),
[ShaderCache_metal.cpp](../../libs/bgl/src/metal/shadercache/ShaderCache_metal.cpp)), a per-machine
write-back cache of driver PSOs. The layering rule holds without effort: this feature does not touch
`bgl`.

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

### The editor keeps authoring loose files. The archive is a cook output.

Answering the prompt's first two questions directly: after this feature the editor is **unchanged**.
Drag-and-drop still writes a `.bmesh` into `Data/Meshes/`, saving a material still rewrites a
`.bmaterial`, delete still unlinks. Nothing is written *into* an archive, ever.

**Rejected: a read-write archive the editor mounts and saves through.** It fails on four counts, any
one of which is disqualifying:

1. Writing an entry either rewrites the whole container or patches in place with a free list. The
   first makes every material save proportional to the size of the project; the second is a
   filesystem, and one that then needs its own crash-safety, compaction and repair — for a problem
   the OS already solved.
2. The staleness machinery is `stat`. Bake-vs-loose, `vatIsStale`, the thumbnail cache's `FileStamp`
   ([asset_paths.h](../../apps/editor/src/util/asset_paths.h)) and the texture prune all read file
   metadata that an archive would have to store and keep coherent across partial writes.
3. `AssetRefGraph` is explicitly a snapshot rebuilt per question, because the user's file manager can
   move things underneath it. An archive either breaks that contract or reimposes it in a worse
   place.
4. The editor's cost is decode and upload, not `open`. It has no problem to solve here.

Every shipping engine that has an archive format draws this line the same way: `.pak`, `.bsa`,
`.vpk` and `.pck` are all written by a cook step and read-only at runtime.

**Consequence for the prompt's third question — what should not be archived:**

| excluded | why |
|---|---|
| `textures_src/` | authoring source; the bake reads it, the runtime never does |
| `.glb` / `.hdr` and anything else awaiting import | same |
| the `.berniniproject` file | editor metadata, not runtime data |
| the shader cache (`.bsc`, `pipelines.psolib`) | per-machine, write-back, disposable — an archive entry would be write-once and wrong |
| `Levels/` | **included** — levels are runtime data |
| `Textures/` (baked) | **included**, and it is most of the bytes |
| `.bvat` | **included**, and packed fresh — see below |

The rule is one line: *an archive carries what the runtime reads and nothing that produces it.*
`pack` derives that from the same `assetTypeFromExtension`
([asset_refs.h:61](../../libs/assetlib/include/assetlib/asset_refs.h)) the reference graph uses, plus
an explicit directory exclusion for `textures_src/`, so a new container type joins the archive by
being registered once rather than by editing a list here.

### One archive per project by default, `Data.bpak`, mounted as one search path entry.

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
core::file::IFileSystem          Exists / Stat / Read / ReadRange / Enumerate
  ├── core::file::LooseFileSystem   a directory, what everything does today
  ├── core::file::SearchPath        an ordered list of mounts, first hit wins
  └── assetlib::PakFile             a .bpak
```

`core` because `assetlib` (the loaders), `gamelib` (the seam) and `assetlib_cli` all link it and all
need it, while `bgl` links it and must not grow a dependency on an asset container. `PakFile` in
`assetlib` because writing one is a cook, and `assetlib` is the cook library.

**`ReadRange` is not optional.** Without it `readChunksFromFile` loses its partial read and
`AssetRefGraph::Scan` becomes a full read of every mesh in the project. The interface carries a
ranged read from the first commit, and `LooseFileSystem` implements it with the seek it already does.

**`Stat` returns the `SourceStamp` the bake compares** — size and modification time. `PakFile`
stores the stamp each entry had when it was packed, so `drawsLoose` and `bakeIsStale` return the
same verdict against an archive that they returned against the tree it was packed from. A packed
project must not silently change which material representation it draws.

### Both archived and non-archived, always, and loose wins.

Answering the prompt's fourth question: yes, and it is not a transitional state. Three consumers
need loose forever — the editor, the golden-image tests, and a standalone baked model directory
([AssetManager.h:78](../../libs/gamelib/include/gamelib/AssetManager.h)) — and mount ordering is
half the point of having archives at all.

`SearchPath` resolves in order and takes the first hit. The runtime mounts loose-first,
archive-second, so an unpacked file shadows its packed twin: that is the debug workflow (drop one
`.bmaterial` next to the exe and re-run), the patch mechanism, and the mod mechanism, all from one
rule.

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
authoritative and versioned. Nothing to share beyond the atomic-write helper.

```
+--------------------------------------------------+
| Header  magic 'BPAK', version, entryCount,        |
|         entryTableOffset, stringPoolOffset        |
+--------------------------------------------------+
| Payloads, each aligned to 16                      |   <- ReadRange lands here
+--------------------------------------------------+
| Entry table, sorted by pathHash                   |   <- binary search, then verify the string
|   pathHash u64 | pathOffset u32 | pathSize u32    |
|   offset u64   | size u64       | stamp (16 B)    |
+--------------------------------------------------+
| String pool: every path, NUL-terminated           |
+--------------------------------------------------+
```

Entries are stored **uncompressed** in this feature. Compression is a per-entry flag the format
reserves and a later task fills in; landing it now would mean choosing a codec before there is a
measurement to choose it with, and it defeats the `mmap` path the aligned layout is there to allow.

Paths are stored in `normalizeRef` form — the same string `AssetManager` keys on — so lookup is the
identity function, not a translation.

### Under a read-only mount, `.bvat` is trusted, not re-baked.

`AcquireVatMesh` re-bakes a stale `.bvat` because it is a derived build product and the bake is
seconds of CPU. Packed, its inputs (the `.bmesh`'s skin, the `.bskel`, the `.banim`) may be present
but the write target is not, and the staleness question is meaningless: `pack` bakes every `.bvat`
fresh as part of packing, so what is in the archive is correct by construction. When the `.bvat`
resolves out of a mount that reports itself read-only, `AcquireVatMesh` uses it without asking
whether it is stale.

**Rejected: bake to a scratch directory beside the archive.** It reintroduces a writable data root
at runtime, which is the thing shipping an archive is meant to remove, and it would let a shipped
build spend seconds skinning on first load.

---

## What changes

| subsystem | change | risk |
|---|---|---|
| `bgl` | none | — |
| `core` | new `core::file::IFileSystem`, `LooseFileSystem`, `SearchPath` | none; additive |
| `assetlib` | every `load*` gains an `IFileSystem&` overload; `readChunksFromFile` → `readChunks`; KTX2 via `ktxTexture2_CreateFromMemory`; `stampOf` and the four staleness predicates take a filesystem; new `pak_io`; new `pack` CLI command | **highest.** The staleness predicates decide which representation a material draws; a wrong verdict is a silent visual change, not a failure |
| `gamelib` | `AssetManager` takes a `SearchPath`; the path-taking constructor stays and builds a loose mount, so every existing caller compiles unchanged; `AcquireVatMesh` respects a read-only mount | moderate |
| `apps/editor` | none | — |
| docs | new `docs/archives.md`; `ROADMAP.md` line under Asset Streaming Pipeline | — |

The path-taking `load*` overloads are all kept, resolving through a process-default loose filesystem.
Roughly a hundred call sites across the CLI and the test suites do not move, and the diff stays
readable.

---

## Tasks

Bottom-up by layer, one PR each.

**1. `core::file::IFileSystem`, `LooseFileSystem`, `SearchPath`.**
The interface with `Exists`, `Stat`, `Read`, `ReadRange`, `Enumerate`; a directory-backed
implementation; an ordered mount list that takes the first hit and reports which mount answered.
Nothing calls it yet — dead scaffolding at the bottom layer, justified by its tests.
*Gate:* new `core_tests` cases — a `LooseFileSystem` over a temp tree round-trips whole and ranged
reads and reports stamps matching `std::filesystem`; a two-mount `SearchPath` resolves to the first,
falls through on a miss, and enumerates the union without duplicates.

**2. `.bpak`: `PakWriter` and `PakFile`.**
The format above, in `assetlib`. `PakFile` implements `core::file::IFileSystem` and reports itself
read-only. Bounds-checked like `chunk::Reader` — every offset and size out of the file is verified
against the real size by subtraction before anything is allocated.
*Gate:* new `assetlib_tests` — pack a generated tree, read every entry back byte-identical through
both `Read` and `ReadRange`; stamps survive the round trip; `Enumerate` returns exactly what went in;
a truncated, a corrupt-magic and an offset-past-EOF archive each throw rather than read out of
bounds.

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
`stampOf`, `bakeIsStale`, `drawsLoose`, `vatIsStale` and `envMapToDraw` take an `IFileSystem&`.
*Gate:* `assetlib_tests` — a material that reads loose from a directory reads loose from an archive
packed out of that directory, and one that reads baked reads baked; a source touched after packing
flips the verdict on the loose mount and not on the archive.

**6. `assetlib_cli pack` (and `list`).**
Walks a data root, applies the exclusion rule, bakes `.bvat` fresh, writes one `.bpak`. `list` prints
the entry table for debugging.
*Gate:* `assetlib_tests` — packing the test project's data root and enumerating the result excludes
`textures_src/` and the project file and includes every asset type; a round trip through
`pack` then `PakFile` reproduces every input byte-for-byte.

**7. `AssetManager` mounts a `SearchPath`.**
New constructor taking a mount list; the existing path-taking one builds a `LooseFileSystem` and
delegates, so no caller changes. `AcquireVatMesh` skips the staleness check on a read-only mount.
*Gate:* `gamelib_tests` — the whole existing suite passes through the loose mount unchanged; a new
case acquires a mesh, its materials and its textures out of a `.bpak` and gets handles equal to the
loose acquire; a loose-over-archive `SearchPath` resolves an overridden `.bmaterial` to the loose
one; `VatAcquire_test` acquires from a read-only mount without re-baking.

**8. Docs and roadmap.**
`docs/archives.md` — the format, the mount model, the exclusion rule, the read-only `.bvat` rule and
the reason the editor is untouched. `ROADMAP.md` gains the sub-bullets under Asset Streaming
Pipeline. The pieces of this plan worth keeping move there, and the plan is deleted.
*Gate:* the doc's every claim carries a file reference, and `just format` is clean.

---

## Explicitly out of scope

- **Per-entry compression.** The format reserves the flag; choosing a codec wants a measurement.
- **`mmap`-backed reads.** The 16-byte alignment is there for it; the interface does not expose a
  borrowed span yet.
- **Streaming / partial residency.** `Asset Streaming Pipeline` is the roadmap line above this one,
  and it is a different feature that this one is a prerequisite for.
- **Editor "Package project" action.** A menu item over `pack`; trivial once `pack` exists, and it
  drags the editor into a feature that otherwise does not touch it.
- **Editor opening a packed project read-only.** Useful for inspecting a shipped build. Wants the
  Content Explorer off `QFileSystemModel`, which is its own change.
- **Patch archives with deletion records.** Mount order covers override; masking a base entry with
  a tombstone does not exist yet and has no consumer.
