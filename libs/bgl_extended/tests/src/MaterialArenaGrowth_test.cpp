#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>

/**
 * The material arena outgrowing its budget, with a frame drawn on either side of the growth.
 *
 * A growth replaces the arena's buffer and mints a fresh descriptor for it, and the arena is read
 * through *two* of those: the raw view the frame graph imports for the payload bytes, and the typed
 * view a draw carries for the texture handles inside them. The second has to be re-issued at the
 * same instant as the first, or a growth pairs a new buffer with a view onto a released one.
 *
 * The first draw is what makes this reachable, and is why no existing case covers it:
 * SceneOverflow_test grows every arena but never draws, and every render test stays inside its
 * budget, so nothing else has a live typed view at the moment an arena grows.
 *
 * Two instances of ONE plane geom. The left keeps a material created *before* the growth, the right
 * takes one created *after* it, so a view left behind fails whichever half it was minted against.
 * Like the other two-region tests here, the frame is compared against itself rather than a stored
 * PNG.
 */

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	// The camera and plane geometry of MaterialOverrideRender_test: two 6-wide planes at x = +-4 land
	// around x = 296 and x = 504, and an 80x80 box at each sits well inside them.
	constexpr float c_PlaneSize   = 6.0f;
	constexpr float c_PlaneOffset = 4.0f;

	constexpr int c_SampleSize = 80;
	constexpr int c_LeftX      = 296 - c_SampleSize / 2;
	constexpr int c_RightX     = 504 - c_SampleSize / 2;
	constexpr int c_SampleY    = 300 - c_SampleSize / 2;

	constexpr uint32_t c_TextureSize = 64;

	// Materials created to push the arena past its budget. One PBR record is 96 bytes against an
	// initial reservation of a few hundred, so this is several growths, not a borderline one.
	constexpr int c_FillerMaterials = 24;

	/** A flat, fully opaque texture -- the base colour a material sampling it renders. */
	assetlib::ImageData
	MakeSolidTexture(uint8_t r, uint8_t g, uint8_t b)
	{
		const size_t texels = static_cast<size_t>(c_TextureSize) * c_TextureSize;

		auto image      = assetlib::ImageData();
		image.width     = c_TextureSize;
		image.height    = c_TextureSize;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_SRGB;
		image.isCubemap = false;
		image.pixels    = core::fixed_buffer<std::byte>(texels * 4);

		for (size_t t = 0; t < texels * 4; t += 4)
		{
			image.pixels[t + 0] = std::byte{ r };
			image.pixels[t + 1] = std::byte{ g };
			image.pixels[t + 2] = std::byte{ b };
			image.pixels[t + 3] = std::byte{ 255 };
		}

		image.subresources.push_back(
			{ 0, static_cast<uint64_t>(c_TextureSize) * 4, image.pixels.size() });

		return image;
	}

	/** A material whose colour comes from a texture, so reading it goes through the typed view. */
	bgl::PbrMaterialDesc
	TexturedDesc(bgl::TextureAssetHandle texture)
	{
		auto desc             = bgl::PbrMaterialDesc();
		desc.baseColorTexture = texture;
		desc.metallicFactor   = 0.0f;
		desc.roughnessFactor  = 1.0f;
		return desc;
	}

	glm::mat4
	At(float x)
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
	}
}

TEST_CASE(
	"Materials survive the arena outgrowing its budget mid-frame-sequence",
	"[material][capacity][growth][render]")
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

	// Geometry is given room; only the material arena is starved, so a growth here is the arena's
	// and nothing else's.
	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 128;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 100000;
	sceneDesc.initialIndices              = 4000;
	sceneDesc.initialPbrMaterials         = 1;
	sceneDesc.initialLoosePbrMaterials    = 1;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);
	REQUIRE(scene != nullptr);
	REQUIRE(view != nullptr);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const auto green = scene->AddTextureAsset(MakeSolidTexture(40, 200, 60), "arena-growth-green");
	const auto blue  = scene->AddTextureAsset(MakeSolidTexture(40, 60, 200), "arena-growth-blue");

	const auto early = scene->CreatePbrMaterial(TexturedDesc(green));
	const auto plane = scene->AddPlaneGeom(1, 1, c_PlaneSize, c_PlaneSize, early);

	const auto left  = view->CreateStaticMeshInstance(plane, At(-c_PlaneOffset));
	const auto right = view->CreateStaticMeshInstance(plane, At(c_PlaneOffset));
	(void)left;  // sampled on screen, never named again

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

	const auto capture = [&](const char* name) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		return path;
	};

	// Frame one, inside the budget: both planes sample the green map.
	{
		const std::string png = capture("bernini_arena_growth_before");

		const bgl::test::Rgba l =
			bgl::test::MeanColor(png, c_LeftX, c_SampleY, c_SampleSize, c_SampleSize);
		const bgl::test::Rgba r =
			bgl::test::MeanColor(png, c_RightX, c_SampleY, c_SampleSize, c_SampleSize);

		// The luma floor is what stops a mis-aimed box from making the rest of this vacuous:
		// background is black, so a miss reads ~0 and passes every channel comparison.
		CHECK(l.Luma() > 0.02f);
		CHECK(r.Luma() > 0.02f);
		CHECK(l.g > l.b);
		CHECK(r.g > r.b);

		std::filesystem::remove(png);
	}

	for (int i = 0; i < c_FillerMaterials; ++i)
	{
		REQUIRE_NOTHROW(scene->CreatePbrMaterial(bgl::PbrMaterialDesc()));
	}

	// Allocated in the grown buffer, past where the retired one ended.
	const auto late = scene->CreatePbrMaterial(TexturedDesc(blue));
	view->SetSubmeshMaterialOverride(right, 0, late);

	// Frame two, after the growth. Both halves are load-bearing: the left proves a record that
	// predates the growth still finds its handles once they have been copied forward, the right that
	// a record allocated after it is addressable at all.
	{
		const std::string png = capture("bernini_arena_growth_after");

		const bgl::test::Rgba l =
			bgl::test::MeanColor(png, c_LeftX, c_SampleY, c_SampleSize, c_SampleSize);
		const bgl::test::Rgba r =
			bgl::test::MeanColor(png, c_RightX, c_SampleY, c_SampleSize, c_SampleSize);

		CHECK(l.Luma() > 0.02f);
		CHECK(l.g > l.b);

		CHECK(r.Luma() > 0.02f);
		CHECK(r.b > r.g);

		std::filesystem::remove(png);
	}
}
