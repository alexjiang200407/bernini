# Grass

Grass is geometry the renderer builds rather than stores. A mesh source carries **clumps** -- points
on the ground, one per glTF `POINTS` vertex -- and a **look** says what grows there. Every frame the
mesh stage turns the clumps it can see into blades, as many and as finely as their distance earns,
and draws them through the look's material in the opaque phase. Nothing per blade exists on the CPU
or in memory: a field costs its clumps.

## The data

| | where | what |
|---|---|---|
| a look | `bgl::GrassDesc` ([GrassDesc.h](../libs/bgl/include/bgl/types/GrassDesc.h)), authored as a `.bgrass` | the material, the blade's shape, blades per clump, the fade, the response to wind, the lighting terms |
| the fields | `assetlib::BGrassFields` ([BGrassFields.h](../libs/assetlib_structs/include/assetlib_structs/BGrassFields.h)), cooked as a `.bgrassfields` beside the `.bmesh`, which names it (`BMesh::grass`) | per field the mesh it grows on and a look slot; chunks of at most `c_GrassClumpsPerChunk` (64) clumps with a bound each; the clumps |

A clump is a point, a height scale, a ground normal and a colour. The cook sorts a field's clumps
along a Morton curve before cutting chunks (`assetlib/src/grass_chunks.cpp`), so a chunk is a
compact patch and its sphere is tight.

`IScene::AttachGrass` binds fields to a static geom and looks to its slots; every instance of the geom
then draws them, placed by its transform. `docs/bgl_api.md` has the call's rules. A game never calls
it: `game::AssetManager::AcquireMesh` follows the mesh's `grass` reference, creates each look its
fields name from the `.bgrass` and attaches them, and the geom's release gives them back
(`libs/gamelib/CLAUDE.md`).

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

## A blade

A blade is a quadratic Bézier strip, built from its clump and a hash of its index
([lib/forward/grass.slang](../libs/bgl_extended/shaders/src/lib/forward/grass.slang)): its root is a
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

Two things fall off with distance: the look's fade, and what the screen can show.

- **How many blades.** All of them up to `fadeStart`, none past `fadeEnd`, linearly between
  (`KeptShare`) -- and on top of that, the engine keeps only as many as stay at least
  `cGrassMinBladePixels` (8) wide at the root on the render grid, widening the survivors by the
  inverse, up to four times, so the field holds its cover (`ThinningAt`). A blade narrower than that
  costs its rasterization and shows nothing fewer, wider ones would not; the rule follows the render
  grid, so the same look thins sooner at a lower resolution or a wider field of view. Blades are addressed interleaved across a chunk's clumps -- blade `b` is clump
  `b % clumpCount`'s blade `b / clumpCount` -- so keeping the first K of a chunk thins every clump
  evenly. The amplification group launches enough mesh groups for the share kept at the chunk's
  *nearest* point, and each mesh group keeps a blade only if its index is under the share at the
  blade's *own* root. So a field thins smoothly across a chunk rather than in steps between chunks.
- **How finely.** Segments along a blade go from `nearSegments` at the camera to `farSegments` at
  `fadeEnd`, and no more than one per `cGrassPixelsPerSegment` (6) pixels of the blade's height on
  screen, chosen once per chunk at its nearest point. As the fade thins blades, the survivors also
  widen by the look's `widening`.

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
  `groundNormalNear` at the camera to `groundNormalFar` at the fade end. At 1 and 1 every blade
  shades as the ground under it: the usual stylized setup, and what hides a field's thin far blades.
- **Colour and occlusion.** The base colour is multiplied by the look's tints from root to tip, the
  clump's colour and the blade's variation, and the occlusion by `rootOcclusion` falling off to the
  tip. Occlusion scales the environment's light and not the sun's, as it does on every surface.
- **Translucency** (`GrassTranslucency`) is the sun through a blade seen against it: a wrapped
  diffuse term on the far side of the normal, in the look's colour and strength, added after
  `ShadeSurface`. It is the one lighting term PBR lacks, and one function, so a toon path swaps its
  body and nothing else.

A surface on the lit contract (`ILitSurfaceSource`) owns all of its lighting, so a blade drawn with
one gets none of the above but the normal: the program calls its `Shade` and adds nothing. The rest
reaches it through the interpolants any surface reads -- `Uv().y` runs root to tip and `Uv1().x` is
the blade's own random -- so a game can ship its own grass shading before the engine has a toon path.

uv1 on a blade is never a second UV set, so `HasUv1()` is false there and a geometry occlusion map
is read as white, for the engine's kinds and a surface's alike. Its `y` carries the entry of the
blade's look, which the program reads the translucency from: an interpolant constant along the blade
costs nothing where a flat attribute for it cost the pass a third more on Apple silicon.

## Cost

`[.grasscost]` (`libs/bgl_extended/tests/src/GrassCost_test.cpp`, run by hand) draws an 82,176-clump
verge at 4K with 0.667 render scale and prints the `Forward Grass 0` row. The cost follows the blades
emitted, about half of it in the mesh stage and half in rasterizing them -- distant blades are
thinner than a pixel, the worst case for a rasterizer -- while the amplification stage is near free.

Screen-size thinning took that verge, fading over 10-60 m, from 2.0 ms to 1.13 ms (best of three
runs; single runs on the M-series machine vary by up to 2x with the GPU's clock). The threshold is
the trade: 6 px read 1.32 ms and is indistinguishable from no thinning, 12 px read 0.88 ms with far
grass visibly chunky. The rest is the look's own density near the camera: the same verge fading over
5-20 m cost 0.83 ms before thinning. Per-blade frustum rejection in the mesh stage was tried and
saved nothing on a verge the camera looks along, so it is not there.

Lighting took the same verge from 1.12 ms to about 1.25 ms. The tint interpolant is 0.04 ms of that
and the translucency term the rest, paid whether a look uses it or not: branching on its strength
saved nothing measurable. Carrying the translucency as a flat per-vertex attribute instead cost
0.42 ms, and as an interpolated one 0.18 ms, which is why the program reads it from the look.

At landing the test verge draws in 1.25 ms (best of three). The reference it was set beside is
animal-run's street, whose grass is baked into its mesh and costs Forward World 0.82 ms; the verge is
shaped after that street, not the same field. The difference buys wind, a field that thins and
coarsens rather than popping, and no grass geometry in the file.

## Where it comes from

- **The blade and the field.** A blade as a tapered strip of solid triangles along a quadratic
  Bezier, fewer segments far away, and a field that thins per blade with distance while the
  survivors widen to hold its cover: Ghost of Tsushima's grass (Eric Wohllaib, "Procedural Grass in
  'Ghost of Tsushima'", GDC 2021). Tsushima places and culls blades in a compute pass writing an
  indirect draw; here the mesh stage builds them, with no blade buffer (ADR-1 of the plan).
- **The blade's three control points.** The root, a guide at the blade's height above it, and the
  tip, with forces acting on the tip: Jahrmann and Wimmer, "Responsive Real-Time Grass Rendering
  for General 3D Scenes", I3D 2017. `PoseBlade` holds the rest pose; wind is its first force.
- **The interleaved addressing is the engine's own.** Numbering blades across a chunk's clumps
  (`BladeAddress`) so that keeping the first K thins every clump evenly is what lets a chunk launch
  mesh groups for only the blades it keeps. Neither source does this: Tsushima's compute pass
  compacts survivors instead.
- **The lighting terms.** A normal rounded across the blade's width, blended toward the terrain's
  with distance, and a translucency term for the sun behind a blade: Tsushima again. Turning each
  blade's face toward the camera is the engine's own, so a blade needs no back-face flip.

## What it does not do

- **A blob shadow barely reaches grass.** Blob Shadows darkens a surface only where it faces up, and a
  blade stands near vertical, so under a caster on a verge the ground between blades darkens and the
  blades stay lit. Measured over the render test's dense field: about 2% darker where bare ground
  shows a clear disc. Nothing in the frame tells grass from a wall, since the renderer keeps no
  G-buffer.
- Grass does not follow deforming ground: clumps are in their mesh's space, and only a static geom
  takes grass.
- Blades are not in a shadow map, and do not collide.
- Clumps are not placed in the editor: a field is its mesh source's `POINTS`, authored where the
  mesh is.

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

**Terrain grass.** When terrain lands, grass on it is placed from a density map over the heightfield,
generated per tile on the GPU as Tsushima and Unreal's Landscape Grass Type do, because a stored clump
list grows with area and a density map does not. It adds its own attach beside `AttachGrass`, which
stays the source for grass on meshes, and shares the look, the blade model, the forces and the
lighting. The seams: the chunk is the unit the pass tests, whatever made it; the grass stage reads a
clump through one function, which a density map can implement by sampling; and placement parameters
that mean something only to stored clumps (`bladesPerClump`, the clump radius) sit in their own group,
where a density in blades per square metre would join them. Baking terrain density into clumps at
cook was rejected: the file grows with area and re-cooks on every painted change.

**Toon lighting.** Covered in [Lighting](#lighting): grass has no model of its own, translucency is
one function, and the root-to-tip value and per-blade random ride the interpolants.
