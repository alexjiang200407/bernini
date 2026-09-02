Bernini is a 3D game engine. It uses CMake as the buildsystem. 

# General Notes

- Use bash
- Do not `#include` standard c++ libraries. They're already in the precompiled header `./PCH/pch.h`, which every compiled target in the tree gets — that universality is what makes omitting them safe.
- **Never drop an `#include` because a *subsystem* PCH has it.** `libs/<lib>/src/pch.h` and `apps/editor/src/pch.h` carry the third-party headers that subsystem leans on (Qt, glm) so they cost nothing — but unlike the root PCH they reach only some targets: assetlib's is `PRIVATE` while its public headers are compiled by gamelib and the editor without it, and two Objective-C++ files skip a PCH entirely. So still write `#include <QString>` and `<core/glm.h>` where you use them; the PCH is an optimisation, not an interface. See [docs/build_performance.md](./docs/build_performance.md).
- Library subsystems live under `./libs` (currently `./libs/bgl`, `./libs/bgl_common`, `./libs/bgl_extended`, `./libs/core`, `./libs/assetlib`, `./libs/gamelib`); executable apps live under `./apps` (currently `./apps/editor`); runnable examples under `./examples`
- **Layering**: `bgl_extended` (renderer) never links `assetlib` — it stays codec-free, taking decoded `assetlib_structs` PODs. `assetlib` (offline cook) never links `bgl_extended` — the CLI baker must not drag in D3D12. `gamelib` is the seam that links both, and is where "load this asset into a scene" lives.
- **`bgl_common` sits between the contract and the renderer**, and links neither `bgl_extended` nor any backend. It holds what every renderer needs and no renderer owns — the `gassert` family, the Slang reflection walk, the serializable `ReflectedLayout`, the constant-buffer mirror's layout walk (`UniformsBase`), the shader cache's salt/key/encoding, the TAA jitter sequence, frustum-plane extraction, the Slang diagnostic checker. A header there may name no backend and no bindless type; `bgl_common_selfcheck` compiles the whole public surface against `bgl_common` alone and fails the build on a reach into `libs/bgl_extended/src`.
- **The design bar is not the same everywhere.** See below.
- For each subsystem `$SUBSYSTEM/src` represents the internal .cpp and .h files that WON'T be shared with others.
- For each subsystem `$SUBSYSTEM/include` represents all the headers that will be shared to others.
- The CMakelists will specify the src and include directory in each subsystem as a include directory for the target, so always `#include` to the relative to that. e.g. If the current source file is `$SUBSYSTEM/src/xx/Y.h` do `#include "X.h"` instead of `#include "../X.h"`
- When we `#include` a file in `include` we use <> if we `#include` a file in a src we use ""
- The source files are globbed. Just place source files where other sources are located.
- Uses vcpkg with manifest mode

## The bar each subsystem is held to

Everything under `./libs` — `bgl`, `bgl_common`, `bgl_extended`, `core`, `assetlib`, `gamelib` — and `assetlib_cli` with it, is
held to a **strict** bar. These are libraries: their headers are the interface a reader learns the
system from, and a client cannot route around a bad one. So the public surface must be readable on
its own — one obvious seam per concern, a rule stated in one place, no second way to do the same
thing. `bgl` is the reference: [docs/bgl_api.md](./docs/bgl_api.md) and the handful of `bgl::I*`
headers behind it are what "structured" means here.

`./apps/editor` is a **frontend**, and local mess in it is tolerated. It is the top of the stack,
nothing links it, and a widget that grew awkwardly costs the person editing that widget and nobody
else. It still follows [STYLE.md](./STYLE.md) and the layering rule — the licence is on
*organisation*, not on style or on layering.

The consequence that matters when you are working: a shortcut that would be waved through in
`apps/editor` is a redesign in `libs`. When a change wants a second path into a library — a bake
that takes a data root when the store is right there, a helper that restates a rule the header
already owns — that is the point to stop and change the seam, not to add beside it. Two ways to do
one thing is how the two start disagreeing, and in a library that disagreement reaches every
caller.

# C++ Style

- clang-format each .cpp .h and .slang file modified via `just format <files...>` (or `python scripts/format.py <files...>`; use `--check` to verify without editing). It finds clang-format via `scripts/config.json`, then PATH, then the Visual Studio LLVM component; if none exists it tells the user to install it.

- `just tidy` checks identifier naming with clang-tidy, against the `.clang-tidy` files in the tree. `just tidy --changed` checks only the lines the current diff touches, which is what the pre-commit hook runs. It needs a build dir with `compile_commands.json`, so a Ninja preset — see [docs/naming.md](./docs/naming.md).

The Style Guide is imported here, so it is in context from the start of every session and never has to be opened: @STYLE.md

## Comments: as few as possible, as short as possible

The default is **no comment**. Write one only for a constraint the code cannot show: a non-obvious
pre/post-condition, a hazard, or why the obvious approach was *not* taken. Keep it to one line where
you can. If it needs a paragraph, it belongs in `./docs`, not in the source.

**Never narrate.** These are all noise and must not be written:

- Play-by-play: `// Now we resolve the material`, `// First, allocate the range`, `// Loop over the submeshes`.
- Restating the code: `// Bump the epoch` above `++m_MaterialEpoch;`.
- Explaining the *change* rather than the code: `// This is now per-instance`, `// Moved from Submesh`,
  `// used to live on the GPU struct`. The diff and the commit message are where that belongs — a
  comment addressed to the reviewer is dead weight the moment the PR merges.
- Justifying yourself to the reader: `// which is what makes this correct`, `// exactly what we want`.

A comment states a fact about the code as it is now, to someone reading it a year from now who has
no idea a change ever happened.

# Documentation Index

Read through these documents if you deem them necessary to your given task. If you modify something that is touched on in these docs, you need to modify the docs as well.

**[Naming](./docs/naming.md)**

Which directories are `lower_case` and which are `PascalCase`, why the boundary is a directory
rather than a judgement call, and how `just tidy` enforces it.

**[bgl Public API](./docs/bgl_api.md)**

What a client links against: `IGraphics`, `IScene`, `ISceneView`, `IRenderTarget`, the handle and
descriptor types, and the lifetime/threading rules that govern them. Start here to *use* bgl.

**[assetlib Public API](./docs/assetlib_api.md)**

The offline half: `AssetStore` and the mount-key rule, `Project`, the reference graph behind every
deletion and rename, one `AssetCodec` per container, and the bakes. Start here to *use* assetlib.

**[Geometry Layout](./docs/geometry_layout.md)**

Describes the collection of structures, descriptors, and resources that are bound to the GPU for Geometry Passes.

**[Render Hardware Interface](./docs/rhi.md)**

RHI usage — the internal abstraction bgl_extended is built *on*, one layer below the public API.

**[Uniforms](./docs/uniforms.md)**

The CPU-side mirror of a constant buffer: why cbuffers are reflected at runtime while structured
buffers are generated by `bgl_idlgen`, how a name resolves to bytes, what assigning a resource
handle actually writes, and why a name that resolves to nothing is silent.

**[Graphics Debug](./docs/gfx_debug.md)**

Graphics debugging practices.

**[Build Performance](./docs/build_performance.md)**

How to tell where a build's time went (`just build --time`, reading the log ninja already writes),
what may and may not go in a precompiled header — a PCH is deserialized into every TU, so a header a
minority of sources need is a net loss — the ccache that survives a branch switch or a wiped build
directory, and the job budget every checkout's build shares so three of them do not thrash one
machine.

**[Profiling](./docs/profiling.md)**

Where load and cook time is measured: taking a Tracy capture, the one rule for naming a zone, and
why the frame loop deliberately has none.

**[Frame Graph](./docs/framegraph.md)**

FrameGraph usage.

**[Passes Overview](./docs/passes.md)**

Overview of all the Frame Graph Passes

**[Slang Shaders](./docs/slang_shaders.md)**

The conventions a shader source follows: `Atomic<T>` and its accessors, the bindless buffer
primitives, where a constant buffer may hold a resource, and how the build enforces them.

**[Shader Cache](./docs/shader_cache.md)**

The persistent shader cache: how compiled DXIL, reflection, and driver PSOs are cached to disk to skip shader compilation across runs, how it is invalidated, and why `.slang-module` IR is not used.

**[IDL Codegen](./docs/idlgen.md)**

How `bgl_idlgen` generates CPU/GPU structs, enums, and constants from one Slang IDL module.

**[Skinned Meshes](./docs/skinning.md)**

A rig posed on the GPU and drawn from a per-instance palette or a shared bone anim table: the
compute pass and its barrier-per-depth-level walk, why the previous pose is re-evaluated rather than
remembered, where the skeleton signature is checked and why the culling box cannot be measured (both
for the same reason — `bgl_extended` does not link `assetlib`), and what the editor's Animation panel does
with the tier.

**[Temporal Antialiasing](./docs/taa.md)**

The jitter, the history ping-pong and the resolve: why the client's camera never sees the offset, why
velocity has it removed, and why the resolve writes history rather than the screen.

**[Coverage](./docs/coverage.md)**

The instrumented build mode and the `just coverage` pipeline: the `macos-clang-metal-coverage`
preset, why `enable_coverage` is per-target with a `PUBLIC` link half, and how to drive
`llvm-profdata`/`llvm-cov` by hand — including the `-object` list a correct report needs.

**[Git LFS](./docs/lfs.md)**

Where the binaries under `assets/` actually live — this project's own R2 bucket, reached by a
standalone transfer agent with no LFS server in front of it — how a machine is set up to read and
write it, and why the agent's git config keys cannot be committed.

**[Asset Standards](./docs/asset_standards.md)**

PBR texture (format/color-space/channel) and static-mesh (vertex layout, meshlets, tangents) conventions, plus the in-flight DDS → KTX2 migration.

**[Asset Containers](./docs/asset_containers.md)**

The two container regimes: authored text documents (canonical JSON, unknown keys preserved) and
derived cache entries (a frozen header carrying the cache key over schema-less chunks; a mismatch
regenerates, never converts), plus the bake-token discipline and `assetlib_cli migrate`.

**[Asset Archives](./docs/archives.md)**

The `core::file::IFileSystem` read seam, the `.bpak` format and what `pack` puts in one, and
`AssetStore` — the mount and the writable data root as one object. Why the editor authors the loose
tree and never reads an archive back, and why a mount key is a `string_view` and never a `path`.

**[Environment Maps](./docs/envmaps.md)**

The `.bsky` / `.benvl` / `.benv` split, how a `.hdr` becomes them, who consumes which, and the
authoring traps — gamma, cube-seam edge fixup, resampling — that still bite on a map from elsewhere.

**[Known Issues](./docs/known_issues.md)**

One entry per bug that cost somebody a day and could return: the symptom as it appears from the
outside, the cause, the gate that pins the fix, and what was already ruled out. Read the matching
entry *before* diagnosing a symptom it describes — a green gate there is the fastest way to learn the
cause is a different one.

**[AI Coding Bots](./docs/ai-coding.md)**

The two GitHub Apps that give AI work its own identity: `morgana-coding-agent`, which posts `bcp-revise`'s PR replies and co-authors commits from your machine, and the review agent that reviews a PR when you comment `/review` from a GitHub Actions runner. Covers registration, key custody, secrets, and revocation for both.

**Specs** — not here, and not in `docs/`

A spec is one problem we have decided **not** to solve yet: what it is, the trigger that makes it
urgent, and the design already settled on so nobody re-derives it. It describes code that does not
exist, which is why it is not documentation and is not on master. Every page above says what the
tree *is*, and the rule that keeps them worth reading — change the code, change the doc — has nothing
to say about a file describing code nobody has written.

They live on `artefacts`, an orphan branch worktree'd once per workspace and symlinked into every
checkout as `docs/specs/`, committed on every write by `.claude/hooks/draft_commit.py`. The branch is
local, never pushed and never merged: a spec is written, revised and deleted there, and no pull
request ever moves one onto master. Read one before building the thing it describes, and delete it
when that thing lands. In a checkout the workspace has not set up, and in CI, the directory is simply
absent.

**Plans and Decision Records** — not here either

One file per change: the context it was written in, the decisions with the alternative each rejected,
what it was explicitly *not* doing, and the gate that accepted it. Like a spec it is a document about
work rather than documentation of the tree, and it is addressed to whoever reviews the change rather
than to whoever reads the code afterwards — so it is not on master.

It sits beside the specs, at `docs/plans/`, the second symlink onto the `artefacts` worktree, and is
committed there as it is written. An ADR is amended only by a change that *reverses* it; never edit
one to match code that drifted, which is exactly what turns it into a second source of truth.

# Directory Structure

## Debug

- The project is built in `./build`. Do not modify.
- Use `just exes` or `just run` to locate and launch binaries.
- When running a binary located here you need to set the cwd to the target directory otherwise the filepaths fail (`just run` does this for you)
- Do not hardcode executable paths. The runtime output dir depends on the generator (`bin/<config>` for multi-config generators like VS/Xcode, `bin` for Ninja/Make) so it can't be read statically from CMakeLists. Use the commands below to resolve and run targets instead.
- If a program crashes, a crashlog may exist. It will be at the location of the executable named `{exe_stem}_crash_YYYYMMDD_HHMMSS.log` — stamped, so crashes accumulate rather than overwrite each other. Read the newest unless you are comparing runs.
- Other logs may also exist. So scan for other `.log` files inside the failing executable directory.

# Scripts

Everything is driven by `just` from the repo root, via the `justfile`. Each recipe is a one-line call into the Python scripts in `./scripts`, which are generator-agnostic: target discovery goes through the CMake File API codemodel (works for Ninja/Xcode/Make/VS) and the Visual Studio toolchain is located via vswhere. Shared helpers live in `./scripts/util/`. The discovery commands need a configured build dir; build first if needed.

```bash
just                              # list the recipes
just init                         # set this machine up and write scripts/config.json (see below)
just build [target]               # build (default: all targets); configures first only if needed. --preset, --config, --dry-run, --time, --jobs, --no-jobserver
just run <target> [-- args...]    # build a target, then run it with cwd set to its output dir; --no-build, --no-lock
just test [names...]              # build and run every test suite (or only the matching ones); --list, --no-build, --no-lock
just coverage [names...]          # macOS: build the coverage preset, run the suites instrumented, report; --diff [ref] names the added lines no test executed (--json for agents)
just format <files...>            # clang-format in place (--check to verify only)
just tidy [paths...]              # clang-tidy the naming rules (--changed for a diff, --fix to apply)
just idl                          # regenerate the IDL C++ headers and Slang copies
just targets                      # list all CMake targets (+ --type EXECUTABLE, --json)
just exes                         # resolve executable paths (--target NAME prints one, --json)
just count                        # count source files and lines by language and by module (bgl, assetlib_cli, editor...), tests counted separately
just cleanup [--delete]           # list the local branches whose PR merged; --delete cuts them, nothing else
just pr <cmd> ...                 # the only way to write to a PR: create/comments/reply/comment/edit/check. Opens PRs as you, comments as the bot, routes replies into their thread, and tabulates the diff by category into the body
just watch-pr <pr>                # block until the PR fails CI, gets a submitted review or new comments, or merges; prints one JSON event. --interval, --timeout, --once, --since
```

`gh pr create`, `gh pr comment`, `gh pr review` and `gh pr merge` are blocked by a hook — `just pr`
is the path, and a turn that opens a PR cannot end until `just watch-pr` runs on it. See
[docs/ai-coding.md](./docs/ai-coding.md).

`just` is a convenience layer, not the contract. It is a **soft** requirement (`pip install -r scripts/requirements.txt`), so if it isn't installed, call the script directly — `python scripts/build.py <target>` is exactly what `just build <target>` runs, and every recipe maps to a script of the obvious name (`run` → `exec_target.py`, `test` → `run_tests.py`, `tidy` → `tidy.py`, `idl` → `gen_idl.py`, `targets` → `get_targets.py`, `exes` → `find_executables.py`, `count` → `count_source.py`).

## Tests

`just test` discovers the suites rather than listing them: every executable target named
`*_tests` is one, so adding a target is all it takes for it to be run. They exist only when
`BUILD_TESTS` is on, which the debug presets set and the release ones do not.

One more suite is not a CMake target: `scripts_tests` is the pytest cases under `scripts/tests`,
covering the Python in `scripts/` itself and the Claude Code hooks in `.claude/hooks/`. It runs
from `just test` like any other and reports in the same summary — but it takes no Catch2 filter,
so `just test -- "[tag]"` skips it and says so. It needs `pytest` (pinned in
`scripts/requirements.txt`, offered by `just init`). Nothing in CI
runs it: `.github/workflows/ci.yml` compiles and runs no suite at all.

Every suite is Catch2, so they all take the same flags. A full run is minutes, nearly all of it
`bgl_extended_tests` (device creation per test). Name a suite to skip that: `just test editor`. A failing
suite does not stop the others; the summary at the end says which failed. To pass a flag to one
suite, use `just run`, which forwards it — `just run bgl_extended_tests -- --gpu-validation`, or
`just run editor_tests -- "[materialgraph]"` to run one tag.

One tag is not about behaviour: **`[perf]`** pins what a cook costs as its inputs grow — a read count
that must not scale with an input, a ratio between two problem sizes that must stay far below the
ratio of the sizes. Never a wall-clock ceiling, so the cases hold in a debug build and under load.
[`bcp-precheck`](.claude/agents/bcp-precheck.md) § 5 runs them when a diff touches a path they cover:
`just run assetlib_tests -- "[perf]" --no-lock`.

**Only one suite runs on the machine at a time.** A suite is expensive — each one is split
across several processes, each holding a graphics device — so several checkouts testing at once
oversubscribe the CPU and every one of them slows down. Both `just test` and `just run <suite>`
take a machine-wide lock (`~/.bernini/suite.lock`, `scripts/util/lock.py`) around running the
binaries, and a second one waits, naming who holds it and for how long. `just test` takes it once
for its whole run rather than per suite. `--no-lock` opts out on either command.

It is an advisory lock on an open file, so a killed agent releases it with nothing to clean up.
It is *not* what stops two runs deleting each other's fixtures — that is the temp directory below,
and it still matters, because one `just test` shards a suite across processes that run together.

Each suite process gets a temp directory of its own (`TMPDIR`/`TMP`/`TEMP`). A fixture is free to
name its scratch directory `temp_directory_path() / "bernini_thing"` and wipe it on the way in —
which is what they all do — because no two of those processes share that root.

## Configuration

`just init` records this machine's settings in `scripts/config.json`, and every command reads them so they don't have to be retyped: the CMake preset, the build configuration, absolute paths to tools that aren't on PATH (`cmake`, `ninja`, `clang`, `clang-format`), the `vcpkg` checkout — exported as `VCPKG_ROOT` into every build environment, which is why that variable never has to be set by hand — and a `precommand`, a shell command run for its effect on the environment, normally `vcvarsall.bat`, whose resulting environment every build then runs in.

It also installs what is missing rather than reporting it: vcpkg is cloned and bootstrapped, `cmake`/`ninja`/`clang-format`/`clang-tidy`/`just` come from the versions pinned in `scripts/requirements.txt` (binary wheels), and `git-lfs`/`gh` come from winget or brew. Anything already installed is kept and recorded.

The file is git-ignored; it describes a machine, not the project. `scripts/util/config.py` documents the schema.

Every key is optional and every lookup falls back to auto-detection, so a fresh clone still builds with no `config.json` at all. **Precedence, highest first: command-line flag > config.json > auto-detection.**

# Build

Use `just build`. It builds the preset from `config.json` (or `windows-vs2026-msvc-dx12-debug` if there is none), and sets up the MSVC developer environment via the configured `precommand` — falling back to locating vcvars with vswhere when no `precommand` is set and the preset's generator needs it (Visual Studio / Ninja / NMake on Windows).

**The configure step is skipped once a build dir has been configured.** CMake regenerates itself: the generated buildsystem re-runs `cmake` when a `CMakeLists.txt` changes, and — because every `file(GLOB_RECURSE)` here passes `CONFIGURE_DEPENDS` — also when a glob picks up a new or deleted source file. So adding a source file needs no configure; just build. `build.py` only configures for what the buildsystem can't see for itself: an unconfigured/wiped dir, a missing File API codemodel, or an edited `CMakePresets.json` (presets never enter the buildsystem, so a changed `cacheVariable` would otherwise be ignored — detected by content hash, not mtime, so a `git checkout` doesn't trigger a needless reconfigure). Force one with `just build --configure`.

```bash
just build                                  # configured preset, all targets
just build bgl_extended_tests                        # one target
just build --preset windows-ninja-msvc-dx12-debug
just build --preset windows-clang-dx12-debug # clang (Ninja generator)
just build --config Release                 # multi-config generators
```

## Compilers

The MSVC presets use the Visual Studio generator; the clang presets
(`windows-clang-dx12-{debug,release}`) use the Ninja generator. `build.py`
resolves the clang/clang++ pair and ninja to absolute paths, preferring what
`config.json` records, then the "C++ Clang tools for Windows" (LLVM) component
and bundled Ninja from the Visual Studio install, and falling back to whatever is
on PATH. Compiler-specific
warning flags live in `cmake/enable_strict_compiler.cmake` (MSVC `/`-flags vs.
clang `-`-flags).