# AI Viewer — what the renderer draws, as files an agent can read

`bgl_ai_viewer` ([apps/ai_viewer](../apps/ai_viewer/src/main.cpp)) renders one
imported model headlessly, writes a PNG at each frame you name, plays a clip when the model is
skinned, and reports what every frame graph pass cost on the GPU. **It is how an agent looks at a
rendering change**: run it, then read the PNGs it names.

It is the sibling of `bgl_pass_timings` ([Profiling](profiling.md) § Capturing a run headlessly),
which asks only what a model *costs*. Both stand on `headless`
([apps/headless](../apps/headless/headless)): the device, the framing and the pass summary. The
viewer is an app rather than an example, so a release build has it too — the build benchmarks are
taken on — while `bgl_pass_timings` needs `-DBERNINI_BUILD_EXAMPLES=ON` there.

## Running it

```bash
just build bgl_ai_viewer

# Every path absolute: `just run` puts the cwd at the binary's output directory.
just run bgl_ai_viewer -- --project "$PWD/test-project/Data" --env-root "$PWD/assets/Data" \
	--import "<a skinned character's .bimport>" --clip "<one of its clips>" \
	--frames 90 --screenshot 0,45,89 --out-dir "<an absolute directory of your own>"
```

Bare, it renders `assets/Data`'s apples — the one project `copy_assets` stages beside the binary.

| Flag | Default | Meaning |
|---|---|---|
| `--project` | `assets/Data` | the data root; every key below is relative to it |
| `--import` | `Authored/Meshes/apples.bimport` | the `.bimport` to render, or the `.glb` it describes |
| `--clip` | the first clip | the clip a skinned mesh plays, by name; refused on a static mesh |
| `--screenshot` | none | frames to write as PNGs, comma-separated, each below `--frames` |
| `--frames` | 60 | frames rendered, timed and numbered |
| `--fps` | 30 | frame `i` renders at clip time `i / fps` |
| `--warmup` | 8 | frames rendered first, held at time 0 |
| `--env`, `--env-root` | `forest.benv`, `--project` | the environment it is lit by, and the root that is keyed under |
| `--sun` | 0, off | an analytic sun's intensity, in the irradiance map's units — **additive** on `--env`, which already integrates whatever sun its source HDR held. The view's one sun, which the toon character model reads too |
| `--sun-azimuth`, `--sun-elevation` | 35, 38 | where that sun sits, in degrees: azimuth about the up axis from +Z toward +X, elevation above the horizon |
| `--sun-color` | `1 1 1` | its colour, as three floats |
| `--backdrop` | `sky` | what is drawn behind the scene: `sky`, the environment's, or `gradient`, the toon look-dev backdrop the editor's toon previews draw, pale horizon to sky blue (`ISceneView::SetBackdrop`). Only the background changes; `--env` still lights the scene |
| `-w`, `-h`, `--taa` | 1280, 720, on | the output, as a viewport renders it |
| `--render-scale` | 1 | the grid the geometry passes render on, relative to the output; below 1 the TAA resolve reconstructs the output ([Temporal Antialiasing](taa.md) § Render scale) |
| `--curve` | `agx` | the display curve the target ends in: `agx`, or `granTurismo`, the stylized one the editor shows toon content under ([Passes](passes.md) § Scene colour). A flag rather than the content's rule, so a toon model can be judged under either |
| `--bloom` | off | bloom at `bgl::BloomSettings`' defaults, added ahead of the curve; its `BloomDown*`/`BloomUp*` passes join the timings |
| `--film-grain` | off | film grain at `bgl::FilmGrainSettings`' defaults. The pattern follows the target's frame count, so frame N carries the same grain every run and two screenshots of one run differ by it |
| `--color-split` | off | the colour split at `bgl::ColorSplitSettings`' defaults: red two pixels left of green at 2160 lines and blue two right, scaled to `-h` |
| `--frame-clip` | off | frame the camera on the playing clip's poses rather than every clip's |
| `--out-dir` | `ai_viewer` | where the PNGs and `gpu_timings.csv` go |
| `--terrain` | none | a shape, `flat`, `hilly` or `mountainous`, to generate a battlefield of and draw, in place of `--import` ([Terrain](terrain.md)) |
| `--terrain-seed`, `--terrain-size`, `--terrain-cell` | 1, 2000, 2 | the generator's seed, the field's side in metres, and the metres between samples |
| `--terrain-material` | a plain green PBR | a `.bmaterial` in the project the field draws through, such as the test project's `Authored/Materials/Terrain/Battlefield.bmaterial` |
| `--terrain-eye` | 40 | how far above the field's middle the camera stands, in metres |
| `--water` | none | with `--terrain`: a water surface `.bmaterial` laid as a sea over the whole field, such as the test project's `Authored/Materials/Water/Lake.bmaterial` ([Water](water.md)); its pass is `Forward Water` in the timings |
| `--water-level` | 30% of the relief | with `--water`: the sea's height above the field's lowest point, in metres |
| `--grass` | none | a `.bgrass` to grow on a patch of bare ground — with `--import`, the model stands at its centre — or with `--terrain` on the field ([Grass § On a terrain](grass.md#on-a-terrain)) |
| `--grass-slope`, `--grass-below`, `--grass-coverage` | 90, unbounded, 1 | with `--terrain`: the steepest ground in degrees, the highest in metres and the share of the ground in patches `--grass` grows on |
| `--patch-size`, `--patch-spacing` | twice the look's fade end, 0.25 | the patch's side and the distance between its clumps, in metres; the spacing is a terrain's grass's too |
| `--distance` | 10 | how far from the patch's centre its camera stands, in metres, at eye height |
| `--wind` | 0, calm | the grass's wind, on a patch or a terrain: steady and gust strength both, in [0, 1] |
| `--blob` | 0, none | with `--grass` on a patch: a blob shadow of this radius in metres under the `--import` model, with a shadow under each foot when its rig has legs, or from a placement too small to see at the patch's centre; cast 0.2 m up, as the editor casts a grounded caster ([Grass § Blob shadows](grass.md#blob-shadows)) |
| `--crowd`, `--crowd-columns` | 0, 6 | copies of the model in rows of that many, receding from the camera; 0 places it once |
| `--source` | `per-instance`, or `auto` for a crowd | the pose source a skinned mesh is spawned on: `per-instance`, `table` or `auto` |
| `--pose-budget`, `--pose-pixels` | `LodSelectionDesc`'s | the view's choice for `auto`: units posed per instance at once, and the size on screen below which a one-level mesh draws from its table |
| `--lod` | by size | the level every placement draws whatever its size (`LodSelectionDesc::forceLevel`); a mesh with fewer draws its coarsest |

What it prints, in order: the mesh and whether it is skinned; for a skinned mesh the clip table with
`>` on the one playing — run once without `--clip` to learn the names; `lit` or `unlit`; one line
per screenshot, `frame  t = …  <absolute path>`; then median and max per pass, costliest first, and
the CSV's path. It exits non-zero only when it could not render.

## Looking at grass

A mesh that grows grass draws it under `--import` as any other load does
(`game::AssetManager::AcquireMesh`, [Grass](grass.md)). To look at a grass *look* on its own, name
the `.bgrass` with `--grass`: the viewer grows it on a square of bare ground
(`AssetManager::CreateGrassPatch` over `assetlib::makeGrassPatch`) and stands the camera at eye height, `--distance` from its centre. Near, mid and
far are three runs; `assets/Data` carries `meadow.bgrass` to try it on:

```bash
for d in 2 10 40; do
	just run bgl_ai_viewer -- --project "$PWD/assets/Data" --grass Authored/Grass/meadow.bgrass \
		--distance $d --sun 2 --frames 30 --screenshot 29 --out-dir "<a directory of your own>/d$d"
done
```

Add `--wind 0.5` to see it bend, and `--blob 1.5` to see a disc land on the blades as on the
ground between them. Name `--import` as well and the model stands at the patch's centre, seen from
the patch's camera rather than framed, and `--blob` is its shadow: one disc per model, however many
mesh parts share its rig, and foot shadows when the rig has legs. A walking clip in place shows a
foot's shadow fade as it lifts:

```bash
just run bgl_ai_viewer -- --project "<the test project>/Data" --env-root "$PWD/assets/Data" \
	--import Authored/Meshes/Coyote.bimport --clip Walk_InPlace --grass Authored/Grass/meadow.bgrass \
	--distance 2.5 --sun 2 --blob 1.5 --frames 30 --screenshot 10 --out-dir "<a directory of your own>"
``` The patch is twice the look's fade end across by default, so the
far run shows the field thinning to nothing; its `Forward Grass 0` row is what the look costs there.

With `--terrain` as well, the look grows on the generated field by the `--grass-*` rules instead of
on a patch ([Grass § On a terrain](grass.md#on-a-terrain)). The terrain's camera stands 40 m up,
where a look that fades by 90 m is under a pixel; `--terrain-eye 3` stands in it:

```bash
just run bgl_ai_viewer -- --project "<the test project>/Data" --terrain hilly \
	--terrain-material Authored/Materials/Terrain/Battlefield.bmaterial \
	--grass Authored/Grass/meadow.bgrass --grass-coverage 0.5 --terrain-eye 6 --sun 2 \
	--frames 30 --screenshot 29 --out-dir "<a directory of your own>"
```

## Decisions a reader relies on

- **The clock is the frame index.** Frame `i` is `RenderJob::time = i / fps`, never wall-clock time,
  so frame 45 is the same pose on every run and two runs' screenshots compare. The clip plays from
  phase 0 at rate 1 ([Skinned Meshes](skinning.md)).
- **Warm-up frames are held at time 0, and count toward nothing.** They are neither numbered nor
  timed: `--screenshot 0` is the first frame after them, drawn over a settled TAA history, and the
  timings exclude pipeline creation.
- **A screenshot is not paired with its frame's GPU time.** `PassTimings::frame` is opaque and a row
  trails the frame that wrote it, so no row can be named as frame `i`'s. `gpu_timings.csv` is the
  per-frame record — frames down, passes across, the editor's export format — and when rows trail
  past the last frame, up to 16 more frames held at its time are drawn to collect them.
- **The input is the authored `.bimport`, and its `outputs` say the rest.** The `.bmesh` it names is
  what is drawn, and a `.banim` among them makes the run skinned — there is no flag for it. Those
  containers are cache, which a project does not commit, so a fresh checkout has none. A missing one
  is a refusal naming it and the command that writes it back:
  `assetlib_cli migrate --project "<the .bproj>" --yes`. The viewer never writes into a project.
  `just run` splits its arguments on spaces, and the test project's `.bproj` has one, so run that
  binary by the path `just exes --target assetlib_cli` prints.
- **A character's toon shading rig comes with it.** A `.bimport` naming a `.btoonrig` draws its
  face with that rig, as any load through `game::AssetManager` does
  ([Toon Shading Rig](toon_shading_rig.md) § Loading); `--sun` with `--sun-azimuth` and
  `--sun-elevation` is how to look at it under a side, a front and an overhead sun.
- **Know whether it was lit.** The test project has no environment of its own; without
  `--env-root "$PWD/assets/Data"` it renders unlit, which is a black image, and says `unlit`.
- **A toon character needs `--sun` for its bands.** It reads the view's one sun and the
  environment's irradiance from straight up; without `--sun` it is lit by that ambient alone, its
  bands falling from an overhead sun of no intensity.
- **The sun is off unless asked for, and it does not replace the environment.** `bgl`'s own default
  intensity is 0, so every render this tool made before there was a sun is the render it still
  makes. Switched on with `--sun`, it *adds* to `--env`, whose cubes already integrate whatever sun
  the source HDR held — so a model lit by both is lit by two suns, and which to turn down is yours
  to decide ([Environment Maps](envmaps.md)). It casts no shadow.
- **A sun near the camera flattens the model; one across from it rakes the form.** The fixed view
  below sits at roughly azimuth 22, elevation 18, so those angles light the model straight down the
  lens and show the least shape. The defaults sit off that on purpose, and a sun a quarter turn
  away is what makes a silhouette read. Far enough round and the model is backlit, where the sun
  reaches nothing the camera can see — an image indistinguishable from `--sun 0`.
- **The camera is not a choice.** The model is framed on its bounding sphere from one fixed
  three-quarter view — a skinned mesh on the box its clip set's poses fill, so the character stays
  in frame through every clip. A clip set with root motion walks that box far past any one pose,
  and the character is a speck in the middle of it; `--frame-clip` frames the box of the playing
  clip alone, measured the same way over a set holding only that clip. The box the geometry culls
  by is the whole set's either way.
- **A project's own surfaces are registered.** Materials may shade through a surface the project
  authors under `Authored/Shaders` ([Game-defined surfaces](game_defined_surfaces.md)), and the
  renderer registers surfaces only when it is created, so the viewer hands it `--project`'s
  directory whenever there is one. Without it such a material is refused at load, naming the
  surface.

## Looking at a crowd

`--crowd N` lays the model out N times in rows of `--crowd-columns`, receding along -Z and spaced by
the playing clip's pose — not the whole clip set's, whose travelling clips would space units by how
far they walk. Each unit plays the clip a golden-ratio fraction of a cycle after the one before, so
neighbours never step together. The camera stands at the near end looking down the rows: the front
row fills much of the frame and the back one a few pixels, which is the range `PoseSource::kAuto`
chooses across ([Skinned Meshes](skinning.md)). A crowd spawns on `auto` unless `--source` says
otherwise, so one comparison is three runs:

```bash
for s in auto per-instance table; do
	just run bgl_ai_viewer -- --project "<project>/Data" --import Authored/Meshes/Rabbit.bimport \
		--clip Walk_In_Place --crowd 60 --source $s --frames 150 --warmup 30 --out-dir "<dir>/$s"
done
```

`Pose Skinned 0` is what posing costs on each source, `Choose Poses 0` what choosing costs, and
`Forward Skinned 0` what drawing does; interleave the runs and read several, since a debug Metal
clock is still ramping on the first. A mesh with levels swaps at level 0 whatever `--pose-pixels`
says; `--pose-budget 0` keeps every `auto` unit on its table.

## What it does not do

No window and no input; one mesh, or one grass look with or without one mesh in it, or one crowd of one mesh, and one clip per run — no blend spaces, no crossfades; no
golden-image comparison. A *wrong* frame is diagnosed with [Graphics Debug](gfx_debug.md); this
only shows you the frame.
