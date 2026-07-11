Bernini is a 3D game engine. It uses CMake as the buildsystem. 

# General Notes

- Use bash
- Do not `#include` standard c++ libraries. They're already in the precompiled header `./PCH/pch.h` for all the targets
- Library subsystems live under `./libs` (currently `./libs/bgl`, `./libs/core`, `./libs/assetlib`, `./libs/gamelib`); executable apps live under `./apps` (currently `./apps/editor`); runnable examples under `./examples`
- **Layering**: `bgl` (renderer) never links `assetlib` — it stays codec-free, taking decoded `assetlib_structs` PODs. `assetlib` (offline cook) never links `bgl` — the CLI baker must not drag in D3D12. `gamelib` is the seam that links both, and is where "load this asset into a scene" lives.
- For each subsystem `$SUBSYSTEM/src` represents the internal .cpp and .h files that WON'T be shared with others.
- For each subsystem `$SUBSYSTEM/include` represents all the headers that will be shared to others.
- The CMakelists will specify the src and include directory in each subsystem as a include directory for the target, so always `#include` to the relative to that. e.g. If the current source file is `$SUBSYSTEM/src/xx/Y.h` do `#include "X.h"` instead of `#include "../X.h"`
- When we `#include` a file in `include` we use <> if we `#include` a file in a src we use ""
- The source files are globbed. Just place source files where other sources are located.
- Uses vcpkg with manifest mode

# C++ Style

- clang-format each .cpp .h and .slang file modified via `just format <files...>` (or `python scripts/format.py <files...>`; use `--check` to verify without editing). It finds clang-format via `scripts/config.json`, then PATH, then the Visual Studio LLVM component; if none exists it tells the user to install it.

Always read the [Style Guide](./STYLE.md)

# Documentation Index

Read through these documents if you deem them necessary to your given task. If you modify something that is touched on in these docs, you need to modify the docs as well.

**[Geometry Layout](./docs/geometry_layout.md)**

Describes the collection of structures, descriptors, and resources that are bound to the GPU for Geometry Passes.

**[Render Hardware Interface](./docs/rhi.md)**

RHI usage.

**[Graphics Debug](./docs/gfx_debug.md)**

Graphics debugging practices.

**[Frame Graph](./docs/framegraph.md)**

FrameGraph usage.

**[Passes Overview](./docs/passes.md)**

Overview of all the Frame Graph Passes

**[IDL Codegen](./docs/idlgen.md)**

How `bgl_idlgen` generates CPU/GPU structs, enums, and constants from one Slang IDL module.

**[Asset Standards](./docs/asset_standards.md)**

PBR texture (format/color-space/channel) and static-mesh (vertex layout, meshlets, tangents) conventions, plus the in-flight DDS → KTX2 migration.

**[Environment Maps](./docs/envmaps.md)**

Generating the IBL pair (radiance + irradiance) in CMFT Studio, why every gamma field must be 1.0, and how to verify the maps before shipping them.

# Directory Structure

## Debug

- The project is built in `./build`. Do not modify.
- Use `just exes` or `just run` to locate and launch binaries.
- When running a binary located here you need to set the cwd to the target directory otherwise the filepaths fail (`just run` does this for you)
- Do not hardcode executable paths. The runtime output dir depends on the generator (`bin/<config>` for multi-config generators like VS/Xcode, `bin` for Ninja/Make) so it can't be read statically from CMakeLists. Use the commands below to resolve and run targets instead.
- If a program crashes, a crashlog may exist. It will be at the location of the executable named `{exe_stem}_crash.log`
- Other logs may also exist. So scan for other `.log` files inside the failing executable directory.

# Scripts

Everything is driven by `just` from the repo root, via the `justfile`. Each recipe is a one-line call into the Python scripts in `./scripts`, which are generator-agnostic: target discovery goes through the CMake File API codemodel (works for Ninja/Xcode/Make/VS) and the Visual Studio toolchain is located via vswhere. Shared helpers live in `./scripts/util/`. The discovery commands need a configured build dir; build first if needed.

```bash
just                              # list the recipes
just init                         # write scripts/config.json for this machine (see below)
just build [target]               # configure + build (default: all targets). --preset, --config, --dry-run
just run <target> [-- args...]    # run an executable target with cwd set to its output dir
just format <files...>            # clang-format in place (--check to verify only)
just idl                          # regenerate the IDL C++ headers and Slang copies
just targets                      # list all CMake targets (+ --type EXECUTABLE, --json)
just exes                         # resolve executable paths (--target NAME prints one, --json)
just count                        # count source files and lines by language
```

`just` is a convenience layer, not the contract. It is a **soft** requirement (`pip install -r scripts/requirements.txt`), so if it isn't installed, call the script directly — `python scripts/build.py <target>` is exactly what `just build <target>` runs, and every recipe maps to a script of the obvious name (`run` → `exec_target.py`, `idl` → `gen_idl.py`, `targets` → `get_targets.py`, `exes` → `find_executables.py`, `count` → `count_source.py`).

## Configuration

`just init` records this machine's settings in `scripts/config.json`, and every command reads them so they don't have to be retyped: the CMake preset, the build configuration, absolute paths to tools that aren't on PATH (`cmake`, `ninja`, `clang`, `clang-format`), and a `precommand` — a shell command run for its effect on the environment, normally `vcvarsall.bat`, whose resulting environment every build then runs in.

The file is git-ignored; it describes a machine, not the project. `scripts/config.example.json` shows the shape and `scripts/util/config.py` documents the schema.

Every key is optional and every lookup falls back to auto-detection, so a fresh clone still builds with no `config.json` at all. **Precedence, highest first: command-line flag > config.json > auto-detection.**

# Build

Use `just build`. It configures and builds the preset from `config.json` (or `windows-vs2026-msvc-dx12-debug` if there is none), and sets up the MSVC developer environment via the configured `precommand` — falling back to locating vcvars with vswhere when no `precommand` is set and the preset's generator needs it (Visual Studio / Ninja / NMake on Windows).

```bash
just build                                  # configured preset, all targets
just build bgl_tests                        # one target
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