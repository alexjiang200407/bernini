# Embedding the engine

A game is a repository of its own — its own source, its own assets, its own `.bproj`, and a `main()`
that links `gamelib`, `assetlib`, `bgl_extended` and `core`. It is the top-level CMake project, and
it pulls the engine in the way any project pulls in a library it builds from source:

```cmake
set(BERNINI_DIR "$ENV{BERNINI_DIR}")          # a checkout, not a copy
set(VCPKG_MANIFEST_DIR "${BERNINI_DIR}")      # the engine's ports are the game's
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
project(subway CXX)
add_subdirectory(${BERNINI_DIR} ${CMAKE_BINARY_DIR}/bernini)
```

Nothing is vendored and nothing is installed: the consumer has the checkout, compiles it into its own
build tree, and links the targets by name. `install()` / `export()` / `find_package(Bernini)` is the
other version of this problem — consuming the engine as a *binary* — and does not exist.

[tests/embed](../tests/embed) is that block with a `main()` under it, and `just embed` builds it.
It is the only consumer in the repository; everything else here is the engine building itself.

## What a consumer owes

Four things: the first two before `add_subdirectory`, the last two on the targets it declares,
which do not exist until after it.

**`VCPKG_MANIFEST_DIR`** at the engine checkout. vcpkg runs once, from its toolchain, during
`project()`, and reads exactly one manifest. Pointing it at the engine gets `vcpkg.json`, its
`builtin-baseline` and — because vcpkg resolves the relative `overlay-ports` path in
`vcpkg-configuration.json` against that file's own directory — the `cmake/ports` overlay. Nothing is
written into the engine checkout: `vcpkg_installed` still lands in the consumer's build tree. The
cost is that the consumer cannot add a port of its own; the day it needs one, that line becomes a
manifest generated from the engine's plus its extras.

**`CMAKE_RUNTIME_OUTPUT_DIRECTORY`**, and the executable in the same directory. The engine stages its
compiled shaders and the contents of `assets/` there, and the binary resolves both relative to its
working directory at runtime. Embedded, the engine sets none of the three output directories — a
`set()` in a subdirectory scope would not reach the consumer's executable anyway — so this is the
consumer's choice and its responsibility. Configuring with it unset is a hard error rather than a
default, because a guess that lands the staged files where the executable is not produces a build
that succeeds and a binary that cannot find its shaders.

**`add_dependencies(<game> copy_assets)`**, if the game wants the engine's own assets — the
environment maps a scene lights itself with, among others — beside its executable.

**The engine's precompiled header**, `${BERNINI_DIR}/PCH/pch.h`, on the consumer's targets. The
engine compiles its own headers behind it; a translation unit that does not compiles them
differently from every one inside the engine. `target_precompile_headers` is the whole of it.

Everything else has a host-derived default and is only worth naming to change it.

## The cache variables

| | |
|---|---|
| `PLATFORM` | `WINDOWS`, `MACOS` or `WEB` — which platform layer `core` compiles. Defaults from `CMAKE_SYSTEM_NAME`. |
| `RENDERER_BACKEND` | `DX12`, `METAL`, or `NONE` for `bgl_extended_objects` alone with no runtime, shaders or tests. Defaults from the host. |
| `IS_DEBUG` | Shader debug info and the `dbg_raise()` bodies. Defaults from `CMAKE_BUILD_TYPE`; a multi-config generator has none, so there it is a choice. |
| `BERNINI_PROFILING` | Tracy zones and the client that opens a socket. `OFF`. |
| `BERNINI_COMPILER_CACHE` | ccache in front of the compiler when one is installed. `ON`. |
| `BUILD_TESTS`, `BERNINI_BUILD_EXAMPLES`, `BUILD_COVERAGE` | **Forced off when the engine is not the top-level project**, whatever the consumer set. See below. |

The defaults apply whether or not the engine is the top of the build. A vcpkg port configures this
tree top-level, so a default keyed on nesting would give a port build different settings from the
same engine added as a subdirectory.

`VCPKG_TARGET_TRIPLET` is deliberately not in that table. It is vcpkg's, the toolchain already
defaults it per host, and a project that sets its own fights its own port.

## The names that are not namespaced

`BUILD_TESTS`, `BUILD_COVERAGE` and `BERNINI_BUILD_EXAMPLES` are cache variables the engine sets from
its presets, and only the last carries a prefix. The first two are names a consuming project is
likely to have taken for its own suites and its own coverage — and `BUILD_COVERAGE` is the one that
bites, because the engine's `enable_coverage` is a hard error under any compiler but clang, so a
consumer that set it for itself would fail the engine's configure rather than merely over-build.

So when the engine is not the top-level project it sets all three to `OFF` in its own directory
scope, which shadows the cache for the engine's subtree and nothing else: whatever the consumer set
still reaches the consumer's targets. `apps/editor` is skipped by the same test, and with it the
`find_package(Qt6)` that looks for it — Qt is not a vcpkg dependency, and a game has no use for the
editor.

**The function names are not namespaced either, and nothing here fixes that.** CMake functions are
global, so `compile_shader`, `copy_to_target`, `assign_folder`, `enable_coverage`,
`enable_strict_compiler`, `target_force_include`, `slang_entry_points`, `slang_stage_profile` and
`strip_msvc_only_interface_flags` all land in the consumer's namespace when `cmake/` is included —
`bernini_collect_executables` is the only one that reads as ours. A consumer that defines its own
`compile_shader()` silently redefines the engine's, or is redefined by it, depending on which
`add_subdirectory` ran last. Renaming them is a change to every call site in the tree and has not
been made; until it is, a consuming project should prefix its own CMake functions.

## The compiler cache

Every game's build tree compiles the engine into itself, so what the cache carries decides whether
that is an afternoon or a coffee. [`cmake/enable_compiler_cache.cmake`](../cmake/enable_compiler_cache.cmake)
puts ccache in front of the compiler and sets `CCACHE_BASEDIR` to **the engine checkout** — not the
build root — so every absolute path under the engine is rewritten relative before it is hashed.

**It does not carry between an engine build and a game's, and it cannot.** A consumer writes
`add_subdirectory(${BERNINI_DIR} ${CMAKE_BINARY_DIR}/bernini)`, which puts the engine's binary
directory one level deeper than the engine's own build puts it, so the generated precompiled header
is `bernini/libs/core/…/cmake_pch.hxx` there and `libs/core/…/cmake_pch.hxx` here. That path is part
of the hash and no basedir removes the difference. Measured: 0/147.

**It does carry between one consumer build and the next**, which is the property worth having.
Deleting a game's build directory and rebuilding it costs nothing — measured at 147/147 — and so does
a second consumer whose build tree sits where the first one's did. `just embed` prints the rate for
its own run.

Two things cost hits and are worth knowing before blaming the basedir. Flags are hashed, so a
consumer building `Release`, or with `BERNINI_PROFILING` set the other way from the engine, shares
nothing with it. And a `vcpkg_installed` tree *inside* the engine checkout is rewritten relative to
each build's own working directory, so pointing two build trees at one is worse than giving each its
own; a tree shared from outside the checkout keeps its absolute path in both and does not have the
problem.

**A consumer must not need to think about the wrapper.** ccache's settings ride in a generated
wrapper script rather than the environment, and it is written into the *consumer's* build tree, named
after the basedir it carries. A consumer that calls `enable_compiler_cache()` itself therefore gets
its own wrapper with its own basedir instead of overwriting ours, which would otherwise leave one of
the two compiling uncached with nothing on screen to say so.

## What a consumer cannot link

`example_util` — `DemoWindow`, `FlyCamera` and `RmlInput`, the scaffolding every example under
[examples/](../examples) uses to stand a window up, fly a camera around it and hand SDL events to
RmlUi. It is declared inside `examples/`, which a consumer's build does not add.

That is deliberate and not a gap to close. Nothing under `libs/` links SDL3 and that is worth keeping
true: `editor_lib` links `gamelib` `PUBLIC`, so anything SDL-shaped put there becomes a dependency of
the Qt editor, which already owns a window, an event loop and an input system. ADR-10 in the UI
runtime plan decided it, and its rejection named this exact failure — an engine event type both SDL
and Qt map onto is the roadmap's Input Engine designed by accident, with no game to shape it.

So a game writes its own window, event pump and camera, about 200 lines, its own from its first
commit. The workspace's `ws game new` scaffolds exactly that. If a real game eventually wants the
scaffolding shared, the shape to cost is a small platform library *beside* `gamelib` rather than
inside it — a game opts into SDL3 by linking it, the editor never does, and `gamelib` still declares
no event type.

## Keeping it true

`CMAKE_SOURCE_DIR` is the top-level source directory of the **build**, so under a consumer's
`add_subdirectory` it is the consumer's root and every path built from it points at a file that is
not there. `BERNINI_ROOT`, captured in the root [CMakeLists.txt](../CMakeLists.txt) before anything
else runs, is this checkout either way, and nothing in the engine's CMake may name the former.

Two things hold that. `scripts/tests/test_cmake_root.py` fails if the name reappears anywhere,
including under `apps/editor` and `examples/`, which a nested configure never reaches; and
`just embed` compiles a real consumer, which is what proves the public include surface — the half a
configure cannot see.
