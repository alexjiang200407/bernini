#include "scene/SceneView.h"
#include "scene/ground_color.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/TextureReadback.h"
#include <assetlib_structs/Heightfield.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/GrassHandle.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainGrassDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <bgl/types/Viewport.h>
#include <bgpu/types/Barrier.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

// A view's ground-colour texture as the pass that draws it and the grass that reads it agree on: the
// square it covers around the camera, and when the view has one at all.

TEST_CASE(
	"The ground-colour square reaches past the camera every way and snaps to texels",
	"[grass][groundcolor]")
{
	constexpr float    c_Reach  = 90.0f;
	constexpr uint32_t c_Texels = 1024;

	const auto  camera = glm::vec3(123.4f, 7.0f, -56.7f);
	const auto  rect   = bgl::GroundColorRectAround(camera, c_Reach, c_Texels);
	const float texel  = rect.size / static_cast<float>(c_Texels);
	INFO("origin " << rect.origin.x << ", " << rect.origin.y << " size " << rect.size);

	CHECK(rect.size == Catch::Approx(2.0f * c_Reach * c_Texels / (c_Texels - 2)));
	CHECK(rect.origin.x <= camera.x - c_Reach);
	CHECK(rect.origin.y <= camera.z - c_Reach);
	CHECK(rect.origin.x + rect.size >= camera.x + c_Reach);
	CHECK(rect.origin.y + rect.size >= camera.z + c_Reach);

	// Whole texels from the world's origin, so a texel covers the same ground wherever the camera is.
	CHECK(rect.origin.x / texel == Catch::Approx(std::round(rect.origin.x / texel)).margin(1e-3));
	CHECK(rect.origin.y / texel == Catch::Approx(std::round(rect.origin.y / texel)).margin(1e-3));

	// A camera moving within its texel leaves the square where it was; one moving a texel moves it
	// by exactly one.
	const auto within =
		bgl::GroundColorRectAround(camera + glm::vec3(0.0f, 3.0f, 0.0f), c_Reach, c_Texels);
	CHECK(within.origin == rect.origin);
	const auto next =
		bgl::GroundColorRectAround(camera + glm::vec3(texel, 0.0f, 0.0f), c_Reach, c_Texels);
	CHECK(next.origin.x == Catch::Approx(rect.origin.x + texel));
	CHECK(next.origin.y == rect.origin.y);

	CHECK(bgl::GroundColorRectAround(camera, 0.0f, c_Texels).size == 0.0f);
}

namespace
{
	assetlib::Heightfield
	Flat(const uint32_t side = 16)
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = side;
		field.samplesZ    = side;
		field.cellSize    = 1.0f;
		field.heightRange = 1.0f;
		field.heights.assign(static_cast<size_t>(side) * side, 0);
		return field;
	}
}

TEST_CASE(
	"A view has a ground-colour texture only while a terrain look takes its ground's colour",
	"[grass][groundcolor]")
{
	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = false;
	auto gfx                         = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto       scene    = gfx->CreateScene(bgl::SceneDesc());
	const auto material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const auto field    = Flat();
	const auto terrain =
		scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(material));

	auto desc            = bgl::GrassDesc();
	desc.material        = material;
	desc.density.fadeEnd = 40.0f;
	const auto look      = scene->CreateGrass(desc);
	const auto layer     = bgl::TerrainGrassDesc().SetLook(look);
	scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));

	auto  viewRef = gfx->CreateSceneView(scene, 8);
	auto* view    = viewRef->As<bgl::SceneView>();
	REQUIRE(view != nullptr);
	const auto camera = glm::vec3(4.0f, 2.0f, 4.0f);

	view->RefreshGrass();
	view->PrepareGroundColor(camera);
	CHECK(view->GetGroundColor().rect.size == 0.0f);
	CHECK(view->GetGroundColor().texture.IsNull());

	desc.color.groundColorFar = 1.0f;
	scene->UpdateGrass(look, desc);
	view->RefreshGrass();
	view->PrepareGroundColor(camera);
	const bgl::SceneView::GroundColorTarget& target = view->GetGroundColor();
	CHECK(target.rect.size >= 2.0f * desc.density.fadeEnd);
	CHECK_FALSE(target.texture.IsNull());
	CHECK_FALSE(target.rtv.IsNull());
	CHECK_FALSE(target.srv.IsNull());

	// Turned off again, the view keeps the texture it made but covers nothing with it.
	desc.color.groundColorFar = 0.0f;
	scene->UpdateGrass(look, desc);
	view->RefreshGrass();
	view->PrepareGroundColor(camera);
	CHECK(view->GetGroundColor().rect.size == 0.0f);
}

namespace
{
	[[nodiscard]] float
	SrgbToLinear(const uint8_t encoded)
	{
		const float c = static_cast<float>(encoded) / 255.0f;
		return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}
}

TEST_CASE(
	"Ground Color writes a terrain's albedo under the square, and nothing off the field",
	"[grass][groundcolor][render]")
{
	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = true;
	auto gfx                         = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 320;
	targetDesc.height   = 240;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                = bgl::SceneDesc();
	sceneDesc.initialPbrMaterials = 4;
	auto scene                    = gfx->CreateScene(sceneDesc);
	auto viewRef                  = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), viewRef.Get());

	const auto albedo          = glm::vec3(0.2f, 0.6f, 0.1f);
	auto       groundDesc      = bgl::PbrMaterialDesc();
	groundDesc.baseColorFactor = glm::vec4(albedo, 1.0f);
	const auto ground          = scene->CreatePbrMaterial(groundDesc);

	// A 63 m field from the origin, the camera 5 m in from its corner: the square, reaching 20 m,
	// hangs off the field on the -x and -z sides.
	const auto field = Flat(64);
	const auto terrain =
		scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(ground));

	auto lookDesc                 = bgl::GrassDesc();
	lookDesc.material             = ground;
	lookDesc.density.fadeStart    = 5.0f;
	lookDesc.density.fadeEnd      = 20.0f;
	lookDesc.color.groundColorFar = 1.0f;
	const auto look               = scene->CreateGrass(lookDesc);
	const auto layer              = bgl::TerrainGrassDesc().SetLook(look);
	scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));

	auto job     = bgl::RenderJob();
	job.view     = viewRef;
	job.viewport = bgl::Viewport(320.0f, 240.0f);
	job.camera
		.LookAt(
			glm::vec3(5.0f, 3.0f, 5.0f),
			glm::vec3(20.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 320.0f / 240.0f, 0.1f, 300.0f);
	gfx->DrawFrame(target, job);

	const auto* view = viewRef->As<bgl::SceneView>();
	REQUIRE(view != nullptr);
	const bgl::SceneView::GroundColorTarget& colour = view->GetGroundColor();
	REQUIRE_FALSE(colour.texture.IsNull());
	const auto texels = bgl::test::ReadRgba8Texels(
		gfx.Get(),
		colour.texture,
		bgl::c_GroundColorTexels,
		bgl::c_GroundColorTexels,
		bgpu::BarrierLayout::kShaderResource);

	const auto at = [&](const float x, const float z) {
		const glm::vec2 uv = (glm::vec2(x, z) - colour.rect.origin) / colour.rect.size;
		const auto      u  = static_cast<uint32_t>(uv.x * bgl::c_GroundColorTexels);
		const auto      v  = static_cast<uint32_t>(uv.y * bgl::c_GroundColorTexels);
		REQUIRE(u < bgl::c_GroundColorTexels);
		REQUIRE(v < bgl::c_GroundColorTexels);
		return texels[static_cast<size_t>(v) * bgl::c_GroundColorTexels + u];
	};

	for (const glm::vec2 on :
	     { glm::vec2(10.0f, 10.0f), glm::vec2(1.0f, 20.0f), glm::vec2(20.0f, 1.0f) })
	{
		const glm::u8vec4 texel = at(on.x, on.y);
		INFO(
			"on the field at " << on.x << ", " << on.y << ": " << int(texel.r) << ", "
							   << int(texel.g) << ", " << int(texel.b) << ", " << int(texel.a));
		CHECK(texel.a == 255);
		CHECK(SrgbToLinear(texel.r) == Catch::Approx(albedo.r).margin(0.01));
		CHECK(SrgbToLinear(texel.g) == Catch::Approx(albedo.g).margin(0.01));
		CHECK(SrgbToLinear(texel.b) == Catch::Approx(albedo.b).margin(0.01));
	}

	// Off the field along each axis: nothing drawn, so a blade there has no ground colour to take.
	for (const glm::vec2 off : { glm::vec2(-10.0f, 10.0f), glm::vec2(10.0f, -10.0f) })
	{
		INFO("off the field at " << off.x << ", " << off.y);
		CHECK(at(off.x, off.y).a == 0);
	}
}

TEST_CASE(
	"A blade taking all of its ground's colour shades as the ground under it",
	"[grass][groundcolor][render]")
{
	constexpr uint32_t c_W = 640;
	constexpr uint32_t c_H = 480;

	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = true;
	auto gfx                         = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_W);
	targetDesc.height   = static_cast<int>(c_H);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                = bgl::SceneDesc();
	sceneDesc.initialPbrMaterials = 4;
	auto scene                    = gfx->CreateScene(sceneDesc);
	auto view                     = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	// Ground and blade alike but for their colour: brown earth, red blades.
	auto earthDesc            = bgl::PbrMaterialDesc();
	earthDesc.metallicFactor  = 0.0f;
	earthDesc.roughnessFactor = 1.0f;
	earthDesc.baseColorFactor = glm::vec4(0.3f, 0.22f, 0.1f, 1.0f);
	const auto earth          = scene->CreatePbrMaterial(earthDesc);
	auto       redDesc        = earthDesc;
	redDesc.baseColorFactor   = glm::vec4(0.8f, 0.05f, 0.05f, 1.0f);
	const auto red            = scene->CreatePbrMaterial(redDesc);

	const auto field = Flat(64);
	const auto terrain =
		scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(earth));

	auto lookDesc                      = bgl::GrassDesc();
	lookDesc.material                  = red;
	lookDesc.blade.rootWidth           = 0.05f;
	lookDesc.density.fadeStart         = 10.0f;
	lookDesc.density.fadeEnd           = 30.0f;
	lookDesc.lighting.groundNormalNear = 1.0f;
	lookDesc.lighting.groundNormalFar  = 1.0f;
	const auto look                    = scene->CreateGrass(lookDesc);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_H));
	job.camera
		.LookAt(
			glm::vec3(32.0f, 3.0f, 40.0f),
			glm::vec3(32.0f, 0.0f, 32.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), static_cast<float>(c_W) / c_H, 0.1f, 300.0f);

	// The mean of the frame's centre once TAA has blended the earlier frames out.
	const auto settled = [&](const char* name) {
		for (int i = 0; i < 24; ++i)
		{
			gfx->DrawFrame(target, job);
		}
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();
		gfx->ScreenshotPng(target, path);
		const bgl::test::Rgba box = bgl::test::MeanColor(path, 220, 200, 200, 120);
		std::filesystem::remove(path);
		return box;
	};

	const bgl::test::Rgba bare = settled("ground_color_bare");

	const auto layer = bgl::TerrainGrassDesc().SetLook(look).SetSpacing(0.12f);
	scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));
	const bgl::test::Rgba own = settled("ground_color_own");

	lookDesc.color.groundColorNear = 1.0f;
	lookDesc.color.groundColorFar  = 1.0f;
	scene->UpdateGrass(look, lookDesc);
	const bgl::test::Rgba taken = settled("ground_color_taken");

	INFO("bare " << bare.r << ", " << bare.g << ", " << bare.b);
	INFO("own colour " << own.r << ", " << own.g << ", " << own.b);
	INFO("ground's colour " << taken.r << ", " << taken.g << ", " << taken.b);

	// Its own colour, the field reads red over the earth; taking the ground's, it reads as the earth.
	CHECK(own.r - own.g > bare.r - bare.g + 0.1f);
	CHECK(std::abs(taken.r - bare.r) < 0.02f);
	CHECK(std::abs(taken.g - bare.g) < 0.02f);
	CHECK(std::abs(taken.b - bare.b) < 0.02f);
}

TEST_CASE(
	"A terrain drawn through a lit surface gives its grass no ground colour",
	"[grass][groundcolor][render]")
{
	// A lit surface owns its lighting and has no albedo apart from it: its ground-colour program
	// writes alpha 0, so the blades over it keep their own colour.
	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = true;
	opts.gpuContext.clientShaderDir  = "./shaders/tests/surfaces";
	auto gfx                         = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 320;
	targetDesc.height   = 240;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.initialPbrMaterials     = 4;
	sceneDesc.initialSurfaceMaterials = 4;
	auto scene                        = gfx->CreateScene(sceneDesc);
	auto viewRef                      = gfx->CreateSceneView(scene, 8);

	const auto unlit = scene->CreateSurfaceMaterial(
		{ .surfaceName = "Unlit", .values = { { "color", glm::vec4(0.2f, 0.6f, 0.1f, 0.0f) } } });
	const auto blade = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());

	const auto field = Flat(64);
	const auto terrain =
		scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(unlit));

	auto lookDesc                 = bgl::GrassDesc();
	lookDesc.material             = blade;
	lookDesc.density.fadeEnd      = 20.0f;
	lookDesc.color.groundColorFar = 1.0f;
	const auto look               = scene->CreateGrass(lookDesc);
	const auto layer              = bgl::TerrainGrassDesc().SetLook(look);
	scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));

	auto job     = bgl::RenderJob();
	job.view     = viewRef;
	job.viewport = bgl::Viewport(320.0f, 240.0f);
	job.camera
		.LookAt(
			glm::vec3(32.0f, 3.0f, 32.0f),
			glm::vec3(40.0f, 0.0f, 40.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 320.0f / 240.0f, 0.1f, 300.0f);
	gfx->DrawFrame(target, job);

	const auto* view = viewRef->As<bgl::SceneView>();
	REQUIRE(view != nullptr);
	const bgl::SceneView::GroundColorTarget& colour = view->GetGroundColor();
	REQUIRE_FALSE(colour.texture.IsNull());
	const auto texels = bgl::test::ReadRgba8Texels(
		gfx.Get(),
		colour.texture,
		bgl::c_GroundColorTexels,
		bgl::c_GroundColorTexels,
		bgpu::BarrierLayout::kShaderResource);

	// The texel under the camera, on the field.
	const glm::vec2 uv = (glm::vec2(32.0f, 32.0f) - colour.rect.origin) / colour.rect.size;
	const auto      u  = static_cast<uint32_t>(uv.x * bgl::c_GroundColorTexels);
	const auto      v  = static_cast<uint32_t>(uv.y * bgl::c_GroundColorTexels);
	CHECK(texels[static_cast<size_t>(v) * bgl::c_GroundColorTexels + u].a == 0);
}

TEST_CASE(
	"A layer that follows its ground gives the view a ground-cover texture, colour taken or not",
	"[grass][groundcolor][groundcover]")
{
	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = false;
	auto gfx                         = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto       scene    = gfx->CreateScene(bgl::SceneDesc());
	const auto material = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
	const auto field    = Flat();
	const auto terrain =
		scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(material));

	auto desc            = bgl::GrassDesc();
	desc.material        = material;
	desc.density.fadeEnd = 40.0f;
	const auto look      = scene->CreateGrass(desc);

	auto  viewRef = gfx->CreateSceneView(scene, 8);
	auto* view    = viewRef->As<bgl::SceneView>();
	REQUIRE(view != nullptr);
	const auto camera = glm::vec3(4.0f, 2.0f, 4.0f);

	const auto attach = [&](const bool follows) {
		const auto layer = bgl::TerrainGrassDesc().SetLook(look).SetGroundCover(follows);
		scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));
		view->RefreshGrass();
		view->PrepareGroundColor(camera);
	};

	attach(false);
	CHECK(view->GetGroundColor().rect.size == 0.0f);
	CHECK(view->GetGroundColor().cover.IsNull());

	attach(true);
	const bgl::SceneView::GroundColorTarget& target = view->GetGroundColor();
	CHECK(target.rect.size >= 2.0f * desc.density.fadeEnd);
	CHECK_FALSE(target.texture.IsNull());
	CHECK_FALSE(target.cover.IsNull());
	CHECK_FALSE(target.coverRtv.IsNull());
	CHECK_FALSE(target.coverSrv.IsNull());

	attach(false);
	CHECK(view->GetGroundColor().rect.size == 0.0f);
}

TEST_CASE(
	"Ground Color writes the surface's cover under the square, and full cover off the field",
	"[grass][groundcolor][groundcover][render]")
{
	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = true;
	opts.gpuContext.clientShaderDir  = "./shaders/tests/surfaces";
	auto gfx                         = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 320;
	targetDesc.height   = 240;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.initialPbrMaterials     = 4;
	sceneDesc.initialSurfaceMaterials = 4;
	auto scene                        = gfx->CreateScene(sceneDesc);
	auto viewRef                      = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), viewRef.Get());

	constexpr float c_Cover = 0.25f;
	const auto      ground  = scene->CreateSurfaceMaterial(
		{ .surfaceName = "Cover", .values = { { "cover", glm::vec4(c_Cover) } } });
	const auto blade = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());

	// As the colour case: the square, reaching 20 m from 5 m in, hangs off the field's -x, -z edges.
	const auto field = Flat(64);
	const auto terrain =
		scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(ground));

	auto lookDesc              = bgl::GrassDesc();
	lookDesc.material          = blade;
	lookDesc.density.fadeStart = 5.0f;
	lookDesc.density.fadeEnd   = 20.0f;
	const auto look            = scene->CreateGrass(lookDesc);
	const auto layer           = bgl::TerrainGrassDesc().SetLook(look).SetGroundCover(true);
	scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));

	auto job     = bgl::RenderJob();
	job.view     = viewRef;
	job.viewport = bgl::Viewport(320.0f, 240.0f);
	job.camera
		.LookAt(
			glm::vec3(5.0f, 3.0f, 5.0f),
			glm::vec3(20.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 320.0f / 240.0f, 0.1f, 300.0f);
	gfx->DrawFrame(target, job);

	const auto* view = viewRef->As<bgl::SceneView>();
	REQUIRE(view != nullptr);
	const bgl::SceneView::GroundColorTarget& colour = view->GetGroundColor();
	REQUIRE_FALSE(colour.cover.IsNull());
	const auto cover = bgl::test::ReadTextureBytes(
		gfx.Get(),
		colour.cover,
		bgl::c_GroundColorTexels,
		bgl::c_GroundColorTexels,
		1,
		bgpu::BarrierLayout::kShaderResource);

	const auto at = [&](const float x, const float z) {
		const glm::vec2 uv = (glm::vec2(x, z) - colour.rect.origin) / colour.rect.size;
		const auto      u  = static_cast<uint32_t>(uv.x * bgl::c_GroundColorTexels);
		const auto      v  = static_cast<uint32_t>(uv.y * bgl::c_GroundColorTexels);
		REQUIRE(u < bgl::c_GroundColorTexels);
		REQUIRE(v < bgl::c_GroundColorTexels);
		return static_cast<int>(cover[static_cast<size_t>(v) * bgl::c_GroundColorTexels + u]);
	};

	for (const glm::vec2 on :
	     { glm::vec2(10.0f, 10.0f), glm::vec2(1.0f, 20.0f), glm::vec2(20.0f, 1.0f) })
	{
		INFO("on the field at " << on.x << ", " << on.y << ": " << at(on.x, on.y));
		CHECK(std::abs(at(on.x, on.y) - static_cast<int>(std::round(c_Cover * 255.0f))) <= 1);
	}

	// Off the field nothing is drawn: full cover, so a clump that could stand there is not held
	// back by the texture.
	for (const glm::vec2 off : { glm::vec2(-10.0f, 10.0f), glm::vec2(10.0f, -10.0f) })
	{
		INFO("off the field at " << off.x << ", " << off.y);
		CHECK(at(off.x, off.y) == 255);
	}
}

TEST_CASE(
	"Grass that follows its ground grows nowhere the surface covers nothing",
	"[grass][groundcolor][groundcover][render]")
{
	constexpr uint32_t c_W = 640;
	constexpr uint32_t c_H = 480;

	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = true;
	opts.gpuContext.clientShaderDir  = "./shaders/tests/surfaces";
	auto gfx                         = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_W);
	targetDesc.height   = static_cast<int>(c_H);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.initialPbrMaterials     = 4;
	sceneDesc.initialSurfaceMaterials = 4;
	auto scene                        = gfx->CreateScene(sceneDesc);
	auto view                         = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	// Brown earth that says it is bare, red blades.
	const auto earth = scene->CreateSurfaceMaterial(
		{ .surfaceName = "Cover",
	      .values      = { { "color", glm::vec4(0.3f, 0.22f, 0.1f, 1.0f) },
	                       { "cover", glm::vec4(0.0f) } } });
	auto redDesc            = bgl::PbrMaterialDesc();
	redDesc.metallicFactor  = 0.0f;
	redDesc.roughnessFactor = 1.0f;
	redDesc.baseColorFactor = glm::vec4(0.8f, 0.05f, 0.05f, 1.0f);
	const auto red          = scene->CreatePbrMaterial(redDesc);

	const auto field = Flat(64);
	const auto terrain =
		scene->CreateTerrain(bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(earth));

	auto lookDesc              = bgl::GrassDesc();
	lookDesc.material          = red;
	lookDesc.blade.rootWidth   = 0.05f;
	lookDesc.density.fadeStart = 10.0f;
	lookDesc.density.fadeEnd   = 30.0f;
	const auto look            = scene->CreateGrass(lookDesc);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_H));
	job.camera
		.LookAt(
			glm::vec3(32.0f, 3.0f, 40.0f),
			glm::vec3(32.0f, 0.0f, 32.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), static_cast<float>(c_W) / c_H, 0.1f, 300.0f);

	const auto settled = [&](const char* name) {
		for (int i = 0; i < 24; ++i)
		{
			gfx->DrawFrame(target, job);
		}
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();
		gfx->ScreenshotPng(target, path);
		const bgl::test::Rgba box = bgl::test::MeanColor(path, 220, 200, 200, 120);
		std::filesystem::remove(path);
		return box;
	};

	const bgl::test::Rgba bare = settled("ground_cover_bare");

	const auto grow = [&](const bool follows) {
		const auto layer =
			bgl::TerrainGrassDesc().SetLook(look).SetSpacing(0.12f).SetGroundCover(follows);
		scene->AttachTerrainGrass(terrain, std::span<const bgl::TerrainGrassDesc>(&layer, 1));
	};

	grow(false);
	const bgl::test::Rgba ruled = settled("ground_cover_ruled");
	grow(true);
	const bgl::test::Rgba followed = settled("ground_cover_followed");

	INFO("bare " << bare.r << ", " << bare.g << ", " << bare.b);
	INFO("by its rules " << ruled.r << ", " << ruled.g << ", " << ruled.b);
	INFO("following the ground " << followed.r << ", " << followed.g << ", " << followed.b);

	// By its rules alone the field reads red over the earth; following a ground that covers
	// nothing, it is the bare earth again.
	CHECK(ruled.r - ruled.g > bare.r - bare.g + 0.1f);
	CHECK(std::abs(followed.r - bare.r) < 0.02f);
	CHECK(std::abs(followed.g - bare.g) < 0.02f);
	CHECK(std::abs(followed.b - bare.b) < 0.02f);
}
