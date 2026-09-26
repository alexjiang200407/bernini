#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/Camera.h>
#include <bgl/GrassHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/LoosePbrMaterialDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/WindDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/**
 * The grass pass, proven at the pixel: a field of green blades on a white ground, compared against
 * the same scene with the field detached. Captures are compared with captures, never with a stored
 * PNG.
 */

namespace
{
	constexpr uint32_t c_Width  = 640;
	constexpr uint32_t c_Height = 480;

	// The plane geoms are authored in XY; this lays one flat with its normal up.
	const glm::mat4 c_Flat =
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	/**
	 * One field of `side` x `side` clumps `spacing` apart over the plane's XY, centred, growing
	 * along +Z (the plane's normal), chunked in rows the way a cook's Morton order would roughly.
	 */
	assetlib::BGrassFields
	MakeField(const uint32_t side, const float spacing)
	{
		auto grass  = assetlib::BGrassFields();
		grass.looks = { "unused.bgrass" };
		grass.names = { "Ground" };

		const float origin = -0.5f * spacing * static_cast<float>(side - 1);
		for (uint32_t y = 0; y < side; ++y)
		{
			for (uint32_t x = 0; x < side; ++x)
			{
				grass.clumps.push_back(
					assetlib::GrassClump{ .position = glm::vec3(
											  origin + spacing * static_cast<float>(x),
											  origin + spacing * static_cast<float>(y),
											  0.0f),
				                          .heightScale = 1.0f,
				                          .normal      = glm::vec3(0.0f, 0.0f, 1.0f),
				                          .color       = glm::u8vec4(255) });
			}
		}

		auto field = assetlib::GrassField{ .mesh = 0, .look = 0, .firstChunk = 0, .chunkCount = 0 };
		for (uint32_t first = 0; first < grass.clumps.size();
		     first += assetlib::c_GrassClumpsPerChunk)
		{
			const auto count = std::min<uint32_t>(
				assetlib::c_GrassClumpsPerChunk,
				static_cast<uint32_t>(grass.clumps.size()) - first);

			glm::vec3 lo(1e30f);
			glm::vec3 hi(-1e30f);
			for (uint32_t k = first; k < first + count; ++k)
			{
				lo = glm::min(lo, grass.clumps[k].position);
				hi = glm::max(hi, grass.clumps[k].position);
			}

			grass.chunks.push_back(
				assetlib::GrassChunk{ .boundingCenter = (lo + hi) * 0.5f,
			                          .boundingRadius = glm::distance(lo, hi) * 0.5f,
			                          .firstClump     = first,
			                          .clumpCount     = count,
			                          .maxHeightScale = 1.0f });
			++field.chunkCount;
		}
		grass.fields = { field };
		return grass;
	}

	struct GrassScene
	{
		bgl::GraphicsRef        gfx;
		bgl::RenderTargetRef    target;
		bgl::SceneRef           scene;
		bgl::SceneViewRef       view;
		bgl::GeomHandle         ground;
		bgl::MeshInstanceHandle groundInstance;
		bgl::MaterialHandle     green;
		bgl::GrassDesc          desc;
		bgl::GrassHandle        look;
		bgl::RenderJob          job;

		GrassScene()
		{
			auto opts             = bgl::GraphicsOptions();
			opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
			opts.enableDebugLayer = true;
			opts.surfaceShaderDir = "./shaders/tests/surfaces";
			gfx                   = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = static_cast<int>(c_Width);
			targetDesc.height   = static_cast<int>(c_Height);
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			auto sceneDesc                        = bgl::SceneDesc();
			sceneDesc.initialGeom                 = 4;
			sceneDesc.initialMeshlets             = 128;
			sceneDesc.initialSubmeshes            = 4;
			sceneDesc.initialVertexBufferByteSize = 100000;
			sceneDesc.initialIndices              = 4000;
			sceneDesc.initialPbrMaterials         = 8;
			sceneDesc.initialSurfaceMaterials     = 8;
			scene                                 = gfx->CreateScene(sceneDesc);
			view                                  = gfx->CreateSceneView(scene, 8);
			bgl::test::ApplyEnvironment(scene.Get(), view.Get());

			auto whiteDesc            = bgl::PbrMaterialDesc();
			whiteDesc.metallicFactor  = 0.0f;
			whiteDesc.roughnessFactor = 1.0f;
			const auto white          = scene->CreatePbrMaterial(whiteDesc);

			auto greenDesc            = whiteDesc;
			greenDesc.baseColorFactor = glm::vec4(0.1f, 0.8f, 0.1f, 1.0f);
			green                     = scene->CreatePbrMaterial(greenDesc);

			ground         = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, white);
			groundInstance = view->CreateStaticMeshInstance(ground, c_Flat);

			desc.material        = green;
			desc.blade.rootWidth = 0.05f;
			look                 = scene->CreateGrass(desc);

			auto camera = bgl::Camera();
			camera.LookAt(glm::vec3(0.0f, 3.0f, 6.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(
					glm::radians(60.0f),
					static_cast<float>(c_Width) / static_cast<float>(c_Height),
					0.1f,
					200.0f);

			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
		}

		void
		Attach(const assetlib::BGrassFields& field) const
		{
			const std::array<bgl::GrassHandle, 1> looks = { look };
			scene->AttachGrass(ground, field, 0, looks);
		}

		void
		Detach() const
		{
			scene->AttachGrass(ground, assetlib::BGrassFields(), 0, {});
		}

		void
		Relook(const bgl::GrassDesc& changed)
		{
			desc = changed;
			scene->UpdateGrass(look, desc);
		}

		/**
		 * The mean colour of the central box once enough frames have passed for TAA to have blended
		 * the previous frames' scene out of its history.
		 */
		[[nodiscard]] bgl::test::Rgba
		Settled(const char* name) const
		{
			for (int i = 0; i < 23; ++i) gfx->DrawFrame(target, job);
			return Centre(name);
		}

		/** The mean colour of the frame's central box, rendered now. */
		[[nodiscard]] bgl::test::Rgba
		Centre(const char* name) const
		{
			const auto path =
				(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();
			gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, path);
			const bgl::test::Rgba box = bgl::test::MeanColor(path, 220, 200, 200, 120);
			std::filesystem::remove(path);
			return box;
		}
	};
}

TEST_CASE("Grass draws its blades over the ground it grows on", "[grass][render]")
{
	const GrassScene grass;

	const bgl::test::Rgba bare = grass.Centre("bernini_grass_bare");

	grass.Attach(MakeField(40, 0.12f));
	const bgl::test::Rgba grown = grass.Centre("bernini_grass_grown");

	grass.Detach();
	const bgl::test::Rgba again = grass.Centre("bernini_grass_detached");

	// The white ground reads grey-white; the green blades pull the box toward green.
	CHECK(grown.g - grown.r > bare.g - bare.r + 0.1f);
	CHECK(again.g - again.r < grown.g - grown.r);
}

TEST_CASE("Still grass under a still camera writes no motion", "[grass][render][motionvectors]")
{
	const GrassScene grass;
	grass.Attach(MakeField(40, 0.12f));

	grass.gfx->DrawFrame(grass.target, grass.job);
	grass.gfx->DrawFrame(grass.target, grass.job);

	const std::vector<glm::vec4> motion =
		bgl::test::ReadVelocityTexels(grass.gfx.Get(), grass.target.Get(), c_Width, c_Height);
	for (const glm::vec4& texel : motion)
	{
		REQUIRE(std::abs(texel.x) < 1e-4f);
		REQUIRE(std::abs(texel.y) < 1e-4f);
	}
}

TEST_CASE(
	"Grass moves with its placement, and says so in the velocity",
	"[grass][render][motionvectors]")
{
	const GrassScene grass;
	grass.Attach(MakeField(40, 0.12f));
	grass.gfx->DrawFrame(grass.target, grass.job);

	grass.view->SetInstanceTransform(
		grass.groundInstance,
		glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, 0.0f)) * c_Flat);
	grass.gfx->DrawFrame(grass.target, grass.job);

	const std::vector<glm::vec4> motion =
		bgl::test::ReadVelocityTexels(grass.gfx.Get(), grass.target.Get(), c_Width, c_Height);
	const glm::vec4 centre = motion[(c_Height / 2) * c_Width + c_Width / 2];

	INFO("centre velocity " << centre.x << ", " << centre.y);
	CHECK(std::abs(centre.x) > 1e-3f);
	CHECK(std::abs(centre.y) < std::abs(centre.x));
}

namespace
{
	/** The largest motion any texel of the frame just drawn wrote, in either axis. */
	[[nodiscard]] float
	LargestMotion(const GrassScene& grass)
	{
		float largest = 0.0f;
		for (const glm::vec4& texel :
		     bgl::test::ReadVelocityTexels(grass.gfx.Get(), grass.target.Get(), c_Width, c_Height))
		{
			largest = std::max({ largest, std::abs(texel.x), std::abs(texel.y) });
		}
		return largest;
	}
}

// Wind is evaluated at this frame's time and the last one's, so a blowing field writes its sway
// into the velocity TAA reprojects with, and a calm one writes none however the clock runs.
TEST_CASE("Wind moves blades, and says so in the velocity", "[grass][render][motionvectors]")
{
	GrassScene grass;
	grass.Attach(MakeField(40, 0.12f));

	grass.job.time = 0.0f;
	grass.gfx->DrawFrame(grass.target, grass.job);
	grass.job.time = 0.1f;
	grass.gfx->DrawFrame(grass.target, grass.job);
	CHECK(LargestMotion(grass) < 1e-4f);

	auto wind         = bgl::WindDesc();
	wind.strength     = 0.4f;
	wind.gustStrength = 0.4f;
	grass.view->SetWind(wind);

	grass.job.time = 0.2f;
	grass.gfx->DrawFrame(grass.target, grass.job);
	grass.job.time = 0.3f;
	grass.gfx->DrawFrame(grass.target, grass.job);
	CHECK(LargestMotion(grass) > 1e-3f);
}

namespace
{
	const glm::vec4 c_Green = glm::vec4(0.1f, 0.8f, 0.1f, 1.0f);

	// One texel of a colour quantized to eight bits, and a little for TAA's last blend.
	constexpr float c_SameMargin = 2.0f / 255.0f;

	[[nodiscard]] bool
	Same(const bgl::test::Rgba& a, const bgl::test::Rgba& b)
	{
		return std::abs(a.r - b.r) < c_SameMargin && std::abs(a.g - b.g) < c_SameMargin &&
		       std::abs(a.b - b.b) < c_SameMargin;
	}

	/** A sun travelling toward the camera, so a blade turned to the camera stands against it. */
	void
	BackLight(const GrassScene& grass)
	{
		grass.view->SetDirectionalLight(
			{ .direction = glm::normalize(glm::vec3(0.0f, -0.3f, 1.0f)),
		      .color     = glm::vec3(1.0f),
		      .intensity = 3.0f });
	}

	/** Green, matte and with no specular lobe: lit by its normal and nothing else. */
	[[nodiscard]] bgl::MaterialHandle
	MatteGreen(bgl::IScene& scene, const bgl::TextureAssetHandle occlusion = {})
	{
		return scene.CreatePbrMaterial(
			{ .baseColorFactor          = c_Green,
		      .metallicFactor           = 0.0f,
		      .roughnessFactor          = 1.0f,
		      .specularFactor           = 0.0f,
		      .geometryOcclusionTexture = occlusion });
	}

	/** One fully occluding texel. */
	[[nodiscard]] assetlib::ImageData
	BlackMap()
	{
		auto image      = assetlib::ImageData();
		image.width     = 1;
		image.height    = 1;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_UNORM;
		image.isCubemap = false;
		image.pixels    = core::fixed_buffer<std::byte>(4);
		image.pixels[0] = std::byte{ 0 };
		image.pixels[1] = std::byte{ 0 };
		image.pixels[2] = std::byte{ 0 };
		image.pixels[3] = std::byte{ 255 };
		image.subresources.push_back({ 0, 4, 4 });
		return image;
	}
}

// ADR-10: translucency is added after the engine's lighting, so it reaches the engine's own kinds
// and a surface the engine lights, and never a surface that owns its lighting.
TEST_CASE(
	"Translucency lights a blade the engine lights, and never one that lights itself",
	"[grass][render][lighting]")
{
	GrassScene grass;
	grass.Attach(MakeField(40, 0.12f));
	BackLight(grass);

	struct Case
	{
		const char*         name;
		bgl::MaterialHandle material;
		bool                glows;
	};
	const std::array<Case, 3> cases = {
		{ { "pbr", grass.green, true },
		  { "surface",
		    grass.scene->CreateSurfaceMaterial(
				{ .surface = "PbrLike",
		          .values  = { { "baseColorFactor", c_Green },
		                       { "roughnessFactor", glm::vec4(1.0f) },
		                       { "metallicFactor", glm::vec4(0.0f) } } }),
		    true },
		  { "lit",
		    grass.scene->CreateSurfaceMaterial(
				{ .surface = "Unlit", .values = { { "color", c_Green } } }),
		    false } }
	};

	for (const Case& c : cases)
	{
		INFO(c.name);
		auto look                  = grass.desc;
		look.material              = c.material;
		look.lighting.translucency = 0.0f;
		grass.Relook(look);
		const bgl::test::Rgba opaque = grass.Settled("bernini_grass_opaque");

		look.lighting.translucency = 1.0f;
		grass.Relook(look);
		const bgl::test::Rgba translucent = grass.Settled("bernini_grass_translucent");

		INFO("opaque g " << opaque.g << ", translucent g " << translucent.g);
		if (c.glows)
		{
			CHECK(translucent.g > opaque.g + 0.05f);
		}
		else
		{
			CHECK(Same(translucent, opaque));
		}
	}
}

// At a blend of 1 near and far every blade shades with its clump's ground normal, so a field of the
// ground's own matte material vanishes into it: nothing else a blade does changes the light it gets.
TEST_CASE(
	"A full ground-normal blend shades every blade as the ground it grows on",
	"[grass][render][lighting]")
{
	GrassScene                grass;
	const bgl::MaterialHandle matte = MatteGreen(*grass.scene);
	grass.view->SetSubmeshMaterialOverride(grass.groundInstance, 0, matte);

	auto look                      = grass.desc;
	look.material                  = matte;
	look.lighting.rootOcclusion    = 0.0f;
	look.lighting.groundNormalNear = 1.0f;
	look.lighting.groundNormalFar  = 1.0f;
	grass.Relook(look);
	const bgl::test::Rgba bare = grass.Settled("bernini_grass_ground_bare");

	grass.Attach(MakeField(40, 0.12f));
	const bgl::test::Rgba grounded = grass.Settled("bernini_grass_ground_blend");

	look.lighting.groundNormalNear = 0.0f;
	look.lighting.groundNormalFar  = 0.0f;
	grass.Relook(look);
	const bgl::test::Rgba own = grass.Settled("bernini_grass_own_normal");

	INFO("bare g " << bare.g << ", grounded g " << grounded.g << ", own g " << own.g);
	CHECK(Same(grounded, bare));
	CHECK(std::abs(own.g - bare.g) > 0.02f);
}

// uv1 on a blade is its random, not a second UV set, so a material's geometry occlusion map is read
// as white -- for the engine's kinds and for a surface sampling one through its reader alike.
TEST_CASE("A blade reads a material's geometry occlusion as absent", "[grass][render][lighting]")
{
	GrassScene grass;
	grass.Attach(MakeField(40, 0.12f));
	const bgl::TextureAssetHandle black =
		grass.scene->AddTextureAsset(BlackMap(), "black_occlusion");

	const auto loose = [&](const bgl::TextureAssetHandle occlusion) {
		auto desc                     = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor          = c_Green;
		desc.metallicFactor           = 0.0f;
		desc.roughnessFactor          = 1.0f;
		desc.specularFactor           = 0.0f;
		desc.geometryOcclusionTexture = occlusion;
		return grass.scene->CreateLoosePbrMaterial(desc);
	};
	const auto surface = [&](const bgl::TextureAssetHandle occlusion) {
		auto desc    = bgl::SurfaceMaterialDesc();
		desc.surface = "PbrLike";
		desc.values  = { { "baseColorFactor", c_Green },
			             { "roughnessFactor", glm::vec4(1.0f) },
			             { "metallicFactor", glm::vec4(0.0f) } };
		if (occlusion.textureSlot)
			desc.textures.push_back({ .name = "geometryOcclusion", .texture = occlusion });
		return grass.scene->CreateSurfaceMaterial(desc);
	};

	struct Case
	{
		const char*         name;
		bgl::MaterialHandle plain;
		bgl::MaterialHandle mapped;
	};
	const std::array<Case, 3> cases = {
		{ { "pbr", MatteGreen(*grass.scene), MatteGreen(*grass.scene, black) },
		  { "loose", loose({}), loose(black) },
		  { "surface", surface({}), surface(black) } }
	};

	for (const Case& c : cases)
	{
		INFO(c.name);
		auto look     = grass.desc;
		look.material = c.plain;
		grass.Relook(look);
		const bgl::test::Rgba plain = grass.Settled("bernini_grass_occlusion_plain");

		look.material = c.mapped;
		grass.Relook(look);
		const bgl::test::Rgba mapped = grass.Settled("bernini_grass_occlusion_mapped");

		INFO("plain g " << plain.g << ", mapped g " << mapped.g);
		CHECK(Same(mapped, plain));
	}
}
