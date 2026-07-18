#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace
{
	constexpr uint32_t c_Width  = 600;
	constexpr uint32_t c_Height = 800;

	constexpr uint32_t c_MaskSize = 256;

	assetlib::ImageData
	makeHoleMask()
	{
		const float centre = static_cast<float>(c_MaskSize) * 0.5f;
		const float radius = static_cast<float>(c_MaskSize) * 0.3f;

		const size_t texels = static_cast<size_t>(c_MaskSize) * c_MaskSize;

		auto image      = assetlib::ImageData();
		image.width     = c_MaskSize;
		image.height    = c_MaskSize;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_SRGB;
		image.isCubemap = false;
		image.pixels    = core::fixed_buffer<std::byte>(texels * 4);

		for (uint32_t y = 0; y < c_MaskSize; ++y)
		{
			for (uint32_t x = 0; x < c_MaskSize; ++x)
			{
				const float dx = static_cast<float>(x) + 0.5f - centre;
				const float dy = static_cast<float>(y) + 0.5f - centre;

				const bool   inHole = dx * dx + dy * dy <= radius * radius;
				const size_t t      = (static_cast<size_t>(y) * c_MaskSize + x) * 4;

				image.pixels[t + 0] = std::byte{ 60 };
				image.pixels[t + 1] = std::byte{ 180 };
				image.pixels[t + 2] = std::byte{ 75 };
				image.pixels[t + 3] = inHole ? std::byte{ 0 } : std::byte{ 255 };
			}
		}

		image.subresources.push_back(
			{ 0, static_cast<uint64_t>(c_MaskSize) * 4, image.pixels.size() });

		return image;
	}
}

TEST_CASE("An alpha-tested material cuts a hole in a plane", "[alphatest][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;
	sceneDesc.maxPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	// PBR does not render without an environment; there is no default.
	view->SetEnvironmentMap(
		{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

	// Through the real encoder: RGBA8 -> UASTC -> BC7, exactly what bakeMaterial does for a cutout.
	const auto encoded = std::filesystem::temp_directory_path() / "bernini_alphatest_mask.ktx2";
	assetlib::writeKTX2(makeHoleMask(), encoded, true, assetlib::Ktx2Compression::kBC7_RGBA);

	assetlib::ImageData baseColor = assetlib::loadKTX2(encoded);
	std::filesystem::remove(encoded);

	// If this is BC1, the alpha was silently destroyed and no cutout is possible.
	REQUIRE(baseColor.vkFormat == assetlib::VkFormat::BC7_SRGB_BLOCK);

	const auto texture = scene->AddTextureAsset(std::move(baseColor), "alphatest-mask");

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

	auto context     = bgl::RenderContext();
	context.view     = view;
	context.camera   = camera;
	context.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	SECTION("kAlphaTest discards the transparent texels - alpha_test_plane.png")
	{
		auto desc             = bgl::PbrMaterialDesc();
		desc.baseColorTexture = texture;
		desc.metallicFactor   = 0.0f;
		desc.roughnessFactor  = 0.6f;
		desc.layerType        = bgl::LayerType::kMask;
		desc.alphaCutoff      = 0.5f;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);
		view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, "assets/golden/alpha_test_plane.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/alpha_test_plane.exp.png",
				"assets/golden/alpha_test_plane.got.png"));
	}

	SECTION("kOpaque on the same texture has no hole - alpha_test_opaque.png")
	{
		// The control, and the whole reason this test is trustworthy. Same geometry, same texture, same
		// cutoff -- only the layer differs. If this rendered a hole too, the hole would be coming from
		// the texture rather than from the discard, and the test above would prove nothing.
		auto desc             = bgl::PbrMaterialDesc();
		desc.baseColorTexture = texture;
		desc.metallicFactor   = 0.0f;
		desc.roughnessFactor  = 0.6f;
		desc.layerType        = bgl::LayerType::kOpaque;
		desc.alphaCutoff      = 0.5f;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);
		view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, "assets/golden/alpha_test_opaque.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/alpha_test_opaque.exp.png",
				"assets/golden/alpha_test_opaque.got.png"));
	}
}

/**
 * The same proof, but against a *real baked asset* rather than a mask synthesised in the test.
 *
 * `basecolor_38b0077028376769.ktx2` is a leaf card's base color exactly as `bakeMaterial` emitted it:
 * a material authored with the Alpha Tested Material Output sink, whose routed alpha therefore took
 * the BC7 path. The case above proves the renderer discards; this one proves the *pipeline* hands it
 * something to discard against -- that the mask survived compositing, UASTC, BC7 and the
 * coverage-preserving mip chain, and reached the GPU intact.
 *
 */
TEST_CASE("A baked cutout material cuts its silhouette out of a plane", "[alphatest][render]")
{
	// Matches the texture's 498x872 aspect, so the leaf is drawn undistorted.
	constexpr float c_LeafWidth  = 8.0f;
	constexpr float c_LeafHeight = 14.0f;

	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;
	sceneDesc.maxPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	// PBR does not render without an environment; there is no default.
	view->SetEnvironmentMap(
		{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

	assetlib::ImageData baseColor =
		assetlib::loadKTX2("assets/Textures/basecolor_38b0077028376769.ktx2");

	// The asset is itself under test. A cutout's base color has to be in a format that *has* alpha: if
	// this ever loads as BC1, the material was re-baked as opaque and the cutout is dead -- which would
	// otherwise surface only as a leaf-shaped rectangle in the golden.
	REQUIRE(baseColor.vkFormat == assetlib::VkFormat::BC7_SRGB_BLOCK);
	CHECK(baseColor.mipLevels > 1);

	const auto texture = scene->AddTextureAsset(std::move(baseColor), "baked-leaf");

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

	auto context     = bgl::RenderContext();
	context.view     = view;
	context.camera   = camera;
	context.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	SECTION("the leaf's silhouette survives the bake - alpha_test_leaf.png")
	{
		auto desc             = bgl::PbrMaterialDesc();
		desc.baseColorTexture = texture;
		desc.metallicFactor   = 0.0f;
		desc.roughnessFactor  = 0.6f;
		desc.layerType        = bgl::LayerType::kMask;
		desc.alphaCutoff      = 0.5f;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, c_LeafWidth, c_LeafHeight, material);
		view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, "assets/golden/alpha_test_leaf.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/alpha_test_leaf.exp.png",
				"assets/golden/alpha_test_leaf.got.png"));
	}

	SECTION("the same map drawn opaque is an unbroken card - alpha_test_leaf_opaque.png")
	{
		// The control. The card is a quad; the leaf shape lives *only* in the alpha channel. Drawn
		// opaque the whole quad fills, which also shows what BC7 chose to store under the transparent
		// texels -- nothing was ever meant to sample there, so it is free to store anything.
		auto desc             = bgl::PbrMaterialDesc();
		desc.baseColorTexture = texture;
		desc.metallicFactor   = 0.0f;
		desc.roughnessFactor  = 0.6f;
		desc.layerType        = bgl::LayerType::kOpaque;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, c_LeafWidth, c_LeafHeight, material);
		view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, "assets/golden/alpha_test_leaf_opaque.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/alpha_test_leaf_opaque.exp.png",
				"assets/golden/alpha_test_leaf_opaque.got.png"));
	}
}
