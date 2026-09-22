#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/SyntheticCube.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/envmap.h>
#include <assetlib/image_io.h>
#include <assetlib/import_document.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

// The environment bake stores an LDR sky and prefilter as BC7 sRGB where it used to store RGB9E5
// (docs/envmaps.md). assetlib pins the encode against RGB9E5 on the CPU, but no CPU decoder reads a
// BC7 block, so that measures the UASTC payload the file is transcoded from. This is the other half:
// the actual BC7 maps the bake wrote, uploaded, sampled and shaded, against the same environment
// packed RGB9E5 -- the sampler's sRGB decode and the block format's upload path included.

namespace
{
	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	constexpr uint32_t c_SourceFace     = 64;
	constexpr uint32_t c_IrradianceFace = 32;
	constexpr uint32_t c_PrefilterMips  = 7;  // must stay MAX_REFLECTION_LOD + 1

	// 40 dB, the bar the bake's own test holds a compressed sky to, as a normalized frame MSE.
	constexpr float c_MaxFrameMse = 1e-4f;

	/**
	 * A painted-sky stand-in, every value within [0, 1]: a vertical gradient per face with a hue
	 * shift across it and a soft bright disc, so both the backdrop and a reflection have structure
	 * that block compression could get wrong.
	 */
	assetlib::ImageData
	GradientSky(uint32_t faceSize)
	{
		auto  out = bgl::test::MakeBlackFloatCube(faceSize);
		auto* px  = reinterpret_cast<float*>(out.pixels.data());

		const size_t perFace = static_cast<size_t>(faceSize) * faceSize;
		for (uint32_t face = 0; face < 6; ++face)
			for (uint32_t y = 0; y < faceSize; ++y)
				for (uint32_t x = 0; x < faceSize; ++x)
				{
					const float u  = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize);
					const float v  = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize);
					const float du = u - 0.3f - 0.07f * static_cast<float>(face);
					const float dv = v - 0.4f;
					const float disc = 0.35f * std::exp(-(du * du + dv * dv) / 0.01f);

					const size_t t = (face * perFace + static_cast<size_t>(y) * faceSize + x) * 4;
					px[t + 0]      = std::min(1.0f, 0.05f + 0.35f * v + 0.1f * u + disc);
					px[t + 1]      = std::min(1.0f, 0.10f + 0.45f * v + disc);
					px[t + 2]      = std::min(1.0f, 0.30f + 0.55f * v - 0.1f * u + disc);
					px[t + 3]      = 1.0f;
				}
		return out;
	}

	/** One environment, as three decoded maps ready to upload. */
	struct EnvMaps
	{
		assetlib::ImageData sky;
		assetlib::ImageData prefilter;
		assetlib::ImageData irradiance;

		// Move-only, following ImageData.
		EnvMaps(
			assetlib::ImageData skyMap,
			assetlib::ImageData prefilterMap,
			assetlib::ImageData irradianceMap) :
			sky(std::move(skyMap)), prefilter(std::move(prefilterMap)),
			irradiance(std::move(irradianceMap))
		{}
		EnvMaps(EnvMaps&&) noexcept = default;
		EnvMaps(const EnvMaps&)     = delete;
		EnvMaps&
		operator=(EnvMaps&&) noexcept = default;
		EnvMaps&
		operator=(const EnvMaps&) = delete;
	};

	/**
	 * One environment as a project imports it: a gradient cube under `Authored/EnvSources` with the
	 * `.bimport` beside it that says what it is cooked at, which is what a bake reads.
	 */
	struct Source
	{
		std::filesystem::path                 root;
		assetlib::ImageData                   radiance;
		assetlib::EnvironmentImportParameters parameters;

		Source() : root(std::filesystem::temp_directory_path() / "bernini_env_compression")
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / assetlib::c_EnvSourcesDirectoryName);

			radiance = GradientSky(c_SourceFace);

			parameters.skyFaceSize        = c_SourceFace;
			parameters.skyMips            = 1;
			parameters.prefilterFaceSize  = c_SourceFace;
			parameters.prefilterMips      = c_PrefilterMips;
			parameters.prefilterSamples   = 32;
			parameters.irradianceFaceSize = c_IrradianceFace;

			assetlib::writeKTX2(radiance, root / c_Key, false, assetlib::Ktx2Compression::kNone);

			auto document        = assetlib::ImportDocument();
			document.source      = c_Key;
			document.environment = parameters;
			assetlib::AssetStore(root).Save(document, assetlib::importDocumentKeyFor(c_Key));
		}

		~Source() { std::filesystem::remove_all(root); }

		Source(const Source&) = delete;
		Source&
		operator=(const Source&) = delete;

		/** What the bake makes of it: BC7 sky and prefilter, RGB9E5 irradiance. */
		[[nodiscard]] EnvMaps
		Baked() const
		{
			const auto store = assetlib::AssetStore(root);

			auto bakedSky       = assetlib::BSky();
			bakedSky.sky.source = c_Key;
			store.BakeSky(bakedSky);

			auto lighting              = assetlib::BEnvLighting();
			lighting.prefilter.source  = c_Key;
			lighting.irradiance.source = c_Key;
			store.BakeEnvLighting(lighting);

			return { assetlib::loadKTX2(root / bakedSky.sky.baked),
				     assetlib::loadKTX2(root / lighting.prefilter.baked),
				     assetlib::loadKTX2(root / lighting.irradiance.baked) };
		}

		/**
		 * The same three maps, convolved by the same functions at the same parameters, packed as
		 * every one of them shipped before BC7: RGB9E5.
		 */
		[[nodiscard]] EnvMaps
		Rgb9e5() const
		{
			auto desc      = assetlib::PrefilterDesc();
			desc.faceSize  = parameters.prefilterFaceSize;
			desc.mipLevels = parameters.prefilterMips;
			desc.samples   = parameters.prefilterSamples;

			return { assetlib::packRgb9e5(
						 assetlib::skyChain(radiance, parameters.skyFaceSize, parameters.skyMips)),
				     assetlib::packRgb9e5(assetlib::prefilterRadiance(radiance, desc, nullptr)),
				     assetlib::packRgb9e5(
						 assetlib::irradianceSh(radiance, parameters.irradianceFaceSize)) };
		}

	private:
		static constexpr const char* c_Key = "Authored/EnvSources/sky.ktx2";
	};

	/**
	 * A glossy metal sphere in front of the skybox, lit only by `maps`, shot from +Z and left at
	 * `path`. A fresh device per shot, so the TAA jitter sequence -- and so every sample position --
	 * is the same for both encodings and the frames differ only by what they sampled.
	 */
	void
	Shoot(EnvMaps maps, const std::string& path)
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
		auto target         = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 400000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 4;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 4);

		auto irradiance = scene->AddTextureAsset(std::move(maps.irradiance), "env_irradiance");
		auto prefilter  = scene->AddTextureAsset(std::move(maps.prefilter), "env_prefilter");
		auto skybox     = scene->AddTextureAsset(std::move(maps.sky), "env_sky");
		REQUIRE(irradiance.textureSlot);
		REQUIRE(prefilter.textureSlot);
		REQUIRE(skybox.textureSlot);

		view->SetEnvironmentMap({ irradiance, prefilter });
		view->SetSkyBox(bgl::SkyboxDesc{ skybox });
		view->SetExposure(1.0f);

		const auto glossy = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = 1.0f,
		      .roughnessFactor = 0.3f });
		const auto sphere = scene->AddSphereGeom(32, 32, 5.0f, glossy);
		(void)view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));

		auto camera = bgl::Camera();
		camera.LookAt(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);

		gfx->ScreenshotPng(target, path);
	}

	// The sphere is radius 5 seen from 20 through 60 degrees, about 66 px around (200, 150); a box
	// at its centre and one in a frame corner, outside it, where only the backdrop is drawn.
	constexpr int c_BoxSize   = 16;
	constexpr int c_SphereX   = 192;
	constexpr int c_SphereY   = 142;
	constexpr int c_BackdropX = 10;
	constexpr int c_BackdropY = 20;
}

TEST_CASE(
	"A BC7 environment renders within 40 dB of its RGB9E5 bake",
	"[ibl][skybox][envbake][render]")
{
	const Source source;

	auto baked = source.Baked();
	REQUIRE(baked.sky.vkFormat == assetlib::VkFormat::BC7_SRGB_BLOCK);
	REQUIRE(baked.prefilter.vkFormat == assetlib::VkFormat::BC7_SRGB_BLOCK);
	REQUIRE(baked.prefilter.mipLevels == c_PrefilterMips);

	const std::string bc7    = "assets/golden/env_compression_bc7.got.png";
	const std::string rgb9e5 = "assets/golden/env_compression_rgb9e5.got.png";

	Shoot(std::move(baked), bc7);
	Shoot(source.Rgb9e5(), rgb9e5);

	const auto sphere   = bgl::test::MeanColor(bc7, c_SphereX, c_SphereY, c_BoxSize, c_BoxSize);
	const auto backdrop = bgl::test::MeanColor(bc7, c_BackdropX, c_BackdropY, c_BoxSize, c_BoxSize);

	const float mse = bgl::test::FrameDelta(
		bc7,
		rgb9e5,
		0,
		0,
		static_cast<int>(c_Width),
		static_cast<int>(c_Height));
	const float psnr =
		bgl::test::PsnrDb(bc7, rgb9e5, 0, 0, static_cast<int>(c_Width), static_cast<int>(c_Height));

	INFO(
		"frame mse " << mse << " (" << psnr << " dB), sphere luma " << sphere.Luma()
					 << ", backdrop luma " << backdrop.Luma());

	// Both boxes landed on something lit, so the comparison is between two drawn environments.
	REQUIRE(sphere.Luma() > 0.05f);
	REQUIRE(backdrop.Luma() > 0.05f);

	CHECK(mse <= c_MaxFrameMse);

	// Kept on a failure, as a golden's `.got.png` is, so the two frames can be compared by eye.
	if (mse <= c_MaxFrameMse)
	{
		std::filesystem::remove(bc7);
		std::filesystem::remove(rgb9e5);
	}
}
