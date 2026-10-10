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
| its node bounds | `idl::TerrainNodeBounds` per node, level-major, a range in `scene.terrainNodeBoundsBuffer` | the lowest and highest world y of every node of every level, computed once at creation |

`IScene::CreateTerrain` copies the samples into the texture, computes the levels and the node
bounds ([scene/terrain_lod.h](../libs/bgl/src/scene/terrain_lod.h)), writes the record and
returns a handle; `DeleteTerrain` frees all of it. A terrain has no placement and no transform: a
heightfield is axis-aligned, and where it lies is its origin. It is the scene's, so every view
draws every live terrain; the view keeps one `TerrainBatch` per terrain, rebuilt when the scene's
terrain epoch moves, each with the bucket its material resolves to.

The generator fills the same struct: `terrain::Generate` turns a seed and a shape -- flat, hilly,
mountainous -- into a `Heightfield` from fractal gradient noise over a warped domain, ridged for
the mountains, each shape's features sized in world units so a finer cell resolves the same land
rather than smaller land, and scaled by the desc's `relief` before anything reads it, so every
slope is measured on the ground as it will stand. It is deterministic by seed and linear in the
samples. The offline
container a terrain is stored as later is this struct serialised; nothing stores one yet.

## Erosion

Noise is a plain with no history: its hollows drain nowhere and its slopes carry no mark of the
water that would have run down them. `TerrainGenerateDesc::erosion`
([terrainlib/ErosionDesc.h](../libs/terrainlib/include/terrainlib/ErosionDesc.h)) wears the
generated heights down, on the CPU, once, before they are quantised. The default erodes nothing.
Every length in it is in world units, so one desc erodes a battlefield and a mountain range alike.
It runs in rounds, `passes` of them, each three steps
([src/erode.cpp](../libs/terrainlib/src/erode.cpp)):

- **Breaching.** A priority flood from the field's edge (Barnes, Lehman and Mulla 2014) that,
  entering a hollow lower than the way it came, cuts that way down below the hollow's floor, as
  least-cost breaching does (Lindsay 2016). Each cut is a V with gentle banks, tapering into the
  ground around it, so a basin drains through a valley rather than a gorge; on ground steeper than
  the banks the slope is lowered rather than faceted. A hollow that would need a cut deeper than
  `breachDepth` keeps its water. It runs before every pass and once after the last, since what
  droplets lay down can dam a channel again. Without it, a hilly field's noise holds closed
  basins hundreds of metres across, and droplets pool there instead of joining into valleys.
- **Droplets.** Particle hydraulic erosion after SimpleHydrology (McDonald) and Lague's write-up of
  Olsen 2004. A droplet moves a cell a step down the gradient with some inertia, carrying sediment
  toward an equilibrium proportional to the height it drops; below it, it lays the difference
  down where it stands, above it it wears ground from a brush `erosionRadius` across, so it cuts a
  channel rather than a pit; it lays down everything it holds where it stops. Each pass also
  remembers where water ran: a later droplet carries more down a worn channel and is steered along
  it (SimpleHydrology's discharge and momentum maps), which is what joins gullies into valleys.
- **Thermal erosion.** A Jacobi relaxation that slides what stands steeper than the talus angle
  above a neighbour down to it, split by how far each neighbour is exceeded.

The droplets and the thermal step keep the field's volume to rounding; breaching takes ground away.
The result depends on the desc and the seed alone, never on how the work is scheduled: the droplets run in
tiles wider than two droplets' reach, in four phases of every other tile along each axis, each
tile's droplets in order from a key of the seed, the pass and the tile, so no two droplets
running at once touch one sample. The thermal step reads only the heights before it.

**Cost**, measured in a release build with 12 hardware threads on a 1001 x 1001 field at the
defaults (one droplet per sample over four passes, four thermal iterations a pass): **1.9 s** for
the battlefield's hilly shape at 2 m cells, **1.7 s** for the mountainous shape at 4 m; the noise
itself is 0.04 s. It is linear in the samples at a fixed droplet density (`Erosion_test`'s
`[perf]` case). Longer droplet paths (`maxSteps`) widen the tiles and starve the phases of
parallel work: 160 steps at three droplets a sample took 17 s.

## Fields

`terrain::DeriveFields` ([terrainlib/TerrainFields.h](../libs/terrainlib/include/terrainlib/TerrainFields.h))
reads any heightfield -- generated, eroded, or later painted or loaded -- and says what its ground
is like at each sample, as layers laid like it:

| field | what | how |
|---|---|---|
| `slope` | rise over run | central differences; past the edge a sample repeats the edge's |
| `curvature` | per metre: positive in a hollow, negative on a crest, 0 on any plane | the five-point Laplacian |
| `flow` | square metres draining through the sample, its own cell included | multiple flow directions (Quinn et al. 1991): each sample, highest first, passes what reaches it to its lower neighbours by slope |
| `wetness` | [0, 1] | `flow` on a log scale between fixed areas, 100 m² and 1 km², so it reads alike on any field |
| `lakeDepth` | metres of standing water, were each hollow filled to where it spills | a priority flood from the edge, where water leaves |

Deterministic, and linear in the samples but for one sort by height and one heap: 0.3 s for
1001 x 1001 in release. Lake depth and flow are what a water surface will stand on; nothing
draws one yet.

## Masks

`terrain::GenerateMasks` ([terrainlib/TerrainMasks.h](../libs/terrainlib/include/terrainlib/TerrainMasks.h))
turns a field and its fields into where each kind of thing stands, one hard layer per kind --
every value 0 or 1 -- so a channel painted by hand later replaces one kind without touching the
others:

- **Water** first, from the fields alone: a lake at least `minLakeDepth` deep, or a channel that
  `riverArea` drains through. Nothing else of the masks lies on it.
- **Woods** in hollows and on gentle wet ground, never on a crest, past `maxSlope` or on water.
  Each sample scores a low-frequency noise `patchSize` metres across plus its wetness and how deep
  in a hollow it lies, each by a bias; hollow and crest are the topographic position index, a
  sample's height against the mean within `positionRadius`, and wetness is averaged over the same
  radius, so a wood follows a valley rather than every gully up its sides. The highest scorers
  become woods; clearings smaller than `minClearing` are filled, woods smaller than `minArea`
  dropped, and the share taken is corrected over a few rounds so the woods as they stand cover
  `coverage` of the field. `forestDepth` is each wood sample's distance in metres to the nearest
  sample outside it, by an exact Euclidean distance transform (Felzenszwalb and Huttenlocher
  2012); the field's edge is not an edge of a wood.
- **Rock** on steep ground and ridges, never in a wood or on water, scored by noise, steepness and
  ridge, groups smaller than its `minArea` dropped.

A wood or an outcrop is a group of samples joined along either axis or diagonally. Where a painted
mask arrives it takes the noise's place and keeps the cleanup. Deterministic from the desc's seed;
0.16 s for 1001 x 1001 in release. Generation, erosion, fields and masks together are about 2.4 s
for the battlefield's field.

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
placement's shading rig off its vertices; `CreateTerrain` refuses that one. Ground is environment,
and environment is PBR: a terrain's surface is an `ISurfaceSource` that picks its albedo and
roughness from the reader's world position and normal
([Game-Defined Surfaces](game_defined_surfaces.md)); the test project's `BandedTerrain.slang` is
one.

## Reading the ground

[lib/terrain/heightfield.slang](../libs/bgl/shaders/src/lib/terrain/heightfield.slang) is the one
place a heightfield is sampled: `TerrainHeightAt` and `TerrainNormalAt` take the record, the
texture and the clamping sampler, and a world `xz`. The mesh stage reads its vertices through
them, and so does the grass a terrain grows ([Grass § On a terrain](grass.md#on-a-terrain)), which
also reads the node bounds to cull its tiles through the same node layout
([lib/terrain/nodes.slang](../libs/bgl/shaders/src/lib/terrain/nodes.slang)). Whatever stands on
the ground later -- a planted foot, a crowd's slope cost, the ground blood field -- reads the same
functions, so the ground a unit walks is the ground it sees. Nothing of that reads them yet;
`IScene::SetGround`'s plane is still what the pose pass plants on.

On the CPU, `terrain::HeightAt` ([terrainlib/height.h](../libs/terrainlib/include/terrainlib/height.h))
is the same read: bilinear between samples, clamped at the edge, the heightfield laid at the
origin `TerrainDesc` gives it, a sample of 0 at the origin's height plus the field's `minHeight`. It is how a game stands a camera or a unit on the ground it drew
without a GPU readback.
`terrain::LayerAt` ([terrainlib/TerrainLayer.h](../libs/terrainlib/include/terrainlib/TerrainLayer.h))
reads a `TerrainLayer` -- one float per sample, laid exactly as the heightfield is -- the same
way, and the fields of the ground and the masks over it (`TerrainFields`, `TerrainMasks`) are
layers, so a game asks what the ground is like where something stands by the position it stood
it at.

## Looking at one

`bgl_ai_viewer --terrain <flat|hilly|mountainous>` generates a field and draws it with a project
material ([AI Viewer](ai_viewer.md)). `TerrainRender_test` proves the pass at the pixel: a field
under the camera against the same frame without it, zero motion under a still camera, and a second
view of the scene seeing it.

## What is not here

No offline container, import or cook: a terrain is generated or built at load and never stored.
No material layers: a surface shades from where a pixel stands. No range past the view's far
plane, and no occlusion of anything by the terrain beyond the depth it writes. No consumer of the
heightfield on the GPU but the stage and the grass. ROADMAP.md § Terrain and § Level Editor name each of these.
