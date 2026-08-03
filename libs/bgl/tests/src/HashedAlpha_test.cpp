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

	constexpr int c_ConvergeFrames = 24;

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
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);
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

		for (int frame = 0; frame < frames; ++frame)
		{
			gfx->DrawFrame(target, job);
		}

		gfx->ScreenshotPng(target, path);
		return bgl::test::MeanColor(path, c_BoxX, c_BoxY, c_Box, c_Box);
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
