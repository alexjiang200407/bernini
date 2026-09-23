# Game-Defined Surfaces

A **surface** is a shading function a game writes and the engine draws through. It lives in the
game's project as a `.slang` file, the engine reads it at startup, and a `.bmaterial` names it and
sets its parameters by name. Nothing about it is compiled into the engine and nothing about it is
generated: the shader is the only declaration of what a material may say.

This page is the map. The contract is
[`libs/bgl/shaders/src/bgl/SurfaceSource.slang`](../libs/bgl/shaders/src/bgl/SurfaceSource.slang)
and the reader beside it, and when this page disagrees with those, trust them.

## Writing one

A surface conforms to `ISurfaceSource`: a parameter struct, a `Coverage` and an `Evaluate`.

```slang
import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;

struct RimParams
{
    [Default(0.2, 0.6, 1.0)]
    float3 rimColor;

    [Default(3.0)]
    float rimPower;

    ColorSlot baseColor;
};

struct RimSurface : ISurfaceSource
{
    typealias MaterialParams = RimParams;

    static float Coverage<R : IMaterialReader>(R reader, RimParams params)
    {
        return reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, RimParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = reader.Sample(params.baseColor, reader.Uv());

        let n = normalize(reader.WorldNormal());
        let v = normalize(reader.CameraPos() - reader.WorldPos());
        surface.emissive = params.rimColor * pow(1.0 - saturate(dot(n, v)), params.rimPower);

        return surface;
    }
};
```

Three rules the file has to keep, each of which the engine checks and names:

* **One conforming struct per file**, and the **file's stem is the surface's name** — `Rim.slang`
  declares the surface a material writes as `"Rim"`. The struct inside may be called anything.
* **The parameter struct is declared and never filled.** The engine reflects it to learn where each
  field sits, packs a material's values at those offsets, and loads the struct back out to hand to
  the two functions. That is why there is no second file to keep in step, and why a field's type is
  what decides how a material may set it.
* **A field is a value or a slot.** A `float`, `float2`, `float3` or `float4` is a value a material
  sets by name; a `ColorSlot`, `DataSlot`, `NormalSlot` or `CoverageSlot` is a texture a material
  binds by name. `[Default(...)]` on a value is what a material that says nothing about it gets;
  an unbound slot samples white, or a flat normal for a `NormalSlot`. `[Color]` on a `float3` or
  `float4` value marks it as a colour — presentation only: the editor shows a swatch and a picker
  for it, and packing and shading read the value the same either way. Registration refuses it on
  anything narrower.

**A file that never imports the contract is not a surface**, and is skipped rather than refused.
The directory is the game's whole module search path, not a list of surfaces, so a shared header or
a helper module can sit beside them and a game that keeps one still starts. The cost is that a
surface which forgets the import is simply not registered — the mistake surfaces one step later,
when a material names it, and still by name.

`Evaluate` returns a `PbrSurface` — the material's half of shading, which the engine's own PBR
lighting then reads. A surface under this contract chooses what a pixel *is*, not how it is lit.
A sibling contract for a surface that owns its lighting — `ILitSurfaceSource`, whose `Shade`
returns pre-exposure radiance through an `ISurfaceLight` of the sun and the environment — draws
through lit programs of its own, and the engine's PBR never runs for it. Nothing reaches either
contract's answer after it returns: geometry AO baked on a second UV set is the surface's to take,
like any other map (below).

`Coverage` runs first on an alpha-tested layer and discards before `Evaluate` is called, so a cheap
coverage answers without the rest of the surface's samples. It is not read at all on an opaque
layer, and a **hashed** layer calls it *twice* — see § Hashed alpha below.

## Where the file goes, and when it is read

Shaders live in the project at **`Authored/Shaders/`**
([`project_layout.h`](../libs/assetlib/include/assetlib/project_layout.h)). They are authored: a
person wrote them, no bake puts one back, and losing one loses work.

They are the one asset category no codec reads. Slang opens them itself, so the renderer is handed
a **host path** rather than a mount key — `GraphicsOptions::surfaceShaderDir` — and the directory is
read by the `Graphics` constructor
([`surface_registry.cpp`](../libs/bgl_extended/src/gfx/surface_registry.cpp)). The consequences
follow from that and are worth stating plainly:

* **Registration happens once, inside `CreateGraphics`.** Each surface found then is bound to a
  slot, its programs are generated from source, and every pipeline is built against them. An edited surface is seen at the
  next launch.
* **The editor registers the project it started with**, and opens one with other shaders by
  restarting into it. The first project never restarts: without one the editor starts on a landing
  page that builds no renderer, and the renderer is built for the project chosen there. After that,
  New or Open Project on a project whose `Authored/Shaders` is a different directory asks first, then
  relaunches the editor with `--project`. Two projects with no shaders
  at all share a session. ([`surface_relaunch.h`](../apps/editor/src/util/surface_relaunch.h))
* **A `.bpak` holds no shaders.** `pack` skips a file whose extension names no container, and a
  packed game reads its shaders off the loose directory beside it.

Registration is in filename order, so which surface takes which slot is the directory's decision
and not a document's. The count is bounded only by the draw-bucket ceiling: past `cMaxDrawBuckets
- 1` surfaces not even one draw bucket each could exist beside the unlit fallback's, so the next is
refused by name at startup rather than ignored. Below that, what a frame draws is still subject to
the ceiling per (tier, kind, layer) key, which clamps to the unlit fallback
([Passes](passes.md)). A surface's programs -- its opaque, alpha-test and hashed colour
programs and an arm in the shared blend program -- are
generated at registration (`src/gfx/surface_registry.cpp`); each is one call into
`lib/forward/GameSurface.slang`, and `programs/forward/GameSurfaceShapes.slang` instantiates every
shape on the null surface so the build validates them. Only the slot bindings load into every Slang
session; a generated program loads from its text the first time a pipeline names it. That is also
when it first compiles: a surface is reflected, and so type-checked, in `CreateGraphics`, but a
program of it that fails only for this backend -- a construct the Metal path accepts and DXC does
not (see [Slang Shaders](slang_shaders.md)) -- fails the first `Draw` that uses that layer, and an
error in its blend arm takes the shared blend program with it. Pipelines are built on demand, so
this is the cost of not building every surface's every layer at startup.

The colour and coverage programs are one per surface and layer rather than a switch, because a
runtime surface switch makes every draw pay the costliest surface's register pressure and diverges
inside a draw, and dynamic dispatch through witness tables is not portable across the backends.
The shared blend program is the one runtime switch on the surface: a program has to name a
surface's type, and the depth-sorted list draws in one dispatch through one pipeline so it can stay
in depth order across surfaces. It costs one `ShadeGameBlended` instantiation per registered
surface, compiled once per surface set and only when something blends. Splitting that draw per
surface would lose the ordering between two surfaces' blended instances.

## What it draws on

Static and skinned geometry both, at every layer. A slot's material kind gets a draw bucket for each
(tier, layer) a material of it is drawn at, allocated the first time one resolves there, and only
the draw buckets a scene uses build a pipeline: opaque, alpha-test and hashed each have their own per
tier, and blended draws through the one shared blend pipeline, whose geometry stage is `AnyMesh`
and branches tier per instance. The tiers differ in nothing
else — a slot's pixel shader reads a `ForwardVSOut` and a material offset, and neither says which
geometry stage filled them, so a surface is written once and a rig costs it nothing.

What the reader gives is the same on both: the interpolants, the camera and the material's own
fields. A surface cannot see the pose, the palette or the bone it was skinned by; by the time it
runs, a skinned vertex is a world-space position like any other.

`WorldNormal` is the normal of the face being shaded: on a double-sided material a back face reads
the interpolated normal negated — the same flip the engine applies to its own lighting — so a
view-dependent term is correct on both faces and a surface never sees a facing bit.

`Uv1` is the mesh's second UV set, where geometry AO is baked on a unique unwrap
([Asset Standards](asset_standards.md#geometry-ao-on-a-second-uv-set)), and `HasUv1` says whether the
mesh carries one. A surface that wants that AO declares a slot for it and samples it itself, exactly
as it samples a normal map — the engine applies nothing on its behalf, so a surface that declares no
such slot neither gets the AO nor shows a port for it in the editor. Sample first and select after,
so the sample stays in uniform control flow, and answer the absent case, which hands back a `Uv1`
far outside the unit square:

```slang
DataSlot occlusion;   // in the params
...
let ao = reader.Sample(params.occlusion, reader.Uv1()).r;
surface.orm.r *= reader.HasUv1() ? ao : 1.0;
```

The map binds by the slot's name, like any other. Multiplying it into `orm.r` keeps it off the sun,
which the engine scales by no AO ([Passes](passes.md)).

## Hashed alpha

A hashed layer replaces the cutoff with stochastic coverage: a fragment survives with probability
equal to its coverage and writes real depth, and the blend is what the ensemble over pixels and
frames averages to — so it is usable only with temporal AA running. The mechanism and every figure
behind it are in [docs/taa.md](taa.md); what is a surface's business is the two things the engine
needs from one, and neither is in `ISurfaceSource`.

**`Coverage` is called twice, through a reader that displaces every sample along the mip chain.**
The hash reads coverage one level finer than the pixel's footprint, where a sub-texel strand still
has shape, and the mean it is steepened about an octave coarser than that. A surface answers with
arithmetic rather than a texel, so the engine cannot sample two levels of it — it evaluates the
*function* at two levels instead, by handing `Coverage` a `BiasedArenaReader`
([GameSurface.slang](../libs/bgl_extended/shaders/src/lib/forward/GameSurface.slang)). The surface
is written once and says nothing about any of it; a shader that never heard of a mip gets the
correction.

**The minification is measured against one declared texture — the coverage carrier.** Steepening
needs to know how many texels of the coverage map a pixel spans, and a surface may bind eight
textures or none. So the surface says which by the kind it declared the field as:

| What the surface declares | The carrier |
|---|---|
| a `CoverageSlot` | that slot, whatever else it declares |
| no `CoverageSlot`, one or more `ColorSlot`s | the first `ColorSlot`, where alpha rides in the colour |
| neither | none — `CreateSurfaceMaterial` refuses the hashed layer, naming the surface |

The fallback is PBR's own rule, which is why a surface whose alpha is its base colour's needs no
extra field to be drawn hashed, and why `PbrLike` — the parity gate for the whole contract — is
measured exactly as the engine's own record is.

**The carrier supplies a resolution and constrains nothing.** What `Coverage` samples is the
surface's business and the engine cannot see it, so a surface that takes coverage off a 512 mask
while declaring a 4K `ColorSlot` is steepened three octaves too hard. Declaring the mask a
`CoverageSlot` is the fix, and it is the reason that kind exists.

The material's `alphaCutoff` is unread on a hashed layer, exactly as it is for a PBR material: a
cutoff is the thing being replaced.

## The document

A material drawn by a surface says so, names it, and sets what it wants by name
([`BMaterial.h`](../libs/assetlib_structs/include/assetlib_structs/BMaterial.h)).

The model names the contract, and there are two: **`pbrSurface`** for a surface on
`ISurfaceSource` — the lighting is the engine's PBR, and what the surface supplies is the
material's half of it, the `PbrSurface` its `Evaluate` returns — and **`litSurface`** for one on
`ILitSurfaceSource`, whose `Shade` is the whole lighting. The document's model is its contract
*expectation*: `CreateSurfaceMaterial` refuses a named surface that conforms to the other one, so
a surface that changes contract fails loud instead of silently changing what every material drawn
by it means. Everything else in the document — the surface name, `parameters`, `textures`, the
per-slot bakes — is identical under both models.

```json
{
	"alphaMode": "blend",
	"doubleSided": false,
	"name": "rimmed_glass",
	"parameters": {
		"rimColor": [1.0, 0.3, 0.1],
		"rimPower": 2.0
	},
	"shadingModel": "pbrSurface",
	"surface": "Rim",
	"textures": {
		"baseColor": "Derived/BakedTextures/glass_basecolor.ktx2"
	}
}
```

* **`surface`** is the shader file's stem.
* **`parameters`** is one to four numbers per name, as many as the parameter was declared with. A
  scalar may be written as a number. A name the surface does not declare is an error, not a value
  dropped on the floor.
* **`textures`** is one mount key per name — the whole binding — or, for a slot composited from
  channel routes, an object: `routes` maps `r`/`g`/`b`/`a` to `{texture, channel}` with the
  source stamps beside them, and `baked`/`token` name the packed map the bake wrote. The two
  forms are exclusive per slot, the routes winning where a document carries both. Routing is the
  editor's offer on *data* slots only — a colour or a normal map is authored whole — and the bake
  behind it is `AssetStore::BakeMaterial`, the same compositor the PBR triplet uses, writing one
  linear BC7 map per routed slot under the shared `slot_` prefix. A routed slot whose bake is
  stale or absent draws each channel from its own source instead: the routes ride the material's
  record and the shader gathers them at draw (`AssetStore::LooseSurfaceSlots` decides per slot,
  against the disk once at load), so the material renders the same either way and nothing is
  composited outside the bake — which remains the shipping form, one BC7 map and a single fetch
  where the loose form samples up to four sources.
* **The layer keys are every model's** and sit beside `shadingModel`, not inside the parameters —
  `alphaMode`, `alphaCutoff`, `doubleSided`.
* **Everything else is PBR's.** `baseColorFactor`, `routes`, `baked` and the rest belong to
  `shadingModel: "pbr"` and a save strips them from a surface material, exactly as a save strips
  these three from a PBR one.

## What is refused, and where

The document reader knows nothing about any surface — the shader is not there when a material is
cooked — so a name is checked at the one place a surface is in hand, which is
`IScene::CreateSurfaceMaterial`:

| The mistake | Where it is caught |
|---|---|
| a parameter that is not one to four numbers | reading the document (`bmaterial_io.cpp`) |
| a `shadingModel` this build does not know | reading the document |
| a surface no shader declared | `CreateSurfaceMaterial`, naming the surface |
| a `shadingModel` naming a surface on the other contract | `CreateSurfaceMaterial`, naming the surface and both contracts |
| a value or texture the surface does not declare | `CreateSurfaceMaterial`, naming both |
| a value bound to a name declared as a texture, or the reverse | `CreateSurfaceMaterial`, saying which it is |
| `alphaMode: "hashed"` on a surface declaring no `CoverageSlot` and no `ColorSlot` | `CreateSurfaceMaterial`, naming the surface and both kinds |
| a file that will not compile, or a surface past `cMaxDrawBuckets - 1` | `CreateGraphics`, naming the file |
| a surface that compiles but whose generated programs do not, for this backend | fatal, at the first `Draw` that uses that layer |

## Boundaries

Deliberate, and each is a decision rather than an omission:

* **A bake per routed data slot, and nothing else.** A colour, normal or coverage slot names a
  `.ktx2` the project already holds; a data slot may instead be composited from channel routes
  (`textures` above). Slot kinds are reflected and reported through
  `IGraphics::GetSurfaceTypes()`, but beyond choosing which slots offer routing they drive no
  format or colour-space rule yet.
* **Editor UI is reflected, never authored twice.** A surface material opens in the Material
  Editor as a sink node generated from `GetSurfaceTypes()` — one port per texture slot, one row
  per value — the layer keys are edited in the properties panel beside the board, and Save writes
  the document from that board. The
  Output selector lists every registered surface beside the four PBR sinks, which is how a
  surface material is created from scratch: pick the surface, and its board replaces the PBR
  one. The
  `.slang` stays the only declaration of what a material may say; the panel edits the *material*.
  A surface the session did not register has no board: the editor refuses to open its materials,
  naming the surface, because the only board it could offer is a PBR one a Save would compile
  into a demotion.
* **No hot reload**, and no export-time compile.
* **No scene inputs.** The reader gives interpolants, the camera and the material's own fields.
  Nothing of the frame — no depth, no history. A lit surface additionally reads the light through
  `ISurfaceLight` — the sun and the environment, and only those; a PBR surface reads no light at
  all, because the engine lights it.
* **No say over bloom beyond `emissive`.** A surface cannot mark itself as glowing or not; bloom
  selects by brightness alone, which is wrong for flat toon shading. `emissive` above the
  target's threshold is the one lever — see [Passes Overview](passes.md) § Bloom.

## Reading further

* [bgl Public API](bgl_api.md) — `SurfaceType`, `SurfaceMaterialDesc`, `GetSurfaceTypes()`.
* [Passes Overview](passes.md) § Two-sided surfaces, and how the forward pass picks a draw bucket's programs.
* [Uniforms](uniforms.md) — why a record is reflected under scalar rules whatever backend draws it.
* [Slang Shaders](slang_shaders.md) — the conventions every module in the tree keeps.
