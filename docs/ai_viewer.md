# AI Viewer — what the renderer draws, as files an agent can read

`bgl_ai_viewer` ([examples/bgl_ai_viewer](../examples/bgl_ai_viewer/src/main.cpp)) renders one
imported model headlessly, writes a PNG at each frame you name, plays a clip when the model is
skinned, and reports what every frame graph pass cost on the GPU. **It is how an agent looks at a
rendering change**: run it, then read the PNGs it names.

It is the sibling of `bgl_pass_timings` ([Profiling](profiling.md) § Capturing a run headlessly),
which asks only what a model *costs*. Both stand on `example_headless`
([examples/headless](../examples/headless/headless)): the device, the framing and the pass summary.

## Running it

```bash
just build bgl_ai_viewer

# Every path absolute: `just run` puts the cwd at the binary's output directory.
just run bgl_ai_viewer -- --project "$PWD/test-project/Data" --env-root "$PWD/assets/Data" \
	--import Authored/Meshes/cha800_00.reduced.bimport --clip cha8_general_0120_to_general \
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
| `--sun` | 0, off | an analytic sun's intensity, in the irradiance map's units — **additive** on `--env`, which already integrates whatever sun its source HDR held |
| `--sun-azimuth`, `--sun-elevation` | 35, 38 | where that sun sits, in degrees: azimuth about the up axis from +Z toward +X, elevation above the horizon |
| `--sun-color` | `1 1 1` | its colour, as three floats |
| `-w`, `-h`, `--taa` | 1280, 720, on | the output, as a viewport renders it |
| `--render-scale` | 1 | the grid the geometry passes render on, relative to the output; below 1 the TAA resolve reconstructs the output ([Temporal Antialiasing](taa.md) § Render scale) |
| `--bloom` | off | bloom at `bgl::BloomSettings`' defaults; its `BloomDown*`/`BloomUp*` passes join the timings |
| `--frame-clip` | off | frame the camera on the playing clip's poses rather than every clip's |
| `--out-dir` | `ai_viewer` | where the PNGs and `gpu_timings.csv` go |

What it prints, in order: the mesh and whether it is skinned; for a skinned mesh the clip table with
`>` on the one playing — run once without `--clip` to learn the names; `lit` or `unlit`; one line
per screenshot, `frame  t = …  <absolute path>`; then median and max per pass, costliest first, and
the CSV's path. It exits non-zero only when it could not render.

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
- **Know whether it was lit.** The test project has no environment of its own; without
  `--env-root "$PWD/assets/Data"` it renders unlit, which is a black image, and says `unlit`.
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

## What it does not do

No window and no input; one mesh and one clip per run — no blend spaces, no crossfades; no
golden-image comparison. A *wrong* frame is diagnosed with [Graphics Debug](gfx_debug.md); this
only shows you the frame.
