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
| Foreign | `.ktx2` (Basis/RGB9E5 textures) | the bakes; stamp-governed via the routes that name them |

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
| `bakeToken` | the engine's bake revision for this container kind — `src/bake_tokens.h` |
| `sourceSize` / `sourceHash` | the copied source as it measured when this was written |
| `parametersHash` | the import document's parameter subtree, hashed |
| source mount key | which source; empty when none was ever recorded |

A mismatch in any component — in either direction — is a **cache miss, not an error**. There is no
conversion and no old shape to parse: the recovery is always regeneration from the authored side.
For geometry that happens in memory at load through `AssetStore`'s `LoadRegen*` seam (a read-only
store trusts its keys, because `pack` made them true); for `.bvat` and the env family the re-bake
is deliberate — `pack`, the editor, gamelib's bake-on-demand — rather than at load.

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

## Rewriting a whole project

`assetlib_cli migrate -p <project>` reads every container and re-saves whatever is not
byte-identical to the current form — geometry through the regeneration seam
(meshes before rigs before clips, so a regenerated `.banim` measures its posed boxes against
current meshes), everything else as read. A second run rewrites nothing; a file it cannot read is
reported per-file, and the CLI exits non-zero. `assetlib_cli describe -p <project> <key> --key`
prints a cache entry's key without loading its payload.

`pack` is the other writer: stale geometry and env bakes are made current *in the archive* (and
for env, on disk first), because a shipped read-only mount has nowhere to regenerate — see
[Asset Archives](archives.md).

## Files from before either regime

Chunk-era files — the retired self-describing containers — are not readable by this build. A
project that still carries one is migrated with a build from before the schema removal, and the
error messages say so.
