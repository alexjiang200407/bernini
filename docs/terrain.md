# Terrain

A terrain is one heightfield laid on the ground, drawn by every view of its scene through one
opaque material at the level of detail its cells' size on screen earns. Nothing per vertex is
stored: the mesh stage builds each patch from the height texture every frame, as the grass builds
its blades from clumps ([Grass](grass.md)). This page is the map: what a terrain is made of, how
it is cut into levels and why the cuts never crack, how it is shaded, and how anything else reads
the ground. The API is in [bgl API](bgl_api.md) § IScene; the generator that makes a battlefield
is `terrainlib` ([terrainlib/Generate.h](../libs/terrainlib/include/terrainlib/Generate.h)).

## The data

| | where | what |
|---|---|---|
| the samples | `assetlib::Heightfield` ([Heightfield.h](../libs/assetlib_structs/include/assetlib_structs/Heightfield.h)) | `samplesX` by `samplesZ` 16-bit shares of `heightRange` above `minHeight`, `cellSize` apart, row-major |
| a terrain | `bgl::TerrainDesc` ([TerrainDesc.h](../libs/bgl/include/bgl/types/TerrainDesc.h)) | the heightfield, the world origin of sample (0, 0), the material, and `pixelsPerCell` |
| its record | `idl::Terrain` ([Terrain.slang](../libs/bgl/shaders/src/idl/Terrain.slang)) | the origin and cell size, the height range and the pixels per cell, the sample counts, the levels, the material offset |
| its texture | one `R16_UNORM` through the scene's texture store | sample (x, z) at the centre of texel (x, z), read with the clamping sampler |
| its node bounds | `idl::TerrainNodeBounds` per node, level-major, a range in `scene.terrainNodeBoundsBuffer` | the lowest and highest height of every node of every level, computed once at creation |

`IScene::CreateTerrain` copies the samples into the texture, computes the levels and the node
bounds ([scene/terrain_lod.h](../libs/bgl/src/scene/terrain_lod.h)), writes the record and
returns a handle; `DeleteTerrain` frees all of it. A terrain has no placement and no transform: a
heightfield is axis-aligned, and where it lies is its origin. It is the scene's, so every view
draws every live terrain; the view keeps one `TerrainBatch` per terrain, rebuilt when the scene's
terrain epoch moves, each with the bucket its material resolves to.

The generator fills the same struct: `terrain::Generate` turns a seed and a shape -- flat, hilly,
mountainous -- into a `Heightfield` from fractal gradient noise over a warped domain, ridged for
the mountains, each shape's features sized in world units so a finer cell resolves the same land
rather than smaller land. It is deterministic by seed and linear in the samples. The offline
container a terrain is stored as later is this struct serialised; nothing stores one yet.

## The levels

A terrain is a quadtree of **patches** of `cTerrainPatchQuads` (7) cells a side: level 0 at the
samples' own resolution, each level above doubling the cell, until one node spans the longer axis.
Every node of every level is one amplification thread of the terrain phase, and each decides alone
whether it draws, from two distances to the camera -- its own box's nearest point and its
parent's:

> A node at level `l` draws when it is no nearer than `range(l-1)` and its parent is nearer than
> `range(l)` (or it is the coarsest level).

`range(l)` is the distance at which a cell of level `l` spans the terrain's `pixelsPerCell` on
screen, from the same `pixelsPerUnit` a mesh's level of detail is chosen by and scaled by the
view's `LodSelectionDesc::pixelScale`. The chain above every finest cell then draws exactly once,
with no holes and no overlaps, and no traversal and no state across frames: the CPU twin in
`scene/terrain_lod.h` is what `TerrainLod_test` holds the rule to, over a field from six cameras.
This is CDLOD (Strugar 2009) with the selection made per node rather than recursively.

**Cracks** between a patch and a coarser neighbour are closed the CDLOD way, by morphing rather
than stitching. A vertex at an odd index of its level is not on the coarser grid; as its distance
nears `range(l)` it slides onto the even vertex before it, so at the range the patch's edge is the
coarser neighbour's, vertex for vertex, and the height it reads at that vertex is the neighbour's
own sample. The morph runs over the last quarter of the range. For that to be enough, a node that
borders a finer one -- which sits near the finer level's range, half its own -- must be outside
its morph zone, so `range(l)` is floored at four times a node's diagonal; the same floor keeps any
two neighbours within a level of each other. A node's cull box is widened by one of its cells on
the near sides, where an odd vertex can slide outside it.

The patch is 64 vertices and 98 triangles, which is one mesh group's output at the static tier's
limits; a patch hanging past the field's edge folds its vertices onto the edge, and the folded
triangles are degenerate. A vertex on the field's last row or column is never morphed along that
axis: the field's outline is then the same from every level, where a morph would have pulled an
edge vertex inward on a field whose width is not a multiple of the coarsest node.

## The pass

**Forward Terrain** ([passes/TerrainForwardPhase.cpp](../libs/bgl/src/passes/TerrainForwardPhase.cpp))
is the first forward phase: the ground is the largest occluder and the cheapest fragment, so it
goes down before the world. Per terrain it dispatches one amplification group per node, as the
grass dispatches one per chunk, through the bucket of `GeometryStage::kTerrain` and the material's
kind; the amplification stage ([programs/forward/Terrain.slang](../libs/bgl/shaders/src/programs/forward/Terrain.slang))
frustum-culls the node by its box and applies the level rule, and launches one mesh group when it
draws. The mesh group builds the patch from the texture and emits an ordinary
`ForwardVSOut` -- the position reprojected through both cameras as a still surface, the normal
from central differences of the heights, a UV over the field, the material's offset -- so **the
pixel program is the material's own opaque program**, exactly the one a static mesh with that
material draws through. No program is generated per surface for terrain, and cel or PBR is
whatever the material is. The bucket culls back faces in hardware and has no dissolve lane.

A terrain takes an opaque material of any kind but a toon character surface, whose programs read a
placement's shading rig off its vertices; `CreateTerrain` refuses that one. A toon ground is a
surface on `IToonEnvironmentSurfaceSource` that picks its bands from the reader's world position
and normal, drawn by the toon environment model, which paints the base colour flat until the
environment's look is designed ([Game-Defined Surfaces](game_defined_surfaces.md)); the test
project's `ToonTerrain.slang` is one, and it takes that look the day the model has it, with no
change here.

## Reading the ground

[lib/terrain/heightfield.slang](../libs/bgl/shaders/src/lib/terrain/heightfield.slang) is the one
place a heightfield is sampled: `TerrainHeightAt` and `TerrainNormalAt` take the record, the
texture and the clamping sampler, and a world `xz`. The mesh stage reads its vertices through
them, and whatever stands on the ground later -- a planted foot, a crowd's slope cost, the ground
blood field -- reads the same functions, so the ground a unit walks is the ground it sees. Nothing
reads them yet but the stage; `IScene::SetGround`'s plane is still what the pose pass plants on.

## Looking at one

`bgl_ai_viewer --terrain <flat|hilly|mountainous>` generates a field and draws it with a project
material ([AI Viewer](ai_viewer.md)). `TerrainRender_test` proves the pass at the pixel: a field
under the camera against the same frame without it, zero motion under a still camera, and a second
view of the scene seeing it.

## What is not here

No offline container, import or cook: a terrain is generated or built at load and never stored.
No material layers: a surface shades from where a pixel stands. No range past the view's far
plane, and no occlusion of anything by the terrain beyond the depth it writes. No consumer of the
heightfield but the stage. ROADMAP.md § Terrain and § Level Editor name each of these.
