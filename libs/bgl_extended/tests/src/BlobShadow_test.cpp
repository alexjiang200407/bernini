#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>

/**
 * The blob shadow, proven at the pixel.
 *
 * A white plane lies flat as the ground (SetGround refuses any normal that does not point up), a
 * small caster hovers over the origin, and the camera looks down at it from (0, 8, 14). The disc
 * lands on the ground around the origin; a sample box beside the caster reads its darkening.
 *
 * Every capture is compared against another capture of the same scene, never against a stored
 * PNG, so there is no golden to regenerate when the lighting changes.
 */

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	// The origin projects to the exact centre of the frame, and the disc around it spans roughly
	// +-60 px by +-25 px on screen. The caster hovers above the ground, so it lands *above* the
	// disc's centre (~y=286 when grounded, higher when lifted); the box sits just below centre,
	// in the disc's dark core and clear of the caster at every height the sections use.
	constexpr int c_SampleSize = 20;
	constexpr int c_SampleX    = 400 - c_SampleSize / 2;
	constexpr int c_SampleY    = 308 - c_SampleSize / 2;

	constexpr float c_Radius     = 3.0f;
	constexpr float c_Intensity  = 0.9f;
	constexpr float c_FadeHeight = 2.0f;

	// The plane geoms are authored in XY; this lays one flat with its normal up.
	const glm::mat4 c_Flat =
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	glm::mat4
	Lifted(float y)
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, y, 0.0f)) * c_Flat;
	}
}

TEST_CASE("A blob shadow darkens the ground under its placement", "[blobshadow][render]")
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

	scene->SetGround(bgl::GroundPlaneDesc());

	auto whiteDesc            = bgl::PbrMaterialDesc();
	whiteDesc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	whiteDesc.metallicFactor  = 0.0f;
	whiteDesc.roughnessFactor = 1.0f;

	const auto white = scene->CreatePbrMaterial(whiteDesc);

	const auto groundGeom = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, white);
	const auto casterGeom = scene->AddPlaneGeom(1, 1, 0.5f, 0.5f, white);

	const auto groundInstance = view->CreateStaticMeshInstance(groundGeom, c_Flat);
	const auto caster         = view->CreateStaticMeshInstance(casterGeom, Lifted(0.5f));

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

	const auto sample = [&](const char* name) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		const bgl::test::Rgba box =
			bgl::test::MeanColor(path, c_SampleX, c_SampleY, c_SampleSize, c_SampleSize);

		std::filesystem::remove(path);
		return box.Luma();
	};

	auto desc       = bgl::BlobShadowDesc();
	desc.radius     = c_Radius;
	desc.intensity  = c_Intensity;
	desc.fadeHeight = c_FadeHeight;

	SECTION("the disc appears, fades with height, and clears")
	{
		const float base = sample("bernini_blob_base");

		// The box hits the lit ground: background would read ~0 and make the rest vacuous.
		REQUIRE(base > 0.05f);

		view->SetBlobShadow(caster, desc);
		const float grounded = sample("bernini_blob_grounded");
		CHECK(grounded < base * 0.88f);

		// Half the fade height: fainter than grounded, still darker than bare ground.
		view->SetInstanceTransform(caster, Lifted(c_FadeHeight * 0.5f));
		const float lifted = sample("bernini_blob_lifted");
		CHECK(lifted > grounded * 1.04f);
		CHECK(lifted < base * 0.98f);

		// Past the fade height the disc is gone entirely.
		view->SetInstanceTransform(caster, Lifted(c_FadeHeight * 2.5f));
		const float above = sample("bernini_blob_above");
		CHECK(above > base * 0.95f);

		view->SetInstanceTransform(caster, Lifted(0.5f));
		view->ClearBlobShadow(caster);
		const float cleared = sample("bernini_blob_cleared");
		CHECK(cleared > base * 0.95f);
	}

	SECTION("the record round-trips, refuses a bad desc, and dies with its placement")
	{
		CHECK_FALSE(view->GetBlobShadow(caster).has_value());

		// Clearing a placement that carries none is a no-op, not an error.
		view->ClearBlobShadow(caster);

		view->SetBlobShadow(caster, desc);

		const auto stored = view->GetBlobShadow(caster);
		REQUIRE(stored.has_value());
		CHECK(stored->radius == c_Radius);
		CHECK(stored->intensity == c_Intensity);
		CHECK(stored->fadeHeight == c_FadeHeight);

		auto bad   = desc;
		bad.radius = 0.0f;
		CHECK_THROWS_AS(view->SetBlobShadow(caster, bad), bgl::SceneError);

		bad        = desc;
		bad.radius = -1.0f;
		CHECK_THROWS_AS(view->SetBlobShadow(caster, bad), bgl::SceneError);

		bad           = desc;
		bad.intensity = 1.5f;
		CHECK_THROWS_AS(view->SetBlobShadow(caster, bad), bgl::SceneError);

		bad            = desc;
		bad.fadeHeight = 0.0f;
		CHECK_THROWS_AS(view->SetBlobShadow(caster, bad), bgl::SceneError);

		// A refused write leaves the stored record untouched.
		CHECK(view->GetBlobShadow(caster)->radius == c_Radius);

		CHECK_THROWS_AS(view->SetBlobShadow(bgl::MeshInstanceHandle(), desc), bgl::SceneError);
		CHECK_THROWS_AS(view->ClearBlobShadow(bgl::MeshInstanceHandle()), bgl::SceneError);
		CHECK_THROWS_AS(view->GetBlobShadow(bgl::MeshInstanceHandle()), bgl::SceneError);

		// Deleting the placement takes its disc with it: the next frame draws without a crash and
		// without darkening where the disc was.
		const float before = sample("bernini_blob_predelete");
		view->DeleteMeshInstance(caster);
		const float after = sample("bernini_blob_postdelete");
		CHECK(after > before);

		CHECK_THROWS_AS(view->GetBlobShadow(caster), bgl::SceneError);
	}

	(void)groundInstance;
}
