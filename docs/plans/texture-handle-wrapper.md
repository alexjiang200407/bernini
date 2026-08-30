# texture-handle-wrapper — implementation plan

## Context

`libs/bgl/idl/src/TextureHandle.slang` declares `TextureHandle` and `TextureCubeHandle`, two structs
whose whole payload is one `Texture2D.Handle` / `TextureCube.Handle`. They were introduced to give a
handle a place to live when the intent was to pack it as a single `uint`; that intent is gone —
`DescriptorHandle<T>` is eight bytes on every target the engine builds for, and `bgl_idlgen` mirrors
it with no help from a wrapper.

Two things kept the structs alive: they hold the sampling helpers, and
`Uniforms::operator=(SrvHandle)` / `operator=(TextureAssetHandle)` can only write into a struct —
they search member index 0 and throw otherwise, so a field declared as a bare handle would reflect
as a `kDescriptorHandle` *value* and fail to bind. `SamplerHandle` already has that value path,
which is why `MaterialData` declares `SamplerState.Handle` bare beside a wrapped `TextureHandle` in
the same struct, for no reason a reader can find.

`docs/specs/texture_handle_wrapper.md` recorded the problem and the design. This change carries it
out and deletes the spec.

## Decisions

- **ADR-1 — the wrapper structs go, and nothing is aliased in their place.** Every declaration site
  spells `Texture2D.Handle` / `TextureCube.Handle`, which is what `SamplerState.Handle` beside it
  already does. *Rejected: `typealias TextureHandle = Texture2D.Handle`, the spec's design point 1,
  because an alias whose only content is "this is a texture handle" is a second name for a type that
  already says so, and it would leave the wrapped/bare asymmetry the spec complains about visible in
  the spelling. It would also need a new `bgl_idlgen` feature — parsing `public typealias` into a
  C++ `using` — or `bgl::idl::TextureHandle` would vanish from its C++ callers regardless.*

- **ADR-2 — the helpers become `extension Texture2D` / `extension TextureCube`, not extensions of
  the handle.** `extension Texture2D.Handle` compiles but is unreachable: member lookup on a
  `DescriptorHandle<T>` value coerces to `T` first, so the extension is never found (checked with
  `slangc`, both `-target metal` and `-target hlsl`). Extending the resource type is reachable
  through the handle and overloads the built-in `Load` / `GetDimensions` cleanly.
  *Rejected: free functions, because a call site reads worse than the method it replaces and every
  existing one is already `handle.Method(...)`.*

- **ADR-3 — only three helpers survive; `Sample`, `SampleLevel` and `SampleBias` are deleted
  outright.** They were pass-throughs to identically-named built-ins on the handle, so the built-in
  answers every existing call site unchanged. What is genuinely additive is `Load(uint2, uint)` (an
  integer-texel fetch with no sampler), `GetDimensions() -> float2` (mip-0 texel counts as a value)
  and `CubeFaceTexels()`. `SampleCube` / `SampleCubeLevel` become plain `Sample` / `SampleLevel` at
  their four call sites. *Rejected: re-declaring all five on the extension, because an overload
  identical to a built-in is a second way to spell one call.*

- **ADR-4 — the helpers live in `libs/bgl/shaders/src/types/Texture.slang`, not in `idl/`.** An
  extension has no layout to mirror, and `types/` is where the bindless vocabulary already lives
  (`RawHandleView`, `EntryBuffer`, `ComputeBuffer`). *Rejected: keeping the module under
  `libs/bgl/idl/src/`, because a module there that emits no C++ header has to be kept out of
  `IDL_CPP_SOURCES` by hand and claims a place in a CPU/GPU layout contract it has nothing to do
  with.*

- **ADR-5 — `operator=(SrvHandle)` / `operator=(TextureAssetHandle)` are *given* the value path and
  *lose* the struct one, and `c_HandleUniformMember` goes with it.** After ADR-1 nothing declares a
  struct-wrapped SRV, so all three handle kinds bind by one rule. *Rejected: adding the value path
  beside the struct path, which is the spec's wording, because the struct branch would then be dead
  code a reviewer cannot tell from live code, and `docs/uniforms.md` would keep documenting an
  asymmetry that no longer has a reason.*

- **ADR-6 — the C++ side spells `bgl::DescriptorHandle`.** `bgl::idl::TextureHandle` disappears with
  its module; the five C++ sites that named it name the eight bytes directly. *Rejected: teaching
  `bgl_idlgen` to emit `using TextureHandle = DescriptorHandle;` from a Slang `typealias`, because
  that is codegen machinery bought for one alias that ADR-1 does not want.*

## Non-goals

- **`RawTextureHandle` stays exactly as it is.** A payload read out of a `ByteAddressBuffer` cannot
  declare a resource type at all on Metal, so a material record still stores handle-free bytes and
  samples them through a typed view of the same allocation. Different type, different domain.
- **The records are not touched.** `PbrMaterial` and `LoosePbrMaterial` keep their
  `RawTextureHandle` fields and their leading-and-contiguous layout rule.
- **No new handle kind.** `Texture3D` and acceleration structures were the trigger this change
  removes the cost of; neither is added here.
- **`bgl_idlgen` gains no features.** ADR-1 and ADR-6 are chosen so it needs none.

## Acceptance

- `just test bgl` — in particular the golden-image cases (`[render]`), since every texture in the
  engine binds through what this change deletes and a mistake is a picture rather than a crash.
- `just run bgl_tests -- "[texture]"`, `"[twoview]"`, `"[taa]"` — the bindless-sample, typed-view
  and TAA-resolve readbacks, which read a sampled texel back rather than comparing an image.
- `just run bgl_tests -- --gpu-validation` — the change moves descriptors, which is one of the two
  things that suite exists to catch.
- `just test` — the whole set, since `Uniforms` is shared.

## Commits

1. `docs(plans): plan the removal of the TextureHandle wrapper` — this file.
2. `feat(bgl): bind a texture to a bare descriptor-handle uniform` — the value path on
   `operator=(SrvHandle)` / `operator=(TextureAssetHandle)`, added beside the struct path, and
   `CSTextureSampleReadback.slang` switched to a bare `Texture2D.Handle` as the case that proves it.
   Gate: `just run bgl_tests -- "[texture]"`.
3. `refactor(bgl): sample a bindless texture through its handle, not a wrapper` — delete
   `TextureHandle.slang`, add `types/Texture.slang`, sweep every shader, IDL module and C++ site,
   drop the struct path and `c_HandleUniformMember`, update `docs/` and delete the spec.
   Gate: `just test`, plus `just run bgl_tests -- --gpu-validation`.
