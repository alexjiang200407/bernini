#include "scene/SceneView.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MeshInstanceFlag.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/MeshInstanceFlags.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

/**
 * A placement's flags word, proven at the pixel.
 *
 * A black cube sits on a flat white ground at the origin, and the camera looks down at it from
 * (0, 8, 14). One sample box lands on the cube's front face, another on the ground just to its
 * right, inside the blob shadow the cube casts. Every capture is compared against another capture
 * of the same scene, never a stored PNG.
 */

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	// The cube spans [-1, 1] on x and z and [0, 2] on y; its front face projects to about
	// x 366..434, y 255..317, so (400, 285) is well inside it.
	constexpr int c_CubeX = 400;
	constexpr int c_CubeY = 285;

	// Ground at (1.6, 0, 0): beside the cube and clear of its silhouette, inside its blob shadow.
	constexpr int c_GroundX = 452;
	constexpr int c_GroundY = 300;

	constexpr int c_Box = 8;

	// The plane geoms are authored in XY; this lays one flat with its normal up.
	const glm::mat4 c_Flat =
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST_CASE("A hidden placement draws nothing and casts nothing", "[meshinstanceflags][render]")
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

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 128;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 100000;
	sceneDesc.initialIndices              = 4000;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	// PBR does not render without an environment; there is no default.
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto whiteDesc            = bgl::PbrMaterialDesc();
	whiteDesc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	whiteDesc.metallicFactor  = 0.0f;
	whiteDesc.roughnessFactor = 1.0f;

	auto blackDesc            = whiteDesc;
	blackDesc.baseColorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

	const auto groundGeom =
		scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, scene->CreatePbrMaterial(whiteDesc));
	const auto cubeGeom = scene->AddCubeGeom(scene->CreatePbrMaterial(blackDesc));

	const auto groundInstance = view->CreateStaticMeshInstance(groundGeom, c_Flat);
	const auto cube           = view->CreateStaticMeshInstance(
		cubeGeom,
		glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 8.0f, 14.0f),
			glm::vec3(0.0f, 0.0f, 0.0f),
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

	const auto sample = [&](const char* name, int x, int y) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		const bgl::test::Rgba box =
			bgl::test::MeanColor(path, x - c_Box / 2, y - c_Box / 2, c_Box, c_Box);

		std::filesystem::remove(path);
		return box.Luma();
	};

	const auto hidden = bgl::MeshInstanceFlags(bgl::MeshInstanceFlag::kHidden);

	SECTION("hiding takes the placement off screen and unhiding puts it back")
	{
		const float shown = sample("bernini_flags_shown", c_CubeX, c_CubeY);

		view->SetMeshInstanceFlags(cube, hidden);
		const float gone = sample("bernini_flags_hidden", c_CubeX, c_CubeY);

		// The black face gives way to the lit white ground behind it.
		CHECK(gone > shown + 0.3f);

		view->SetMeshInstanceFlags(cube, bgl::MeshInstanceFlags());
		const float back = sample("bernini_flags_unhidden", c_CubeX, c_CubeY);
		CHECK(std::abs(back - shown) < 0.02f);
	}

	SECTION("a hidden placement's blob shadow is not drawn")
	{
		const float base = sample("bernini_flags_blob_base", c_GroundX, c_GroundY);
		REQUIRE(base > 0.05f);

		// Wide, strong and slow to fade, so the sample 1.6 to the side and 1.0 below the cube's
		// origin sits in the disc's dark core: about 0.6 alpha there, which the display transform
		// still leaves a clear drop.
		auto desc       = bgl::BlobShadowDesc();
		desc.radius     = 4.0f;
		desc.intensity  = 1.0f;
		desc.fadeHeight = 8.0f;
		view->SetBlobShadow(cube, desc);

		const float shadowed = sample("bernini_flags_blob_shadowed", c_GroundX, c_GroundY);
		REQUIRE(shadowed < base * 0.92f);

		// Without TAA both captures are deterministic, so the shadow's absence reads as the base
		// itself rather than as merely lighter.
		view->SetMeshInstanceFlags(cube, hidden);
		const float cleared = sample("bernini_flags_blob_hidden", c_GroundX, c_GroundY);
		CHECK(cleared > base * 0.98f);

		view->SetMeshInstanceFlags(cube, bgl::MeshInstanceFlags());
		const float returned = sample("bernini_flags_blob_unhidden", c_GroundX, c_GroundY);
		CHECK(returned < base * 0.92f);
	}

	SECTION("a hidden placement leaves the selection list and returns to it")
	{
		auto* sceneView = view->As<bgl::SceneView>();
		REQUIRE(sceneView != nullptr);

		view->SetSubmeshSelected(cube, 0, true);
		CHECK(sceneView->GetSelectedInstances().size() == 1);

		// The outline is drawn off this list, so a hidden selected placement draws no contour.
		view->SetMeshInstanceFlags(cube, hidden);
		CHECK(sceneView->GetSelectedInstances().empty());

		// The selection itself survives the hide.
		CHECK(view->IsSubmeshSelected(cube, 0));

		view->SetMeshInstanceFlags(cube, bgl::MeshInstanceFlags());
		CHECK(sceneView->GetSelectedInstances().size() == 1);
	}

	SECTION("the word round-trips and refuses a dead handle")
	{
		CHECK(view->GetMeshInstanceFlags(cube).empty());

		view->SetMeshInstanceFlags(cube, hidden);
		CHECK(view->GetMeshInstanceFlags(cube) == hidden);

		view->SetMeshInstanceFlags(cube, bgl::MeshInstanceFlags());
		CHECK(view->GetMeshInstanceFlags(cube).empty());

		CHECK_THROWS_AS(
			view->SetMeshInstanceFlags(bgl::MeshInstanceHandle(), hidden),
			bgl::SceneError);
		CHECK_THROWS_AS(view->GetMeshInstanceFlags(bgl::MeshInstanceHandle()), bgl::SceneError);

		view->DeleteMeshInstance(cube);
		CHECK_THROWS_AS(view->SetMeshInstanceFlags(cube, hidden), bgl::SceneError);
		CHECK_THROWS_AS(view->GetMeshInstanceFlags(cube), bgl::SceneError);
	}

	(void)groundInstance;
}
