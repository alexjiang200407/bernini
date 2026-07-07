Bernini is a 3D game engine. It uses CMake as the buildsystem. 

# General Notes

- Use bash
- Do not `#include` standard c++ libraries. They're already in the precompiled header `./PCH/pch.h` for all the targets
- Library subsystems live under `./libs` (currently `./libs/bgl`, `./libs/core`, `./libs/assetlib`); executable apps live under `./apps` (currently `./apps/editor`); runnable examples under `./examples`
- For each subsystem `$SUBSYSTEM/src` represents the internal .cpp and .h files that WON'T be shared with others.
- For each subsystem `$SUBSYSTEM/include` represents all the headers that will be shared to others.
- The CMakelists will specify the src and include directory in each subsystem as a include directory for the target, so always `#include` to the relative to that. e.g. If the current source file is `$SUBSYSTEM/src/xx/Y.h` do `#include "X.h"` instead of `#include "../X.h"`
- When we `#include` a file in `include` we use <> if we `#include` a file in a src we use ""
- The source files are globbed. Just place source files where other sources are located.
- Uses vcpkg with manifest mode

# C++ Style

- clang-format each .cpp .h and .slang file modified via `python ./scripts/clang_format.py <files...>` (use `--check` to verify without editing). It finds clang-format on PATH or the Visual Studio LLVM component; if neither exists it tells the user to install it.

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

# Directory Structure

## Debug

- The project is built in `./build`. Do not modify.
- Use `get_executables.py` or `exec_target.py` in scripts.
- When running a binary located here you need to set the cwd to the target directory otherwise the filepaths fail
- Do not hardcode executable paths. The runtime output dir depends on the generator (`bin/<config>` for multi-config generators like VS/Xcode, `bin` for Ninja/Make) so it can't be read statically from CMakeLists. Use the scripts below to resolve and run targets instead.
- If a program crashes, a crashlog may exist. It will be at the location of the executable named `{exe_stem}_crash.log`
- Other logs may also exist. So scan for other `.log` files inside the failing executable directory.

# Scripts

All scripts are Python and generator-agnostic. Target discovery goes through the CMake File API codemodel (works for Ninja/Xcode/Make/VS) and the Visual Studio toolchain is located via vswhere. Shared helpers live in `./scripts/cmake_tools.py`. The discovery scripts need a configured build dir; build first if needed.

```bash
python ./scripts/build.py [target]            # configure + build (default preset, all targets); sets up vcvars on Windows MSVC generators. --preset, --config, --dry-run
python ./scripts/get_targets.py               # list all CMake targets (+ --type EXECUTABLE, --json)
python ./scripts/exec_target.py <target>      # run an executable target with cwd set to its output dir; program args after a literal --
python ./scripts/find_executables.py          # resolve executable paths (--target NAME prints one, --json)
python ./scripts/clang_format.py <files...>   # format in place (--check to verify only)
```

# Build

Use `python ./scripts/build.py`. It configures and builds the default preset (`windows-vs2026-msvc-dx12-debug`), and sets up the MSVC developer environment (vcvars, located via vswhere) automatically when the preset's generator needs it (Visual Studio / Ninja / NMake on Windows).

```bash
python ./scripts/build.py                                  # default preset, all targets
python ./scripts/build.py bgl_tests                        # one target
python ./scripts/build.py --preset windows-ninja-msvc-dx12-debug
python ./scripts/build.py --config Release                 # multi-config generators
```