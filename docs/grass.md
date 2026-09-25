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
times the clump's scale, and it faces a random way around the clump's ground normal. `curvature` and
`lean` bend it forward about its root. Its width tapers from `rootWidth` to `rootWidth * tipWidth`.

**Everything that bends a blade goes through `PoseBlade`**, which returns the three control points.
Any force is a term there, evaluated at the frame's time and at the previous frame's so the velocity
describes it. A force may bend a blade about its root and may not stretch it, so a chunk's culling
bound -- its clumps' sphere inflated by `BladeReach`, the Bézier hull of the tallest blade -- holds
whatever bends it.

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

## What it does not do

- **A blob shadow barely reaches grass.** Blob Shadows darkens a surface only where it faces up, and a
  blade stands near vertical, so under a caster on a verge the ground between blades darkens and the
  blades stay lit. Measured over the render test's dense field: about 2% darker where bare ground
  shows a clear disc. Nothing in the frame tells grass from a wall, since the renderer keeps no
  G-buffer.
- Grass does not follow deforming ground: clumps are in their mesh's space, and only a static geom
  takes grass.
- Blades are not in a shadow map, and do not collide.
