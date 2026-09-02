# ui-runtime — implementation plan

## Context

Nothing in the tree draws 2D. The pass catalog ([docs/passes.md](../passes.md) `:203-525`) has no
UI, text or sprite pass; the only scissor is the viewport rect; `apps/` holds the editor alone and
`libs/gamelib` is four headers (`AssetManager`, `ClipInfo`, `Ray`, `Raycaster`). The roadmap's
in-game UI entry ([ROADMAP.md](../../ROADMAP.md) `:407-409`) names Noesis as an example and nothing
else. The closest thing to a runtime client is `examples/util`'s `DemoWindow` — an SDL3 window
with an event pump and a fly camera, and no UI.

The design was settled two days ago in an untracked spec written from the main clone
(`docs/specs/ui_runtime.md`, never committed). It priced the libraries, placed the pieces by layer,
and named its own trigger: *the first thing that needs a HUD*, which has not arrived. This feature
builds it anyway, thin, with an example as the client — and the roadmap is rewritten so the choice
is a record rather than an example.

## Decisions

- **ADR-1 — RmlUi.** MIT, vcpkg port `rmlui` at our baseline (6.2, FreeType by default). It emits
  geometry, which is bgl's "PODs in, draw commands out" shape; the source format is HTML/CSS, the
  largest corpus an agent authoring a document can draw on; and it carries flexbox, the cascade,
  transitions, a DOM with event bubbling and a data-binding MVC in the box. *Rejected: Noesis
  (commercial, a second authoring world), Ultralight and CEF (browser engines: licence or hundreds
  of MB, assertions behind a JS bridge), Slint (rasters to a pixel buffer, needs a Rust toolchain
  in a manifest build), Dear ImGui (no styling, no retained tree to assert against — the
  debug-overlay answer), an own tree over Yoga or Clay (defines its own styling, the thing being
  avoided).* **It is not AAA-proven, and that is accepted knowingly:** one primary maintainer,
  shipped in The Thing: Remastered, ROSE Online and the Recoil engine, in no AAA title. The
  HTML/CSS runtimes with that history are commercial — Coherent Gameface (Civilization VII, World
  of Tanks) and Ultralight (paid above $100k revenue) — and both put logic and assertions behind a
  JavaScript bridge, which is the testing shape ADR-1 chose against. The mitigation is the exit
  path in ADR-7, not a hope.
- **ADR-2 — build now, with an example as the only client.** The spec's trigger — a client that
  needs a HUD — has not arrived, so the seams here are guessed and a game may move them. The
  mitigation is scope: the runtime stays the eight required render methods plus `SetTransform`, no
  facade, no engine input type. *Rejected: deferring to the trigger, which the user weighed and
  declined — recorded so the next reader knows the cost was named.*
- **ADR-3 — the overlay is a general 2D draw declared in `bgl_intfc`, and `bgl` never sees an
  `Rml::` type.** `IGraphics::CreateOverlay()` mints an `IOverlay` that compiles 2D geometry and
  owns textures; `IGraphics::DrawOverlay` submits a list of draws inside a frame, rendered after
  PostProcess. RmlUi's `RenderInterface` is gamelib's, translating into it. *Rejected: implementing
  `Rml::RenderInterface` inside `bgl`, which links a UI library into the renderer and shapes its
  public API around one consumer's vocabulary.*
- **ADR-4 — geometry reaches the GPU as structured buffers read by a mesh shader, not as a
  vertex/index buffer draw.** This deviates from the standard: every RmlUi reference backend, and
  every engine's UI pass, binds a vertex buffer and calls `DrawIndexed`. bgl has no such verb — the
  RHI's only graphics pipeline is `IMeshletPipeline` ([docs/rhi.md](../rhi.md) `:9-12`), and the
  bar it is drawn at ([ROADMAP.md](../../ROADMAP.md) § Guiding Constraints) is *among APIs with bindless
  resource access and mesh shaders*. So a compiled geometry is two bindless structured buffers, and
  a draw is `DispatchMesh(⌈triangles / 64⌉, 1, 1)` with each group emitting its 64 triangles as 192
  unshared vertices — the same shape `OutlineMaskPass` already uses for a CPU list. *Rejected: a
  vertex-input stage on the RHI, because it is a second draw path in both backends for one
  consumer — every draw in the tree today is a dispatch, and the overlay is not the feature to
  change that.*
- **ADR-5 — the overlay owns its textures.** `IOverlay::CreateTexture(assetlib::ImageData)` and
  `ReleaseTexture`, uploaded by the overlay pass's own flush. *Rejected: `IScene::AddTextureAsset`,
  which the spec proposed — its upload rides `Scene::Update`, which runs only on a frame where that
  scene is drawn (`libs/bgl_extended/src/scene/Scene.cpp:442-463`), so a UI's atlas would be hostage to a
  scene it has nothing to do with.*
- **ADR-6 — a capture includes the overlay, and there is no flag.** PostProcess's "only writer of
  the backbuffer" ([docs/passes.md](../passes.md) `:517-518`) exists so that `SubmitCapture`
  describes what was displayed; the overlay is displayed, so it is in the capture. A scene golden
  submits no overlay and is unchanged. *Rejected: a second capture point or a flag — machinery for
  a distinction no test needs.*
- **ADR-7 — gamelib hands out `Rml::Context&`, and links RmlUi `PUBLIC`.** A game registers data
  models and event callbacks with RmlUi's API as documented; gamelib owns lifetime, the three
  interfaces and the render translation. gamelib's public headers forward-declare `Rml::Context`
  and include no RmlUi header; a client that uses the context includes `<RmlUi/Core.h>` itself,
  reached through the imported target, whose include directories CMake treats as system — so
  `enable_strict_compiler`'s `/Wall /WX` never compiles RmlUi's headers in the editor.
  **Replacement is designed for, without a facade.** Everything engine-side is library-neutral —
  `IOverlay`, the asset kinds and the mount, the input translation, the test machinery — and the
  only RmlUi-facing code in gamelib is the three interface translations. So `Rml::` reaches a
  client through exactly one gamelib header, and a game keeps its data models and event bindings
  in one module of its own. Replacing RmlUi — Gameface and Ultralight expose the same
  render-backend shape, Noesis too — rewrites those translations and that module, and nothing
  under `bgl`, `assetlib` or the documents' plumbing; what does not survive is the markup
  dialect of the documents themselves. `docs/ui_runtime.md` carries a *Replacing RmlUi* section
  saying what stays and what goes. *Rejected: a facade that re-exposes documents and data models,
  a second API that must track RmlUi's and forbids the corpus ADR-1 chose it for — the exit is
  kept cheap by counting the doors, not by hiding the library.*
- **ADR-8 — documents, styles and fonts are authored assets addressed by mount key.**
  `Rml::FileInterface` reads through the `AssetStore`'s `IFileSystem`, never `std::filesystem`;
  `SystemInterface::JoinPath` resolves `@import` and `src=""` into `normalizePath` form and refuses
  an escape — through assetlib's own check, `requireInsideDataRoot`, which is private today
  (`libs/assetlib/src/ref_paths.h:40-41`) and gains a public spelling beside `normalizePath` in
  `codecs.h` rather than a second body in gamelib. `.rml`, `.rcss` and `.ttf` become asset kinds
  the project knows, because `pack` drops every extension no kind claims
  (`libs/assetlib/src/pak_pack.cpp:218-223`) and `planDeletion` refuses one
  (`libs/assetlib/src/asset_refs.cpp:466-470`). *Rejected: host paths, whose failure is silent,
  packed-only and Windows-only ([docs/archives.md](../archives.md) § Paths are keys); and
  re-rolling the escape check in gamelib, two rules for one thing.*
- **ADR-13 — a foreign asset kind is a listed set, not the one exception.** The container table
  admits exactly one kind with no codec: `static_assert(containers + 1 == kCount)` with `kTexture`
  named in the message (`libs/assetlib/src/container_table.cpp:60-67`), and
  `assetTypeFromExtension` hard-codes `.ktx2` ahead of the table (`asset_refs.cpp:229-243`). Four
  such kinds want a second constexpr table — `ForeignKind { type, extension }` — holding `.ktx2`,
  `.rml`, `.rcss` and `.ttf`, with the assertion counting both tables and the extension lookup
  consulting both. A foreign kind is stored, packed, deleted and renamed by the project and never
  encoded by it. *Rejected: an identity `AssetCodec` per text file, which gives `Load<T>` a struct
  nothing wants and lets a `.rml` masquerade as a container the library serialises.*
- **ADR-14 — a live 3D render inside or beneath the UI is a headless target sampled as an overlay
  texture.** A menu background, then an animated scene, then buttons over it is the case the
  example shows, and the frame cannot express it as layers: PostProcess covers the backbuffer
  whole and writes alpha 1, so nothing drawn before the scene survives it. The standard is
  render-to-texture — Unreal's `SceneCapture2D` feeding a UMG `Image`, Unity's camera
  `targetTexture` in a `RawImage` — where the scene is drawn every tick into its own target and
  the UI, the one compositor, places this frame's output wherever the document says. So
  `IOverlay::CreateTexture(const RenderTargetRef&)` wraps a headless target's last-presented
  backbuffer, whose ring gains `kSRV` beside `kRenderTarget` like every other attachment already
  has. The overlay retains a strong ref to the target until `ReleaseTexture`, so the ring cannot
  be freed under a live handle ([docs/bgl_api.md](../bgl_api.md) § lifetime: the last `SharedRef`
  frees GPU state). The preview target is drawn first, then the window's frame samples it; each
  has its own TAA history and clock, and one queue in submission order is what makes that sound —
  not a fence, which is the thing to revisit if a pass ever moves to a second queue.

  The barrier is two declarations, not one, because the frame graph persists an imported
  resource's final state and restores nothing (`libs/bgl_extended/src/fg/FrameGraph.cpp:591-598`), while
  every consumer of a backbuffer assumes `kPresent` as its *before* layout — `BeginFrame`'s
  import at `RenderContext.cpp:415-418` and the capture at `:876`. So the borrowed backbuffer is
  imported under a name of its own with an explicit `kPresent` initial (the handle behind it
  changes as the ring advances, so a resumed state would be another handle's), the overlay pass
  declares the read, and `PreparePresent` declares it back to `kPresent` beside the frame's own
  backbuffer before `EndFrame` returns — the pass `:703` already uses, widened to every
  presentable the frame touched (task 2 built one pass rather than the second one first
  imagined here). A windowed target's swapchain image is refused, not because it cannot be sampled
  (both backends could allow it) but because sampling the surface a frame is presenting to is a
  self-reference nobody has asked for.

  gamelib's `LoadTexture` resolves a `target://<name>` source to a target the game registered, so
  a document names it as it names any image; `JoinPath` (ADR-8) passes a scheme-prefixed source
  through untouched rather than treating it as a mount key. The seam is overlay-scoped rather
  than an SRV on `IRenderTarget` because the overlay is its only consumer; when a second one
  arrives — the hero damage-mask target in [ROADMAP.md](../../ROADMAP.md) `:325` is the candidate
  — the general spelling is the refactor, and this overload becomes its first caller.
  *Rejected: an underlay slot before the scene passes, which needs the tonemap to blend and the
  skybox to be optional — fighting the frame for a menu; and `SubmitCapture` into a bytes
  texture, a GPU→CPU→GPU round trip per frame.*
- **ADR-9 — the UI clock is the runtime's own.** `SystemInterface::GetElapsedTime` reads a steady
  clock the client advances. The spec said it should be `RenderJob::time`; the survey disproved
  that — the field is a clip-local transport position that scrubs, rewinds and resets to zero
  (`apps/editor/src/Windows/AnimationEditor/AnimationEditorWindow.cpp:408-426`,
  `RenderTargetWindow.h:97-102`). *Rejected: `RenderJob::time`, for that reason.*
- **ADR-10 — input is RmlUi's vocabulary; gamelib declares no event type.** A client calls
  `Context::ProcessMouseMove` and friends itself; the SDL translation lives in `examples/util`
  beside the window that produces the events. *Rejected: an engine event type both SDL and Qt map
  onto — that is the roadmap's Input Engine designed by accident, with no game to shape it.*
- **ADR-11 — v1 is the eight required `RenderInterface` methods plus `SetTransform`.** Clip masks,
  layers, filters and shaders stay at RmlUi's no-op defaults, so `border-radius` does not clip,
  `opacity` groups flatten and gradients are absent. Any move past `SetTransform` is its own ADR,
  because that column is shaped like a 2D vector renderer and chasing it makes `IOverlay` an
  RmlUi-shaped API. *Rejected: layers in v1, whose `CompositeLayers` needs a destination read.*
- **ADR-12 — the shader blends premultiplied in linear, and un-premultiplies before it decodes.**
  The target is `SBGRA8_UNORM`, so blending happens in linear space with `ONE, INV_SRC_ALPHA` and
  depth off — the state `ForwardPass.cpp:202-214` already expresses. RmlUi hands over vertex
  colours premultiplied *in sRGB* (`ColourbPremultiplied`), and decoding that directly weights a
  half-covered glyph edge by `0.5^2.2 ≈ 0.22` instead of `0.5`. So the shader computes
  `decode(v.rgb / v.a) · v.a` (alpha zero guarded), which is exact up to 8-bit rounding at low
  alpha. Textures live in `_SRGB` views holding straight alpha and are premultiplied after
  sampling: `LoadTexture`'s decoded image is straight already, and `GenerateTexture`'s
  premultiplied bytes are divided out once on upload — exact for the white glyph atlas that is its
  only v1 producer. A document therefore blends in linear where a browser blends in sRGB; that
  residual is the one accepted. *Rejected: decoding the premultiplied value as-is, which task 5's
  golden would have blessed; and a UNORM view of the backbuffer, which the target's format does
  not offer.*

## Non-goals

- **The editor's UI.** Qt Widgets, settled (`apps/editor/CLAUDE.md` § UI). No document preview in
  the editor either — the second trigger, not this one.
- **A second renderer.** The overlay is declared in `bgl_intfc` because one is planned; nothing
  here builds, stubs or prices WebGPU.
- **Lua, or any script engine.** No `EventListenerInstancer` is installed; inline `onclick` is
  inert. `data-event-*` against a data model the binary registers is the whole interaction story.
  The door is real and stays open: the roadmap's scripting item ([ROADMAP.md](../../ROADMAP.md)
  `:414`, "e.g. Lua") decides the engine's language, and RmlUi's official Lua plugin
  (`rmlui[lua]`) then gives documents `<script>` blocks, the element and event API, and data
  models declared from Lua tables — UI-only logic in the asset tree, the Scaleform/ActionScript
  split, without a second language. JavaScript was considered and set aside: RmlUi has no
  official binding, so it would be either hand-written bindings or a browser-engine runtime
  (ADR-1), and either way a second VM beside the engine's. The roadmap rewrite in task 7 lists
  UI scripting as a `[ ]` child behind the scripting item.
- **Clip masks, layers, filters, shaders** (ADR-11).
- **Controller and focus navigation, IME, clipboard, cursors.** `SystemInterface`'s defaults.
- **An engine input layer** (ADR-10) and a game loop abstraction. The example's loop is the
  example's.
- **Hot reload.** `ElementDocument::ReloadStyleSheet` exists and is reachable through the context;
  nothing here wires a file watcher to it.
- **Consuming bernini from a game repo.** There is no `install()` anywhere in the tree.
- **The test project.** The example and the tests read the repo's `assets/` fixture tree; no UI
  document is authored into `test-project`.

## Acceptance

- `METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1 just run bgl_extended_tests -- "[overlay]"` green
  (`--gpu-validation` is D3D12's spelling and does nothing on Metal — `libs/bgl/CLAUDE.md`
  § bgl_extended_tests): a textured quad, a scissored quad and a transformed quad land where `MeanColor`
  expects them, and the capture contains them (ADR-6); a frame drawn to a headless target and
  sampled into a second target's overlay reads that frame's colour, and re-reads it after the
  preview target draws a different frame (ADR-14) — a solid overlay fill in `bgl_extended_tests`,
  since the mechanism is the same for any frame; the scene case is the example's (task 6).
- `just run assetlib_tests -- "[pack]"` green: a `.rml`, `.rcss` and `.ttf` under `Authored/`
  ship in the archive rather than in `skippedByExtension`.
- `just run gamelib_tests -- "[ui]"` green, headless: a document loads from a loose mount and from
  the same tree packed (`Archived()` pattern); a data-model write followed by `Update()` moves the
  bound element's computed box; a click dispatched on an element by id changes the model; **a
  click through the input door** — `ProcessMouseMove` to the button's computed box, then
  `ProcessMouseButtonDown(0)` / `ProcessMouseButtonUp(0)` — fires the bound `data-event-click`
  callback, `ProcessMouseButtonDown` returns `false` over the button (consumed) and `true` over
  empty space, which is the bool the example gates the fly-cam on; `JoinPath` resolves a sibling
  `@import` to mount-key form, refuses `../` out of the root, and passes `target://` through.
- `just run gamelib_tests -- "[ui][render]"` green: a document with a coloured box and a line of
  text matches `assets/golden/ui_hud.exp.png`, guarded so two blanks cannot pass.
- `just run bgl_ui` opens on a menu: a styled background, the sphere scene animating live inside a
  framed panel, buttons over it; a click on the button increments the bound counter and the
  fly-cam orbits the preview only while the cursor is over the panel — the landing PR's **Eyes**
  box.
- The landing PR's **Windows** box: `[overlay]` under GPU validation and `[ui][render]` on D3D12,
  and `assetlib_cli pack` of a directory holding UI assets on a path with `\` separators.
- `ROADMAP.md`'s In-game UI entry names RmlUi, what shipped, what is deferred and why, and links
  `docs/ui_runtime.md`.

## What the survey found

**Renderer.** `IGraphics` is `BeginFrame(target)` / `Draw(job)` / `EndFrame()`; there is no
`Present`, it is the tail of `EndFrame` (`libs/bgl_extended/src/gfx/RenderContext.cpp:750`). The target is
bound by `BeginFrame`; a job carries `view`, `camera`, `viewport`, `time`. The frame graph is
built across those three functions and never reorders: `EndFrame` adds TaaResolve, PostProcess
(`:701`) and PreparePresent (`:703`), then compiles and executes. **The overlay pass goes between
`:701` and `:703`**, or the graph transitions to present and back. Pass objects are `RenderContext`
members (`RenderContext.h:180-190`); a pass is `Init(IDevice*)` + `AttachToFrameGraph` building a
`PassDesc` whose `TextureArg`/`BufferArg` declarations derive every barrier
([docs/framegraph.md](../framegraph.md) `:16-21`). The backbuffer resource is `"backbuffer"`
(`constants.h:15`), `Format::SBGRA8_UNORM`, imported at `kPresent` (`RenderContext.cpp:413-418`),
never cleared because PostProcess covers it (`:435-436`).

`SubmitCapture` copies `GetBackbufferTexture(GetLastPresentedIndex())` on its own list outside any
frame (`RenderContext.cpp:847-848`), so a pass writing the backbuffer after PostProcess appears in
every capture with no other change. The readback swaps R/B for BGRA and PostProcess writes alpha
`1.0` (`PostProcess.slang:95-98`), so the overlay blends into an opaque destination.

**A target's output is not reachable through the public API.** `IRenderTarget` exposes size and
the TAA and outline toggles (`IRenderTarget.h:40-121`); the texture behind it,
`GetBackbufferTexture(frameIndex)`, is `RenderTargetBase.h:120`, internal, read only by the
capture. A headless ring's textures are created `TextureUsageFlag::kRenderTarget` alone
(`RenderTarget_d3d12.cpp:147`, `RenderTarget_metal.cpp:97`) while the scene colour, motion,
depth and outline attachments beside them all carry `kSRV` too (`:247-248`, `:193-194`). One
`IGraphics` drives many targets in separate frames — `bgl_two_windows` draws two — and only one
frame is active at a time (`IGraphics.h:119-126`). A frame with **no** `Draw` between
`BeginFrame` and `EndFrame` is legal and pinned: `Resize_test.cpp` does it seven times
(`:111-112` onward), each followed by a screenshot. What no test covers is that frame on a
**TAA-enabled** target, where `TaaResolvePass` runs with `cameraPairValid` false
(`RenderContext.cpp:692`) over matrices only `Draw` writes (`:518-520`); the example's UI-only
window target is created with TAA off, since there is nothing to accumulate, so it never enters
that case. The frame graph persists an imported resource's final state across frames and never
restores it (`FrameGraph.cpp:591-598`); `BeginFrame` imports the backbuffer with an explicit
`kPresent` initial (`RenderContext.cpp:415-418`) and the capture assumes the same (`:876`).

**No vertex buffers exist.** `DrawIndexed`, `SetVertexBuffer`, `SetIndexBuffer` match nothing under
`libs/bgl`; the draw verbs are `Dispatch`, `DispatchMesh`, `DispatchMeshIndirect`
(`libs/bgl_extended/src/cmd/CommandList.h:175-187`). Every screen-space pass synthesises its triangle in a
mesh shader (`PostProcess.slang:74-93`, `DispatchMesh(1,1,1)` at `PostProcessPass.cpp:125`).
Topology is declared on the mesh shader, not the PSO. The CPU-list-to-GPU path is
`UploadBuffer<T>` (`libs/bgl_extended/src/scene/UploadBuffer.h`) — `Assign` is a no-op on unchanged bytes,
`Update(cmdList)` grows and uploads, `GetBufferHandle()` must be re-read after growth — and
`OutlineMaskPass.cpp:141-160` is the precedent for reading one from a mesh shader.

The PSO is `MeshletPipelineDesc { amp, mesh, pixel, RenderState, rtvFormats, dsvFormat }`
(`MeshletPipeline.h:17-63`); premultiplied blending is already a `BlendState`
(`ForwardPass.cpp:202-214`). Scissor rides `MeshletState` (`MeshletState.h:10-16`) — one rect per
`SetMeshletState`, so one per draw — and `ViewportState::scissorRects` is a public `static_vector`
that can be pushed directly (`ViewportState.h:14-24`, `Rect.h:6-16`). A texture binds by writing
its bindless index into the cbuffer (`tonemap["sceneColor"].SetIfValid(...)`,
`PostProcessPass.cpp:103`); the whole cbuffer is re-uploaded on every dispatch into a per-frame
suballocation ([docs/uniforms.md](../uniforms.md) `:120-121`, `:199`), so one constant block per
draw is the normal path, not a new one.

Textures: `assetlib::ImageData` needs `width`, `height`, `vkFormat`, one subresource per mip
(`TextureAssetStore.cpp:29-67`); the resulting bindless index is device-wide, not scene-scoped
(`Graphics_d3d12.cpp:58-61`, `Graphics_metal.cpp:171-174`). Release is deferred behind in-flight
frames (`TextureAssetStore.cpp:104-113`) — the overlay's stores copy that.

Shaders: a `.slang` under `libs/bgl_extended/shaders/src/programs/<dir>/` is module `programs.<dir>.<File>`
with no CMake change ([docs/slang_shaders.md](../slang_shaders.md) `:8-21`); the D3D12
build-time validation does not run on macOS (`:129-132`), so a DXC-only error surfaces on Windows.

Goldens: `MatchesGolden` is MSE over RGBA with a `1e-4` default (`GoldenImage.cpp:64-74`), paths
are `assets/golden/<name>.exp.png` / `.got.png` relative to the exe, staged by `copy_assets`
(`CMakeLists.txt:40-58`). `MeanColor(path, x, y, w, h)` (`GoldenImage.h:46-47`) measures a region
without a golden — the right instrument for the overlay's quads. `SkinnedAcquire_test.cpp:407-517`
is the gamelib golden pattern, including the not-two-blanks guard (`:498-516`).

**Assets.** `AssetStore` takes a data root, or a data root plus an `IFileSystem` (`AssetStore.h:71`,
`:82-84`); `GetFiles()` (`:87-91`) is the only public door for bytes no codec claims — `Load<T>` and
`Save` are codec-gated. `gamelib::AssetManager::GetStore()` (`AssetManager.h:118-122`) reaches it.
Reads are unchecked by origin; writes of a foreign file address the host directly
(`libs/assetlib/CLAUDE.md` § A caller that genuinely addresses the host). The public normaliser is
`assetlib::normalizePath` (`codecs.h:50-51`); the rejection of `..`, a leading `/` and absolute
paths is `requireInsideDataRoot`, private to assetlib (`ref_paths.cpp:63-75`); `extensionOf`
reads an extension off a key without `std::filesystem` (`ref_paths.cpp:36-54`). Asset kinds are
`AssetType` (`asset_refs.h:9-24`, nine today) and `assetTypeFromExtension`;
`c_RequiredDirectories` (`project_layout.h:60-72`) has no UI directory.

`AssetManager_test.cpp:890-903`'s `Archived()` packs a fixture and remounts it read-only; the
layered case at `:953-975` proves loose-over-packed. `docs/archives.md:243-245` names these as the
gate against "loose works, packed does not".

**Examples.** `examples/CMakeLists.txt:2-13` names an example in `add_subdirectory` and, for the
four that have assets, again in the `VS_DEBUGGER_WORKING_DIRECTORY` loop — a new one goes in both. `bgl_sphere/CMakeLists.txt` is the template: glob, link
`core bgl gamelib assetlib CLI11::CLI11 example_util`, a DX12-only Agility block,
`enable_strict_compiler`, root PCH only, `add_dependencies(copy_assets)`. `example_util` links SDL3
`PUBLIC` so vcpkg deploys the DLL beside each example. `demo::PumpEvents()` drains `SDL_PollEvent`
itself and handles quit, close and non-repeat key-down; **mouse events fall to `default: break`**
and nothing is forwarded (`DemoWindow.cpp:89-121`). `bgl_sphere/src/main.cpp:113-124` is the loop:
pump, fly-cam, `DrawFrame`. **Both examples are broken today** against the `Authored/`/`Derived/`
split: they mount `assets/Data` and ask for `Environments/forest.benv` (`bgl_sphere:75,77`,
`bgl_base:119`), and `bgl_base` first asks for `Meshes/apples.bmesh` (`:27`, `:109`) where the
file is `Derived/Meshes/apples.bmesh`; each throws, shows a message box and exits 0.

**Fonts.** `*.ttf` and `*.otf` are already LFS patterns (`.gitattributes`); no font exists in the
tree. `enable_strict_compiler` adds no `SYSTEM` include handling, so a third-party header that
trips `/Wall /WX` needs `target_include_directories(SYSTEM)` on the consumer, as `bgl` does for
metal-cpp.

**RmlUi 6.2, from its headers.** `Rml::Initialise()` is process-global, and the system, file and
font interfaces are set once before it; the render interface may be per context
(`CreateContext(name, dims, render_interface)`). `RenderInterface`'s required set is
`CompileGeometry(Span<const Vertex>, Span<const int>)`, `RenderGeometry(handle, Vector2f, TextureHandle)`,
`ReleaseGeometry`, `LoadTexture(Vector2i& dims, const String& source)`,
`GenerateTexture(Span<const byte> rgba, Vector2i)`, `ReleaseTexture`, `EnableScissorRegion(bool)`,
`SetScissorRegion(Rectanglei)`; `SetTransform(const Matrix4f*)` is optional with a default.
`FileInterface` is `Open`/`Close`/`Read`/`Seek`/`Tell`, with `Length` and `LoadFile` defaulted over
them. `Context::ProcessMouseButtonDown` returns **true when the UI did not consume it**. RmlUi
guarantees no thread safety; `IGraphics` is thread-affine ([docs/bgl_api.md](../bgl_api.md)) —
the loop stays single-threaded.

## What changes

- **`libs/bgl`** — `bgl/IOverlay.h` (`OverlayVertex` mirroring `Rml::Vertex`'s
  position/colour/uv, `OverlayGeometryHandle`, `OverlayTextureHandle`, `OverlayDraw` = geometry +
  translation + texture + optional transform + optional scissor, `OverlayJob` = the overlay plus a
  span of draws, the `IOverlay` interface) and two methods on `IGraphics`: `CreateOverlay()` and
  `DrawOverlay(const OverlayJob&)`; then the `CreateTexture(const RenderTargetRef&)` overload of
  ADR-14. The `bgl_selfcheck` TU compiles the header alone. Could break: nothing links it
  yet; the risk is API shape, which the tests and gamelib's translation exercise.
- **`libs/bgl_extended`** — `src/overlay/Overlay.{h,cpp}` (geometry and texture stores over
  `ResourceManager`, deferred release), `src/passes/OverlayPass.{h,cpp}` (PSO with the forward
  pass's premultiplied blend, depth off, `SBGRA8_UNORM`; per-draw cbuffer; scissor on
  `MeshletState`; texture flush), `shaders/src/programs/overlay/Overlay.slang` (mesh shader: 64
  triangles per group, projection from pixel space to NDC, optional transform; pixel shader:
  sRGB decode, texture × colour). `RenderContext` gains the member and the `EndFrame` insertion.
  For ADR-14 the headless ring gains `kSRV`, the texture store learns a second backing — a
  target's last-presented backbuffer, resolved at draw time so the handle survives the ring
  advancing and a `Resize` — the pass imports it under its own name with a `kPresent` initial
  and declares the read, and a side-effect pass returns it to `kPresent`. Wrapping a windowed
  target throws. Could break: the "only writer of the backbuffer" statement, in `docs/passes.md:517-518` and
  again in the source comment at `RenderContext.cpp:435-437` — both restated per ADR-6; and any
  golden, if a frame with no overlay were to differ, which the pass avoids by attaching nothing
  when the frame's draw list is empty.
- **`libs/assetlib`** — the foreign-kind table of ADR-13 with three new `AssetType`s
  (`kUiDocument`, `kUiStyle`, `kFont`), `Authored/UI/` and `Authored/Fonts/` in
  `c_RequiredDirectories`, `pack` shipping them, delete and rename knowing them as reference-free
  leaves, and `requireInsideDataRoot` exported (ADR-8). Every exhaustive `switch` over `AssetType`
  gains three cases — `pak_pack.cpp:138` and `:255`, `asset_rename.cpp:197`, `migrate.cpp:136`,
  `reimport.cpp:147`, `cli/main.cpp:775` — which the compiler finds but which is the size of the
  task. Could break: `Project::Create` on an existing project missing the new directories — check
  how the required list is reconciled on open before adding to it.
- **`vcpkg.json`** — `rmlui` (default features: FreeType).
- **`libs/gamelib`** — `include/gamelib/ui/UiRuntime.h` (process-global RmlUi lifetime, the
  system and file interfaces, the clock; one per process, asserted), `UiContext` or equivalent
  (an `Rml::Context` bound to a size, handing out `Rml::Context&`), `UiRenderer` (the
  `RenderInterface` over `IOverlay`, recording `OverlayDraw`s during `Context::Render()` and
  submitting them). `LoadTexture` decodes through `assetlib`'s image reader from the mount, and
  resolves a `target://<name>` source to a render target the game registered on the runtime
  (ADR-14). A document load is a new gamelib load door and gets a Tracy zone like every `Acquire*`
  ([docs/profiling.md](../profiling.md) `:141`), the key and byte size in `ZoneTextF`.
  `libs/gamelib/CLAUDE.md` gains the UI section. Could break: the editor links gamelib and now
  links RmlUi and FreeType transitively — link cost only, since no public header includes RmlUi
  (ADR-7).
- **`examples/util`** — `PumpEvents` forwards every event to an optional sink; an `RmlInput.{h,cpp}`
  translates SDL mouse and key events into `Context::Process*` calls. **`examples/bgl_ui`** — the
  three-layer menu: the sphere scene drawn every tick to a headless preview target, and the
  window's frame drawing only a `menu.rml`/`menu.rcss` — a styled background, the preview in a
  framed panel via `target://preview`, a frame-time readout, a counter and a button bound through
  a data model. Fixes the stale keys in `bgl_sphere` (the environment) and `bgl_base` (the mesh and
  the environment).
- **`assets/`** — `Data/Authored/Fonts/Lato-Regular.ttf` with its OFL licence beside it,
  `Data/Authored/UI/menu.rml` and `menu.rcss`, `golden/ui_document.exp.png`.
- **Docs** — `docs/bgl_api.md` (the overlay), `docs/passes.md` (the pass, the restated invariant,
  and the stale member list at `:19`), `docs/asset_containers.md` and `docs/archives.md` (the new
  kinds; `archives.md:45-47` also names the private `normalizeRef` where `normalizePath` is the
  public spelling), `docs/ui_runtime.md` (new, by bcp-docs, with the RCSS traps a web author
  trips on: `dp`/`vp` units, `decorator: image()` not `background-image`, no `grid`, `border-radius`
  clips nothing in v1, `data-*` binding attributes), `CLAUDE.md`'s index, `ROADMAP.md`.

## The tasks in order

1. **`feat(bgl): a 2D overlay drawn after post-processing`** — `IOverlay`, `OverlayDraw`, the two
   `IGraphics` methods, the stores, the pass, the shader, the docs. Dead scaffolding by design:
   nothing outside `bgl_extended_tests` calls it until task 5. Gate: `[overlay][render]` cases in
   `bgl_extended_tests` — a white quad with a generated 2×2 texture reads its texel colours at the four
   corners through `MeanColor`; a scissored quad leaves the excluded region at the scene's colour;
   a translated and transformed quad lands where the matrix says; a frame with an empty draw list
   matches the frame before the pass existed (an existing golden, unchanged). Run with Metal's
   validation environment set, as in Acceptance.
2. **`feat(bgl): a headless target's output is an overlay texture`** — `kSRV` on the headless
   ring, the target-backed texture, the pass's import and barrier, the throw on a windowed
   target. Gate: a `[overlay][render]` case draws a solid-colour frame to a headless target, then
   a second headless target whose overlay samples it into a quad; `MeanColor` on the quad is the
   frame's colour; the preview target draws a second frame in another colour and the next sample
   follows it; a `ScreenshotPng` of the preview target after it was sampled still works, which is
   what proves the return-to-present pass on D3D12. Metal's barriers are no-ops
   (`libs/bgl_extended/src/metal/cmd/CommandList_metal.h`), so the barrier half of this task is
   verified only by the landing PR's Windows box, `[overlay]` under `--gpu-validation` — a
   mismatched `layoutBefore` is the silent class there.
3. **`feat(assetlib): UI documents, styles and fonts are authored assets`** — the foreign-kind
   table (ADR-13) with `.ktx2` moved into it and the three kinds beside it, the two directories,
   `pack`, delete/rename, the exported escape check (ADR-8). Gate: `[pack]` ships a `.rml`,
   `.rcss` and `.ttf` and `skippedByExtension` is empty; `[refs]` deletes one as a leaf and
   refuses nothing; every existing `assetlib_tests` case still green, since `.ktx2` changed
   tables.
4. **`feat(gamelib): the UI runtime reads its documents from the mount`** — `rmlui` in
   `vcpkg.json`, `UiRuntime` with the system interface (clock, log to spdlog, `JoinPath` in
   mount-key form) and the file interface over `IFileSystem`, the font asset and its licence
   (needed by any test that lays out text), the context wrapper handing out `Rml::Context&`, and
   a test utility that dumps a context's element tree — tag, id, classes, computed box — as text,
   so a case asserts or diffs a whole layout in one call and a failing golden in task 5 logs the
   tree beside the PNG. Tests use a no-op `RenderInterface` stub, so nothing here depends on
   task 1. Gate: the headless `[ui]` cases in Acceptance, loose and packed.
5. **`feat(gamelib): the UI renders through the overlay`** — `UiRenderer`, the `LoadTexture`
   decode path and its `target://` resolution, `Render(IGraphics&)`. Gate: `[ui][render]` golden
   of a document with a coloured box and a line of text, with the not-two-blanks guard; a
   `LoadTexture` of a `.ktx2` from the mount decodes and draws; a document whose `<img>` names a
   registered target shows that target's frame.
6. **`feat(examples): bgl_ui draws a menu, a live preview and buttons in three layers`** — the
   event sink and SDL→RmlUi translation in `examples/util`, the example, the documents, the three
   stale keys in the two existing examples. The window target is created with TAA off: its frame
   holds no `Draw`, which `Resize_test` already pins for a TAA-less target and nothing pins with
   TAA on. Gate: builds on both platforms (CI); an **Eyes** box — the background is styled, the
   sphere animates inside the panel, the frame-time readout ticks, the button's click increments
   the counter, and the fly-cam moves the preview only while the cursor is over the panel
   (`ProcessMouseButtonDown`'s return and the element under the cursor gate it).
7. **`docs: the UI runtime page, the roadmap, and the plan's retirement`** — `docs/ui_runtime.md`
   by bcp-docs, with its *Replacing RmlUi* section (ADR-7) and the RCSS traps, `libs/gamelib/CLAUDE.md`, the `CLAUDE.md` index entry, `ROADMAP.md`'s In-game UI
   entry rewritten in the tree's pattern (the mechanism, what it replaced, the link, the deferred
   children as `[ ]` with their reason), and this plan deleted. Gate: every claim in the new doc
   resolves to a file that exists.
