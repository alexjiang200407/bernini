#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
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

	// A box well inside the plane's silhouette, so every pixel in it is a coverage decision rather
	// than an edge.
	constexpr int c_BoxX = 88;
	constexpr int c_BoxY = 88;
	constexpr int c_Box  = 80;

	// Several times the resolve's 1/`c_BlendWeight` time constant, so the accumulation has settled
	constexpr int c_ConvergeFrames = 100;

	// The band just below the horizon, where a pixel's footprint on the receding plane is long along
	// the view axis and short across it. Wide and short, because that is the shape of the region --
	// the foreground half is already near-isotropic and would dilute the measurement.
	constexpr int c_GrazeX = 28;
	constexpr int c_GrazeY = 130;
	constexpr int c_GrazeW = 200;
	constexpr int c_GrazeH = 22;

	// The strafe the grazing flicker measurement pans with: about a pixel of screen motion per
	// frame over the band.
	constexpr float c_GrazePanStep   = 0.05f;
	constexpr int   c_GrazePanWarmup = 40;

	// One scene, its target, and the job that keeps drawing it. Held together because a test that
	// captures more than one frame has to drive the same target across them -- a fresh stack per
	// frame would start the accumulation over each time.
	struct PatchScene
	{
		bgl::GraphicsRef     gfx;
		bgl::RenderTargetRef target;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderJob       job;
	};

	bgl::Camera
	GrazingCameraAt(float x)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(x, 1.5f, 0.0f),
				glm::vec3(x, 1.4f, -10.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.1f,
				500.0f);
		return camera;
	}

	// A plane laid flat and viewed almost edge-on, so one pixel spans a long stretch of it along the
	// view axis and almost none across -- the anisotropy a hair card sits in for most of its area.
	PatchScene
	MakeGrazingScene(bgl::LayerType layer, float alpha, bool taaEnabled)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 8;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 8;
		sceneDesc.initialVertexBufferByteSize = 800000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 8;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.6f;
		desc.layerType       = layer;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 400.0f, 400.0f, material);

		// Laid flat, so the camera just above it sees it recede to the horizon.
		view->CreateStaticMeshInstance(
			plane,
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = GrazingCameraAt(0.0f);
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		return PatchScene{ gfx, target, scene, view, job };
	}

	bgl::test::Rgba
	RenderGrazing(const std::string& path, bgl::LayerType layer, float alpha)
	{
		PatchScene grazing = MakeGrazingScene(layer, alpha, false);

		grazing.gfx->DrawFrame(grazing.target, grazing.job);
		grazing.gfx->ScreenshotPng(grazing.target, path);
		return bgl::test::MeanColor(path, c_GrazeX, c_GrazeY, c_GrazeW, c_GrazeH);
	}

	// The temporal counterpart of RenderGrazing: the same plane drawn under TAA for `warmup` frames
	// with the camera strafing `panStep` sideways each frame -- zero holds it still -- then two
	// consecutive frames captured and differenced over the grazing band.
	float
	GrazingFrameDelta(
		const std::string& pathA,
		const std::string& pathB,
		bgl::LayerType     layer,
		float              alpha,
		int                warmup,
		float              panStep)
	{
		PatchScene grazing = MakeGrazingScene(layer, alpha, true);

		for (int frame = 0; frame < warmup; ++frame)
		{
			grazing.job.camera = GrazingCameraAt(-static_cast<float>(warmup - 1 - frame) * panStep);
			grazing.gfx->DrawFrame(grazing.target, grazing.job);
		}

		grazing.gfx->ScreenshotPng(grazing.target, pathA);

		grazing.job.camera = GrazingCameraAt(panStep);
		grazing.gfx->DrawFrame(grazing.target, grazing.job);
		grazing.gfx->ScreenshotPng(grazing.target, pathB);

		return bgl::test::FrameDelta(pathA, pathB, c_GrazeX, c_GrazeY, c_GrazeW, c_GrazeH);
	}

	// One plane at `alpha` facing the camera, filling the middle of the frame.
	PatchScene
	MakePatchScene(bgl::LayerType layer, float alpha, bool taaEnabled, float planeSize = 12.0f)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 8;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 8;
		sceneDesc.initialVertexBufferByteSize = 800000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 8;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);

		// PBR does not render without an environment; there is no default.
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.6f;
		desc.layerType       = layer;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, planeSize, planeSize, material);
		view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		return PatchScene{ gfx, target, scene, view, job };
	}

	// Renders one plane at `alpha` with the given layer, `frames` times, and returns the mean colour
	// of the patch. The background is black, so a discarded fragment contributes nothing and the mean
	// over the patch is the surviving fraction times what a fully covered patch would read.
	bgl::test::Rgba
	RenderPatch(
		const std::string& path,
		bgl::LayerType     layer,
		float              alpha,
		bool               taaEnabled = false,
		int                frames     = 1)
	{
		PatchScene patch = MakePatchScene(layer, alpha, taaEnabled);

		for (int frame = 0; frame < frames; ++frame)
		{
			patch.gfx->DrawFrame(patch.target, patch.job);
		}

		patch.gfx->ScreenshotPng(patch.target, path);
		return bgl::test::MeanColor(path, c_BoxX, c_BoxY, c_Box, c_Box);
	}

	// Sideways camera slide per frame while panning over the patch, and how long the accumulation is
	// given to reach its moving steady state. The travel it adds up to must stay inside the margin a
	// `planeSize` of 48 leaves around the frame, so the sample box never sees the plane's edge.
	constexpr float c_PanStep   = 0.2f;
	constexpr int   c_PanWarmup = 40;
	constexpr float c_PanPlane  = 48.0f;

	bgl::Camera
	PanCameraAt(float x)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(x, 0.0f, 20.0f),
				glm::vec3(x, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);
		return camera;
	}

	// The moving counterpart of ConsecutiveFrameDelta: the camera slides throughout, including
	// between the two captures, so what separates them is flicker under motion.
	float
	PanningFrameDelta(
		const std::string& pathA,
		const std::string& pathB,
		bgl::LayerType     layer,
		float              alpha)
	{
		PatchScene patch = MakePatchScene(layer, alpha, true, c_PanPlane);

		for (int frame = 0; frame < c_PanWarmup; ++frame)
		{
			patch.job.camera =
				PanCameraAt(-static_cast<float>(c_PanWarmup - 1 - frame) * c_PanStep);
			patch.gfx->DrawFrame(patch.target, patch.job);
		}

		patch.gfx->ScreenshotPng(patch.target, pathA);

		patch.job.camera = PanCameraAt(c_PanStep);
		patch.gfx->DrawFrame(patch.target, patch.job);
		patch.gfx->ScreenshotPng(patch.target, pathB);

		return bgl::test::FrameDelta(pathA, pathB, c_BoxX, c_BoxY, c_Box, c_Box);
	}

	// Draws `warmup` frames, captures, draws one more and captures again -- two consecutive frames of
	// one accumulation, with the camera never moving. What separates them is flicker and nothing else.
	float
	ConsecutiveFrameDelta(
		const std::string& pathA,
		const std::string& pathB,
		bgl::LayerType     layer,
		float              alpha,
		int                warmup)
	{
		PatchScene patch = MakePatchScene(layer, alpha, true);

		for (int frame = 0; frame < warmup; ++frame)
		{
			patch.gfx->DrawFrame(patch.target, patch.job);
		}

		patch.gfx->ScreenshotPng(patch.target, pathA);

		patch.gfx->DrawFrame(patch.target, patch.job);
		patch.gfx->ScreenshotPng(patch.target, pathB);

		return bgl::test::FrameDelta(pathA, pathB, c_BoxX, c_BoxY, c_Box, c_Box);
	}
}

// The contract, and the whole reason the technique works: a fragment survives with probability equal
// to its alpha. Measured against the same material at kOpaque, which is what full coverage reads --
// so this is a ratio and owes nothing to the shading, the exposure or the tonemap.
//
// Two alphas, because a single one could be matched by a threshold that ignored alpha and happened
// to land near it.
TEST_CASE(
	"Hashed alpha keeps the fraction of fragments its alpha asks for",
	"[hashedalpha][render]")
{
	const bgl::test::Rgba full =
		RenderPatch("assets/golden/hashed_alpha_opaque.got.png", bgl::LayerType::kOpaque, 1.0f);

	// Well clear of the background, or every ratio below divides by noise.
	REQUIRE(full.Luma() > 0.1f);

	const float alphas[] = { 0.25f, 0.75f };
	for (const float alpha : alphas)
	{
		const std::string path =
			std::format("assets/golden/hashed_alpha_{}.got.png", static_cast<int>(alpha * 100.0f));

		const bgl::test::Rgba patch = RenderPatch(path, bgl::LayerType::kHashed, alpha);
		const float           ratio = patch.Luma() / full.Luma();

		INFO("alpha = " << alpha << ", survived = " << ratio);

		// One frame of a stochastic pattern over 80x80 pixels: the sampling error on the mean is a
		// few percent, and the margin is what separates "equals alpha" from "correlates with it".
		CHECK(ratio == Catch::Approx(alpha).margin(0.06));
	}
}

// A pattern that did not move between frames would leave temporal AA converging to one sample of the
// distribution rather than through it -- the noise would resolve to itself and stay. The seed is what
// moves it, and it only moves on a target that has an accumulation to integrate it.
TEST_CASE("The hashed pattern changes between frames, but only under TAA", "[hashedalpha][render]")
{
	const std::string first  = "assets/golden/hashed_alpha_seed0.got.png";
	const std::string second = "assets/golden/hashed_alpha_seed1.got.png";

	SECTION("with temporal AA the pattern advances")
	{
		// Two frames rather than one, so the second sees a different seed. Compared before the
		// accumulation has had time to smooth them together.
		RenderPatch(first, bgl::LayerType::kHashed, 0.5f, true, 1);
		RenderPatch(second, bgl::LayerType::kHashed, 0.5f, true, 2);

		CHECK_FALSE(bgl::test::MatchesGolden(first, second));
	}

	SECTION("without it the pattern is still")
	{
		// A moving pattern with nothing accumulating it is flicker, so a target without TAA must
		// draw the same fragments every frame.
		RenderPatch(first, bgl::LayerType::kHashed, 0.5f, false, 1);
		RenderPatch(second, bgl::LayerType::kHashed, 0.5f, false, 2);

		CHECK(bgl::test::MatchesGolden(first, second));
	}
}

// The claim that makes the technique usable at all: the noise is something TAA integrates away. A
// converged patch must be far smoother than a single frame of the same pattern, and must still be
// partial coverage rather than having drifted to opaque or to nothing.
TEST_CASE("Temporal AA resolves the hashed noise", "[hashedalpha][render]")
{
	const std::string single    = "assets/golden/hashed_alpha_single.got.png";
	const std::string converged = "assets/golden/hashed_alpha_converged.got.png";

	const bgl::test::Rgba full =
		RenderPatch("assets/golden/hashed_alpha_full.got.png", bgl::LayerType::kOpaque, 1.0f);

	const bgl::test::Rgba one = RenderPatch(single, bgl::LayerType::kHashed, 0.5f, true, 1);
	const bgl::test::Rgba many =
		RenderPatch(converged, bgl::LayerType::kHashed, 0.5f, true, c_ConvergeFrames);

	const float noisy  = bgl::test::AliasEnergy(single, c_BoxX, c_BoxY, c_Box, c_Box);
	const float smooth = bgl::test::AliasEnergy(converged, c_BoxX, c_BoxY, c_Box, c_Box);

	INFO("patch grain: one frame = " << noisy << ", converged = " << smooth);
	INFO(
		"patch luma:  one frame = " << one.Luma() << ", converged = " << many.Luma()
									<< ", full = " << full.Luma());

	// A single frame has to be grainy in the first place, or there is nothing to resolve and the
	// comparison proves nothing.
	CHECK(noisy > 1e-3f);
	CHECK(smooth < noisy * 0.5f);

	// Still half-covered: converging to full coverage would mean the threshold stopped discarding,
	// and converging to nothing would mean it stopped keeping.
	CHECK(many.Luma() < full.Luma() * 0.95f);
	CHECK(many.Luma() > full.Luma() * 0.05f);

	// The converged patch reads *brighter* than one frame of the same coverage, and should. A single
	// frame is half black pixels and half lit ones, and the mean of their display values is below the
	// display value of their mean -- the curve is concave, so averaging in linear before it lands
	// higher. Asserting the two were equal would be asserting the tonemap is linear.
	CHECK(many.Luma() > one.Luma());
}

// Smooth is not the same as still. A patch can measure flat across its pixels every frame and be a
// different flat patch each time, which is what flicker is -- and the spatial measure above scores
// that as a success. This is the temporal half: with the camera fixed and the accumulation warm, two
// consecutive frames have to agree.
//
// The opaque patch is the floor. It draws the same fragments every frame and differs only by the
// jitter, so whatever it scores is what "still" costs here; hashed alpha is asked to come within a
// small multiple of it rather than to reach zero.
TEST_CASE("A converged hashed patch stops changing between frames", "[hashedalpha][render]")
{
	const float opaque = ConsecutiveFrameDelta(
		"assets/golden/hashed_alpha_still_opaque_a.got.png",
		"assets/golden/hashed_alpha_still_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f,
		c_ConvergeFrames);

	const float hashed = ConsecutiveFrameDelta(
		"assets/golden/hashed_alpha_still_a.got.png",
		"assets/golden/hashed_alpha_still_b.got.png",
		bgl::LayerType::kHashed,
		0.5f,
		c_ConvergeFrames);

	INFO("frame-to-frame delta: opaque = " << opaque << ", hashed = " << hashed);

	// The instrument has to be able to read zero, or the bound below is measuring its own noise floor.
	REQUIRE(opaque < 1e-5f);

	// Measured 0.0049 with a hash cell one to two pixels wide, 0.0020 once it is sub-pixel, and 0.0013
	// at a blend weight of 0.05. The bound sits between the last two, so neither the correlated pattern
	// nor a weight raised back to 0.1 can return unnoticed.
	CHECK(hashed < 1.6e-3f);
}

// The moving half of the flicker contract. A pixel where the hash discarded this frame's fragment
// carries whatever velocity was written behind the surface -- nothing, for a background at rest --
// while its history carries the surface, and reprojecting the one by the other snaps the
// accumulation every frame. The camera never exposes new geometry during the pan, so the patch
// interior should change no more than the same pan over an opaque surface, give or take the noise
// the coverage adds.
//
// The opaque pan is the floor rather than a fixed number because a moving camera changes the image
// legitimately -- specular shift, jitter -- and that cost has to be read out of the hashed figure.
TEST_CASE("A pan does not flicker a converged hashed patch", "[hashedalpha][render]")
{
	const float opaque = PanningFrameDelta(
		"assets/golden/hashed_alpha_pan_opaque_a.got.png",
		"assets/golden/hashed_alpha_pan_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f);

	const float hashed = PanningFrameDelta(
		"assets/golden/hashed_alpha_pan_a.got.png",
		"assets/golden/hashed_alpha_pan_b.got.png",
		bgl::LayerType::kHashed,
		0.5f);

	INFO("panning frame-to-frame delta: opaque = " << opaque << ", hashed = " << hashed);

	// The floor has to be small, or the bound below hides the flicker inside legitimate motion.
	REQUIRE(opaque < 1e-5f);

	// Measured 0.0029, about twice the still figure -- motion adds sub-pixel reprojection phase the
	// still case never sees. What the bound guards against is reprojection changes that shake the
	// accumulation: dilating the motion vector to the longest in the 3x3 measured 0.0040-0.0073
	// here, because on stochastic coverage it re-samples the noise field at a fresh fractional
	// offset every frame.
	CHECK(hashed < 3.5e-3f);
}

// The temporal contract at the anisotropy a hair card sits in. Head-on, the hash cells are
// near-isotropic and the test above already pins the flicker; at grazing incidence the cells are
// clamped by c_MaxAnisotropy, and if that leaves them wider than a pixel across the compressed
// axis, neighbouring pixels share a threshold, the clamp's box collapses, and the resolve snaps
// the accumulation back onto the noise -- flicker that only exists at grazing angles, which is why
// the head-on test cannot catch it.
//
// Measured at c_MaxAnisotropy = 4: still = 0.0097, panning = 0.0134 -- seven times the head-on
// figure. At 2, an octave finer -- one to two pixels across the compressed axis: 0.0011 and
// 0.0024, parity with head-on. The bounds sit between the two states.
TEST_CASE("A converged hashed surface stays still at grazing angles", "[hashedalpha][render]")
{
	const float opaqueStill = GrazingFrameDelta(
		"assets/golden/hashed_grazing_still_opaque_a.got.png",
		"assets/golden/hashed_grazing_still_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f,
		c_ConvergeFrames,
		0.0f);
	const float hashedStill = GrazingFrameDelta(
		"assets/golden/hashed_grazing_still_a.got.png",
		"assets/golden/hashed_grazing_still_b.got.png",
		bgl::LayerType::kHashed,
		0.5f,
		c_ConvergeFrames,
		0.0f);
	const float opaquePan = GrazingFrameDelta(
		"assets/golden/hashed_grazing_pan_opaque_a.got.png",
		"assets/golden/hashed_grazing_pan_opaque_b.got.png",
		bgl::LayerType::kOpaque,
		1.0f,
		c_GrazePanWarmup,
		c_GrazePanStep);
	const float hashedPan = GrazingFrameDelta(
		"assets/golden/hashed_grazing_pan_a.got.png",
		"assets/golden/hashed_grazing_pan_b.got.png",
		bgl::LayerType::kHashed,
		0.5f,
		c_GrazePanWarmup,
		c_GrazePanStep);

	INFO(
		"grazing flicker: opaqueStill = " << opaqueStill << ", hashedStill = " << hashedStill
										  << ", opaquePan = " << opaquePan
										  << ", hashedPan = " << hashedPan);

	// The instrument has to read zero on a surface that draws the same fragments every frame, or
	// the bounds below measure the harness rather than the hash.
	REQUIRE(opaqueStill < 1e-5f);
	REQUIRE(opaquePan < 1e-5f);

	CHECK(hashedStill < 2.5e-3f);
	CHECK(hashedPan < 5.0e-3f);
}

// A hash cell is isotropic in world space; the projection is not. Sizing it off the larger screen
// derivative alone leaves it many pixels wide along the compressed axis, so a whole streak of pixels
// shares one threshold -- coverage stops being independent per pixel and becomes a blotch. It reads
// as an occlusion failure rather than as noise, and no amount of temporal accumulation removes it,
// because it does not move.
//
// Blotchiness is the discriminator, not variance: a correlated pattern and a fine one can have the
// same mean and the same spread while differing entirely in how much they change between neighbours.
// That is what AliasEnergy measures.
TEST_CASE("The hashed pattern stays per-pixel at grazing angles", "[hashedalpha][render]")
{
	const std::string grazing = "assets/golden/hashed_alpha_grazing.got.png";
	const std::string flat    = "assets/golden/hashed_alpha_grazing_flat.got.png";

	const bgl::test::Rgba covered = RenderGrazing(flat, bgl::LayerType::kOpaque, 1.0f);
	const bgl::test::Rgba patch   = RenderGrazing(grazing, bgl::LayerType::kHashed, 0.5f);

	// The box has to be on the surface, or everything below measures the background.
	REQUIRE(covered.Luma() > 0.1f);

	const float ratio = patch.Luma() / covered.Luma();
	const float grain = bgl::test::AliasEnergy(grazing, c_GrazeX, c_GrazeY, c_GrazeW, c_GrazeH);

	INFO("grazing: survived = " << ratio << ", grain = " << grain);

	// Correlated cells bias coverage as well as clumping it: sizing the cell off the larger
	// derivative alone measures 0.57 here, and bounding the ratio brings it back to 0.51.
	CHECK(ratio == Catch::Approx(0.5f).margin(0.05));

	// And it has to be per-pixel. Measured: 0.027 with the cell sized off the larger derivative
	// alone, 0.100 with the ratio bounded at 4, and 0.34 bounded at 2. The floor sits between the
	// correlated figure and the bounded ones with room either side.
	CHECK(grain > 0.06f);
}
