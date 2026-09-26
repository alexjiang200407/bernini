---
name: bernini
build: just build
test: just test
format: just format
tidy: just tidy --changed
docs_index: CLAUDE.md
review: bypass
flows:
  one-shot: { pr: true }
  fast: { contract: true, landing: pr }
---

# Bernini — the agent profile

What an agent flow needs to know about this repo and cannot learn from the flow itself. The flows
live in the bernini-workspace repo; this file carries only the engine's half of them. The front
matter is read by the workspace's scripts, and the sections below by its skills, each named for the
step that reads it.

`CLAUDE.md` and `STYLE.md` load on their own and own the rules: the layering, the include rule, the
naming domains, comments. This file does not restate them; it says how a flow applies them.

## Map

- **Docs:** the Documentation Index in `CLAUDE.md`. Subsystems carry their own `CLAUDE.md`
  (`libs/bgl_extended/`, `libs/gamelib/`, `libs/assetlib/`, `libs/core/`, `libs/editor_plugin_api/`,
  `apps/editor/`) with rules the root one does not.
- **Roadmap:** `ROADMAP.md` § Guiding Constraints — design rules, not aspirations. Every grill reads
  them, and every precheck reads the diff back against them.
- **The docs most changes need first:**

| Change touches | Read |
|---|---|
| renderer, passes, barriers | `docs/rhi.md`, `docs/framegraph.md`, `docs/passes.md` |
| GPU-bound structs / descriptors | `docs/geometry_layout.md`, `docs/idlgen.md` |
| assets, cooking, textures | `docs/asset_standards.md`, `docs/asset_containers.md` |
| a GPU problem | `docs/gfx_debug.md`, then `docs/known_issues.md` |

- **C++ is read with clangd** (`CLAUDE.md` § Read the code with clangd); `scripts/bgrep` is grep
  when the search was meant — a completeness sweep, a Slang identifier, a string.

## Grill

The lenses this repo makes worth asking, beyond the flow's own roots:

- **Does it already exist?** Grep `core` and the subsystem by *behaviour*, not by name (the table in
  § Precheck). The duplicate is never called the same thing.
- **Which layer owns it?** A request that needs a layering violation is a wrong request — find the
  seam (`gamelib`, `bgl_common`, `assetlib` for a question about a container) and say so.
- **Which Guiding Constraint does it touch?** GPU-driven by default, one dominant light, instances
  as the unit of scale, an API-agnostic RHI, the IDL as single source of truth, data-oriented state.
  Breaking one is a roadmap decision, the user's to make knowingly.
- **The standard, and our deviations from it.** `ROADMAP.md` rules out clustered/tiled many-light
  lighting, which Unreal and Unity both ship — a deviation made knowingly and written down. The one
  that was not: the TAA resolve's per-pixel variance store, machinery no standard resolve carries,
  held until skinned meshes outran a 3×3 neighbourhood and was removed in `f637fa7d` at the cost of a
  refactor. `docs/taa.md` records the recipe it returned to.
- **Is it a library change?** `libs/` and `assetlib_cli` are held to the strict bar, `apps/editor` to
  the frontend one (`CLAUDE.md` § The bar each subsystem is held to). A second path into a library
  is a redesign, not an addition.

## Implement

- **Slice by layer, bottom-up:** `bgl_common`/`bgl_extended`, then `assetlib`, then `gamelib`, then
  `apps/editor` — the direction the dependencies point. A refactor that enables the change is its
  own commit ahead of it.
- **New sources need no CMake edit.** Every glob is `CONFIGURE_DEPENDS`; place the file beside its
  siblings.
- **Includes** follow `CLAUDE.md` § General Notes: include what you use, standard library too; `<>`
  under `include/`, `""` under `src/`. `just tidy --changed` enforces it, and the pre-commit hook
  runs it with `just format`.
- **Tests are Catch2.** A new `*_tests` executable target is discovered by `just test` with no
  listing. Name a case for the behaviour it pins, tag every case, and prefer a test that proves a
  negative. `bgl_extended_tests` renders headlessly against golden images; `editor_tests` creates a
  device too. Tag GPU cases `[render]`; `[perf]` pins a scaling shape, never a wall-clock ceiling.
- **Verify:** `just build`, then `just test` or the suites the change reaches (`just test editor
  gamelib`). Never two builds against one build dir at once. A run is not green until the logs
  beside the executable have been read — `bgl.log`, and the newest `<exe>_crash_<stamp>.log`, since
  crash logs accumulate.
- **Shaders, barriers or descriptors** also run `just run bgl_extended_tests -- --gpu-validation`
  before the PR: the only thing that catches a bad barrier.
- **Docs change in the same commit** as the behaviour they describe (`CLAUDE.md` § Documentation
  Index). Look for the stated constraint the change made false.

## Feature

- **The contract (`fast: contract: true`)** is any public interface a later task or another repo
  builds on: between subsystems, backends, plugins, and out-of-tree consumers — a game links
  `gamelib`, `assetlib` and `bgl`. It is compilable headers, a small compiled client against a fake
  host, and contract tests that say what the fake cannot prove. A change to no public interface has
  no contract commit; do not manufacture one.
- **Dead scaffolding** is the one thing that lands unused: a `bgl_extended` interface nothing calls
  yet, provided its tests call it.
- **The landing is where the Windows box is written**, once for the whole feature (§ Pull request).

## Precheck

### Has this already been written?

Before accepting any new helper, grep `core` for what it does — by behaviour. A hand-rolled
`(x + a - 1) & ~(a - 1)`, a bespoke `throw std::runtime_error(std::format(...))`, a local
fixed-capacity vector, an ad-hoc FNV — each is a finding, and the fix is the call that already
exists. The second implementation of a pattern three files away in the same subsystem is the same
defect.

| Header | Holds |
|---|---|
| `core/err/util.h` | `throw_runtime_error`, `throw_runtime_error_if` (both `std::format`-style), crash handlers |
| `core/math.h` | `align`, `div_ceil`, `round_up`, `c_Pi` |
| `core/glm.h` | vectors, matrices, quaternions — all real vector math |
| `core/hash.h` | `hash_bytes`, `hash_string`, `hash_pod`, `hash_seed` |
| `core/str/str.h` | `split_once`, UTF conversions, transparent string hash/compare for heterogeneous lookup |
| `core/io/ByteReader.h`, `ByteWriter.h` | binary container reads and writes |
| `core/file/file.h` | `read_file_bytes`, executable and library paths |
| `core/platform/util.h` | `expand_home`, `process_id`, `get_executable_name` |
| `core/containers/` | `static_vector`, `packed_vector`, `slot_vector`, `multi_slot_vector`, `ordered_map`, `enum_set`, `fixed_buffer`, and the handle types |
| `core/ref/` | `Ref`, `SharedRef`, `RefCounter` |
| `core/log/log.h`, `core/settings/Settings.h`, `core/stats/RollingWindow.h`, `core/type_traits.h` | logging, settings, rolling statistics, trait concepts |

### Does the design fight the roadmap?

Read `ROADMAP.md` § Guiding Constraints, then the module the change sits in and its unchecked items.
A change that breaks a constraint is a finding even when it works. Then: **does this design survive
the next roadmap item?** Name the item that would tear it out.

### Is it feasible at AAA scale — in time, and in memory?

The bottleneck is not the renderer — bindless and GPU-driven force that path. It is **asset
structure**: offline and load-time work over one asset element at a time, and it is always found
after landing. `2e22adf5` (#401) introduced a posed-bounds walk that `0cb8003b` (#421) and
`96b9419a` (#457) had to fix, ~6 min to 3.5 s and 740 ms to 29 ms.

**Covered:** any path that touches one asset element at a time — import, bake, load, reimport — in
`assetlib`'s cook, `gamelib`'s load seam or the editor's import. Steady-state GPU work is out.

**The question:** for every new or changed loop on such a path, name the dimensions it multiplies —
bones, frames, mesh entries, vertices, clips, submeshes — and put the numbers below into that
product. This table is an index; each row names the doc that measured it, and a finding that turns
on a figure checks it there. The rows measure particular operations, not a per-element price: never
carry one row's cost onto a different operation that shares a dimension. Where no row measures the
operation, give the shape without a number.

`cha800_00.glb` is the reference character — a real AAA rig, and the largest thing the project cooks.

| Asset | Dimensions | Measured cost | Measured in |
|---|---|---|---|
| Skinned character | 663 bones, 27 mesh entries, 170k vertices, 2254 frames | posed-bounds bake 3.5 s, 2.4 s of it the pose walk; the per-vertex `exactPosedBounds` reference ~6 min (debug) | `docs/skinning.md` |
| Grounding that character's clips | the same rig, 5 clips | 14 s debug — the cook's largest stage; mostly the pose walk | `docs/skinning.md` |
| Re-importing it from source | a 97 MB `.glb` | parsed once per output kind, three times per rebuild: ~14 s debug, overlapped across `Reimport`'s stages | `libs/assetlib/src/reimport.cpp` |
| Clip set (`.banim`) | `boneCount * frameCount` 40-byte `Transform`s | 59.7 MB, ~780 ms to deserialize (debug) | `docs/skinning.md` |
| Posed-bounds read-back | one signature over the whole mesh | 29 ms; asked once per entry instead, 740 ms | `docs/skinning.md` |
| Mesh container (`.bmesh`) | vertex data is nearly all of it | 16.8 MB, of which a reference scan reads ~3 KB | `docs/asset_standards.md` |
| Environment bake | prefilter 256 px / 7 mips / 2048 samples; skybox 512 px / 6 mips; irradiance 128 px | — | `docs/envmaps.md` |
| Cooking one character | the reference rig, from a 97 MB `.glb` | **1.22 GiB resident peak**, none of it tagged | `docs/profiling.md` § What a run costs |
| A device and the editor's widgets | `editor_tests`, whole suite | **1.48 GiB resident peak**; 48.6 MiB device buffers, 15.2 MiB device textures | `docs/profiling.md` § What a run costs |

Terrain and the LOD/atlas multipliers are absent: terrain has no container yet, and LODs and
atlasing multiply these rows rather than adding one.

**Run the cases** where the diff touches a path they cover:
`just run assetlib_tests -- "[perf]" --no-lock`. `--no-lock` because `assetlib` holds no graphics
device, so the suite lock's reason does not apply. Never report a number that was not observed.

**A new cook stage with no zone** is a finding (`revise`): name the call and the dimensions its
`ZoneTextF` should carry (`docs/profiling.md`). A zone whose *name* is interpolated is a finding of
its own — Tracy aggregates by name. A path that runs every frame wants no zone at all.

**Linear and still infeasible.** An honest `O(frames * vertices)` bake is minutes on the reference
rig. Ask what it costs at that scale and whether the caller can find out before paying — a size
query ahead of the bake, or the number in the PR body. `revise`, not `block`.

**Memory.** The finding is a new allocation whose size scales with a dimension in the table, that is
held rather than transient, and that carries no `core::profiling::MemoryTag` and no number in the PR
body. `core::profiling::TaggedBytes`, a member of whatever owns the buffer, is how a door is charged.
A charge inside a per-element loop is the finding, as is a charge that outlives its buffer. Not
findings: an untagged allocation the table shows is small, and the known-untagged `assetlib` cook and
editor thumbnail cache (`docs/profiling.md` § What is measured).

**Severity.** `block` only when the diff makes such a path superlinear in a dimension the table
names — a per-vertex walk inside a per-frame loop, a whole-mesh hash per entry, a re-read per clip
set. Everything else is `revise`, and memory never blocks. Not findings: a constant factor on
offline work nobody waits on, an allocation once per asset rather than per element, cost in a test
or tool, a loop touched without changing its per-element cost.

### Style

The mechanical half — prefixes, `PascalCase`, the directory-decides naming split, include hygiene —
is `just tidy`'s, and the pre-commit hook ran it on the changed lines (skipped with no compile
database, so a Visual Studio-generator clone commits without it). What no tool owns: comments that
should not exist (quote and say "delete"), an identifier you cannot read without its definition
(propose the name, never a comment), whether every function is marked `noexcept` or deliberately is
not, and west const. Formatting is `just format`'s.

### What did no test execute?

Only on a PR based on `origin/master` that adds executable lines under `libs/` or `apps/`:
`just coverage <suites> --diff origin/master --json`, naming the suites the change reaches (an
argument is a substring: `assetlib`, `editor`). macOS only — MSVC has no source-based coverage
(`docs/coverage.md`). It takes the machine-wide suite lock and offers no `--no-lock`, so bound the
call and say so when it did not run. `no_data` — a changed file nothing measured — is a finding on
its own; an uncovered line is a question. It never sets the verdict: `revise` at most, ranges not
percentages.

## Review

What to look for, in order of the damage it does:

- **Layering** (`CLAUDE.md` § General Notes). A new `#include` or link that crosses a boundary is a
  finding even when it compiles today; the selfcheck targets catch only some of them.
- **Correctness.** For barriers, descriptors and shader bindings, be slower and more suspicious than
  the diff size suggests: they fail silently and no suite may catch them.
- **Includes:** paths from the subsystem's `src/` and `include/` roots, never `../`; `<>` under
  `include/`, `""` under `src/`.
- **Comments** that narrate, restate the code or explain the change: delete, not reword.
- **Docs.** The Documentation Index is a contract: a change to what a page describes changes the
  page in the same PR. The usual miss is a stated invariant the change quietly falsified.
- **Naming.** If an identifier cannot be read without its definition, the finding is the name.

**Never findings here**, each a rule a general-purpose reviewer gets backwards:

- **A missing comment.** The default is none.
- **An un-updated `CMakeLists.txt`** for a new source: the globs are `CONFIGURE_DEPENDS`.
- **Formatting**, owned by `just format`; **a missing include**, owned by `just tidy`.
- **Stale golden images** after a deliberate render change: the author's call.

CI compiles on `windows-latest` and `macos-latest` and runs no suite on either; a reviewer has no
GPU. Never say a change passes or fails tests.

## Pull request

**Windows.** CI compiles on Windows but runs no suite and builds `apps/editor` nowhere (Qt is
absent). A compile failure is caught for free; a Windows *behaviour* difference by nothing. Built on
macOS, the change earns a Windows box when it reaches:

- D3D12 in `libs/bgl_extended` — the RHI, barriers, descriptors, PSOs, the Agility SDK;
- shaders — DXIL is not the Metal path, and no runner compares a `[render]` golden image;
- paths and files — `libs/core/file`, separators, case, the mount-key rule in `STYLE.md` § Paths;
- `apps/editor` — nothing builds it in CI.

Name the command, not the need: *"`just run bgl_extended_tests -- "[taa]" --gpu-validation` on
Windows"*. Otherwise state the negative with its reason. A red Windows build reproduces with
`just build --preset windows-ninja-msvc-dx12-debug`.

**Eyes.** Anything whose result is a picture or a gesture — editor UI, a pass's output, a material,
the frame loop's feel. An editor change leaves at least one; say what *right* looks like.
`bgl_ai_viewer` (`docs/ai_viewer.md`) is how an agent looks first.
