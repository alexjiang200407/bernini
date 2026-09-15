#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace
{
	constexpr uint32_t c_W = 256;

	struct FacingCase
	{
		bool           backFacing  = false;
		bool           doubleSided = true;
		bgl::LayerType layer       = bgl::LayerType::kMask;
		// A red wall behind the plane, so whatever of the plane is not drawn shows red.
		bool occluded = false;
		// Draws one frame, then rewrites the material's sidedness before the frame that is captured.
		std::optional<bool> doubleSidedAfterFirstFrame;
	};

	// Renders a plane whose front faces the camera, or whose back does. Same material, same light,
	// same screen position -- only the winding the camera sees differs.
	bgl::test::Rgba
	RenderFacing(const std::string& path, const FacingCase& facing)
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
		// kMask with alpha 1 discards nothing, so by default this is a fully opaque plane drawn by a
		// cut-out pipeline -- the shape a hair card is.
		desc.layerType   = facing.layer;
		desc.doubleSided = facing.doubleSided;

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
		const float pitch = facing.backFacing ? (-55.0f + 180.0f) : -55.0f;

		view->CreateStaticMeshInstance(
			plane,
			glm::rotate(glm::mat4(1.0f), glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f)));

		if (facing.occluded)
		{
			auto wallDesc            = bgl::PbrMaterialDesc();
			wallDesc.baseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
			wallDesc.metallicFactor  = 0.0f;
			wallDesc.roughnessFactor = 0.6f;
			wallDesc.layerType       = bgl::LayerType::kOpaque;

			// Facing the camera and wider than the frustum at its depth, behind the plane's deepest
			// corner (12 * sin 55 / 2 = 4.9).
			auto wall = scene->AddPlaneGeom(1, 1, 60.0f, 60.0f, scene->CreatePbrMaterial(wallDesc));
			view->CreateStaticMeshInstance(
				wall,
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -8.0f)));
		}

		auto camera = bgl::Camera();
		camera.LookAt({ 0.0f, 0.0f, 20.0f }, { 0.0f, 0.0f, 19.0f }, { 0.0f, 1.0f, 0.0f })
			.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_W));

		if (facing.doubleSidedAfterFirstFrame)
		{
			gfx->DrawFrame(target, job);
			desc.doubleSided = *facing.doubleSidedAfterFirstFrame;
			scene->UpdatePbrMaterial(material, desc);
		}

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
	const bgl::test::Rgba front = RenderFacing("assets/golden/twosided_front.got.png", {});
	const bgl::test::Rgba back =
		RenderFacing("assets/golden/twosided_back.got.png", { .backFacing = true });

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

// A single-sided material's back faces never reach the rasterizer: the mesh stage collapses them,
// so from behind the plane the frame holds only the background. The pipeline is the same two-sided
// one either way -- what changes is the material. Opaque and mask draw through the static mesh
// stage; the blend bucket is checked on its own because it draws through the tier-branching AnyMesh
// stage rather than the static one.
TEST_CASE(
	"A single-sided surface is culled from behind and drawn from the front",
	"[twosided][render]")
{
	const auto layer =
		GENERATE(bgl::LayerType::kOpaque, bgl::LayerType::kMask, bgl::LayerType::kBlend);
	INFO("layer " << static_cast<int>(layer));

	const bgl::test::Rgba front = RenderFacing(
		"assets/golden/onesided_front.got.png",
		{ .doubleSided = false, .layer = layer });
	const bgl::test::Rgba back = RenderFacing(
		"assets/golden/onesided_back.got.png",
		{ .backFacing = true, .doubleSided = false, .layer = layer });
	const bgl::test::Rgba bothBack = RenderFacing(
		"assets/golden/onesided_ref_back.got.png",
		{ .backFacing = true, .doubleSided = true, .layer = layer });

	INFO(
		"front = " << front.Luma() << ", back = " << back.Luma()
				   << ", two-sided back = " << bothBack.Luma());

	// The front is drawn: the same patch a two-sided plane shows from behind.
	REQUIRE(front.Luma() > 0.05f);

	// From behind, nothing of it: the patch reads far darker than the surface a two-sided
	// material puts there, which pins the sign of the facing test -- a flipped one would draw the
	// back and cull the front.
	CHECK(back.Luma() < bothBack.Luma() * 0.5f);
}

// An opaque wall asked to be two-sided is a wall from both sides: its back face is rasterized, writes
// depth and hides what stands behind it, exactly as its front does. The single-sided render is the
// instrument's guard -- it proves the red wall really is behind the plane, so a back face that failed
// to draw would show red rather than a background that happens to match.
TEST_CASE(
	"A two-sided opaque surface hides what is behind it from either side",
	"[twosided][render]")
{
	const bgl::test::Rgba front = RenderFacing(
		"assets/golden/twosided_opaque_front.got.png",
		{ .layer = bgl::LayerType::kOpaque, .occluded = true });
	const bgl::test::Rgba back = RenderFacing(
		"assets/golden/twosided_opaque_back.got.png",
		{ .backFacing = true, .layer = bgl::LayerType::kOpaque, .occluded = true });
	const bgl::test::Rgba culled = RenderFacing(
		"assets/golden/onesided_opaque_occluded.got.png",
		{ .backFacing  = true,
	      .doubleSided = false,
	      .layer       = bgl::LayerType::kOpaque,
	      .occluded    = true });

	INFO("front = " << front.r << "," << front.g << "," << front.b);
	INFO("back  = " << back.r << "," << back.g << "," << back.b);
	INFO("culled back = " << culled.r << "," << culled.g << "," << culled.b);

	REQUIRE(culled.r > culled.g * 2.0f);

	// The plane is white and the wall red, so any of the wall showing through pulls green and blue
	// away from the front's.
	CHECK(back.r == Catch::Approx(front.r).margin(0.03));
	CHECK(back.g == Catch::Approx(front.g).margin(0.03));
	CHECK(back.b == Catch::Approx(front.b).margin(0.03));
}

// Sidedness is read off the material record every frame, so rewriting it on a live material changes
// what the very next frame draws -- with no geometry re-upload and no rebinding. Each direction is
// checked, from a material that already drew a frame the other way.
TEST_CASE(
	"Rewriting an opaque material's sidedness takes effect on the next frame",
	"[twosided][render]")
{
	const bgl::test::Rgba nowSingle = RenderFacing(
		"assets/golden/twosided_toggle_to_single.got.png",
		{ .backFacing                 = true,
	      .doubleSided                = true,
	      .layer                      = bgl::LayerType::kOpaque,
	      .doubleSidedAfterFirstFrame = false });
	const bgl::test::Rgba nowDouble = RenderFacing(
		"assets/golden/twosided_toggle_to_double.got.png",
		{ .backFacing                 = true,
	      .doubleSided                = false,
	      .layer                      = bgl::LayerType::kOpaque,
	      .doubleSidedAfterFirstFrame = true });

	INFO(
		"back after -> single = " << nowSingle.Luma()
								  << ", back after -> double = " << nowDouble.Luma());

	CHECK(nowDouble.Luma() > 0.05f);
	CHECK(nowSingle.Luma() < nowDouble.Luma() * 0.5f);
}
