#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <bgl/Camera.h>
#include <bgl/GrassHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
		bgl::GrassHandle        look;
		bgl::RenderJob          job;

		GrassScene()
		{
			auto opts             = bgl::GraphicsOptions();
			opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
			opts.enableDebugLayer = true;
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
			scene                                 = gfx->CreateScene(sceneDesc);
			view                                  = gfx->CreateSceneView(scene, 8);
			bgl::test::ApplyEnvironment(scene.Get(), view.Get());

			auto whiteDesc            = bgl::PbrMaterialDesc();
			whiteDesc.metallicFactor  = 0.0f;
			whiteDesc.roughnessFactor = 1.0f;
			const auto white          = scene->CreatePbrMaterial(whiteDesc);

			auto greenDesc            = whiteDesc;
			greenDesc.baseColorFactor = glm::vec4(0.1f, 0.8f, 0.1f, 1.0f);
			const auto green          = scene->CreatePbrMaterial(greenDesc);

			ground         = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, white);
			groundInstance = view->CreateStaticMeshInstance(ground, c_Flat);

			auto lookDesc            = bgl::GrassDesc();
			lookDesc.material        = green;
			lookDesc.blade.rootWidth = 0.05f;
			look                     = scene->CreateGrass(lookDesc);

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
