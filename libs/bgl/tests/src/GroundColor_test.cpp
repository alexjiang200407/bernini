#include "scene/SceneView.h"
#include "scene/ground_color.h"
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
#include <span>

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
		bgpu::BarrierLayout::kRenderTarget);

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
