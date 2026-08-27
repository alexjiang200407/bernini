# Asset Containers — authored text, derived cache entries

Every file under a project's data root is one of two things, and which one decides everything
about how it is read, written, merged and recovered:

- **Authored** — a person made a decision and this file records it. Authored files are canonical
  JSON text documents: diffing and merging them is the point, and losing one loses work.
- **Derived** — a bake or an import computed it from something authored. Derived files are cache
  entries: binary, committed for speed, and disposable — a stale or unreadable one is regenerated
  from its source, never repaired.

**This document is a map, not a mirror.** The headers at the linked paths are the source of truth;
when this page disagrees, trust the header, then fix this page.

| Kind | Files | Written by |
|---|---|---|
| Authored text | `.bmaterial`, `.benv`, `.bimport` | the editor, `migrate`, deliberate saves |
| Derived cache entry | `.bmesh`, `.bskel`, `.banim`, `.bvat`, `.bsky`, `.benvl` | the import, the bakes, `migrate`, `pack` |
| Foreign | `.ktx2` (Basis/RGB9E5 textures) | the bakes and the mesh import; stamp-governed by whatever names them |

## Text documents

One shape, shared by every authored container ([libs/assetlib/src/json_doc.h](libs/assetlib/src/json_doc.h)):

* **Canonical on every write**: sorted keys, tab indent, one trailing newline, floats at the
  float's shortest decimal (`doc::plainFloat`). One content is one byte sequence, so `migrate`'s
  byte-compare and git's diff both mean something.
* **Unknown keys survive a round-trip**, at every depth. A sibling branch's field passes through a
  reader that has never heard of it, which is what lets two branches author assets in parallel
  without a merge eating one side's work.
* **Known values are refused, not defaulted**: an enum name or shape the build does not recognise
  throws rather than silently becoming something else.
* `.gitattributes` pins them `text eol=lf` — the writers emit LF and `migrate` byte-compares, so
  an autocrlf checkout would otherwise re-report every file on every run.

A tool can tell a text document from a container without an extension:
`isTextAssetDocument` ([libs/assetlib/include/assetlib/container_info.h](libs/assetlib/include/assetlib/container_info.h))
skips leading whitespace and asks whether the bytes open a JSON object, exactly as the loaders do.

## Cache entries

One format, in [libs/assetlib/src/cache_io.h](libs/assetlib/src/cache_io.h): a frozen 64-byte
header, the source's mount key, 16-byte-aligned schema-less chunks, and a chunk table at the end.
The header is versioned (`headerVersion`, currently 1) and **frozen forever** — tools address its
fields by offset (see `tests/src/CacheTamper.h`).

The header carries the **cache key**, and the key is the whole design:

| Component | Meaning |
|---|---|
| `bakeToken` | the engine's bake revision for this container kind — `AssetCodec<T>::c_BakeToken` |
| `sourceSize` / `sourceHash` | the copied source as it measured when this was written |
| `parametersHash` | the import document's parameter subtree, hashed |
| source mount key | which source; empty when none was ever recorded |

A mismatch in any component — in either direction — is a **cache miss, not an error**. There is no
conversion and no old shape to parse: the recovery is always regeneration from the authored side,
through `AssetStore`'s `LoadRegen*` seam, and it is taken **deliberately** rather than at load:
`migrate`, `pack`, the VAT bake. A read-only store never takes it, because `pack` made its keys true.

A **scene load refuses** a stale container instead, naming `migrate` — re-cooking one there costs an
import, writes none of it back, and pays that again on the next load. The editor offers to rebuild
as a project opens (`AssetStore::GetStaleGeometry`), which is where that refusal is meant to be
answered. Its *inspection* surfaces — thumbnails, the material preview, the animation preview — do
still regenerate: they exist to show you the project, including the project you have not updated
yet.

Chunks are addressed by id and **an absent chunk is not an error** — a mesh with no roots chunk is
a mesh with no roots. Ranged reads (`readCacheChunks*`) fetch named chunks and the key without the
payload, which is what keeps a whole-project staleness survey off the disk's throat.

* **A never-recorded source** (empty key, zeroed stamp) is *current while its token holds and
  unrecoverable once it does not* — the rule that keeps synthetic fixtures and pre-recording
  files loading.
* **The token moves on any output-affecting change** — layout *and* meaning — to a fresh random
  value, never a counter: two branches bumping a counter to the same number would produce two
  layouts no reader can tell apart. `TokenCanary_test` pins each writer's output hash beside its
  token, so a layout change without a bump fails in the PR that made it; a semantic change the
  fixture cannot see is still the author's bump to remember.

### What an import document records about its outputs

A `.bimport` names two things nothing else can derive
([import_document.h](libs/assetlib/include/assetlib/import_document.h)):

* **`skeleton`** -- the `.bskel` this source's joint indices address. Authored rather than inferred,
  which is what lets one rig serve several sources: a second `.glb` skinned to a rig already in the
  project binds it instead of forking a signature-matching duplicate. A skinned source whose
  document names none refuses at regeneration and says to run `migrate`.
* **`outputs`** -- every container this source produced, as mount keys, sorted. A *produced* rig is
  listed; a *bound* one is not, so deleting a source never takes another source's rig with it.

Both sit outside `parameters`, with `bindings`: neither changes what the importer computes.
`outputs` is what makes the derived set answerable from the authored side, which is the only way to
produce a container that is not on disk at all -- a walk over derived files has nothing to
enumerate.

### The textures a mesh import extracts

A `.ktx2` is Foreign: it has nowhere to carry a header, so it cannot hold a key of its own. The
extracted textures of a mesh import are keyed by the two fields their `.bimport` carries instead --
`textureDir`, the folder they went into, and `textureStamp`, the source as it stood when they were
written. Together those are the pair `AssetStore::GetStaleImportedTextureSources` compares, and they
sit outside the document's `parameters`, so neither keys the geometry beside them.

The miss is **not** taken at load. `LoadRegen*` passes `GltfTextures::kSkip`, deliberately: it is
called on every mesh load, on every deletion's reference scan, and by `vat_bake` and `pack`, and
Basis-supercompressing a source's maps there would freeze a level load.
`AssetStore::RefreshImportedTextures`
([libs/assetlib/include/assetlib/AssetStore.h](libs/assetlib/include/assetlib/AssetStore.h))
is the explicit operation that takes it, reached from `migrate` and from the editor's offer when a
project opens.

What makes a re-extract safe over a folder materials route into is the *name*: an extracted texture
is named after the image it came from, so an edited image lands back on the file every route already
holds, and an inserted one takes a new name rather than displacing its neighbours. A file the
extract no longer produces is reported and left alone -- a material may still draw it, and both
re-routing and deleting it are the user's.

## Which of these a project commits

Now that a project can be *produced* from its sources rather than only re-cooked, the rule the two
regimes were always pointing at is available:

| | |
|---|---|
| **Committed** | the copied sources, and every authored document — `.glb`, the images, `.bimport`, `.bmaterial`, `.benv`, `.berniniproject`. Losing one loses work. |
| **Ignorable** | every derived container — `.bmesh`, `.bskel`, `.banim`, `.bvat`, `.bsky`, `.benvl`, and the `.ktx2` under `Textures/`. `Reimport` puts them back. |

It is a rule about **projects**. This repository's own `assets/` tree is not one: it is a fixture
tree that `bgl_tests`, `assetlib_tests` and `editor_tests` read directly — `assets/Data` is opened
as a store, a baked `.ktx2` is loaded by its content-hashed name, `assets/Data/Meshes/apples.bmesh`
is read as a file — so those files are test inputs no import here produces, and they stay committed.

A project that takes the second half up must run `assetlib_cli migrate` **before** it does: the
producing side reads each source's `outputs`, and a document written before that field existed
records none. `migrate` backfills them by reading the derived files, which have to be there to be
read.

## Producing a whole project

`AssetStore::Reimport` ([AssetStore.h](libs/assetlib/include/assetlib/AssetStore.h)) is the one
operation that runs from the authored side: it walks the `.bimport` documents and writes the outputs
they name that are **not on disk at all**. Everything else here is keyed on the derived file already
existing — `LoadRegen*` peeks the header of the file it was handed, `migrate` walks the data root —
so nothing else can put back a container that was deleted or never checked out.

What it writes is what a fresh import would have written, byte for byte, which includes sweeping a
clip set's posed boxes exactly as the writer that produced it did: a source that produced a `.bmesh`
swept that mesh, a clips-only source swept the project's. Re-measuring those across the project is
`bakebounds`, deliberately its own operation.

A source's extracted textures are covered too, but asked differently: a `.ktx2` carries no header,
so no `outputs` entry can name one and the only signal available is the texture folder being absent
or empty. That is exactly the fresh-checkout case, and it is why `Textures/` can be ignored at all —
`GetStaleImportedTextureSources` compares the source's *stamp*, which says nothing about whether the
files are there.

Deleting a derived container **through the project** -- `DeleteAsset`, whether the caller named it
or a cascade freed it -- drops the claim from the `.bimport` that produced it, so it stays deleted.
A file removed behind the project's back is a different thing: nothing dropped the claim, so it is
simply absent, and producing it again is the right answer.

Absent only. A container that is on disk but stale is `migrate`'s below — it can read and re-save
one, which is cheaper than a re-import, and splitting them that way is what keeps one problem from
being reported twice when `migrate` runs both.

## Rewriting a whole project

`assetlib_cli migrate -p <project>` backfills any import document written before it recorded its
rig and outputs, produces whatever those documents name that is absent, re-extracts the textures of
every source that has moved since its import, then reads every container and re-saves whatever is not byte-identical to the current
form — geometry through the regeneration seam
(meshes before rigs before clips, so a regenerated `.banim` measures its posed boxes against
current meshes), everything else as read. A second run rewrites nothing; a file it cannot read is
reported per-file, and the CLI exits non-zero. `assetlib_cli describe -p <project> <key> --key`
prints a cache entry's key without loading its payload.

`pack` is the other writer: stale geometry and env bakes are made current *in the archive* (and
for env, on disk first), because a shipped read-only mount has nowhere to regenerate — see
[Asset Archives](archives.md).

## Files from before either regime

Chunk-era files — the retired self-describing containers — are not readable by this build, and
no build on this lineage converts them. The recovery is the same as any cache miss, from the
authored side: re-import the source or re-author the document; the error messages say so.
