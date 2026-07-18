#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

/**
 * The override, proven at the pixel.
 *
 * Every other override test asserts on CPU state -- the resolved SubmeshInstance, a reference count.
 * None of them can catch a mesh shader that went back to reading `submesh.material`, or a counting
 * sort that bucketed both instances of a geom into one PSO. Only a frame can.
 *
 * Two instances of ONE plane geom, side by side. They share every byte of geometry; the only thing
 * that differs is the override. So any difference on screen has exactly one possible cause.
 *
 * These compare two regions of the same frame against each other rather than against a stored PNG, so
 * there is no golden to regenerate when the lighting or the plane changes.
 */

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	// Where the two instances sit, and where they land on screen. The camera is 20 back with a 60-deg
	// vertical fov, so the visible half-width at z=0 is 20*tan(30)*aspect ~= 15.4 world units: a plane
	// centred at x=+-4 falls around x=296 / x=504 in pixels, and a 6-wide plane covers ~156 of them.
	// The 80x80 sample boxes below sit comfortably inside that.
	constexpr float c_PlaneSize   = 6.0f;
	constexpr float c_PlaneOffset = 4.0f;

	constexpr int c_SampleSize = 80;
	constexpr int c_LeftX      = 296 - c_SampleSize / 2;
	constexpr int c_RightX     = 504 - c_SampleSize / 2;
	constexpr int c_SampleY    = 300 - c_SampleSize / 2;

	constexpr uint32_t c_MaskSize = 256;

	// An opaque green card with a transparent disc punched out of the middle. Only a material whose
	// layer is kAlphaTest tests against that alpha; an opaque one ignores it.
	assetlib::ImageData
	makeHoleMask()
	{
		const float centre = static_cast<float>(c_MaskSize) * 0.5f;
		const float radius = static_cast<float>(c_MaskSize) * 0.35f;

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

	glm::mat4
	At(float x)
	{
		return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
	}
}

TEST_CASE(
	"A material override paints one instance and not its sibling",
	"[material][override][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 4;
	sceneDesc.maxMeshlets             = 128;
	sceneDesc.maxSubmeshes            = 4;
	sceneDesc.maxVertexBufferByteSize = 100000;
	sceneDesc.maxIndices              = 4000;
	sceneDesc.maxPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	// PBR does not render without an environment; there is no default.
	view->SetEnvironmentMap(
		{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

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

	const auto capture = [&](const char* name) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, path);

		return path;
	};

	const auto sampleLeft = [](const std::string& png) {
		return bgl::test::MeanColor(png, c_LeftX, c_SampleY, c_SampleSize, c_SampleSize);
	};
	const auto sampleRight = [](const std::string& png) {
		return bgl::test::MeanColor(png, c_RightX, c_SampleY, c_SampleSize, c_SampleSize);
	};

	SECTION("the base colour follows the override, on that instance alone")
	{
		// The default: an unmistakably red plane.
		auto redDesc            = bgl::PbrMaterialDesc();
		redDesc.baseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		redDesc.metallicFactor  = 0.0f;
		redDesc.roughnessFactor = 1.0f;

		auto blueDesc            = bgl::PbrMaterialDesc();
		blueDesc.baseColorFactor = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
		blueDesc.metallicFactor  = 0.0f;
		blueDesc.roughnessFactor = 1.0f;

		const auto red  = scene->CreatePbrMaterial(redDesc);
		const auto blue = scene->CreatePbrMaterial(blueDesc);

		// ONE geom. Both instances share it, so the geometry cannot explain any difference below.
		const auto plane = scene->AddPlaneGeom(1, 1, c_PlaneSize, c_PlaneSize, red);

		const auto worn  = view->CreateStaticMeshInstance(plane, At(-c_PlaneOffset));
		const auto plain = view->CreateStaticMeshInstance(plane, At(c_PlaneOffset));

		{
			const std::string png = capture("bernini_override_before");

			const bgl::test::Rgba left  = sampleLeft(png);
			const bgl::test::Rgba right = sampleRight(png);

			// Both hit lit geometry, and both are red. The luma check is what stops a mis-aimed sample
			// box from making the rest of this vacuous: background is black, so a miss reads ~0.
			CHECK(left.Luma() > 0.02f);
			CHECK(right.Luma() > 0.02f);
			CHECK(left.r > left.b);
			CHECK(right.r > right.b);

			std::filesystem::remove(png);
		}

		view->SetSubmeshMaterialOverride(worn, 0, blue);

		{
			const std::string png = capture("bernini_override_after");

			const bgl::test::Rgba left  = sampleLeft(png);
			const bgl::test::Rgba right = sampleRight(png);

			// The overridden instance turned blue...
			CHECK(left.Luma() > 0.02f);
			CHECK(left.b > left.r);

			// ...and its sibling, drawn from the very same submesh, did not.
			CHECK(right.Luma() > 0.02f);
			CHECK(right.r > right.b);
		}

		// Clearing puts it back, which no stale-cache bug would survive.
		view->ClearSubmeshMaterialOverride(worn, 0);
		{
			const std::string png = capture("bernini_override_cleared");

			const bgl::test::Rgba left = sampleLeft(png);
			CHECK(left.r > left.b);

			std::filesystem::remove(png);
		}

		(void)plain;
	}

	SECTION("the PSO follows the override: one geom drawn by two pipelines in one frame")
	{
		// The strongest claim the feature makes. The override flips the *layer*, so the two instances
		// land in different PSO buckets -- kOpaque_StaticMesh_PBR and kAlphaTest_StaticMesh_PBR -- and
		// the counting sort has to dispatch the same submesh twice, under two different pipelines, in
		// a single frame. If it bucketed per submesh (as it did before), both would draw opaque and the
		// hole below would never appear.
		const auto masked = scene->AddTextureAsset(makeHoleMask(), "override-mask");

		auto opaqueDesc             = bgl::PbrMaterialDesc();
		opaqueDesc.baseColorTexture = masked;
		opaqueDesc.metallicFactor   = 0.0f;
		opaqueDesc.roughnessFactor  = 1.0f;
		opaqueDesc.layerType        = bgl::LayerType::kOpaque;

		auto cutoutDesc             = bgl::PbrMaterialDesc();
		cutoutDesc.baseColorTexture = masked;
		cutoutDesc.metallicFactor   = 0.0f;
		cutoutDesc.roughnessFactor  = 1.0f;
		cutoutDesc.layerType        = bgl::LayerType::kMask;
		cutoutDesc.alphaCutoff      = 0.5f;

		const auto opaque = scene->CreatePbrMaterial(opaqueDesc);
		const auto cutout = scene->CreatePbrMaterial(cutoutDesc);

		const auto plane = scene->AddPlaneGeom(1, 1, c_PlaneSize, c_PlaneSize, opaque);

		const auto worn  = view->CreateStaticMeshInstance(plane, At(-c_PlaneOffset));
		const auto plain = view->CreateStaticMeshInstance(plane, At(c_PlaneOffset));

		view->SetSubmeshMaterialOverride(worn, 0, cutout);

		const std::string png = capture("bernini_override_pso");

		const bgl::test::Rgba left  = sampleLeft(png);
		const bgl::test::Rgba right = sampleRight(png);

		// The sample boxes sit over the middle of each plane, which is where the mask is transparent.
		// The overridden instance discarded there, so the box sees background: dark.
		CHECK(left.Luma() < 0.02f);

		// The sibling, same submesh and same texture, kept its opaque layer and painted the green card
		// straight over the hole.
		CHECK(right.Luma() > 0.02f);
		CHECK(right.g > right.r);
		CHECK(right.g > right.b);

		std::filesystem::remove(png);

		(void)plain;
	}
}
