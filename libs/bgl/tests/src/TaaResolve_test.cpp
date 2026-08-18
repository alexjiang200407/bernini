#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VatSynth.h"
#include "util/jitter.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>

namespace
{
	constexpr uint32_t c_Width  = 256;
	constexpr uint32_t c_Height = 256;

	// Enough frames for the exponential blend to have actually settled, which is several times its
	// 1/`c_BlendWeight` time constant and not merely a couple of passes over the eight-term jitter
	// sequence. Sized for the weight the resolve ships with: too few and "converged" means "wherever
	// the accumulation had got to", which lands somewhere different for each jitter phase and makes
	// any comparison between two converged images a comparison of where they were interrupted.
	constexpr int c_ConvergeFrames = 100;

	// A quad rotated off-axis, so its edges cross pixels diagonally. An axis-aligned edge lands on a
	// pixel boundary and is already free of the stair-stepping TAA is meant to remove, which would
	// leave the antialiasing assertion below measuring nothing.
	constexpr float c_QuadYaw   = 20.0f;
	constexpr float c_QuadScale = 6.0f;
	constexpr float c_CameraZ   = 20.0f;

	bgl::Camera
	Camera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, c_CameraZ),
				glm::vec3(0.0f, 0.0f, c_CameraZ - 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);
		return camera;
	}

	// The tilted quad, sized so its edges cross pixels diagonally at the camera above.
	void
	AddQuad(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		auto plane = scene->AddPlaneGeom(1, 1, c_QuadScale * 2.0f, c_QuadScale * 2.0f);
		view->CreateStaticMeshInstance(
			plane,
			glm::rotate(glm::mat4(1.0f), glm::radians(c_QuadYaw), glm::vec3(0.0f, 0.0f, 1.0f)));
	}

	// Abutting slats in two mid greys: fine detail at moderate contrast, which is what actual scene
	// content is and what the quad cannot stand in for. Blur has to be measured against content the
	// neighbourhood clamp does not repair -- between black and white the box degenerates to the slat's
	// own colour and snaps any history back to full contrast, but between two greys the box is as wide
	// as their difference and a softened history survives inside it.
	constexpr float c_SlatWidth = 0.5f;
	constexpr int   c_SlatCount = 76;

	// Sized so the slats cover the measurement box from the drift's starting camera through its
	// arrival at x = 0.
	constexpr float c_FenceStartX = -34.0f;

	constexpr float c_SlatLight = 0.62f;
	constexpr float c_SlatDark  = 0.38f;

	// What the light slats are edited to mid-run: far enough from both to move the fence's mean well
	// clear of the frame-to-frame noise, and still inside the range the display curve resolves.
	constexpr float c_SlatEdited = 0.20f;

	bgl::PbrMaterialDesc
	Grey(float value)
	{
		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(value, value, value, 1.0f);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.6f;
		return desc;
	}

	// Takes the two materials rather than creating them, so a caller that means to edit one of them
	// mid-run holds its handle.
	void
	AddSlatWall(
		const bgl::SceneRef&     scene,
		const bgl::SceneViewRef& view,
		int                      count,
		float                    startX,
		bgl::MaterialHandle      light,
		bgl::MaterialHandle      dark,
		float                    slatWidth = c_SlatWidth)
	{
		const bgl::GeomHandle slats[] = {
			scene->AddPlaneGeom(1, 1, slatWidth, c_QuadScale * 2.0f, light),
			scene->AddPlaneGeom(1, 1, slatWidth, c_QuadScale * 2.0f, dark),
		};

		for (int i = 0; i < count; ++i)
		{
			const float x = startX + static_cast<float>(i) * slatWidth;
			view->CreateStaticMeshInstance(
				slats[i % 2],
				glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)));
		}
	}

	void
	AddSlatWall(const bgl::SceneRef& scene, const bgl::SceneViewRef& view, int count, float startX)
	{
		AddSlatWall(
			scene,
			view,
			count,
			startX,
			scene->CreatePbrMaterial(Grey(c_SlatLight)),
			scene->CreatePbrMaterial(Grey(c_SlatDark)));
	}

	void
	AddFence(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());
		AddSlatWall(scene, view, c_SlatCount, c_FenceStartX);
	}

	// The same fence with slats about two output pixels across, which is one render pixel at half
	// render scale. That is the regime the output-resolution accumulation exists for: the fence
	// above is nearly three render pixels per slat even at half scale, where a filtered upscale
	// already reproduces it and nothing can beat it by much.
	constexpr float c_FineSlatWidth = 0.1875f;
	constexpr int   c_FineSlatCount = 76;

	// Finer still: about one *output* pixel per slat, which is below what a single sample per output
	// pixel can resolve at all. The fence above is the reconstruction's regime -- what a render grid
	// misses and an output grid can hold; this one is the regime where drawing the frame once gets
	// it visibly wrong, which is what the supersampled truth needs in order to bite.
	constexpr float c_SubPixelSlatWidth = 0.09375f;

	void
	AddSubPixelFence(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());
		AddSlatWall(
			scene,
			view,
			c_FineSlatCount,
			-0.5f * static_cast<float>(c_FineSlatCount) * c_SubPixelSlatWidth,
			scene->CreatePbrMaterial(Grey(c_SlatLight)),
			scene->CreatePbrMaterial(Grey(c_SlatDark)),
			c_SubPixelSlatWidth);
	}

	void
	AddFineFence(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());
		AddSlatWall(
			scene,
			view,
			c_FineSlatCount,
			-0.5f * static_cast<float>(c_FineSlatCount) * c_FineSlatWidth,
			scene->CreatePbrMaterial(Grey(c_SlatLight)),
			scene->CreatePbrMaterial(Grey(c_SlatDark)),
			c_FineSlatWidth);
	}

	bgl::SceneDesc
	QuadSceneDesc()
	{
		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 64;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 8192;
		sceneDesc.initialIndices              = 256;
		sceneDesc.initialPbrMaterials         = 4;
		return sceneDesc;
	}

	bgl::GraphicsOptions
	TestOptions()
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		return opts;
	}

	bgl::Viewport
	FullViewport()
	{
		return bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
	}

	// The quad's upper-left edge, which the 20-degree roll puts on a diagonal through this box.
	constexpr int c_EdgeBoxX = 60;
	constexpr int c_EdgeBoxY = 60;
	constexpr int c_EdgeBox  = 40;

	using ScenePopulator = void (*)(const bgl::SceneRef&, const bgl::SceneViewRef&);
	using CameraProvider = bgl::Camera (*)(int frame);
	using ClockProvider  = float (*)(int frame);

	bgl::Camera
	StillCamera(int)
	{
		return Camera();
	}

	float
	StoppedClock(int)
	{
		return 0.0f;
	}

	// Renders the populated scene for `frames` frames, each from `cameraAt(frame)` at
	// `clockAt(frame)`, and writes the last one to `path`.
	void
	RenderTo(
		const std::string& path,
		bool               taaEnabled,
		int                frames,
		ScenePopulator     populate            = AddQuad,
		CameraProvider     cameraAt            = StillCamera,
		ClockProvider      clockAt             = StoppedClock,
		float              renderScale         = 1.0f,
		int                outputScale         = 1,
		float              reconstructionWidth = bgl::RenderTargetDesc().taaReconstructionWidth)
	{
		auto gfx = bgl::CreateGraphics(TestOptions());
		REQUIRE(gfx != nullptr);

		auto targetDesc                   = bgl::RenderTargetDesc();
		targetDesc.width                  = static_cast<int>(c_Width) * outputScale;
		targetDesc.height                 = static_cast<int>(c_Height) * outputScale;
		targetDesc.headless               = true;
		targetDesc.taaEnabled             = taaEnabled;
		targetDesc.renderScale            = renderScale;
		targetDesc.taaReconstructionWidth = reconstructionWidth;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto scene = gfx->CreateScene(QuadSceneDesc());
		auto view  = gfx->CreateSceneView(scene, 128);
		populate(scene, view);

		auto job = bgl::RenderJob();
		job.view = view;

		// The job's viewport is the target's output size, which the renderer maps onto the render
		// grid itself. Same framing at every scale, so the same box measures the same content.
		job.viewport = bgl::Viewport(
			static_cast<float>(c_Width * outputScale),
			static_cast<float>(c_Height * outputScale));

		for (int frame = 0; frame < frames; ++frame)
		{
			job.camera = cameraAt(frame);
			job.time   = clockAt(frame);
			gfx->DrawFrame(target, job);
		}

		gfx->ScreenshotPng(target, path);
	}

	// How far the camera slides sideways per frame while panning, in world units. Fast enough that a
	// pixel's history comes from well outside its own neighbourhood, which is the case ghosting shows
	// up in and the one the report was about.
	constexpr float c_PanStep   = 0.35f;
	constexpr int   c_PanFrames = 10;

	bgl::Camera
	CameraAt(float x)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(x, 0.0f, c_CameraZ),
				glm::vec3(x, 0.0f, c_CameraZ - 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);
		return camera;
	}

	// A slower, longer slide than the ghosting pan: slow enough that the quad never leaves the frame,
	// long enough that the accumulation reaches its steady state under motion rather than measuring
	// wherever the warm-up had got to.
	constexpr float c_DriftStep   = 0.5f;
	constexpr int   c_DriftFrames = 30;

	// A constant `c_DriftStep` per frame, arriving at x = 0 on the last -- which is captured while
	// the camera is still moving.
	bgl::Camera
	DriftingCamera(int frame)
	{
		return CameraAt(-static_cast<float>(c_DriftFrames - 1 - frame) * c_DriftStep);
	}

	// A quad floating in front of a wall of grey slats, for the ghost test the empty-background pan
	// cannot perform: over emptiness the neighbourhood colour clamp degenerates to the background's
	// own colour and scrubs a trail by itself, but over mid-contrast detail the clamp's box is as
	// wide as the slats' difference and a bright ghost survives inside it. Only depth can tell that
	// ghost from detail. The quad is nearer than the slats so a camera pan uncovers them by
	// parallax.
	constexpr float c_ParallaxQuadZ    = 8.0f;
	constexpr float c_ParallaxQuadSize = 6.0f;
	constexpr int   c_WallSlatCount    = 80;

	void
	AddQuadOverSlats(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());
		AddSlatWall(
			scene,
			view,
			c_WallSlatCount,
			-static_cast<float>(c_WallSlatCount / 2) * c_SlatWidth);

		auto quad = scene->AddPlaneGeom(1, 1, c_ParallaxQuadSize, c_ParallaxQuadSize);
		view->CreateStaticMeshInstance(
			quad,
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, c_ParallaxQuadZ)) *
				glm::rotate(glm::mat4(1.0f), glm::radians(c_QuadYaw), glm::vec3(0.0f, 0.0f, 1.0f)));
	}

	// Converges at the pan's start, then arrives at x = 0 exactly as RenderPan's pan does -- so the
	// accumulation is full of the quad when the parallax uncovers the slats behind it.
	bgl::Camera
	GhostPanCamera(int frame)
	{
		const int panFrame  = frame - c_ConvergeFrames;
		const int remaining = std::max(0, c_PanFrames - 1 - std::max(panFrame, 0));
		return CameraAt(-static_cast<float>(remaining) * c_PanStep);
	}

	bgl::Camera
	GhostStillCamera(int)
	{
		return CameraAt(0.0f);
	}

	// Pans the camera to x = 0 from `c_PanTotal` to its left, and captures the frame it lands on.
	// `panning` false holds it at the destination throughout, which is the reference: the same camera,
	// the same geometry, nothing behind it.
	void
	RenderPan(const std::string& path, bool taaEnabled, bool panning)
	{
		auto gfx = bgl::CreateGraphics(TestOptions());
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto scene = gfx->CreateScene(QuadSceneDesc());
		auto view  = gfx->CreateSceneView(scene, 4);
		AddQuad(scene, view);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.viewport = FullViewport();

		for (int frame = 0; frame < c_PanFrames; ++frame)
		{
			const float offset = static_cast<float>(c_PanFrames - 1 - frame) * c_PanStep;
			job.camera         = CameraAt(panning ? -offset : 0.0f);
			gfx->DrawFrame(target, job);
		}

		gfx->ScreenshotPng(target, path);
	}

	// An animating surface under a still camera: the fixture's VAT quad, tilted like the others,
	// dark, floating in front of a flat mid-grey backdrop, sweeping back and forth along its own X.
	// Every pixel it sweeps over is background whose own motion is zero -- which no pan can
	// produce, and which is where its outline smears. The sweep is `c_AnimSweepFrames` rendered
	// frames long, so the edges cross about two texels a frame, as a limb does at 1080p.
	constexpr float c_AnimQuadScale   = 2.0f;
	constexpr int   c_AnimSweepFrames = 18;
	constexpr int   c_AnimSweeps      = 7;
	constexpr float c_AnimQuadGrey    = 0.08f;
	constexpr float c_BackdropGrey    = 0.5f;
	constexpr float c_BackdropSize    = 60.0f;

	// One clip frame per sweep at the fixture's sample rate; the capture lands on an odd multiple
	// of a whole clip frame, so the quad has just arrived at c_Step from the left.
	constexpr float c_AnimStepSeconds =
		1.0f / (bgl::test::vat_synth::c_SampleRate * c_AnimSweepFrames);
	constexpr int c_AnimFrames = c_AnimSweepFrames * c_AnimSweeps + 1;

	float
	AnimationClock(int frame)
	{
		return static_cast<float>(frame) * c_AnimStepSeconds;
	}

	// A slow slide under the whole animation, arriving at x = 0 on the capture frame: about a third
	// of a texel per frame on the backdrop, half on the quad, so the camera's motion is neither
	// zero nor enough to carry the mesh's own.
	constexpr float c_AnimDriftStep = 0.03f;

	bgl::Camera
	AnimDriftCamera(int frame)
	{
		return CameraAt(-static_cast<float>(c_AnimFrames - 1 - frame) * c_AnimDriftStep);
	}

	glm::mat4
	AnimatedQuadTransform()
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, c_ParallaxQuadZ)) *
		       glm::rotate(glm::mat4(1.0f), glm::radians(c_QuadYaw), glm::vec3(0.0f, 0.0f, 1.0f)) *
		       glm::scale(glm::mat4(1.0f), glm::vec3(c_AnimQuadScale));
	}

	void
	AddVatQuadOverBackdrop(
		const bgl::SceneRef&                    scene,
		const bgl::SceneViewRef&                view,
		const bgl::ISceneView::VatInstanceDesc& desc)
	{
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto backdrop = scene->AddPlaneGeom(
			1,
			1,
			c_BackdropSize,
			c_BackdropSize,
			scene->CreatePbrMaterial(Grey(c_BackdropGrey)));
		view->CreateStaticMeshInstance(backdrop, glm::mat4(1.0f));

		const auto quad = bgl::test::vat_synth::AddSlidingQuadGeom(
			*scene,
			scene->CreatePbrMaterial(Grey(c_AnimQuadGrey)));
		view->CreateVatMeshInstance(quad, AnimatedQuadTransform(), desc);
	}

	void
	AddAnimatingQuadOverBackdrop(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		AddVatQuadOverBackdrop(scene, view, { bgl::test::vat_synth::c_LoopClip, 0.0f, 1.0f });
	}

	// The pose the animation is captured at, held: the reference the outline is read against.
	// Frame 1 of the clip, which is the pose an odd number of sweeps arrives at.
	constexpr float c_ArrivedPhase = 1.0f;

	void
	AddArrivedQuadOverBackdrop(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		AddVatQuadOverBackdrop(
			scene,
			view,
			{ bgl::test::vat_synth::c_LoopClip, c_ArrivedPhase, 0.0f });
	}

	// Where a point of the quad's bake space lands on screen when the pose is at `offset`.
	glm::ivec2
	AnimatedQuadPx(float offset, glm::vec3 bakePoint)
	{
		const glm::vec4 world =
			AnimatedQuadTransform() * glm::vec4(bakePoint + glm::vec3(offset, 0.0f, 0.0f), 1.0f);
		const glm::vec4 clip = Camera().GetViewProjection() * world;
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return glm::ivec2(
			std::lround((ndc.x * 0.5f + 0.5f) * static_cast<float>(c_Width)),
			std::lround((ndc.y * -0.5f + 0.5f) * static_cast<float>(c_Height)));
	}

	// The box the whole quad sits in at the captured pose, with room for its outline's smear.
	struct PxBox
	{
		int x, y, w, h;
	};

	PxBox
	ArrivedQuadBox()
	{
		constexpr int c_Margin = 6;

		int minX = c_Width, minY = c_Height, maxX = 0, maxY = 0;
		for (const glm::vec3& corner : bgl::test::vat_synth::c_QuadAtOrigin)
		{
			const glm::ivec2 px = AnimatedQuadPx(bgl::test::vat_synth::c_Step, corner);
			minX                = std::min(minX, px.x);
			minY                = std::min(minY, px.y);
			maxX                = std::max(maxX, px.x);
			maxY                = std::max(maxY, px.y);
		}
		return { minX - c_Margin,
			     minY - c_Margin,
			     maxX - minX + 2 * c_Margin,
			     maxY - minY + 2 * c_Margin };
	}
}

// The antialiasing claim, as a number. AliasEnergy is the mean squared difference between
// horizontally adjacent pixels, which is what a stair-stepped edge has and a resolved one does not.
// The box straddles the quad's tilted edge; a box wholly inside or outside it would measure a flat
// region and score near zero either way.
TEST_CASE("A converged TAA frame has less edge aliasing than an unjittered one", "[taa][render]")
{
	const std::string off = "assets/golden/taa_off.got.png";
	const std::string on  = "assets/golden/taa_converged.got.png";

	RenderTo(off, false, c_ConvergeFrames);
	RenderTo(on, true, c_ConvergeFrames);

	// The quad's upper-left edge, which the 20-degree roll puts on a diagonal through this box.
	constexpr int c_BoxX = 60;
	constexpr int c_BoxY = 60;
	constexpr int c_Box  = 40;

	const float aliased  = bgl::test::AliasEnergy(off, c_BoxX, c_BoxY, c_Box, c_Box);
	const float resolved = bgl::test::AliasEnergy(on, c_BoxX, c_BoxY, c_Box, c_Box);

	INFO("alias energy: off = " << aliased << ", converged = " << resolved);

	// The unjittered edge has to be aliased in the first place, or the comparison is vacuous.
	CHECK(aliased > 1e-3f);
	CHECK(resolved < aliased * 0.6f);
}

// Under jitter a static image must resolve to what it supersamples to, so away from the edges the
// converged frame and the unjittered one are the same picture. This is what catches a resolve that
// converges to something -- a darkened, tinted or drifting accumulation -- rather than to the truth.
TEST_CASE("A static scene converges to the unjittered image", "[taa][render]")
{
	const std::string off = "assets/golden/taa_conv_off.got.png";
	const std::string on  = "assets/golden/taa_conv_on.got.png";

	RenderTo(off, false, c_ConvergeFrames);
	RenderTo(on, true, c_ConvergeFrames);

	// Interior of the quad and a corner of the background: both flat, so jitter cannot change them
	// and any difference is the accumulation being wrong rather than antialiasing.
	const bgl::test::Rgba interiorOff = bgl::test::MeanColor(off, 118, 118, 20, 20);
	const bgl::test::Rgba interiorOn  = bgl::test::MeanColor(on, 118, 118, 20, 20);

	INFO(
		"interior: off = " << interiorOff.r << "," << interiorOff.g << "," << interiorOff.b
						   << "  on = " << interiorOn.r << "," << interiorOn.g << ","
						   << interiorOn.b);

	// Well clear of the background, or a converged-to-black bug would satisfy the equality below.
	CHECK(interiorOff.Luma() > 0.1f);

	CHECK(interiorOn.r == Catch::Approx(interiorOff.r).margin(0.02));
	CHECK(interiorOn.g == Catch::Approx(interiorOff.g).margin(0.02));
	CHECK(interiorOn.b == Catch::Approx(interiorOff.b).margin(0.02));

	const bgl::test::Rgba backgroundOn = bgl::test::MeanColor(on, 4, 4, 16, 16);
	CHECK(backgroundOn.Luma() < 0.02f);
}

// Ghosting, as a number: how much of where the camera *was* is still on screen the moment it stops.
//
// The TAA-off control is the instrument's zero. With no history the frame the pan lands on and the
// frame after holding still are the same render, so anything it scores is the measurement's own noise
// and the TAA figure has to be read against it.
//
// This is the other half of the trade. Flicker (in HashedAlpha_test) falls as the resolve leans harder
// on history; ghosting rises. Neither number means anything alone, and changing the clamp or the blend
// weight against one of them while blind to the other is how the branch got its two open complaints.
TEST_CASE("A pan leaves no more than a bounded trail behind it", "[taa][render]")
{
	const std::string reference = "assets/golden/taa_ghost_reference.got.png";
	const std::string panned    = "assets/golden/taa_ghost_panned.got.png";
	const std::string still     = "assets/golden/taa_ghost_still.got.png";

	// TAA off at the destination: what is genuinely background, decided by where the geometry is
	// rather than by how far any accumulation has got.
	RenderPan(reference, false, false);

	RenderPan(panned, true, true);
	RenderPan(still, true, false);

	const float trail = bgl::test::BackgroundBleed(panned, reference);
	const float floor = bgl::test::BackgroundBleed(still, reference);

	INFO("background bleed: arrived from a pan = " << trail << ", never moved = " << floor);

	// The same renderer that never moved is the zero. It is not exactly zero -- jitter spreads the
	// quad's edge a fraction of a pixel into its neighbours -- so the trail is read against it and not
	// against nothing.
	CHECK(floor < 2.5e-3f);

	// Measured 0.0066 against a floor of 0.0017 -- most of that gap is the resolved edge spreading a
	// fraction of a pixel, not a trail. What this actually guards is the clamp: bypassing it takes the
	// history whole and the figure goes to 0.090, so the bound is set an order of magnitude below that
	// rather than tight against the current number, which the blend weight moves by only a percent.
	CHECK(trail < 2.0e-2f);
}

// The wake over detail, as a number -- the case the empty-background pan cannot measure. Over
// emptiness the clamp's box collapses to the background's own colour and scrubs a trail
// trivially; over mid-contrast slats the box is as wide as their difference, which is where a
// bright occluder's remnant would have room to hide. It does not: wherever the wake is locally
// flat the box is tight again, and the remnant is snapped out within a frame or two of the
// reveal. The wake box is slats the quad covered at the pan's start and has fully uncovered on
// the arrival frame; the still render is the same converged scene, so anything above its own
// convergence noise is ghost.
TEST_CASE("A pan leaves no ghost on the detail it uncovers", "[taa][render]")
{
	const std::string still  = "assets/golden/taa_parallax_still.got.png";
	const std::string panned = "assets/golden/taa_parallax_panned.got.png";

	RenderTo(still, true, c_ConvergeFrames + c_PanFrames, AddQuadOverSlats, GhostStillCamera);
	RenderTo(panned, true, c_ConvergeFrames + c_PanFrames, AddQuadOverSlats, GhostPanCamera);

	// Slats the quad's right edge (at x = 199 on arrival) has left behind, inside its span at the
	// pan's start.
	constexpr int c_WakeX = 204;
	constexpr int c_WakeY = 70;
	constexpr int c_WakeW = 32;
	constexpr int c_WakeH = 116;

	const float ghost = bgl::test::FrameDelta(panned, still, c_WakeX, c_WakeY, c_WakeW, c_WakeH);

	INFO("wake delta against the converged still: " << ghost);

	// Measured 1.2e-4 with a constant blend, 6.9e-5 with the weight ramped by fetch motion --
	// convergence-state noise either way; a ghost the clamp admitted would sit an order of
	// magnitude above. Depth-based disocclusion rejection was measured against this very number
	// and moved it nowhere -- the clamp owns the wake.
	CHECK(ghost < 1.0e-3f);
}

// Blur under motion, as a number. Every off-centre history fetch low-passes the accumulation, and
// while the camera moves the fetches are off-centre every frame, so the softening compounds until
// the resolve reaches a steady state visibly blurrier than the still image -- and snaps back once
// the camera stops, which is what makes it read as motion blur rather than as the antialiasing.
//
// The instrument is the fence's alias energy, which is contrast at the pixel scale: filtering the
// accumulation too softly dims the pattern towards its mean and the energy falls. The still
// converged frame is what the resolve considers finished, so the bound is a ratio to it.
TEST_CASE("Fine detail survives the camera moving", "[taa][render]")
{
	const std::string still  = "assets/golden/taa_drift_still.got.png";
	const std::string moving = "assets/golden/taa_drift_moving.got.png";

	RenderTo(still, true, c_ConvergeFrames, AddFence);
	RenderTo(moving, true, c_DriftFrames, AddFence, DriftingCamera);

	constexpr int c_FenceBoxX = 98;
	constexpr int c_FenceBoxY = 98;
	constexpr int c_FenceBox  = 60;

	const float stillDetail =
		bgl::test::AliasEnergy(still, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);
	const float movingDetail =
		bgl::test::AliasEnergy(moving, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);

	INFO("fence alias energy: still = " << stillDetail << ", moving = " << movingDetail);

	// The still fence has to carry real detail, or the ratio below compares noise to noise.
	CHECK(stillDetail > 3e-4f);

	// Measured 0.66 with the Catmull-Rom history fetch and 0.60 with a plain bilinear one, which is
	// the regression this pins out. The clamp bounds how much softness can survive, so the gap is
	// modest -- but it is the visible part of the moving image going soft.
	CHECK(movingDetail > stillDetail * 0.62f);
}

// The first frame has no accumulation to blend against. If it blended anyway it would come out at
// blendWeight of its true brightness -- a tenth -- so this is the cheapest check that the
// history-invalid path exists at all.
TEST_CASE("The first TAA frame is the scene colour whole", "[taa][render]")
{
	const std::string first = "assets/golden/taa_first_frame.got.png";
	const std::string off   = "assets/golden/taa_first_off.got.png";

	RenderTo(first, true, 1);
	RenderTo(off, false, 1);

	const bgl::test::Rgba withTaa    = bgl::test::MeanColor(first, 118, 118, 20, 20);
	const bgl::test::Rgba withoutTaa = bgl::test::MeanColor(off, 118, 118, 20, 20);

	INFO("first frame luma: taa = " << withTaa.Luma() << ", off = " << withoutTaa.Luma());

	CHECK(withoutTaa.Luma() > 0.1f);
	CHECK(withTaa.Luma() == Catch::Approx(withoutTaa.Luma()).margin(0.02));
}

// The toggle exists so a temporal artifact can be judged against its absence without a restart, so
// the flag round-tripping is not the contract -- the image changing is. Turning it off must put the
// aliasing back, and turning it on again must resolve it a second time from a history that was
// discarded rather than resumed across frames that were never rendered.
TEST_CASE("Toggling temporal AA at runtime turns the resolve off and on", "[taa][render]")
{
	auto gfx = bgl::CreateGraphics(TestOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(c_Width);
	targetDesc.height     = static_cast<int>(c_Height);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = true;

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);
	CHECK(target->IsTaaEnabled());

	auto scene = gfx->CreateScene(QuadSceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);
	AddQuad(scene, view);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = Camera();
	job.viewport = FullViewport();

	const auto drive = [&](int frames, const std::string& path) {
		for (int frame = 0; frame < frames; ++frame)
		{
			gfx->DrawFrame(target, job);
		}
		gfx->ScreenshotPng(target, path);
		return bgl::test::AliasEnergy(path, c_EdgeBoxX, c_EdgeBoxY, c_EdgeBox, c_EdgeBox);
	};

	const float converged = drive(c_ConvergeFrames, "assets/golden/taa_toggle_on.got.png");

	target->SetTaaEnabled(false);
	CHECK_FALSE(target->IsTaaEnabled());

	// One frame is enough: with the resolve off there is nothing to converge, so the very next
	// frame is the raw unjittered image.
	const float disabled = drive(1, "assets/golden/taa_toggle_off.got.png");

	target->SetTaaEnabled(true);
	CHECK(target->IsTaaEnabled());

	const float reconverged = drive(c_ConvergeFrames, "assets/golden/taa_toggle_back_on.got.png");

	INFO(
		"alias energy: converged = " << converged << ", disabled = " << disabled
									 << ", reconverged = " << reconverged);

	// Off is aliased, and both on-states resolve it. The middle term is what proves the toggle does
	// something rather than leaving the resolve running.
	CHECK(disabled > converged * 1.5f);
	CHECK(reconverged < disabled * 0.6f);

	// Re-converging must reach the same place as the first time. A history that resumed across the
	// gap instead of restarting would land somewhere between the two.
	CHECK(reconverged == Catch::Approx(converged).margin(converged * 0.25f));
}

// Enabling it on a target that allocated nothing has no history to accumulate into. Silently
// ignoring the call would leave a caller believing TAA is on and wondering why the image never
// resolves, so it is a caller error rather than a no-op.
TEST_CASE("Enabling temporal AA on a target without it is an error", "[taa][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);
	CHECK_FALSE(target->IsTaaEnabled());

	CHECK_THROWS_AS(target->SetTaaEnabled(true), bgl::GraphicsError);

	// Turning it off on a target that never had it is the caller asking for what it already has.
	CHECK_NOTHROW(target->SetTaaEnabled(false));
}

// A material's contents change with nothing on screen moving, and no motion vector describes that:
// the accumulation holds the surface as it was, and the resolve goes on reprojecting onto it.
//
// Fine detail is where that survives, which is why this measures the fence rather than the quad:
// over a uniform colour the neighbourhood box collapses onto the new colour and drags the
// accumulation across by itself, but between two greys the box is as wide as their difference and
// the old grey sits inside it -- and at rest the remembered spread widens it further.
TEST_CASE(
	"A material edit lands whole rather than fading in over the frames after it",
	"[taa][render]")
{
	auto gfx = bgl::CreateGraphics(TestOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(c_Width);
	targetDesc.height     = static_cast<int>(c_Height);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = true;

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto scene = gfx->CreateScene(QuadSceneDesc());
	auto view  = gfx->CreateSceneView(scene, 128);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const bgl::MaterialHandle edited = scene->CreatePbrMaterial(Grey(c_SlatLight));
	AddSlatWall(
		scene,
		view,
		c_SlatCount,
		c_FenceStartX,
		edited,
		scene->CreatePbrMaterial(Grey(c_SlatDark)));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = Camera();
	job.viewport = FullViewport();

	// Slats, several pixels wide, across the middle of the frame.
	constexpr int c_FenceBoxX = 96;
	constexpr int c_FenceBoxY = 96;
	constexpr int c_FenceBox  = 48;

	const auto drive = [&](int frames, const std::string& path) {
		for (int frame = 0; frame < frames; ++frame)
		{
			gfx->DrawFrame(target, job);
		}
		gfx->ScreenshotPng(target, path);
		return path;
	};

	const std::string before = drive(c_ConvergeFrames, "assets/golden/taa_material_before.got.png");

	scene->UpdatePbrMaterial(edited, Grey(c_SlatEdited));
	const std::string edit  = drive(1, "assets/golden/taa_material_edit.got.png");
	const std::string after = drive(c_ConvergeFrames, "assets/golden/taa_material_after.got.png");

	const float lightBefore =
		bgl::test::MeanColor(before, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox).Luma();
	const float lightEdit =
		bgl::test::MeanColor(edit, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox).Luma();
	const float lightAfter =
		bgl::test::MeanColor(after, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox).Luma();

	INFO(
		"fence luma: before the edit = " << lightBefore << ", one frame after = " << lightEdit
										 << ", converged after = " << lightAfter);

	// The edit is worth measuring only if it moved the region at all.
	REQUIRE(std::abs(lightBefore - lightAfter) > 0.05f);

	// Antialiasing changes across the box's edges, not its mean, so the frame the edit lands on
	// reads as the converged one does -- unless it is still carrying the old material.
	CHECK(
		lightEdit == Catch::Approx(lightAfter).margin(std::abs(lightBefore - lightAfter) * 0.15f));
}

// The outline of an animating mesh, as a number: the same pose drawn animating into and drawn
// held, both under TAA. The raw pair is the instrument's guard -- pixel-identical, so anything
// the TAA pair differs by is the resolve's doing. A pan never sees this: the mesh and its
// backdrop share the camera's motion, but under a still camera a silhouette pixel half covered
// by the mesh carries either its velocity or the backdrop's zero, by which fragment won its
// centre, and the backdrop beside it reports zero motion and banks the passing edge's contrast
// as if it were at rest. Together those smear the outline into a doubled edge, which is what
// the Animation panel showed and what the resolve's neighbourhood motion now answers.
//
// The same renders also say whether the moving edges still *resolve*: the tilted top edge's
// stair-step against the unjittered render. Alias energy cannot tell a resolved edge from a
// smeared one -- both are smooth -- so that half only guards that accumulation on a moving mesh
// keeps working; the outline figure is what tells the two apart. One case for both, since each
// render is a device creation.
TEST_CASE("An animating mesh's outline is as sharp as when it is held", "[taa][vat][render]")
{
	using namespace bgl::test::vat_synth;

	const std::string held     = "assets/golden/taa_anim_held.got.png";
	const std::string animated = "assets/golden/taa_anim_moving.got.png";
	const std::string heldRaw  = "assets/golden/taa_anim_held_raw.got.png";
	const std::string movedRaw = "assets/golden/taa_anim_moving_raw.got.png";

	RenderTo(held, true, c_ConvergeFrames, AddArrivedQuadOverBackdrop);
	RenderTo(
		animated,
		true,
		c_AnimFrames,
		AddAnimatingQuadOverBackdrop,
		StillCamera,
		AnimationClock);
	RenderTo(heldRaw, false, 1, AddArrivedQuadOverBackdrop);
	RenderTo(
		movedRaw,
		false,
		c_AnimFrames,
		AddAnimatingQuadOverBackdrop,
		StillCamera,
		AnimationClock);

	const PxBox box = ArrivedQuadBox();

	const float pose    = bgl::test::FrameDelta(movedRaw, heldRaw, box.x, box.y, box.w, box.h);
	const float outline = bgl::test::FrameDelta(animated, held, box.x, box.y, box.w, box.h);

	INFO("quad box delta, animating against held: raw = " << pose << ", resolved = " << outline);

	// The animation must have arrived at exactly the held pose, or the figure below measures the
	// pose and not the resolve.
	REQUIRE(pose == 0.0f);

	// Measured 6.9e-4 with the pixel's own motion alone -- the doubled outline -- 1.25e-4 with the
	// neighbourhood's, and 1.04e-4 with the blend weighted by the fetch's motion; the bound sits
	// between the first two, nearer the fix. The last step is not bounded apart: its margin is
	// within what one GPU differs from another by here.
	CHECK(outline < 3.0e-4f);

	// The same under a drifting camera, which is what tells a surface's own motion from the
	// camera's: a pan alone must not dilate (the hashed figures depend on it), an animating mesh
	// under one still must.
	const std::string heldDrift  = "assets/golden/taa_anim_drift_held.got.png";
	const std::string movedDrift = "assets/golden/taa_anim_drift_moving.got.png";
	const std::string driftRaw   = "assets/golden/taa_anim_drift_moving_raw.got.png";

	RenderTo(heldDrift, true, c_AnimFrames, AddArrivedQuadOverBackdrop, AnimDriftCamera);
	RenderTo(
		movedDrift,
		true,
		c_AnimFrames,
		AddAnimatingQuadOverBackdrop,
		AnimDriftCamera,
		AnimationClock);
	RenderTo(
		driftRaw,
		false,
		c_AnimFrames,
		AddAnimatingQuadOverBackdrop,
		AnimDriftCamera,
		AnimationClock);

	const float driftPose = bgl::test::FrameDelta(driftRaw, heldRaw, box.x, box.y, box.w, box.h);
	const float driftOutline =
		bgl::test::FrameDelta(movedDrift, heldDrift, box.x, box.y, box.w, box.h);

	INFO(
		"quad box delta under a drifting camera, animating against held: raw = "
		<< driftPose << ", resolved = " << driftOutline);

	// Measured 8.8e-4 reprojecting by each pixel's own vector, 2.9e-4 by the neighbour that moves
	// most on its own, and 2.3e-4 with the blend weighted by that fetch's motion.
	REQUIRE(driftPose == 0.0f);
	CHECK(driftOutline < 5.0e-4f);

	const glm::ivec2 topEdge       = AnimatedQuadPx(c_Step, glm::vec3(0.0f, 1.0f, 0.0f));
	constexpr int    c_EdgeBoxSize = 32;

	const auto energy = [&](const std::string& path) {
		return bgl::test::AliasEnergy(
			path,
			topEdge.x - c_EdgeBoxSize / 2,
			topEdge.y - c_EdgeBoxSize / 2,
			c_EdgeBoxSize,
			c_EdgeBoxSize);
	};

	const float aliased = energy(movedRaw);
	const float resting = energy(held);
	const float moving  = energy(animated);

	INFO(
		"edge alias energy: unjittered = " << aliased << ", held = " << resting
										   << ", animating = " << moving);

	// The unjittered edge has to be aliased in the first place, or the ratios below are vacuous;
	// a dark quad on mid grey at this tilt measures 6.6e-4.
	CHECK(aliased > 4e-4f);
	CHECK(resting < aliased * 0.6f);
	CHECK(moving < aliased * 0.6f);
}

// The jitter sequence is the target's, not the renderer's. Two viewports drawn each frame by one
// renderer -- the level editor beside the Animation panel -- would otherwise each see every second
// term of the eight, four lopsided sub-pixel positions instead of the footprint, and a third
// viewport would change what the first two converge to. Bit-identical, since the second target
// cannot reach the first's accumulation by any route but the shared counter.
TEST_CASE(
	"A target's jitter walks the whole sequence however many targets share the renderer",
	"[taa][render]")
{
	const std::string alone  = "assets/golden/taa_targets_alone.got.png";
	const std::string shared = "assets/golden/taa_targets_shared.got.png";

	const auto render = [&](const std::string& path, bool secondTarget) {
		auto gfx = bgl::CreateGraphics(TestOptions());
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = true;

		auto target = gfx->CreateRenderTarget(targetDesc);
		auto other  = secondTarget ? gfx->CreateRenderTarget(targetDesc) : bgl::RenderTargetRef();

		auto scene = gfx->CreateScene(QuadSceneDesc());
		auto view  = gfx->CreateSceneView(scene, 4);
		AddQuad(scene, view);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = Camera();
		job.viewport = FullViewport();

		for (int frame = 0; frame < c_ConvergeFrames; ++frame)
		{
			gfx->DrawFrame(target, job);
			if (other != nullptr)
			{
				gfx->DrawFrame(other, job);
			}
		}

		gfx->ScreenshotPng(target, path);
	};

	render(alone, false);
	render(shared, true);

	const float delta =
		bgl::test::FrameDelta(alone, shared, c_EdgeBoxX, c_EdgeBoxY, c_EdgeBox, c_EdgeBox);

	INFO("edge box delta, alone against sharing the renderer: " << delta);
	CHECK(delta == 0.0f);
}

// The two grids and how one derives from the other. Every figure this file measures is read at the
// default scale, where they coincide -- so what this pins is that the default really is the
// coincidence, and that a scale moves the render grid and nothing else.
TEST_CASE("A render scale moves the geometry grid and not the output", "[taa][render]")
{
	auto gfx = bgl::CreateGraphics(TestOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(c_Width);
	targetDesc.height     = static_cast<int>(c_Height);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = true;

	SECTION("the default scale leaves the two the same size")
	{
		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		CHECK(target->GetRenderWidth() == target->GetWidth());
		CHECK(target->GetRenderHeight() == target->GetHeight());
	}

	SECTION("a scale below one renders on a coarser grid, presenting at the same size")
	{
		targetDesc.renderScale = 0.5f;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		CHECK(target->GetWidth() == c_Width);
		CHECK(target->GetHeight() == c_Height);
		CHECK(target->GetRenderWidth() == c_Width / 2);
		CHECK(target->GetRenderHeight() == c_Height / 2);
	}

	SECTION("a scale above one supersamples, and still presents at the same size")
	{
		targetDesc.renderScale = 2.0f;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		CHECK(target->GetWidth() == c_Width);
		CHECK(target->GetRenderWidth() == c_Width * 2);
	}

	SECTION("a resize keeps the scale")
	{
		targetDesc.renderScale = 0.5f;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		gfx->Resize(target, c_Width * 2, c_Height * 2);

		CHECK(target->GetWidth() == c_Width * 2);
		CHECK(target->GetRenderWidth() == c_Width);
	}

	SECTION("the scale can be changed on a live target, without moving the output")
	{
		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto scene = gfx->CreateScene(QuadSceneDesc());
		auto view  = gfx->CreateSceneView(scene, 4);
		AddQuad(scene, view);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = Camera();
		job.viewport = FullViewport();

		gfx->DrawFrame(target, job);

		auto* base = target->As<bgl::RenderTargetBase>();
		REQUIRE(base != nullptr);

		// What a scale change must not disturb: the output grid, and so the backbuffers and the two
		// histories allocated against it.
		const bgl::TextureHandle backbuffer = base->GetBackbufferTexture(0);
		const bgl::TextureHandle history    = base->GetHistoryTexture(0);

		REQUIRE(base->IsHistoryValid());

		gfx->SetRenderScale(target, 0.5f);

		CHECK(target->GetWidth() == c_Width);
		CHECK(target->GetHeight() == c_Height);
		CHECK(target->GetRenderWidth() == c_Width / 2);
		CHECK(target->GetRenderHeight() == c_Height / 2);

		// The same textures, not merely the same size: rebuilding them would cost the frame ring its
		// fences and the accumulation its buffers, for a change that moved neither grid they sit on.
		CHECK(base->GetBackbufferTexture(0) == backbuffer);
		CHECK(base->GetHistoryTexture(0) == history);

		// The accumulation is dropped even though its buffers are kept: it describes samples the new
		// render grid does not take.
		CHECK_FALSE(base->IsHistoryValid());

		// The frame after must still draw: every attachment the geometry passes write was released
		// and rebuilt underneath it.
		CHECK_NOTHROW(gfx->DrawFrame(target, job));
	}

	SECTION("a scale that is not a positive number is the caller's error")
	{
		targetDesc.renderScale = 0.0f;
		CHECK_THROWS_AS(gfx->CreateRenderTarget(targetDesc), bgl::GraphicsError);

		targetDesc.renderScale = -1.0f;
		CHECK_THROWS_AS(gfx->CreateRenderTarget(targetDesc), bgl::GraphicsError);

		targetDesc.renderScale = std::numeric_limits<float>::quiet_NaN();
		CHECK_THROWS_AS(gfx->CreateRenderTarget(targetDesc), bgl::GraphicsError);
	}
}

// Eight positions walk one render pixel, and the resolve at scale 1.0 is what every measured figure
// in this file was taken against -- so the length must not move there. It grows only where the
// output grid is denser than the render one and those eight would have to serve four pixels each.
TEST_CASE("The jitter sequence grows only when the output grid is denser", "[taa]")
{
	CHECK(bgl::JitterSequenceLength(256, 256, 256, 256) == bgl::c_JitterSequenceLength);

	// Supersampling: every output pixel already sees several render samples per frame.
	CHECK(bgl::JitterSequenceLength(512, 512, 256, 256) == bgl::c_JitterSequenceLength);

	CHECK(bgl::JitterSequenceLength(128, 128, 256, 256) == 4 * bgl::c_JitterSequenceLength);

	// Capped: past this the tail costs more than the phases are worth.
	CHECK(bgl::JitterSequenceLength(64, 64, 256, 256) == bgl::c_MaxJitterSequenceLength);
}

// The claim of the whole change, at its smallest: at half render scale the accumulation lives on
// the output grid, so jittered half-resolution frames reconstruct an image closer to the full-scale
// one than any single half-resolution frame stretched to the same size can be.
//
// The full-scale converged frame stands in for the truth here, which is enough to order two
// candidates against each other; what it cannot do is say how far either is from the real thing,
// and that is what the supersampled truth measures instead.
//
// Detail energy deliberately is not the instrument. A half-scale render of the fence aliases, and
// aliasing is adjacent-pixel energy too -- the raw upscale scores *higher* than the reconstruction
// while looking worse, because the energy it carries is moire rather than slats.
TEST_CASE(
	"An upscaling resolve lands closer to the full-scale image than a stretch",
	"[taa][render]")
{
	const std::string upscaled  = "assets/golden/taa_scale_half_off.got.png";
	const std::string resolved  = "assets/golden/taa_scale_half_on.got.png";
	const std::string reference = "assets/golden/taa_scale_full_on.got.png";

	constexpr float c_HalfScale = 0.5f;

	RenderTo(upscaled, false, 1, AddFineFence, StillCamera, StoppedClock, c_HalfScale);
	RenderTo(
		resolved,
		true,
		c_ConvergeFrames,
		AddFineFence,
		StillCamera,
		StoppedClock,
		c_HalfScale);
	RenderTo(reference, true, c_ConvergeFrames, AddFineFence);

	constexpr int c_FenceBoxX = 98;
	constexpr int c_FenceBoxY = 98;
	constexpr int c_FenceBox  = 60;

	// The output grid, not the render one: what a half-scale target presents is still the size it
	// was asked for, which is what makes these three boxes the same box.
	const bgl::test::Rgba resolvedMean =
		bgl::test::MeanColor(resolved, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);
	const bgl::test::Rgba referenceMean =
		bgl::test::MeanColor(reference, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);

	const float referenceDetail =
		bgl::test::AliasEnergy(reference, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);

	const float stretchError = bgl::test::FrameDelta(
		upscaled,
		reference,
		c_FenceBoxX,
		c_FenceBoxY,
		c_FenceBox,
		c_FenceBox);
	const float resolveError = bgl::test::FrameDelta(
		resolved,
		reference,
		c_FenceBoxX,
		c_FenceBoxY,
		c_FenceBox,
		c_FenceBox);

	INFO(
		"distance from the full-scale image: stretch = " << stretchError
														 << ", reconstruction = " << resolveError);

	// The box has to be on the fence at all, or both distances are noise against noise.
	CHECK(referenceDetail > 3e-4f);

	CHECK(resolveError < stretchError);

	// Converged to the same picture, not merely to a sharp one: a reconstruction that drifts,
	// darkens or tints would satisfy the detail bound above on its own.
	INFO(
		"fence mean: resolved = " << resolvedMean.r << "," << resolvedMean.g << ","
								  << resolvedMean.b << "  reference = " << referenceMean.r << ","
								  << referenceMean.g << "," << referenceMean.b);

	CHECK(resolvedMean.r == Catch::Approx(referenceMean.r).margin(0.03));
	CHECK(resolvedMean.g == Catch::Approx(referenceMean.g).margin(0.03));
	CHECK(resolvedMean.b == Catch::Approx(referenceMean.b).margin(0.03));
}

// The other half of the reconstruction kernel's trade. Narrowing it sharpens a held frame without
// limit, because a still pixel eventually sees every phase; what it costs is the frames a moving
// pixel waits for the phase that serves it, which no still measurement can see. This is that cost,
// bounded: under the drift the reconstruction must still beat the stretch it replaces.
TEST_CASE("An upscaling resolve keeps its lead while the camera moves", "[taa][render]")
{
	const std::string upscaled  = "assets/golden/taa_scale_half_off_drift.got.png";
	const std::string resolved  = "assets/golden/taa_scale_half_on_drift.got.png";
	const std::string reference = "assets/golden/taa_scale_full_on_drift.got.png";

	constexpr float c_HalfScale = 0.5f;

	RenderTo(
		upscaled,
		false,
		c_DriftFrames,
		AddFineFence,
		DriftingCamera,
		StoppedClock,
		c_HalfScale);
	RenderTo(
		resolved,
		true,
		c_DriftFrames,
		AddFineFence,
		DriftingCamera,
		StoppedClock,
		c_HalfScale);
	RenderTo(reference, true, c_DriftFrames, AddFineFence, DriftingCamera);

	constexpr int c_FenceBoxX = 98;
	constexpr int c_FenceBoxY = 98;
	constexpr int c_FenceBox  = 60;

	const float referenceDetail =
		bgl::test::AliasEnergy(reference, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);

	const float stretchError = bgl::test::FrameDelta(
		upscaled,
		reference,
		c_FenceBoxX,
		c_FenceBoxY,
		c_FenceBox,
		c_FenceBox);
	const float resolveError = bgl::test::FrameDelta(
		resolved,
		reference,
		c_FenceBoxX,
		c_FenceBoxY,
		c_FenceBox,
		c_FenceBox);

	INFO(
		"under drift, distance from the full-scale image: stretch = "
		<< stretchError << ", reconstruction = " << resolveError);

	CHECK(referenceDetail > 3e-4f);
	CHECK(resolveError < stretchError);
}

// Above 1.0 the same resolve is a downsample, and the present-time stretch that used to perform it
// is gone -- so what a supersampled target hands back must still be the size it was asked for, and
// still be the same picture.
TEST_CASE("A supersampling target presents and captures at its output size", "[taa][render]")
{
	const std::string supersampled = "assets/golden/taa_scale_double_on.got.png";
	const std::string reference    = "assets/golden/taa_scale_double_ref.got.png";

	constexpr float c_DoubleScale = 2.0f;

	RenderTo(
		supersampled,
		true,
		c_ConvergeFrames,
		AddQuad,
		StillCamera,
		StoppedClock,
		c_DoubleScale);
	RenderTo(reference, true, c_ConvergeFrames);

	// Interior of the quad: flat, so neither the jitter nor the extra samples can change it, and any
	// difference is the downsample being wrong rather than better.
	const bgl::test::Rgba interior = bgl::test::MeanColor(supersampled, 118, 118, 20, 20);
	const bgl::test::Rgba expected = bgl::test::MeanColor(reference, 118, 118, 20, 20);

	INFO(
		"quad interior: supersampled = " << interior.r << "," << interior.g << "," << interior.b
										 << "  reference = " << expected.r << "," << expected.g
										 << "," << expected.b);

	CHECK(expected.Luma() > 0.1f);
	CHECK(interior.r == Catch::Approx(expected.r).margin(0.02));
	CHECK(interior.g == Catch::Approx(expected.g).margin(0.02));
	CHECK(interior.b == Catch::Approx(expected.b).margin(0.02));

	// MeanColor throws on a box outside the image, so both boxes landing is the size assertion: a
	// 2x capture would have put this one somewhere else entirely.
	CHECK(bgl::test::MeanColor(supersampled, 236, 236, 16, 16).Luma() < 0.02f);
}

// The measurement the whole change is judged by, and the only one here that can say a frame is
// *right* rather than unchanged or self-consistent: mean |delta| against the same frame rendered at
// four times the linear resolution, unjittered and unaccumulated, box-filtered back down. Sixteen
// samples per output pixel.
//
// The camera drifts, because that is the case a render-grid accumulation loses -- re-fetching its
// history at a fractional texel offset every frame and shedding contrast near Nyquist on each one,
// which at half render scale took the measurements that motivated this change past the error of
// drawing nothing at all.
//
// Both sides render at the same scale. What is asked is whether accumulating beats not accumulating
// at a given cost, not whether half a frame beats a whole one.
//
// Two fences, because the answer differs by how fine the content is and the difference is the
// finding. At the render grid's Nyquist -- slats about one render pixel across -- the output-grid
// accumulation has samples to reconstruct from and wins outright. Below it, where the render grid
// never sampled the detail in the first place, no accumulation can invent it: the bound there is
// that it must not be *worse* than drawing nothing, which is exactly the state this replaces.
TEST_CASE(
	"An upscaling resolve beats no antialiasing against a supersampled truth",
	"[taa][render][truth]")
{
	constexpr int   c_TruthScale = 4;
	constexpr float c_HalfScale  = 0.5f;

	constexpr int c_FenceBoxX = 98;
	constexpr int c_FenceBoxY = 98;
	constexpr int c_FenceBox  = 60;

	const auto measure = [&](const std::string& name, ScenePopulator populate) {
		const std::string truth = "assets/golden/taa_truth_" + name + ".got.png";
		const std::string raw   = "assets/golden/taa_truth_" + name + "_raw.got.png";
		const std::string taau  = "assets/golden/taa_truth_" + name + "_taau.got.png";

		RenderTo(
			truth,
			false,
			c_DriftFrames,
			populate,
			DriftingCamera,
			StoppedClock,
			1.0f,
			c_TruthScale);
		RenderTo(raw, false, c_DriftFrames, populate, DriftingCamera, StoppedClock, c_HalfScale);
		RenderTo(taau, true, c_DriftFrames, populate, DriftingCamera, StoppedClock, c_HalfScale);

		const float rawError = bgl::test::MeanAbsDiffToTruth(
			raw,
			truth,
			c_TruthScale,
			c_FenceBoxX,
			c_FenceBoxY,
			c_FenceBox,
			c_FenceBox);

		const float taauError = bgl::test::MeanAbsDiffToTruth(
			taau,
			truth,
			c_TruthScale,
			c_FenceBoxX,
			c_FenceBoxY,
			c_FenceBox,
			c_FenceBox);

		WARN(
			name << " under drift, mean |delta| from the supersampled truth: raw = " << rawError
				 << ", reconstruction = " << taauError);

		return std::pair(rawError, taauError);
	};

	const auto [atNyquistRaw, atNyquistTaau] = measure("nyquist", AddFineFence);
	const auto [belowNyquistRaw, belowTaau]  = measure("subpixel", AddSubPixelFence);

	// Both raw frames have to be wrong in the first place, or every bound below is satisfied by
	// nothing happening.
	CHECK(atNyquistRaw > 0.01f);
	CHECK(belowNyquistRaw > 0.01f);

	// At the render grid's Nyquist the reconstruction has samples to work with, and the margin is
	// what the change buys. Measured at 18% better; the bound is set below that rather than at it,
	// because a figure read off one backend is not a bound on another.
	CHECK(atNyquistTaau < atNyquistRaw * 0.9f);

	// Below it, the bound is only that accumulating is not a loss. Measured at parity, which is the
	// honest ceiling: samples never taken cannot be recovered, and the follow-up that would move
	// this is a wider reconstruction gather, not a resolve knob.
	CHECK(belowTaau <= belowNyquistRaw);
}

// The other end of the same instrument: at full render scale the resolve has always beaten the raw
// frame, and it must still, because the output grid is where it now accumulates and scale 1.0 is the
// case every figure in this file was measured at.
TEST_CASE(
	"A full-scale resolve still beats no antialiasing against the truth",
	"[taa][render][truth]")
{
	const std::string truth = "assets/golden/taa_truth_still.got.png";
	const std::string raw   = "assets/golden/taa_truth_still_raw.got.png";
	const std::string held  = "assets/golden/taa_truth_still_taa.got.png";

	constexpr int c_TruthScale = 4;

	RenderTo(truth, false, 1, AddSubPixelFence, StillCamera, StoppedClock, 1.0f, c_TruthScale);
	RenderTo(raw, false, 1, AddSubPixelFence);
	RenderTo(held, true, c_ConvergeFrames, AddSubPixelFence);

	constexpr int c_FenceBoxX = 98;
	constexpr int c_FenceBoxY = 98;
	constexpr int c_FenceBox  = 60;

	const float rawError = bgl::test::MeanAbsDiffToTruth(
		raw,
		truth,
		c_TruthScale,
		c_FenceBoxX,
		c_FenceBoxY,
		c_FenceBox,
		c_FenceBox);

	const float taaError = bgl::test::MeanAbsDiffToTruth(
		held,
		truth,
		c_TruthScale,
		c_FenceBoxX,
		c_FenceBoxY,
		c_FenceBox,
		c_FenceBox);

	WARN(
		"held, mean |delta| from the supersampled truth: raw = " << rawError
																 << ", resolved = " << taaError);

	CHECK(rawError > 0.01f);
	CHECK(taaError < rawError);
}

// The reconstruction kernel is the one thing about the resolve a viewport can sweep while watching
// a scene, so what it can and cannot reach is worth pinning.
//
// It cannot reach scale 1.0. There is one jitter phase per output pixel there and PhaseWeight
// weighs it against its own mean, so the ratio is one whatever the width is -- which is what keeps
// every figure this file measures independent of the setting.
TEST_CASE(
	"The reconstruction width sharpens an upscale and cannot touch scale 1.0",
	"[taa][render]")
{
	constexpr float c_HalfScale = 0.5f;

	// Either side of the 0.4 the target ships with, and far enough apart that the difference is not
	// the measurement's own noise.
	constexpr float c_Narrow = 0.25f;
	constexpr float c_Wide   = 0.6f;

	SECTION("at scale 1.0 the width changes nothing")
	{
		const std::string narrow = "assets/golden/taa_width_full_narrow.got.png";
		const std::string wide   = "assets/golden/taa_width_full_wide.got.png";

		RenderTo(
			narrow,
			true,
			c_ConvergeFrames,
			AddFineFence,
			StillCamera,
			StoppedClock,
			1.0f,
			1,
			c_Narrow);
		RenderTo(
			wide,
			true,
			c_ConvergeFrames,
			AddFineFence,
			StillCamera,
			StoppedClock,
			1.0f,
			1,
			c_Wide);

		// Byte-exact, not merely close: the two renders differ in one shader constant that the
		// arithmetic cancels, so anything but zero here means it did not cancel.
		CHECK(bgl::test::MatchesGolden(narrow, wide, 0.0f));
	}

	SECTION("at half render scale a narrower kernel resolves more detail")
	{
		const std::string narrow = "assets/golden/taa_width_half_narrow.got.png";
		const std::string wide   = "assets/golden/taa_width_half_wide.got.png";

		RenderTo(
			narrow,
			true,
			c_ConvergeFrames,
			AddFineFence,
			StillCamera,
			StoppedClock,
			c_HalfScale,
			1,
			c_Narrow);
		RenderTo(
			wide,
			true,
			c_ConvergeFrames,
			AddFineFence,
			StillCamera,
			StoppedClock,
			c_HalfScale,
			1,
			c_Wide);

		constexpr int c_FenceBoxX = 98;
		constexpr int c_FenceBoxY = 98;
		constexpr int c_FenceBox  = 60;

		const float narrowDetail =
			bgl::test::AliasEnergy(narrow, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);
		const float wideDetail =
			bgl::test::AliasEnergy(wide, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);

		INFO("fence detail: narrow = " << narrowDetail << ", wide = " << wideDetail);

		// Held and converged, so this is the half of the trade a still frame can see. What the
		// narrow setting costs is the frames a moving pixel waits, which is why 0.4 and not 0.25 is
		// what a target ships with.
		CHECK(narrowDetail > wideDetail);
	}

	SECTION("a width that is not a positive number is the caller's error")
	{
		auto gfx = bgl::CreateGraphics(TestOptions());
		REQUIRE(gfx != nullptr);

		auto targetDesc                   = bgl::RenderTargetDesc();
		targetDesc.width                  = static_cast<int>(c_Width);
		targetDesc.height                 = static_cast<int>(c_Height);
		targetDesc.headless               = true;
		targetDesc.taaEnabled             = true;
		targetDesc.taaReconstructionWidth = 0.0f;

		CHECK_THROWS_AS(gfx->CreateRenderTarget(targetDesc), bgl::GraphicsError);

		targetDesc.taaReconstructionWidth = bgl::RenderTargetDesc().taaReconstructionWidth;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		CHECK_THROWS_AS(target->SetTaaReconstructionWidth(-1.0f), bgl::GraphicsError);

		// The rejected call leaves the target usable at what it had.
		CHECK(target->GetTaaReconstructionWidth() == targetDesc.taaReconstructionWidth);
	}
}

// The accuracy half of the reconstruction width's trade, against the only reference that can judge
// it. HashedAlpha_test measures the other half -- what a width costs a smear -- and the default is a
// choice between the two, so neither is worth reading alone.
//
// Measured at the render grid's Nyquist, because that is the only place a width can matter: below
// it the samples were never taken and every width lands within a percent of every other, and above
// it there is nothing to reconstruct.
TEST_CASE("The reconstruction width is measured against the truth", "[taa][render][truth]")
{
	constexpr int   c_TruthScale = 4;
	constexpr float c_HalfScale  = 0.5f;

	constexpr int c_FenceBoxX = 98;
	constexpr int c_FenceBoxY = 98;
	constexpr int c_FenceBox  = 60;

	const std::string truth = "assets/golden/taa_width_truth.got.png";
	RenderTo(truth, false, 1, AddFineFence, StillCamera, StoppedClock, 1.0f, c_TruthScale);

	const auto errorOf = [&](const std::string& path) {
		return bgl::test::MeanAbsDiffToTruth(
			path,
			truth,
			c_TruthScale,
			c_FenceBoxX,
			c_FenceBoxY,
			c_FenceBox,
			c_FenceBox);
	};

	const std::string raw = "assets/golden/taa_width_truth_raw.got.png";
	RenderTo(raw, false, 1, AddFineFence, StillCamera, StoppedClock, c_HalfScale);

	const float rawError = errorOf(raw);

	float narrowest = 0.0f;
	float widest    = 0.0f;

	for (const float width : { 0.25f, 0.4f, 0.6f, 0.8f, 1.0f })
	{
		const auto        tag = std::to_string(static_cast<int>(width * 100.0f));
		const std::string got = "assets/golden/taa_width_truth_" + tag + ".got.png";

		RenderTo(
			got,
			true,
			c_ConvergeFrames,
			AddFineFence,
			StillCamera,
			StoppedClock,
			c_HalfScale,
			1,
			width);

		const float error = errorOf(got);

		if (width == 0.25f)
			narrowest = error;
		if (width == 1.0f)
			widest = error;

		WARN("reconstruction width " << width << ": mean |delta| from the truth = " << error);
	}

	// The raw frame has to be wrong in the first place, or every figure above is read against noise.
	CHECK(rawError > 0.01f);

	// Every width beats drawing nothing; that is what the accumulation is for, and it must not
	// depend on the setting.
	CHECK(widest < rawError);

	// And the axis runs the way the kernel says it does. Measured 0.0044 against 0.0105, so this
	// pins the direction rather than the gap -- a width that stopped sharpening a held frame would
	// mean the kernel had stopped selecting between phases.
	CHECK(narrowest < widest);
}
