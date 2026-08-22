# Asset archives

One `.bpak` file in place of a tree of loose assets, mounted behind the data-root-relative path every
reference in the project is already written against. A shipped game reads its assets out of the
archive; the editor reads them out of the directory; **neither one knows which.**

That last sentence is the whole feature. Everything below is how it is kept true.

---

## The seam

[`core::file::IFileSystem`](../libs/core/include/core/file/IFileSystem.h) is read access to a set of
files addressed by one normalized relative path. Six members, and no more:

| | |
|---|---|
| `Exists(path)` | whether this filesystem answers for it |
| `Stat(path)` | size and mtime, or `nullopt` — absent is not an error |
| `Read(path)` | the whole file |
| `ReadRange(path, offset, size)` | those bytes and no others |
| `Enumerate(prefix)` | every path beneath `prefix`, unordered, whole components matched |
| `IsReadOnly()` | whether *nothing here* can be written |

Three implementations:

- **`LooseFileSystem`** ([header](../libs/core/include/core/file/LooseFileSystem.h)) — a directory.
- **`assetlib::PakFile`** ([pak_io.h:95](../libs/assetlib/include/assetlib/pak_io.h)) — a mounted
  `.bpak`.
- **`LayeredFileSystem`** ([header](../libs/core/include/core/file/LayeredFileSystem.h)) — an
  ordered list of the others; first hit wins, `Enumerate` returns the union with each path once.

`ReadRange` is on the interface rather than layered over `Read`, and that is the one design choice
here that costs a member. A container's chunk table is a few hundred bytes of a file of many
megabytes, and a survey of every mesh in a project reads exactly that much of each. Served by a
whole-file read, such a survey costs a full read of the project.

**Every method is safe to call concurrently on one instance.** The editor decodes two textures on two
threads as a matter of course. This is why `PakFile` opens a stream per read rather than holding one:
a shared handle carries a shared seek position, and the lock that would fix it is a lock on every
read in the engine.

### Paths are keys, not locations

Every path crossing this interface is a `std::string_view`: data-root-relative, `/`-separated,
already in `assetlib::normalizeRef` form. **An implementation does not normalize what it is given** —
two spellings of one path are two paths.

A `std::filesystem::path` is a different thing and never crosses it. The failure is silent and it is
Windows-only: `PakFile` resolves a key through a hash map keyed on the string the archive stored, so
a key that has passed through `path` arrives `\`-separated and misses, while `LooseFileSystem`
resolves it anyway because the OS accepts either separator. Loose works, packed does not, and nothing
reports a problem. See STYLE.md's Paths section, which is the rule this is the reason for.

---

## `AssetStore`

[`assetlib::AssetStore`](../libs/assetlib/include/assetlib/AssetStore.h) is what a caller actually
holds. Two halves, because they are two things:

- **the mount** — what reads resolve through: a directory, an archive, or a loose layer over one;
- **the data root** — what writes land on, always a real directory.

They are separate because an archive entry cannot be unlinked or replaced in place, so a bake, a
rename and a delete all address the directory even when the read that preceded them did not.

```cpp
assetlib::AssetStore store(dataRoot);                       // loose: both halves the directory
assetlib::AssetStore store(dataRoot, std::move(mount));     // reads through mount, writes to dataRoot
```

The loaders are methods — `LoadMesh`, `LoadMaterial`, `LoadTexture`, `LoadVat`, … — as are the
staleness predicates (`BakeIsStale`, `DrawsLoose`, `VatIsStale`) and `Describe`. The mount-taking
free functions they forward to are internal to `assetlib/src`; a caller outside the library reaches
them through a store or not at all.

`IsReadOnly()` is **the whole mount's answer, not one path's.** The question a caller asks is *is
there anywhere at all to put a rebuilt derived file*, and a loose layer over an archive answers yes
even for a path only the archive currently carries — the rebuild lands in the loose layer. Asking
per-path instead gets the archive's answer for exactly the file that needs rebuilding, which is
backwards.

`gamelib::AssetManager` takes one ([AssetManager.h:96](../libs/gamelib/include/gamelib/AssetManager.h)),
and its path-taking constructor is that one over a loose store, so every existing caller compiles
unchanged.

### Staleness crosses the mount by content, not by metadata

A bake records each source as a `SourceStamp` — size and a **content hash**, not an mtime, because a
checkout rewrites mtimes without changing a byte. So `stampOf` through a mount hashes the bytes the
mount serves ([`core::file::hash_file`](../libs/core/include/core/file/file.h) has an overload that
streams them through `ReadRange` in the same fixed chunks the host one uses). **A source stamps the
same loose or packed**, which is what lets a verdict reached against a directory hold against an
archive of it.

Two consequences worth knowing:

- The entry table's per-entry size and mtime are what `Stat` reports, and they are *not* the stamp.
  They still matter — `Stat` is how absence and identity are answered cheaply — but a staleness
  verdict never rests on them.
- The hash memo only applies when the mount is a directory, where a key resolves to a host path that
  can be cached against. Nothing on `IFileSystem` identifies a mount well enough to key a cache on,
  so an archive re-hashes. That is affordable because an archive is read once at load rather than
  swept per material, and because a shipped archive carries no `textures_src` — the routed sources a
  staleness sweep would hash are the very files the exclusion rule leaves out, so they are absent
  and answer without a read.

---

## The format

Header, 16-byte-aligned payloads, entry table, string pool — in that order, all little-endian.

```
+----------------+  48 bytes: magic 'BPAK', version, byte order, entry count,
|     Header     |            table offset, pool offset, pool size, file size
+----------------+
|                |  each payload aligned to 16 bytes
|    payloads    |
|                |
+----------------+  48 bytes each: path offset+size into the pool, payload
|  entry table   |            offset+size, the source file's size and mtime,
|                |            flags (reserved: per-entry compression)
+----------------+
|  string pool   |  the paths, contiguous
+----------------+
```

Both structs are `static_assert`-ed at 48 bytes
([pak_io.cpp:49](../libs/assetlib/src/pak_io.cpp)). The alignment constant is the archive's own and
deliberately *not* `chunk::c_Align`, which happens to be the same number: sharing it would make
bumping one format silently change the other's layout.

**A major version mismatch is refused; a minor one is not.** So is bad magic, a non-little-endian
byte order, a truncated file, or a table whose offsets do not lie inside it — all checked before any
vector is sized from a field the file supplied.

### It is its own format, not the chunk container

`chunk::Writer` ([chunk_io.h](../libs/assetlib/src/chunk_io.h)) builds the whole file in memory
before writing, addresses chunks by a small `uint32` id, and has no path strings. An archive is
gigabytes, addressed by path, and must be readable without loading it. Same *shape* — header, aligned
payloads, table at the end — different problem.

### Writing one

[`PakWriter`](../libs/assetlib/include/assetlib/pak_io.h) **streams.** Each `Add` writes its payload
to a temp file straight away; only the table and the pool are held. Building an archive in memory to
write it would put a ceiling on how big a project can be packed, and the ceiling would be a machine's
RAM rather than anything about the project.

The temp is committed by `Finish` and removed by the destructor otherwise, so an abandoned or failed
pack leaves whatever was at the target untouched — never a shipped artifact missing half the project.

`Finish` sorts the entry table by path, so a reader sees one order however the writer was driven.
That alone is *not* byte-reproducibility: payloads were streamed as they arrived, so `Add` order is
payload order. **Packing one tree twice produces identical bytes** because `packProject` sorts its
walk — `recursive_directory_iterator` order is not the same on two filesystems, and without the sort
an archive would only be reproducible per machine, which is no use to anyone diffing or caching a
shipped one.

---

## What goes in

The rule is one line: *an archive carries what the runtime reads and nothing that produces it.*

`packProject` ([pak_pack.h:75](../libs/assetlib/include/assetlib/pak_pack.h)) derives that from
`assetTypeFromExtension` ([asset_refs.h](../libs/assetlib/include/assetlib/asset_refs.h)) rather than
from a list kept beside it, so a new container type joins the archive by being registered once. On
top of that sit the explicit exclusions: any path with a `textures_src` or `meshes_src` component,
and the `.bimport` import document by its *type* — it is a registered extension, so without its own
rule it would ride into the archive it must never reach.

| | |
|---|---|
| `textures_src/` | excluded — authoring source; the bake reads it, the runtime never does |
| `meshes_src/` | excluded — the imported `.glb` sources and their `.bimport` documents |
| `.bimport` | excluded by type, wherever it sits — authored; a read-only store uses the baked-in bindings. Deliberate, so silent (never in `skippedByExtension`) |
| `.glb` / `.hdr` awaiting import | excluded, by the same rule |
| the `.berniniproject` file | excluded — editor metadata |
| the shader cache (`.bsc`, `pipelines.psolib`) | excluded — per-machine, write-back, disposable |
| `Textures/` (baked) | **included**, and it is most of the bytes |
| `.bmesh` / `.bskel` / `.banim` | **included as the seam answers**, not as the file lies on disk — a stale group re-bakes into the archive, a rebind is baked in, and a group the seam cannot serve fails the pack. `PackReport::geometryRebaked` counts the entries that differ |
| `.bvat` | **included**, packed fresh and re-stamped against the geometry *as archived* — see below |

Everything without a registered extension falls out of the same rule and is **counted**, not dropped
in silence: `PackReport::skippedByExtension` reports each unclaimed extension and how many of it were
found. A runtime container nobody registered would otherwise be absent from every archive with
nothing said about it, and this is what makes that visible. `Levels/` is the live example — no
`.blevel` type is registered yet, so today they are skipped and the count says so.

`PackReport::materialsDrawingLoose` is the other report worth reading. An archive carries no
`textures_src`, so a material still drawing from its authoring routes has nothing to sample once it
ships. The archive is otherwise perfectly valid — every entry reads back — and the failure appears
only as an untextured surface on whoever opens it. The list is sorted, because the walk itself is in
directory-iteration order and that is not the same on two filesystems.

---

## `.bvat` under a read-only mount is trusted, not re-baked

A `.bvat` is a derived build product, so `AcquireVatMesh` normally re-bakes a stale one — it is
seconds of CPU and the inputs are right there. Packed, its inputs may be present but **the write
target is not**, and the staleness question stops being worth asking: `pack` bakes every stale
`.bvat` fresh as part of packing — stale by its own stamps *or* by its geometry group being a
cache miss, since regenerated geometry moves no disk stamp — so what is in the archive is correct
by construction. And because the archive stores the seam's answers for the geometry keys rather
than the disk bytes the bake stamped, every packed `.bvat` is re-stamped against the entries as
archived: the staleness question, asked *inside* the archive, answers fresh.

So `EnsureVatBaked` ([vat_freshness.h](../libs/gamelib/include/gamelib/vat_freshness.h)) branches on
`IsReadOnly` — trusting under a read-only mount, re-baking under any mount with somewhere to write.
This is the one place the seam is not transparent, and it is the reason `IsReadOnly` is on the
interface at all.

Two things it does *not* relax:

- **The clip-set check still holds.** One bake file per (mesh, clip set) via `vatPathFor`; a
  container baked from a different `.banim` is stale even in an archive, and is never silently
  returned. Loading the wrong clips is worse than refusing.
- **A missing one is an error, not a bake.** `pack` only re-bakes the `.bvat` files already present,
  so a rig nothing acquired before packing ships without one. That throws, naming the file, rather
  than failing somewhere inside `saveVat` on a directory that was never there.

---

## Where an archive sits in the workflow

```
   editor  ──writes──>  Data/  ──pack──>  Data.bpak  ──mounts──>  shipped game
                          ^
                          └── the editor also reads here, and only here
```

**The editor never reads an archive.** It authors the loose tree — separate files, which is the unit
version control wants — and `pack` derives an archive from it to ship. One packed blob defeats
diffing, defeats LFS's per-file dedup, and makes every edit rewrite the whole container, so the arrow
points one way and the loose tree is the source of truth.

The consequence worth stating: a packed project cannot be opened in the editor to inspect what
shipped. What that buys is that **every asset the editor lists is one the editor can act on** — no
entry that cannot be renamed, no delete that has to become a tombstone, no window that has to ask
which layer answered.

The packed path is not therefore untested-until-ship-day. `AssetManager_test` acquires a mesh, its
materials and its textures out of a `.bpak`; `VatAcquire_test` acquires from a read-only mount and
from one whose `.bvat` has deliberately drifted; `Pack_test` packs a staged project and reads every
entry back against the tree it came from. That runs on every test run.

The CLI itself is the exception, and it is worth knowing: nothing invokes the `assetlib_cli` binary
from a test. `pack` and `list` are thin wrappers over `packProject` and `PakFile::Enumerate`/`Stat`,
which are covered directly — the argument parsing and the printing are not covered at all.

### Loose still wins, permanently

`LayeredFileSystem` is a search path and not a boolean, and that single rule is three features:

- the **debug workflow** — drop one `.bmaterial` next to the exe and re-run;
- **patching** — ship a later archive, or a loose fix, ahead of the base;
- **mods** — the same, authored by someone else.

`SetMask` marks a path absent across the whole search path. Nothing sets a mask today; it is there
because removal-by-patch is the case that needs it.

---

## The CLI

```bash
assetlib_cli pack -p <project> [-o <archive>]     # default: Data.bpak beside the project file
assetlib_cli list <archive>                       # the entry table, as text
```

`list` is the only command that takes no `--project`, for the same reason the default target sits
beside the data root: an archive is what a project produces, not a member of it, and `PakFile` reads
one standalone.

The walk and the exclusion rule live in `assetlib` (`packProject`), not in the CLI, so the gate is an
`assetlib_tests` gate.

The default target sits **beside** the data root rather than inside it: an archive of a tree is not a
member of that tree, and one packed into the tree it came from would be a candidate for the next
pack. `assetlib::c_DefaultArchiveName` names it once
([pak_pack.h:44](../libs/assetlib/include/assetlib/pak_pack.h)).

---

## Not here yet

- **Per-entry compression.** The format reserves the flag; choosing a codec wants a measurement.
- **`mmap`-backed reads.** The 16-byte alignment is there for it; the interface does not hand out a
  borrowed span yet.
- **Streaming / partial residency.** `ReadRange` is the hook. That is its own roadmap line, and this
  is its prerequisite.
- **Patch archives over a base.** Mount order gives override and `SetMask` gives removal, but a patch
  is authored, versioned and validated, and none of that exists.
