# Asset Containers — authored text, derived cache entries

Every file under a project's data root is one of two things, and which one decides everything
about how it is read, written, merged and recovered:

- **Authored** — a person made a decision and this file records it. Authored files are canonical
  JSON text documents: diffing and merging them is the point, and losing one loses work.
- **Derived** — a bake or an import computed it from something authored. Derived files are cache
  entries: binary, committed for speed, and disposable — a stale or unreadable one is regenerated
  from its source, never repaired.

**The data root's first level is that split.** `Data/Authored/` holds what a person decided —
the copied sources and every text document — and `Data/Derived/` what a bake or an import computed.
The categories sit under one half or the other, and `project_layout.h` is where each is named, so a
project's commit rule is a directory rather than a list of extensions. The one thing a half cannot
say is whether *git* keeps a file: see the table below.

**The split is enforced, not trusted.** `AssetStore::Save` throws when a key names the wrong half
for the container being written, and the half a container belongs to is read off its codec —
`CacheEntryCodecFor<T>`, the same predicate that already tells a cache entry from a document, so
there is no second list to drift. `originOf` and `requireOrigin` (`project_layout.h`) are the rule;
`WriteTextures` and every directory an environment import names are checked against it too, up
front, because a `.benvl` misplaced after its convolutions would have cost minutes to find out.
Reads are deliberately *not* checked: a read that refuses is how a project becomes unopenable, and
the rule exists to stop a bad file being written rather than to relitigate one already there.

**This document is a map, not a mirror.** The headers at the linked paths are the source of truth;
when this page disagrees, trust the header, then fix this page.

| Kind | Files | Written by |
|---|---|---|
| Authored text | `.bmaterial`, `.benv`, `.bimport`, `.bavatar`, `.bblend` | the editor, `migrate`, deliberate saves |
| Derived cache entry | `.bmesh`, `.bskel`, `.banim`, `.bsky`, `.benvl` | the import, the bakes, `migrate`, `pack` |
| Foreign | `.ktx2` (Basis/BC/RGB9E5 textures) | the bakes and the mesh import; stamp-governed by whatever names them |
| Foreign, authored | `.rml`, `.rcss` (UI documents and styles), `.ttf` (fonts) | a person, in `Authored/UI` and `Authored/Fonts`; this library stores and packs them and parses none of them |
| Foreign, authored, unpacked | `.slang` (game surfaces) | a person, in `Authored/Shaders`; the renderer opens them off a host path rather than through the mount, so `pack` skips them — see [Game-Defined Surfaces](game_defined_surfaces.md) |

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
`migrate`, `pack`. A read-only store never takes it, because `pack` made its keys true.

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

A `.bimport` names three things, two of which nothing else can derive
([import_document.h](libs/assetlib/include/assetlib/import_document.h)):

* **`source`** -- the copied file this document describes. Recorded rather than read off the
  document's own name: the swap that reaches `kirk.glb` from `kirk.bimport` answers only for a
  source kind with a single extension, so it is derivable for a mesh and for nothing that follows.
  A document written before the field has none, and `importedSourceKeyFor` falls back to that swap
  for one; `migrate` backfills it. Being stored, it is a reference a rename rewrites -- the one
  thing that separates it from the `.bavatar`'s derived edge beside it.
* **`skeleton`** -- the `.bskel` this source's joint indices address. Authored rather than inferred,
  which is what lets one rig serve several sources: a second `.glb` skinned to a rig already in the
  project binds it instead of forking a signature-matching duplicate. A skinned source whose
  document names none refuses at regeneration and says to run `migrate`.
* **`outputs`** -- every derived file this source produced that a re-import can put back, as mount
  keys, sorted. For a mesh that is its containers: a *produced* rig is listed, a *bound* one is not,
  so deleting a source never takes another source's rig with it. For an environment it is the `.bsky`
  and the `.benvl` -- names an import fixes in advance, where a mesh's extracted textures are named
  after images it has not read yet and so are keyed by folder instead. The float cubes an older
  build wrote beside them are dropped from an environment document as it is read.

An environment source's document differs in what `parameters` holds and in the key recorded beside
it. Its `parameters` hold an `environment` object naming the six numbers that decide its baked
maps' pixels (`EnvironmentImportParameters`, in
[env_import_parameters.h](libs/assetlib/include/assetlib/env_import_parameters.h)), and no `sampleRate`, which nothing reads for it
and which would otherwise re-key every environment whenever the mesh default moved. The float
sources are keyed the way a mesh's extracted textures are, below: `envSourceStampSize` /
`envSourceStampHash` and `envSourceBakeToken`, outside `parameters`. Unlike `c_TextureBakeToken`,
`c_EnvSourceBakeToken` has no canary pin — the stages it covers run through libm trigonometry, whose
last bits differ by platform — so its bump is the author's to remember.

`source`, `skeleton` and `outputs` sit outside `parameters`, with `bindings` and
`materialOverrides`: none of them changes what the importer computes.

`bindings` names each submesh's default material; `materialOverrides` registers named alternatives
per submesh (`"materialOverrides": {"crate[0]": {"Rusty": "Authored/Materials/rust.bmaterial"}}`),
omitted when there are none. `rebuildMaterialSlots` rebuilds every regenerated mesh's material
slots from both, and the `.bmesh` carries the result, since `pack` leaves the document behind. An
override-only material still takes a slot in the mesh's `materials`, so it is a reference the scan,
a rename and a deletion see. A re-import keeps the overrides — nothing in the source can put them
back.
`outputs` is what makes the derived set answerable from the authored side, which is the only way to
produce a container that is not on disk at all -- a walk over derived files has nothing to
enumerate.

It is also what makes an import **renameable as one thing**. Every file here is named from the
source, so `planRename` on a source (or on its `.bimport` -- one asset, two names) moves the source,
the document and each output that still carries the source's stem, and rewrites every reference to
any of them. What makes a file a source is a document naming it, not its extension -- an
environment's may be a `.ktx2`, which is otherwise a texture. A document is never moved out of its category, `Authored/Meshes` or
`Authored/EnvSources`, by a rename of its own or of a folder holding it, since that is the one place
`Reimport` looks for it. A *bound* rig is not in `outputs` and so is never moved by the source that borrowed it;
a *produced* one is moved, and the borrowing document is rewritten to follow. See
[assetlib API](assetlib_api.md).

### The textures a mesh import extracts

A `.ktx2` is Foreign: it has nowhere to carry a header, so it cannot hold a key of its own. The
extracted textures of a mesh import are keyed by the two fields their `.bimport` carries instead --
`textureDir`, the folder they went into, and `textureStamp`, the source as it stood when they were
written. Together those are the pair `AssetStore::GetStaleImportedTextureSources` compares, and they
sit outside the document's `parameters`, so neither keys the geometry beside them.

The miss is **not** taken at load. `LoadRegen*` passes `GltfTextures::kSkip`, deliberately: it is
called on every mesh load, on every deletion's reference scan, and by `pack`, and
Basis-supercompressing a source's maps there would freeze a level load.
`AssetStore::RefreshImportedTextures`
([libs/assetlib/include/assetlib/AssetStore.h](libs/assetlib/include/assetlib/AssetStore.h))
is the explicit operation that takes it, reached from `migrate` and from the editor's offer when a
project opens.

What makes a re-extract safe over a folder materials route into is the *name*: an extracted texture
is named after the image it came from, so an edited image lands back on the file every route already
holds, and an inserted one takes a new name rather than displacing its neighbours. An image the
source names nothing is named `tex_<content hash>` instead -- there is no name to keep stable, and
the position it used to be numbered by is exactly what an insertion moves.

That hash covers the **decoded source image and nothing else**: mip 0, which `rgba8ToImage` copies
in verbatim, plus its dimensions. Not the chain below it and not the format tag, because those are
what this engine made of the image rather than the image, and a name that read them moved every
time mip generation or colour handling changed. It did: tagging base colours sRGB flipped both at
once and renamed every unnamed one, leaving the authored routes naming files nothing writes -- and
the byte-for-byte follow below cannot repair that, because the bytes it matches on are the ones
that moved.

A file the extract no longer produces is reported and left alone -- a material may still draw it,
and both re-routing and deleting it are the user's. The one exception is a file that holds the same
bytes as **exactly one** file the extract wrote: that is the same image under a different name, so
the file is moved onto it and every material routed there is rewritten. Exactly one, because two
identical files leave nothing to say which the routes meant, and guessing is the failure the naming
rule exists to prevent.

`textureStamp` answers whether the *source* moved, and `textureBakeToken` whether the *bake* did:
`c_TextureBakeToken` ([image_io.h](libs/assetlib/include/assetlib/image_io.h)) is the revision of
the mip chain every 8-bit bake writes *and of the names the extract gives the files*, recorded in
the document when the folder is, and a folder
written under another revision is stale whatever the stamp says. A document from before the key
reads as revision zero, which no revision equals, so such a folder is stale exactly once. It moves
under the same rule as a codec's token -- any change to the bytes or to the naming rule, to a fresh
random value -- and `TokenCanary_test` pins the chain beside it. A material's baked maps — the triplet, and the BC4 occlusion map read from the
authored `geometryOcclusion` source — carry the same revision in its `.bmaterial` (`baked.token`),
compared by `BakeIsStale` and mixed into each map's content-addressed name, so a re-bake under a
new revision writes a new file rather than finding the old one already there.

`c_TextureBakeToken` stops at the 8-bit chain; how that chain is *stored* is a second revision,
`c_TextureEncodingToken` beside it. It covers the encoding table (`textureEncoding`, one row per map
role) and what the encoder makes of each row, so it moves on an edited row and on a libktx upgrade
that changes the blocks alike. `TokenCanary_test` pins it against every row's tag and the bytes the
GPU receives for the canary chain -- which holds only because basisu encodes identically on every
platform the suite runs on.

The token carries the naming rule because on disk the two staleness are one: a folder whose files
carry the wrong names is as stale as one whose bytes are wrong, and nothing else can see it. The
positional names before it were the exception -- `tex<N>.ktx2` is a shape a filename sniff can find,
so a folder still holding one is stale on that alone, a migration that runs itself out once no
folder holds one. Every rule since has produced `tex_<16 hex>`, which is indistinguishable from the
rule that replaced it, so a naming change is carried by the token or by nothing.

## Which of these a project commits

Now that a project can be *produced* from its sources rather than only re-cooked, the rule the two
regimes were always pointing at is available:

| | |
|---|---|
| **Committed** | everything under `Data/Authored/`, plus the `.bproj` beside it. Losing one loses work. |
| **Ignorable** | `Data/Derived/`, less the two rows below — `.bmesh`, `.bskel` and `.banim` come back from `Reimport`, a mesh source's extracted `.ktx2` from the texture re-extract, and an environment's `.bsky` and `.benvl` from `Reimport` when absent and from `migrate` when stale. |
| **Ignorable, but by hand** | a *material's* baked maps under `Derived/BakedTextures/` — the triplet and the occlusion map alike. `assetlib_cli migrate` re-bakes one whose maps its sources no longer produce, when those sources are there to cook from, as do `assetlib_cli bakematerials` and the editor's **Bake All**. Until one of those has run, a fresh checkout opens with every material stale, drawing untextured where it routes and from the extracted source where it names an occlusion map. An *environment's* maps in the same directory are not in this row: a `.bsky` or `.benvl` is baked as it is written, so `Reimport` and `migrate` put them back with it, and `migrate` re-bakes one lost from under a container still on disk. |
| **Derived, and committed anyway** | Only an environment imported before its source was copied into `Authored/EnvSources/`: with no `.bimport` beside a source, nothing puts its `.bsky` or `.benvl` back. Re-importing it — from wherever its `.hdr` is — writes the source and the document, and from then on it is ignorable like everything else. Environments imported since are covered by the row above. |

It is a rule about **projects**. This repository's own `assets/` tree is not one: it is a fixture
tree that `bgl_extended_tests`, `assetlib_tests` and `editor_tests` read directly — `assets/Data` is opened
as a store, a baked `.ktx2` is loaded by its content-hashed name, `assets/Data/Derived/Meshes/apples.bmesh`
is read as a file — so those files are test inputs no import here produces, and they stay committed.

The `forest` environment shows what an environment's authored half looks like. Its source lives in
the tree (`Authored/EnvSources/forest.hdr`, Blender 5.2's CC0 `forest.exr` in Radiance form) with
the `.bimport` recording the parameters it was made at and claiming its `.bsky` and `.benvl`, and
the containers and their baked maps are committed beside it because the suites read them. They are
exactly what the source and the document produce: `Reimport` over a checkout missing them writes
the same bytes, and `migrate` re-cooks them when `c_EnvSourceBakeToken` moves — which is how the
committed set is kept current, since the tree is a fixture and not a project.

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

It runs its sources **across threads within a stage** — rigs, then meshes, then clips, then the
extracted textures, then environments — and takes an
optional [`ProgressSink`](libs/assetlib/include/assetlib/progress.h) that names each container
before it is produced. The stage boundary is not an implementation detail: a mesh names the rig it
binds, and a clip set sweeps its boxes through the meshes standing *on disk*, so a fully parallel
run would measure a clip against a mesh that is not written yet. The whole work list is decided
before any of it runs, which is what makes the count the sink is stepped through fixed.

**A present environment file can be stale, and that is `migrate`'s.** A baked map has no header, so
an environment's key lives in the `.bimport`: the copied source's stamp and `c_EnvSourceBakeToken`
for the whole document, and a hash of the parameters each part was written with. A source
re-exported in place or a moved token stales every part the document claims; a hand-edited
parameter stales only its own part, so re-shaping the sky never re-convolves the lighting. So does
a part whose container is on disk at another codec revision, read off its header. That is what every
other checkout holds once one machine has re-cooked and committed the document — the document is
current and the container is not — and nothing else would rebuild it: `Reimport` produces only
what is absent, and the re-save walk cannot read what it would re-save. The
refresh re-cooks the stale parts — each container and the maps it names — and writes the document
last, so a refresh that fails part-way is still reported stale. `migrate` runs it before its re-save
walk, which is why a moved codec token on `.bsky` or `.benvl` is safe to ship with the moved
`c_EnvSourceBakeToken`: the walk reads containers the refresh has just written. The walk also
re-bakes a container whose baked map is missing, when its source is there to cook.

**Environments come last and one at a time.** Each is decoded once and each *part* re-run only when
its container is missing, so a lost `.bsky` is seconds of projection and does not cost the
lighting's minutes of convolution. Every convolution already spreads across all the
cores there are, so running two environments at once would only divide them. The producing code is
the one the import runs (`src/env_produce.h`), which is what makes the result byte for byte a fresh
import's. None of it depends on the thread count; the suite checks that by importing on one thread
and re-producing on all of them.

A source's extracted textures are covered too, but asked differently: a `.ktx2` carries no header,
so no `outputs` entry can name one and the only signal available is the texture folder being absent
or empty. That is exactly the fresh-checkout case, and it is why `Derived/BakedTextures/` can be ignored at all —
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
source, its rig and its outputs -- the source from the document's own key, so that one is backfilled
whether or not the file is there to be read --
re-cooks the parts of every environment whose document no longer matches them
(`GetStaleEnvironmentSources` / `RefreshEnvironmentSource`; first, so a part both absent and stale
is convolved once rather than by `Reimport` and then again),
produces whatever those documents name that is absent, re-extracts the textures of
every source that has moved since its import, then reads every
container and re-saves whatever is not byte-identical to the current
form — geometry through the regeneration seam
(meshes before rigs before clips, so a regenerated `.banim` measures its posed boxes against
current meshes), everything else as read. A second run rewrites nothing; a file it cannot read is
reported per-file, and the CLI exits non-zero. `assetlib_cli describe -p <project> <key> --key`
prints a cache entry's key without loading its payload.

**An import binds a rig that has grown, too.** `FindMatchingSkeleton` pairs an imported rig to the
project's by signature; where nothing matches outright, a project rig that has only *gained* bones
still addresses every bone the imported one has, and is bound instead — the clips being re-addressed
to it before anything is measured, so the container is cooked against the rig it names. An exact
match always wins, so a project holding both the rig as it was and the rig as it grew binds to the
exact one. `assetlib_cli bakebounds` pairs the same way.

**It also bakes down a re-addressing.** A mesh or clip set cooked against a rig that has since
*gained* a bone is re-addressed at load, every load, by `AssetManager::AcquireSkinnedMesh` (see
[Skinned Meshes](skinning.md)). `migrate` applies the same remap to the file, so the pairing is
signature-equal again and no load has to. A pairing the remap will not resolve — a rename, a
deletion, a reparent — is left exactly as it was: there is no current state to put it at, and the
acquire refuses it by name where it is read.

What this does not put back is a posed box. Re-addressing a *mesh* rewrites its joint indices, so
its geometry no longer hashes to what the box beside it was measured against, and a re-measure runs
once per load until `assetlib_cli bakebounds -p <project>` writes a current one. A clip set
re-addressed on its own is unaffected — its geometry never moved.

`pack` is the other writer: stale geometry and env bakes are made current *in the archive* (and
for env, on disk first), because a shipped read-only mount has nowhere to regenerate — see
[Asset Archives](archives.md). It bakes down a re-addressing for the same reason, and the reason is
sharper there: a loose project can run `migrate` later, and an archive cannot, so a pairing left
mismatched on the way in re-addresses on every load of the shipped game for as long as it ships.

## Files from before either regime

Chunk-era files — the retired self-describing containers — are not readable by this build, and
no build on this lineage converts them. The recovery is the same as any cache miss, from the
authored side: re-import the source or re-author the document; the error messages say so.
