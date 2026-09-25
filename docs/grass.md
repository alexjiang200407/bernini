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
| the fields | `assetlib::BGrassFields` ([BGrassFields.h](../libs/assetlib_structs/include/assetlib_structs/BGrassFields.h)), cooked as a `.bgrassfields` beside the `.bmesh` | per field the mesh it grows on and a look slot; chunks of at most `c_GrassClumpsPerChunk` (64) clumps with a bound each; the clumps |

A clump is a point, a height scale, a ground normal and a colour. The cook sorts a field's clumps
along a Morton curve before cutting chunks (`assetlib/src/grass_chunks.cpp`), so a chunk is a
compact patch and its sphere is tight.

`IScene::AttachGrass` binds fields to a static geom and looks to its slots; every instance of the geom
then draws them, placed by its transform. `docs/bgl_api.md` has the call's rules.

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

Two things fall off with distance, both on the look's `fadeStart` and `fadeEnd`:

- **How many blades.** All of them up to `fadeStart`, none past `fadeEnd`, linearly between
  (`KeptShare`). Blades are addressed interleaved across a chunk's clumps -- blade `b` is clump
  `b % clumpCount`'s blade `b / clumpCount` -- so keeping the first K of a chunk thins every clump
  evenly. The amplification group launches enough mesh groups for the share kept at the chunk's
  *nearest* point, and each mesh group keeps a blade only if its index is under the share at the
  blade's *own* root. So a field thins smoothly across a chunk rather than in steps between chunks.
- **How finely.** Segments along a blade go from `nearSegments` at the camera to `farSegments` at
  `fadeEnd`, chosen once per chunk at its nearest point. As blades thin, the survivors widen by
  `widening` so the field keeps its cover.

A mesh group holds as many blades as fit its 64 vertices and 124 triangles at the chunk's segment
count: 16 at one segment, 8 at three, 4 at seven.

## Drawing

Grass is a forward phase, `Forward Grass <n>`, after Forward World and before Blob Shadows
([passes.md](passes.md#forward-grass)), and a geometry stage of its own: a look's material picks a
draw bucket on the `kGrass` stage, keyed by material kind as a static opaque mesh's is, whose kernel
pairs the grass stage with that material's pixel program. So every material kind draws and a blade
shades as a surface of that material. Blades are solid, two-sided and depth-tested, with no alpha test: a blade is its own
geometry, not a card.

Grass writes depth and velocity like the world. Still grass under a still camera writes no motion;
a placement that moves carries its blades' motion with it.

## Cost

`[.grasscost]` (`libs/bgl_extended/tests/src/GrassCost_test.cpp`, run by hand) draws an 82,176-clump
verge at 4K with 0.667 render scale and prints the `Forward Grass 0` row. The cost follows the blades
emitted, about half of it in the mesh stage and half in rasterizing them -- distant blades are
thinner than a pixel, the worst case for a rasterizer -- while the amplification stage is near free.
So the fade distance is the first lever: on that verge, fading over 10-60 m costs 2.2-2.4 ms,
5-30 m 1.08 ms and 5-20 m 0.83 ms.

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

## What it does not do

- **A blob shadow barely reaches grass.** Blob Shadows darkens a surface only where it faces up, and a
  blade stands near vertical, so under a caster on a verge the ground between blades darkens and the
  blades stay lit. Measured over the render test's dense field: about 2% darker where bare ground
  shows a clear disc. Nothing in the frame tells grass from a wall, since the renderer keeps no
  G-buffer.
- Grass does not follow deforming ground: clumps are in their mesh's space, and only a static geom
  takes grass.
- Blades are not in a shadow map, and do not collide.
