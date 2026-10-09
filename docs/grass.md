# Grass

Grass is geometry the renderer builds rather than stores. A mesh source carries **clumps** -- points
on the ground, one per glTF `POINTS` vertex -- or a terrain's layer places them over its heightfield
by rules, and a **look** says what grows there. Every frame the
mesh stage turns the clumps it can see into blades, as many and as finely as their distance earns,
and draws them through the look's material in the opaque phase. Nothing per blade exists on the CPU
or in memory: a field costs its clumps.

## The data

| | where | what |
|---|---|---|
| a look | `bgl::GrassDesc` ([GrassDesc.h](../libs/bgl/include/bgl/types/GrassDesc.h)), authored as a `.bgrass` | the material, the blade's shape, blades per clump, the fade and the thinning, the response to wind, the lighting terms, how far a terrain's blades take their ground's colour |
| the fields | `assetlib::GrassGeometry` ([GrassGeometry.h](../libs/assetlib_structs/include/assetlib_structs/GrassGeometry.h)), embedded in `BMesh::grassFields` | named fields with a mesh index and look slot; chunks of at most `c_GrassClumpsPerChunk` (64) clumps with a bound each; the clumps |
| a terrain's layers | `bgl::TerrainGrassDesc` ([TerrainGrassDesc.h](../libs/bgl/include/bgl/types/TerrainGrassDesc.h)), through `IScene::AttachTerrainGrass` | the look, the clumps' spacing, and the slope, height and patch rules that scale them -- nothing per clump ([On a terrain](#on-a-terrain)) |

A clump is a point, a height scale, a ground normal and a colour. The cook sorts a field's clumps
along a Morton curve before cutting chunks (`assetlib/src/grass/grass_chunks.cpp`), so a chunk is a
compact patch and its sphere is tight.

`IScene::AttachGrass` binds fields to a static geom and looks to its slots; every instance of the geom
then draws them, placed by its transform. `docs/bgl_api.md` has the call's rules. A game never calls
it: `game::AssetManager::AcquireMesh` reads embedded fields and the sidecar's grass bindings,
creates each named `.bgrass` look and attaches them, and the geom's release gives them back
(`libs/gamelib/CLAUDE.md`).
The renderer still receives `BGrassFields` as its decoded input. The standalone `.bgrassfields`
codec and asset kind are retired. `migrate` regenerates foreign-token meshes from the copied
source, retains the sidecar's look bindings, and removes legacy grass output claims and files.
It never parses the retired cache. Unresolved bindings or shared output ownership refuse
conversion. A dry run leaves the project unchanged.

A look with no mesh under it grows on a patch: `assetlib::makeGrassPatch` jitters clumps over a
square, and `AssetManager::CreateGrassPatch` puts them on a ground plane. That is what
`bgl_ai_viewer --grass` draws ([AI Viewer](ai_viewer.md) § Looking at grass). To watch it move, `just run bgl_grass`
opens the same patch in a window with a fly camera, and changes the wind as it runs: `[` `]` its
strength, `,` `.` its heading, `G` the gusts.

In the editor a `.bgrass` opens in the **Grass Editor**: every value of the look beside a patch of it
in a wind the panel sets and never saves. An edit is drawn at once and written on Save, or when the
panel closes with it pending: `AssetManager::SetGrassLook` puts the unsaved document on the look in
place, and a patch whose look could not be drawn as stored is grown from the document instead. Where
the clumps go is the mesh source's, and is not edited there.

## On a terrain

`IScene::AttachTerrainGrass` grows looks on a terrain in **layers**, and stores nothing per clump: a
layer is a record of its rules, and the grass stage builds the clumps near the camera from the
heightfield every frame ([lib/forward/terrain_grass.slang](../libs/bgl/shaders/src/lib/forward/terrain_grass.slang)).
A stored clump list grows with the field's area; a layer costs the same on a field of any size.

- **Tiles.** A layer covers the field with square tiles of 8 x 8 clumps, `spacing` apart, counted
  from the terrain's origin. A tile is a chunk to the stage: it culls, fades and thins exactly as a
  mesh's chunk does, and past reading a clump the blades are the same blades. Each clump stands
  jittered within its cell, on the heightfield (`TerrainHeightAt`), facing its normal, its blades
  hashed from the tile and the clump, so a clump is the same clump every frame.
- **The window.** The view lists only the tiles of a square window centred on the camera's tile,
  wide enough that a blade within the look's `fadeEnd` lies inside it
  (`SceneView::RefreshGrass`); the stage finds which tile each slot is from the camera's position,
  so the window moves with the camera at no CPU cost and a tile off the field draws nothing. A
  tile's culling box is its square between the lowest and highest heights of the terrain nodes it
  overlaps, on the finest level whose nodes are no smaller than a tile
  ([Terrain](terrain.md) § The levels).
- **The rules.** How tall a clump grows is the product of three shares: the slope rule (full height
  up to `maxSlope`, nothing `slopeBlend` steeper), the height rule (nothing outside
  `[minHeight, maxHeight]`, full height `heightBlend` inside it), and the patches (a low-frequency
  value noise about `patchSize` across against `patchCoverage`, with a soft edge `patchEdge` of the
  noise wide either side of the threshold). A clump scaled
  to nothing is not drawn, and one near a rule's edge is shorter, so grass thins toward rock or
  snow rather than ending on a line. Nothing is painted: the rules are set to agree with the
  ground's surface by hand, since the engine cannot read a project surface's bands.

The window is what a layer pays for whether or not a blade survives in it: one amplification group
per tile, (2 * ceil(fadeEnd / tile) + 3)^2 of them, at most `c_MaxTerrainGrassWindowTiles` (255) a
side -- an attach whose spacing would pass it is refused, and a look whose fade is lengthened past it
afterwards ends short of its fade -- -- about 9,000 for the test project's meadow,
fading at 90 m, at a clump every 0.25 m. A layer's clumps carry no colour (white), so a field's
variation is the look's per-blade `variation`.

## A blade

A blade is a quadratic Bézier strip, built from its clump and a hash of its index
([lib/forward/grass.slang](../libs/bgl/shaders/src/lib/forward/grass.slang)): its root is a
uniform point on the clump's disc (`clumpRadius`), its height is between `minHeight` and `maxHeight`
times the clump's scale, and it faces a random way around the clump's ground normal. Its width
tapers from `rootWidth` to `rootWidth * tipWidth`.

**Everything that bends a blade goes through `PoseBlade`**, which returns the three control points:
the root, a middle point, and the tip. The tip leans out from above the root by the look's `lean`
plus whatever pushes it -- wind, and every later force -- but never past 95% of the height, and it
drops so the blade keeps its length. The middle point bows between a straight blade (`curvature` 0)
and a guide standing above the root that sinks as the tip leans out (`curvature` 1), and a length
correction then brings the curve back to the blade's height. Posed this way a blade arcs over rather
than folding: a middle point held at full height above the root, whatever the tip does, bends every
leaning blade through a corner near its tip.

A pose is computed in world space, from the root, up and facing the placement's transform gives the
blade, once at the frame's time and once at the previous frame's with the previous transform, so
the velocity describes both the placement's motion and the pose's. Because a pose never lengthens a
blade, a chunk's culling bound -- its clumps' sphere inflated by `BladeReach`, the blade's height
with slack for the length correction and half its widest width -- holds whatever bends it.

## Wind

The wind is the view's (`ISceneView::SetWind`), and how a blade answers it is the look's
(`GrassResponseDesc`). It pushes the tip along the wind's horizontal direction by a share of the
blade's height: the steady `strength`, plus a gust -- smooth value noise over the ground's xz,
`gustScale` across and scrolling downwind at `gustSpeed` -- up to `gustStrength`, scaled by the
look's `gustResponse`; the sum is softened by `1 - stiffness`. Each blade adds a small flutter of its
own, in phase with its hash, so neighbours do not sway as one. A calm view pushes nothing, however
the clock runs. Changing the wind is not a temporal-epoch event: the next frame's pose simply
differs from the last, and the velocity says so.

## Distance

Three things fall off with distance: how large every blade is, how many a look keeps, and how
finely each is built.

- **How large.** Every blade is whole up to `fadeStart` and shrinks, height and width together, to
  nothing at `fadeEnd`, linearly between (`FadeScale`, `ThinningAt`); the look's `widening` makes a
  fading blade narrow more slowly than it shortens. The whole field shrinks as one: unless the look
  thins by count (below), **no blade is dropped while it can be seen**. The only other blades not
  drawn are those whose height spans less than
  `cGrassMinBladeHeightPixels` (1) on the render grid (`BladeVisible`), which show nothing; a chunk
  whose largest blade, at its *nearest* point, is under that launches no mesh groups at all.

  What is left of a fading field is short grass settling into the ground, which the look's
  ground-normal blend (below) makes read as the ground itself.
- **How many.** A look with a `thinStart` keeps every blade of a clump up to that distance and a share
  `(thinStart / d)^2` of them past it, never fewer than one a clump (`ThinShare`), so the blades a
  pixel of ground holds stay about constant. Blade `k` of a clump keeps `bladesPerClump * share - k`
  of its size, clamped to [0, 1], so a dropped blade shrinks out over the distance the share falls by
  one blade, and the survivors widen by `1 / share` to cover the ground the clump did. Blades are
  numbered across a chunk's clumps (`BladeAddress`), so the blades kept at the chunk's nearest point
  are a prefix of its numbering, and the amplification stage launches only that prefix
  (`KeptBladesPerClump`): a thinned field costs the blades it keeps. A look whose `thinStart` is 0,
  the default, never thins by count.

  Thinning was off for every look before it was a look's choice. At a runner's speed every blade of
  a verge crosses its threshold as the camera moves, and with blades whose colour is not the
  ground's, that reads as patches of grass swapping in and out, shrinking or not. A look whose blades
  sit on a ground of nearly their colour, seen from where far grass is a few pixels tall, hides it,
  and saves most of the field: gpu-battle-sim's meadow, thinning from 25 m, takes its grass from
  2.56 ms to 0.95 ms from 1.8 m up in it (RTX 4060, release, 1080 lines shaded).
- **How finely.** Segments along a blade go from `nearSegments` at the camera to `farSegments` at
  `fadeEnd`, and no more than one per `cGrassPixelsPerSegment` (6) pixels of the blade's height on
  screen, chosen once per chunk at its nearest point.

A mesh group holds as many blades as fit its 64 vertices and 124 triangles at the chunk's segment
count: 16 at one segment, 8 at three, 4 at seven.

## Drawing

Grass is a forward phase, `Forward Grass <n>`, after Forward World and before Blob Shadows
([passes.md](passes.md#forward-grass)), and a geometry stage of its own: a look's material picks a
draw bucket on the `kGrass` stage, keyed by material kind as a static opaque mesh's is, whose kernel
pairs the grass stage with that kind's grass program (`programs.forward.Grass_<kind>`, generated for
each registered surface). So every material kind draws and a blade shades as a surface of that
material. Blades are solid, two-sided and depth-tested, with no alpha test: a blade is its own
geometry, not a card, and a mask or hashed material's alpha is never read.

Grass writes depth and velocity like the world. Still grass under a still camera writes no motion;
a placement that moves carries its blades' motion with it.

## Blob shadows

A blob shadow darkens a blade as it darkens the ground the blade grows from, root to tip alike, so a
disc reads as one patch from bare ground into a field. Blob Shadows lands a disc only on what faces
up, and a blade stands near vertical; nothing in the depth tells a blade from a wall, since the
renderer keeps no G-buffer. So grass writes a third target besides colour and velocity,
`grassRootHeight` (R16F): each blade pixel's height above its blade's root in world Y, floored at a
millimetre so the cleared zero means *no blade*. Blob Shadows reads it beside the depth, lowers a
blade pixel by it in world Y to the root's height, whatever the ground normal, and skips the facing
test there
([passes.md](passes.md#blob-shadows)). The fade and the cut above the caster are then measured from
the ground: a grounded caster cast from just above the roots (`BlobShadowDesc::casterLift`, as the
editor's floor slack sets it) shades the whole of a blade that rises past that point.

A foot's shadow lands on grass three times as wide and half again as strong as on bare ground (capped
at full), the disc unchanged. A foot's shadow is sized for a sole on bare ground, about as wide as
the foot, and in grass the blades around a planted foot hide nearly all of that, so a walking unit
lost its foot contact there. The disc already spans the blades its placement stands in. The factors
are the shader's (`cGrassFootWiden`, `cGrassFootStrength`), not `FootShadowDesc` fields: a game sizes
a foot once, and grass is where it lands.

Measuring the height from each blade pixel instead would cut every part of a blade above the cast
point, and fade the rest by up to a quarter against the default 2 m `fadeHeight`. Sampling
the disc list in the grass pixel shader would loop over every disc per blade fragment. A stencil bit
would say "grass" but not how high, and writing a per-pixel stencil value is not guaranteed on
D3D12 and Vulkan. The target is cleared every frame with the others, and written only by the grass
phase, which draws after everything that receives a disc. Every render target carries it, grass or
not: two bytes a render-grid pixel, 4 MB at 1920x1080 and 16.6 MB at 3840x2160, and a clear a
frame.

## Lighting

Grass has no lighting model of its own. A grass program evaluates the material's surface and lights it
through `ShadeSurface`, the entry every Forward program uses, so grass takes whatever the scene's
materials take, and a surface that restyles them restyles the grass with them. What grass adds is
geometry, which the mesh stage builds into the vertex it hands the pixel stage
(`lib/forward/grass_vertex.slang`), and one term:

- **The normal** is built, never taken from the face. Each blade turns its face toward the camera,
  judged once at its middle, so both sides of a blade shade alike and the program always shades the
  front. The normal then tilts toward the edge a vertex sits on by `normalRounding`, which makes the
  flat strip read as rounded, and blends toward its clump's ground normal by a share that runs from
  `groundNormalNear` at the camera to `groundNormalFar` at the look's `groundBy` -- the fade end
  where it names none (`GroundShareAt`). At 1 and 1 every blade shades as the ground under it: the
  usual stylized setup, and what hides a field's thin far blades. A `groundBy` nearer than the fade
  end lights a blade as its ground before it has shrunk away, so the field's edge is ground lit as
  ground, not small blades lit as blades.
- **Colour and occlusion.** The base colour is multiplied by the look's tints from root to tip, the
  clump's colour and the blade's variation, and the occlusion by `rootOcclusion` falling off to the
  tip. Occlusion scales the environment's light and not the sun's, as it does on every surface.
- **Translucency** (`GrassTranslucency`) is the sun through a blade seen against it: a wrapped
  diffuse term on the far side of the normal, in the look's colour and strength, added after
  `ShadeSurface`. It is the one lighting term PBR lacks.
- **The ground's colour**, on a terrain ([Ground colour](#ground-colour), below): as far as a blade
  takes it, its tinted colour gives way to the ground's albedo, and its root occlusion and
  translucency to nothing.

A surface on the lit contract (`ILitSurfaceSource`) owns all of its lighting, so a blade drawn with
one gets none of the above but the normal: the program calls its `Shade` and adds nothing. The rest
reaches it through the interpolants any surface reads -- `Uv().y` runs root to tip and `Uv1().x` is
the blade's own random -- so a game can write grass shading of its own.

Grass is environment, and a look refuses a toon character surface (`CreateGrass`, `UpdateGrass`):
the character model's programs read a placement's shading rig, which a blade has none of.

uv1 on a blade is never a second UV set, so `HasUv1()` is false there and a geometry occlusion map
is read as white, for the engine's kinds and a surface's alike. Its `y` carries the entry of the
blade's look, which the program reads the translucency from: an interpolant constant along the blade
costs nothing where a flat attribute for it cost the pass a third more on Apple silicon.

## Ground colour

A blade's colour is its look's, and a terrain's ground is whatever its surface paints there: a
tiled albedo, varied by noise, banded by slope. Where a field fades or thins, the eye sees blades
of one colour settling onto ground of another, and every blade that leaves shows. A look's
`groundColorNear` and `groundColorFar` -- shares at the camera and at `groundBy`, as the
normal's are -- say how far a terrain's blades take the albedo of the ground under their roots
instead (`GroundColorBlend`). At 1 a far blade is the ground's colour and shades as the ground does,
so a fading or thinning field settles into ground of its own colour. Both 0 by default.

The ground's albedo comes from the view's ground-colour texture, which the Ground Color pass
([passes.md](passes.md#ground-color)) draws every frame from straight above through the terrain's
own material: a square around the camera reaching the furthest fade end of the looks that take it,
`c_GroundColorTexels` a side, its corner snapped to whole texels. The mesh stage reads it once per
blade, at the root, and hands the colour and the share on as one interpolant (`groundColor` in
`GrassVSOut`); the pixel stage blends there, because a blade's tint multiplies the material's
colour there:

- the base colour moves from the tinted material's toward the ground's,
- the occlusion toward 1, and
- the translucency toward nothing,

so a blade taking all of it, with its normal blended to the ground's as well, lights exactly as the
ground beside it. `GroundColor_test` holds that to within 0.02 a channel. Under a lit surface
(`ILitSurfaceSource`) the texture holds no colour, and blades over it keep their own; a mesh's
fields have no ground to read and never take it.

Unreal's runtime virtual texture, used for grass the same way, is the standard this follows. The
texture is redrawn whole every frame; an incremental, toroidal update is the next step if the pass
ever costs.

## Cost

`[.grasscost]` (`libs/bgl/tests/src/GrassCost_test.cpp`, run by hand) draws an 82,176-clump
verge at 4K with 0.667 render scale and prints the `Forward Grass 0` row. The cost follows the blades
emitted, about half of it in the mesh stage and half in rasterizing them -- distant blades are
thinner than a pixel, the worst case for a rasterizer -- while the amplification stage is near free.

Fading by size rather than count draws every blade inside the fade, and that verge fades over
10-60 m: 2.5 ms against the 1.27 ms thinning by count cost it (both in the debug build, best of
three; single runs on the M-series machine vary by up to 2x with the GPU's clock). Most of it is the
far half of the fade, where blades are thinner than a pixel. A look pays for its fade distance: the
same verge fading over 5-20 m cost 0.83 ms before any thinning, and animal-run's verge, fading over
12-45 m, draws in 0.46 ms at 4K in a release build. Per-blade frustum rejection in the mesh stage was
tried and saved nothing on a verge the camera looks along, so it is not there.

Lighting took the same verge from 1.12 ms to about 1.25 ms. The tint interpolant is 0.04 ms of that
and the translucency term the rest, paid whether a look uses it or not: branching on its strength
saved nothing measurable. Carrying the translucency as a flat per-vertex attribute instead cost
0.42 ms, and as an interpolated one 0.18 ms, which is why the program reads it from the look.

The reference the test verge was set beside is animal-run's street, whose grass was baked into its
mesh. That street now grows GPU grass, and at 4K in a release build its Forward World and Forward
Grass together cost 2.33 ms against the 2.56 ms Forward World its baked grass cost; the difference
buys wind, a field that fades rather than popping, and no grass geometry in the file.

On a terrain the cost is the look's fade over the whole ground in view. A look with a 90 m fade on
the viewer's `hilly` field (`bgl_ai_viewer --terrain hilly --grass ...`, a clump every 0.25 m,
1280x720, debug build) costs 9.9 ms in `Forward Grass 0` with the camera 3 m over the
ground, where the 90 m fade fills the frame with about half a million blades, and 1.8 ms from 40 m
up, where nearly every blade in view is under a pixel and the window's tiles are most of the work.
A terrain's grass is paid for in the look's fade and the layer's spacing.

## Where it comes from

- **The blade and the field.** A blade as a tapered strip of solid triangles along a quadratic
  Bezier, fewer segments far away: Ghost of Tsushima's grass (Eric Wohllaib, "Procedural Grass in
  'Ghost of Tsushima'", GDC 2021). Tsushima also thins its field per blade with distance, widening
  the survivors; the engine did too, and fades the whole field by size instead (Distance, above). Tsushima places and culls blades in a compute pass writing an
  indirect draw; here the mesh stage builds them, with no blade buffer (ADR-1 of the plan).
- **The blade's three control points.** The root, a guide at the blade's height above it, and the
  tip, with forces acting on the tip: Jahrmann and Wimmer, "Responsive Real-Time Grass Rendering
  for General 3D Scenes", I3D 2017. `PoseBlade` holds the rest pose; wind is its first force.
- **Grass on a terrain.** Generated per tile on the GPU near the camera rather than stored, as
  Tsushima's grass and Unreal's Landscape Grass Type are; placing it by slope and height rules is
  every engine's first step before a painted map.
- **The interleaved addressing is the engine's own.** Numbering blades across a chunk's clumps
  (`BladeAddress`), so a mesh group's run of blades spreads over the chunk; Tsushima's compute pass
  compacts its blades into a buffer instead.
- **The lighting terms.** A normal rounded across the blade's width, blended toward the terrain's
  with distance, and a translucency term for the sun behind a blade: Tsushima again.
- **The ground's colour.** A camera-centred texture of the terrain's albedo, drawn from its own
  material and read under each blade's root: Unreal's runtime virtual texture as landscape grass
  uses it. Turning each
  blade's face toward the camera is the engine's own, so a blade needs no back-face flip.

## What it does not do

- Grass does not follow deforming ground: clumps are in their mesh's space, and only a static geom
  or a terrain takes grass.
- Blades are not in a shadow map, and do not collide.
- Clumps are not placed in the editor: a field is its mesh source's `POINTS`, authored where the
  mesh is, and a terrain's grass is placed by its layer's rules, with nothing painted.

## Kept open

Three things were left unbuilt on purpose, each with the seam it will arrive through, so none needs
the pass rewritten.

**Collision and trampling.** The standard is a few sphere or capsule displacers evaluated per blade
(Ghost of Tsushima; Unreal's world-position offset) and a camera-following trample texture a splat
pass writes and that relaxes over time. The seams:

- every force is a term into `PoseBlade`, evaluated at `time` and `prevTime`, and wind is the first;
- a force bends a blade about its root and never stretches it, which keeps a chunk's culling bound
  true whatever pushes it;
- a blade carries no state, since it is built from a hash, so anything persistent is a world-space
  texture, ping-ponged so `prevTime` has a previous state as TAA history does;
- a displacer attaches to a mesh instance, the way `SetBlobShadow` does, so its previous position
  comes from the transform history the renderer keeps and its velocity is free;
- a look's response is one group, `GrassResponseDesc`, which collision's parameters join.

The API itself is not designed: it faces gameplay and has no consumer yet.

**A painted density.** A terrain layer's rules decide everywhere alike; a road, a scorched field or a
trampled camp is a place, which a map painted over the field says and a rule does not. The seam is
`TerrainGrassGrowth`, the one function a clump's scale comes from: a density texture sampled there
multiplies the rules, and a layer that names none reads as one. Unreal's Landscape Grass Type reads
its layer weights the same way.

