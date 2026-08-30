# `TextureHandle` is a wrapper that outlived its reason

**Status:** not solved. Nothing is broken; the wrapper costs a little clarity and one
awkward constraint, and removing it touches every texture binding in the engine.

## What it is

[`libs/bgl/idl/src/TextureHandle.slang`](../../libs/bgl/idl/src/TextureHandle.slang)
declares two structs whose entire payload is one bindless handle:

```slang
public struct TextureHandle
{
    public Texture2D.Handle texture;
    public float4 Sample(SamplerState.Handle s, float2 uv) { ... }
    // SampleLevel, Load, SampleBias, GetDimensions
};

public struct TextureCubeHandle { public TextureCube.Handle texture; /* SampleCube, ... */ };
```

The struct was introduced to give a handle a place to live when the intent was to pack it as a
single `uint`. That intent is obsolete: `Texture2D.Handle` is `DescriptorHandle<Texture2D>`, which
is already exactly eight bytes on every target the engine builds for, and `bgl_idlgen` mirrors it to
`bgl::DescriptorHandle` with no help from a wrapper. A struct is no longer needed to carry it.

## Why it is still there

Two reasons, neither of them the original one, and both real:

1. **It is the sampling API.** `Sample`, `SampleLevel`, `Load`, `SampleBias` and `GetDimensions`
   hang off it, along with the comments explaining why the last two exist at all (a caller wanting
   "coarser than this pixel" keeps the filtering the pixel got; a caller relating a UV footprint to
   texels needs the real size rather than a hardcoded one). Deleting the struct moves those onto
   extensions or free functions; it does not delete them.

2. **The uniforms writer requires a struct.** `Uniforms::operator=(SrvHandle)` and
   `operator=(TextureAssetHandle)` both test `GetType() == kStruct` and write member index 0
   (`c_HandleUniformMember`), throwing otherwise. Only `SamplerHandle` has a bare-value path. A
   field declared as a bare `Texture2D.Handle` would reflect as a `kDescriptorHandle` *value* and
   fail to bind until both operators grow that path.

So the wrapper is now a method holder and a binding requirement, not a packing device — and the
header says none of that.

## What the raw-arena work added to this

Two things, both from PR #540's review rather than from building anything:

- **A bare handle works as a buffer element.** `StructuredBuffer<Texture2D.Handle,
  ScalarDataLayout>.Handle`, indexed and then `.Sample`d, compiles and samples on Metal — checked
  with `slangc`, not assumed. So `RawHandleView<T>` takes either spelling and this spec is
  orthogonal to the raw arenas; nothing in that design depends on the wrapper.
- **The split is arbitrary, and visible.** `MaterialData` declares `SamplerState.Handle` bare beside
  `TextureHandle` wrapped, in the same struct, for no stated reason — samplers already have the
  bare-value path this spec says textures need.

## The trigger

Nothing forces this today. It becomes worth doing when one of these arrives:

- **A third handle kind** (`Texture3D`, an acceleration structure). Each one currently needs its own
  wrapper struct duplicating the same five methods, and the duplication is what makes the wrapper's
  cost visible.
- **A change to the uniforms writer's handle paths** for any other reason. The struct requirement is
  cheapest to remove while that code is already open.

## The design, if it is done

1. `typealias TextureHandle = Texture2D.Handle;` and the same for the cube, so every existing
   declaration site keeps its spelling and its meaning.
2. Move the five methods to `extension Texture2D.Handle { ... }`, keeping their comments verbatim —
   they are the reason those methods exist and none of it is obvious from a signature.
3. Give `Uniforms::operator=(SrvHandle)` and `operator=(TextureAssetHandle)` the bare-value path
   `operator=(SamplerHandle)` already has: a leaf of type `kDescriptorHandle` is written directly.
   `docs/uniforms.md` § *Resource assignment* is the contract to amend; it currently states the
   asymmetry as deliberate.
4. Leave the *records* alone. A handle stored in GPU memory is a separate question — see below.

The gate is the golden-image suite: every texture in the engine binds through one of these two
structs, so a mistake is a picture, not a crash.

## What this is **not**

It is not the raw-buffer problem. A payload read out of a `ByteAddressBuffer` cannot declare a
resource type at all on Metal — Slang rebuilds the struct from loaded scalars and MSL will not cast
an integer to a texture — so a material record stores a handle-free `RawTextureHandle` (the same
eight bytes, no resource type) and samples it through a typed view of the same allocation. That is a
different type for a different domain, and it stays whatever happens to this one. Removing
`TextureHandle` would leave `RawTextureHandle` exactly where it is.
