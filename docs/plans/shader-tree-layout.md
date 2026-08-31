# shader-tree-layout — implementation plan

## Context

`libs/bgl/shaders/src` holds 26 entry-point programs in one flat directory, with `forward/`,
`types/`, `util/`, `debug/` and `idl/` beside them holding the shared modules. There is a rule in
that — top level is a program, a subdirectory is a library — but it is unwritten, and
`clip_playback.slang` already breaks it: it exports no entry point and sits among the programs.
Thirteen of the 26 carry a `Forward_` prefix doing the work a directory would do.

The flat pile has a second cost that is not cosmetic. `libs/bgl/shaders/CMakeLists.txt` names each
program in a hand-written `compile_shader` block, and that list is the only thing that compiles a
shader to DXIL at build time — the check that catches a construct Metal accepts and DXC rejects.
Seven live programs are missing from it: `TaaResolve`, `PostProcess`, `OutlineMask`,
`Forward_PBR_AlphaTest`, `Forward_PBR_HashedAlpha`, `Forward_PBR_Loose_AlphaTest` and
`Forward_PBR_Loose_HashedAlpha`. [docs/slang_shaders.md](../slang_shaders.md) records that this exact
gap is how `MaterialType : uint8_t` reached master. A list nobody can derive is a list somebody
forgets, and the flat tree is what makes it underivable — there is no directory that means *this is a
program*.

## Decisions

- **ADR-1 — Split the tree into `programs/` and `lib/`, grouping programs by render feature.**
  `programs/{forward,culling,screen,env,anim}` and `lib/{forward,types,util,debug,anim}`. This is what
  Unreal (`Engine/Shaders/Public` vs `Private`, and `Private/PostProcess`, `Private/Nanite`), Unity
  SRP (`ShaderLibrary/` beside per-feature programs) and Godot's RD renderer (`shaders/effects`,
  `shaders/environment`, `shaders/forward_clustered`) all do. *Rejected: grouping by stage kind
  (`graphics/` vs `compute/`), because it separates `CullInstances` from the pass that dispatches it
  and says nothing about what a shader is for.*

- **ADR-2 — The directory is part of the Slang module name.** `CreateShader("programs.forward.PBR")`,
  `import lib.forward.common`. One name resolves to exactly one file and the call site says where it
  lives — Unreal's rule, where `IMPLEMENT_GLOBAL_SHADER` names a full virtual path. *Rejected: adding
  each program directory to `c_ShaderSearchPaths` to keep names flat, because two leaves with one
  name would then resolve by search order with no diagnostic, and the path list would be duplicated
  in `Device_d3d12.cpp` and `Device_metal.cpp`.*

- **ADR-3 — Drop the `Forward_` prefix.** `programs/forward/PBR.slang`, not
  `programs/forward/Forward_PBR.slang`. The prefix was the directory's job; keeping both stutters at
  every call site. *Rejected: moving without renaming, which keeps `Forward_PBR` greppable across
  history at the cost of a name that repeats itself forever.*

- **ADR-4 — Derive the `compile_shader` list from `programs/`.** A `GLOB_RECURSE` over
  `programs/*.slang`, with the stage and entry-point name read out of each file's `[shader("…")]`
  attributes. All 26 programs are DXIL-validated, including the seven that were not, and a program
  written next month is validated the day it is written. *Rejected: appending the seven missing
  blocks by hand, which fixes today's gap and leaves the mechanism that produced it.*

- **ADR-5 — Leave the generated `idl/` tree at the `src` root.** `import idl.Constants` is unchanged,
  and `lib/` comes to mean *hand-written shared module*, which is the honest distinction: `idl/` has a
  generator, a banner and a lifecycle of its own. *Rejected: `lib/idl/` for a single uniform rule,
  because it costs 83 import rewrites plus the output path in `scripts/gen_idl.py` and
  `IDL_SLANG_OUT_DIR` in `libs/bgl/idl/src/CMakelists.txt` to say nothing new.*

## Non-goals

- `libs/bgl/shaders/tests/` is untouched. Its 17 files are a different regime — the stage is inferred
  from a `VS`/`PS`/`CS` filename prefix and the entry is always `main` — and that convention is
  load-bearing for the glob that already compiles them.
- No shader body changes. Imports, module names and file paths only; not one line of HLSL moves.
- No change to the shader cache, its key, or the two `c_ShaderSearchPaths` roots.
- The seven newly-validated programs are validated, not fixed. If DXC rejects one, that is its own
  change.

## Acceptance

- `just build` on a D3D12 preset compiles 26 programs where it compiled 19, and the configure log
  names each. A DXC rejection in one of the seven is a build failure, which is the point.
- `just test bgl` passes, golden images included: every `CreateShader` name resolves through the
  Slang session, so a mistyped module name is a pipeline that fails to initialise rather than a
  silent miss.
- `just test editor gamelib` passes — they create devices and build the same passes.
- `grep -rn '"Forward_\|"TaaResolve"\|"Skybox"' libs apps` finds no bare module name left behind.

## Commits

1. `docs(plans): plan the shader tree layout` — this file.
2. `refactor(bgl): split the shader tree into programs and a library` — the moves, the `Forward_`
   rename, every `import`, every `CreateShader` constant, and the `compile_shader` paths.
   Gate: `just build` and `just test bgl`.
3. `build(bgl): derive the shader compile list from the program tree` — the glob and the
   `[shader("…")]` parse replace the 32 hand-written blocks; the seven unvalidated programs start
   compiling. Gate: the configure log lists all 26, and `just build` still passes.
