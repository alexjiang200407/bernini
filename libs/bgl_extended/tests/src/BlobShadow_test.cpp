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

	// Laid flat the other way up: its front faces the ground, so a camera above sees only its back.
	glm::mat4
	UpsideDown(float y)
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, y, 0.0f)) *
		       glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
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

		bad            = desc;
		bad.casterLift = -1.0f;
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

/**
 * The decal drapes over what is actually there, not the ground plane.
 *
 * The same scene with a raised platform (a 2x2 plane) between the caster and the ground: the
 * shadow must land on the platform's top, measured by the caster's height above *it* -- under the
 * old one-plane projection these pixels read the plane behind the platform and the disc was
 * depth-tested away entirely.
 *
 * Screen positions, camera at (0, 8, 14) looking at the origin, 800x600: the platform at y=1.2
 * spans roughly y 251..281 on screen at centre x; the caster at y=1.7 sits at y 246..253; the
 * sample box below the caster reads the platform top. With the platform lifted to y=3.0 it spans
 * roughly y 196..220, clear of everything else.
 */
TEST_CASE("A blob shadow drapes over a raised static receiver", "[blobshadow][render]")
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

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	scene->SetGround(bgl::GroundPlaneDesc());

	auto whiteDesc            = bgl::PbrMaterialDesc();
	whiteDesc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	whiteDesc.metallicFactor  = 0.0f;
	whiteDesc.roughnessFactor = 1.0f;

	const auto white = scene->CreatePbrMaterial(whiteDesc);

	const auto groundGeom   = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, white);
	const auto platformGeom = scene->AddPlaneGeom(1, 1, 2.0f, 2.0f, white);
	const auto casterGeom   = scene->AddPlaneGeom(1, 1, 0.5f, 0.5f, white);

	constexpr float c_PlatformY = 1.2f;
	constexpr float c_CasterY   = 1.7f;

	const auto groundInstance = view->CreateStaticMeshInstance(groundGeom, c_Flat);
	const auto platform       = view->CreateStaticMeshInstance(platformGeom, Lifted(c_PlatformY));
	const auto caster         = view->CreateStaticMeshInstance(casterGeom, Lifted(c_CasterY));

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

	const auto sample = [&](const char* name, int x, int y, int size) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		const bgl::test::Rgba box =
			bgl::test::MeanColor(path, x - size / 2, y - size / 2, size, size);

		std::filesystem::remove(path);
		return box.Luma();
	};

	auto desc       = bgl::BlobShadowDesc();
	desc.radius     = c_Radius;
	desc.intensity  = c_Intensity;
	desc.fadeHeight = c_FadeHeight;

	SECTION("the shadow lands on the platform top and fades against it")
	{
		// In the platform's dark core, below the caster's own pixels.
		const int boxX = 400;
		const int boxY = 270;

		const float base = sample("bernini_blob_platform_base", boxX, boxY, 14);
		REQUIRE(base > 0.05f);

		// The caster hovers 0.5 above the platform top: measured against the *platform* the gap
		// is a quarter of the fade height and the shadow lands strong. Under the superseded
		// one-plane projection these pixels showed no shadow at all -- the disc lay on the ground
		// behind the platform and was depth-tested away -- so any solid darkening here is the
		// decal draping.
		view->SetBlobShadow(caster, desc);
		const float shadowed = sample("bernini_blob_platform_shadowed", boxX, boxY, 14);
		CHECK(shadowed < base * 0.9f);

		// More than fadeHeight above the platform top clears it.
		view->SetInstanceTransform(caster, Lifted(c_PlatformY + c_FadeHeight * 1.25f));
		const float cleared = sample("bernini_blob_platform_cleared", boxX, boxY, 14);
		CHECK(cleared > base * 0.95f);
	}

	SECTION("a covered cutout receiver catches the shadow")
	{
		// The platform again, but drawn through the cutout bucket with every texel surviving its
		// cutoff. fadeHeight stops short of the ground 1.7 below the caster, so before the cutout
		// rows rendered into the receiver these pixels could not darken at all.
		view->DeleteMeshInstance(platform);

		auto cutoutDesc      = whiteDesc;
		cutoutDesc.layerType = bgl::LayerType::kMask;
		const auto cutoutGeom =
			scene->AddPlaneGeom(1, 1, 2.0f, 2.0f, scene->CreatePbrMaterial(cutoutDesc));
		(void)view->CreateStaticMeshInstance(cutoutGeom, Lifted(c_PlatformY));

		auto shortFade       = desc;
		shortFade.fadeHeight = 1.5f;

		const int boxX = 400;
		const int boxY = 270;

		const float base = sample("bernini_blob_cutout_base", boxX, boxY, 14);
		REQUIRE(base > 0.05f);

		view->SetBlobShadow(caster, shortFade);
		const float shadowed = sample("bernini_blob_cutout_shadowed", boxX, boxY, 14);
		CHECK(shadowed < base * 0.9f);
	}

	SECTION("a fully cut-out receiver lets the shadow fall through")
	{
		// The same cutout platform with every texel below the cutoff: invisible in the colour
		// pass, so it must be invisible to the receiver too -- and the ground beneath is past
		// fadeHeight, so nothing in the box may darken.
		view->DeleteMeshInstance(platform);

		auto cutoutDesc            = whiteDesc;
		cutoutDesc.layerType       = bgl::LayerType::kMask;
		cutoutDesc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
		const auto cutoutGeom =
			scene->AddPlaneGeom(1, 1, 2.0f, 2.0f, scene->CreatePbrMaterial(cutoutDesc));
		(void)view->CreateStaticMeshInstance(cutoutGeom, Lifted(c_PlatformY));

		auto shortFade       = desc;
		shortFade.fadeHeight = 1.5f;

		const int boxX = 400;
		const int boxY = 270;

		const float base = sample("bernini_blob_cutout_holes_base", boxX, boxY, 14);
		REQUIRE(base > 0.05f);

		view->SetBlobShadow(caster, shortFade);
		const float through = sample("bernini_blob_cutout_holes", boxX, boxY, 14);
		CHECK(through > base * 0.95f);
	}

	SECTION("a hashed receiver catches the shadow only where its coverage survives")
	{
		// Stochastic coverage at 0.6: without temporal AA the seed is 0, so the surviving texel
		// pattern is the same in both captures. The box mixes darkened platform texels with holes
		// showing the ground, which fadeHeight keeps clear -- the mean drops by the covered share.
		view->DeleteMeshInstance(platform);

		auto hashedDesc            = whiteDesc;
		hashedDesc.layerType       = bgl::LayerType::kHashed;
		hashedDesc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 0.6f);
		const auto hashedGeom =
			scene->AddPlaneGeom(1, 1, 2.0f, 2.0f, scene->CreatePbrMaterial(hashedDesc));
		(void)view->CreateStaticMeshInstance(hashedGeom, Lifted(c_PlatformY));

		auto shortFade       = desc;
		shortFade.fadeHeight = 1.5f;

		const int boxX = 400;
		const int boxY = 270;

		const float base = sample("bernini_blob_hashed_base", boxX, boxY, 14);
		REQUIRE(base > 0.05f);

		// The drop is diluted twice -- by the uncovered share and by the display transform -- but
		// the discriminator is binary: before coverage rendered into the receiver, these pixels
		// could not darken at all, and without TAA both captures are deterministic.
		view->SetBlobShadow(caster, shortFade);
		const float shadowed = sample("bernini_blob_hashed_shadowed", boxX, boxY, 14);
		CHECK(shadowed < base * 0.97f);
	}

	SECTION("a double-sided receiver seen from its back catches the shadow")
	{
		// The platform's material is double-sided, so the colour pass draws the back the camera
		// sees; the receiver must hold that face too, or the decal reconstructs the ground beneath
		// and is depth-tested away behind the platform.
		view->SetInstanceTransform(platform, UpsideDown(c_PlatformY));

		const int boxX = 400;
		const int boxY = 270;

		const float base = sample("bernini_blob_backface_base", boxX, boxY, 14);
		REQUIRE(base > 0.05f);

		view->SetBlobShadow(caster, desc);
		const float shadowed = sample("bernini_blob_backface_shadowed", boxX, boxY, 14);
		CHECK(shadowed < base * 0.9f);
	}

	SECTION("a single-sided receiver seen from its back is no receiver at all")
	{
		// Culled in the colour pass, so it must be culled from the receiver too: a receiver that kept
		// it would hang a shadow in mid-air where nothing is drawn. The ground beneath is past
		// fadeHeight, so nothing in the box may darken.
		view->DeleteMeshInstance(platform);

		auto oneSided        = whiteDesc;
		oneSided.layerType   = bgl::LayerType::kMask;
		oneSided.doubleSided = false;
		const auto oneSidedGeom =
			scene->AddPlaneGeom(1, 1, 2.0f, 2.0f, scene->CreatePbrMaterial(oneSided));
		(void)view->CreateStaticMeshInstance(oneSidedGeom, UpsideDown(c_PlatformY));

		auto shortFade       = desc;
		shortFade.fadeHeight = 1.5f;

		const int boxX = 400;
		const int boxY = 270;

		const float base = sample("bernini_blob_culled_base", boxX, boxY, 14);
		REQUIRE(base > 0.05f);

		view->SetBlobShadow(caster, shortFade);
		const float unshadowed = sample("bernini_blob_culled", boxX, boxY, 14);
		CHECK(unshadowed > base * 0.95f);
	}

	SECTION("a wall beside the caster catches nothing")
	{
		// The platform stood vertical: plane geoms are authored in XY, so an unrotated placement
		// is a wall facing the camera, beside the caster and well inside the disc radius.
		view->SetInstanceTransform(
			platform,
			glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 1.0f, 0.0f)));

		// Mid-face of the wall, clear of the caster and of the ground line at its base.
		const int boxX = 450;
		const int boxY = 271;

		const float base = sample("bernini_blob_wall_base", boxX, boxY, 12);
		REQUIRE(base > 0.05f);

		// A blob shadow lands on what faces up: the wall's face points at the camera, so it must
		// keep its brightness while the ground at its base still catches the disc.
		view->SetBlobShadow(caster, desc);
		const float wall = sample("bernini_blob_wall", boxX, boxY, 12);
		CHECK(wall > base * 0.95f);
	}

	SECTION("a receiver above the caster catches nothing")
	{
		view->SetInstanceTransform(platform, Lifted(3.0f));

		// On the lifted platform's top, which now hangs over the caster.
		const int boxX = 400;
		const int boxY = 208;

		const float base = sample("bernini_blob_overhead_base", boxX, boxY, 12);
		REQUIRE(base > 0.05f);

		// A shadow falls down: the overhead platform sits above its caster and must not darken.
		view->SetBlobShadow(caster, desc);
		const float overhead = sample("bernini_blob_overhead", boxX, boxY, 12);
		CHECK(overhead > base * 0.95f);
	}

	SECTION("the caster lift raises where the disc is cast from")
	{
		view->SetInstanceTransform(platform, Lifted(3.0f));

		// The overhead section's platform and sample box: above the placement's origin, so from
		// there it catches nothing.
		const int boxX = 400;
		const int boxY = 208;

		const float base = sample("bernini_blob_lift_base", boxX, boxY, 12);
		REQUIRE(base > 0.05f);

		// Cast from 2 above the origin the same platform is half a fade height below the cast
		// point, and the disc lands on it.
		auto lifted       = desc;
		lifted.casterLift = 2.0f;
		view->SetBlobShadow(caster, lifted);
		const float shadowed = sample("bernini_blob_lift", boxX, boxY, 12);
		CHECK(shadowed < base * 0.9f);
	}

	(void)groundInstance;
}

/**
 * A disc whose volume reaches behind the camera: a runner's track section it has already passed.
 *
 * The camera stands low at (0, 2, 5) looking along -Z. One disc sits under it, its volume
 * crossing the near plane, and must still shade the ground it reaches ahead of the camera; one sits
 * wholly behind it and must change nothing. Which quad each one drew is asserted by
 * BoxBounds_test -- a quad too large shades the same pixels, so no image can tell.
 */
TEST_CASE(
	"A blob shadow reaching behind the camera shades only what is ahead",
	"[blobshadow][render]")
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

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	scene->SetGround(bgl::GroundPlaneDesc());

	auto whiteDesc            = bgl::PbrMaterialDesc();
	whiteDesc.baseColorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	whiteDesc.metallicFactor  = 0.0f;
	whiteDesc.roughnessFactor = 1.0f;

	const auto white = scene->CreatePbrMaterial(whiteDesc);

	const auto groundGeom = scene->AddPlaneGeom(1, 1, 40.0f, 40.0f, white);
	const auto casterGeom = scene->AddPlaneGeom(1, 1, 0.5f, 0.5f, white);

	const glm::vec3 eye(0.0f, 2.0f, 5.0f);
	const glm::vec3 lookAt(0.0f, 0.0f, -3.0f);

	const auto groundInstance = view->CreateStaticMeshInstance(groundGeom, c_Flat);

	// Radius 5 about z = 4: the volume runs from z = -1, ahead, to z = 9, behind the eye.
	const auto underfoot = view->CreateStaticMeshInstance(
		casterGeom,
		glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 4.0f)) * Lifted(0.5f));

	// Radius 5 about z = 20: every corner of the volume is behind the eye.
	const auto behind = view->CreateStaticMeshInstance(
		casterGeom,
		glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 20.0f)) * Lifted(0.5f));

	const float aspect = static_cast<float>(c_Width) / static_cast<float>(c_Height);

	auto camera = bgl::Camera();
	camera.LookAt(eye, lookAt, glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	const glm::mat4 viewProj = glm::perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f) *
	                           glm::lookAt(eye, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

	const auto pixelOf = [&](const glm::vec3& world) {
		const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return glm::ivec2(
			static_cast<int>((ndc.x * 0.5f + 0.5f) * static_cast<float>(c_Width)),
			static_cast<int>((0.5f - ndc.y * 0.5f) * static_cast<float>(c_Height)));
	};

	// On the ground 2 m ahead of the underfoot disc's centre, inside its radius; and 7 m ahead,
	// outside it.
	const glm::ivec2 inside  = pixelOf(glm::vec3(0.0f, 0.0f, 2.0f));
	const glm::ivec2 outside = pixelOf(glm::vec3(0.0f, 0.0f, -4.0f));
	REQUIRE(inside.y < static_cast<int>(c_Height) - c_SampleSize);

	struct Frame
	{
		float inside;
		float outside;
	};

	const auto capture = [&](const char* name, const char* keepAs = nullptr) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		const auto luma = [&](const glm::ivec2 at) {
			return bgl::test::MeanColor(
					   path,
					   at.x - c_SampleSize / 2,
					   at.y - c_SampleSize / 2,
					   c_SampleSize,
					   c_SampleSize)
			    .Luma();
		};

		const Frame frame{ luma(inside), luma(outside) };
		if (keepAs != nullptr)
			std::filesystem::rename(path, keepAs);
		else
			std::filesystem::remove(path);
		return frame;
	};

	auto desc       = bgl::BlobShadowDesc();
	desc.radius     = 5.0f;
	desc.intensity  = c_Intensity;
	desc.fadeHeight = c_FadeHeight;

	const auto basePath =
		(std::filesystem::temp_directory_path() / "bernini_blob_near_base_kept.png").string();
	const Frame base = capture("bernini_blob_near_base", basePath.c_str());
	REQUIRE(base.inside > 0.05f);
	REQUIRE(base.outside > 0.05f);

	SECTION("a disc wholly behind the camera changes nothing")
	{
		view->SetBlobShadow(behind, desc);

		const auto gotPath =
			(std::filesystem::temp_directory_path() / "bernini_blob_near_behind_kept.png").string();
		capture("bernini_blob_near_behind", gotPath.c_str());
		CHECK(bgl::test::MatchesGolden(basePath, gotPath, 0.0f));
	}

	SECTION("a disc straddling the near plane shades the ground ahead and nothing past it")
	{
		view->SetBlobShadow(underfoot, desc);

		const Frame shadowed = capture("bernini_blob_near_straddle");
		CHECK(shadowed.inside < base.inside * 0.95f);
		CHECK(shadowed.outside > base.outside * 0.98f);
	}

	std::filesystem::remove(basePath);
	(void)groundInstance;
}
