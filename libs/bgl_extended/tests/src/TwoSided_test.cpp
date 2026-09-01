#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
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
	constexpr uint32_t c_W = 256;

	// Renders a plane whose front faces the camera, or whose back does. Same material, same light,
	// same screen position -- only the winding the camera sees differs.
	bgl::test::Rgba
	RenderFacing(const std::string& path, bool backFacing)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto td     = bgl::RenderTargetDesc();
		td.width    = static_cast<int>(c_W);
		td.height   = static_cast<int>(c_W);
		td.headless = true;
		auto target = gfx->CreateRenderTarget(td);

		auto sd                        = bgl::SceneDesc();
		sd.initialGeom                 = 8;
		sd.initialMeshlets             = 512;
		sd.initialSubmeshes            = 8;
		sd.initialVertexBufferByteSize = 800000;
		sd.initialIndices              = 20000;
		sd.initialPbrMaterials         = 8;

		auto scene = gfx->CreateScene(sd);
		auto view  = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(1.0f);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.6f;
		// kMask is two-sided (RasterCullMode::kNone) and alpha 1 discards nothing, so this is a
		// fully opaque plane drawn by a two-sided pipeline -- the shape a hair card is.
		desc.layerType = bgl::LayerType::kMask;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);

		// The two cases must differ ONLY in which side the rasterizer sees, so the back one is the
		// front one turned 180 degrees about an axis lying in the plane. That negates the normal
		// exactly and reverses the winding, and leaves the surface occupying the same screen space.
		//
		// Turning it about Y instead -- the obvious thing -- does not negate a normal that has a Y
		// component: it maps (x, y, z) to (-x, y, -z), so the surface ends up genuinely tilted a
		// different way and the two cases are no longer comparable. That mistake makes the unfixed
		// renderer look correct and the fixed one look broken.
		const float pitch = backFacing ? (-55.0f + 180.0f) : -55.0f;

		view->CreateStaticMeshInstance(
			plane,
			glm::rotate(glm::mat4(1.0f), glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f)));

		auto camera = bgl::Camera();
		camera.LookAt({ 0.0f, 0.0f, 20.0f }, { 0.0f, 0.0f, 19.0f }, { 0.0f, 1.0f, 0.0f })
			.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_W));

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
		return bgl::test::MeanColor(path, 98, 98, 60, 60);
	}
}

// A two-sided pipeline rasterizes the back of a surface with the interpolated normal still pointing
// away from the camera. Unflipped, that sends the view angle, the irradiance lookup and the
// reflection vector into the wrong hemisphere, and the same material shades differently depending on
// which side you happen to see -- which on hair, where cards face both ways through the volume, is
// the grey card-shaped patching that looks like an occlusion failure.
//
// The plane is tilted rather than head-on deliberately: head-on, a flipped normal swaps one
// horizontal direction of the environment for another and the two are similar enough to hide most of
// the error (measured 7%). Tilted, it swaps sky for ground, which is the difference an author sees
// (measured 13%, and a colour shift with it). A hair card is never head-on.
TEST_CASE("A two-sided surface shades the same from either side", "[twosided][render]")
{
	const bgl::test::Rgba front = RenderFacing("assets/golden/twosided_front.got.png", false);
	const bgl::test::Rgba back  = RenderFacing("assets/golden/twosided_back.got.png", true);

	INFO("front = " << front.r << "," << front.g << "," << front.b << " luma " << front.Luma());
	INFO("back  = " << back.r << "," << back.g << "," << back.b << " luma " << back.Luma());

	// The box has to be on the plane, or this compares two patches of background.
	REQUIRE(front.Luma() > 0.05f);

	// 3%: comfortably inside the 13% the unflipped normal produced, and comfortably outside the
	// rounding two paths through the same arithmetic can differ by.
	CHECK(back.Luma() == Catch::Approx(front.Luma()).margin(front.Luma() * 0.03f));

	// Colour as well as brightness -- sampling the opposite hemisphere of an environment shifts hue,
	// and a luma-only check would pass a back face that came back the right brightness and the wrong
	// colour.
	CHECK(back.r == Catch::Approx(front.r).margin(0.03));
	CHECK(back.g == Catch::Approx(front.g).margin(0.03));
	CHECK(back.b == Catch::Approx(front.b).margin(0.03));
}
