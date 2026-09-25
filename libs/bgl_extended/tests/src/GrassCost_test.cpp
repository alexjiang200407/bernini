#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <bgl/Camera.h>
#include <bgl/GrassHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/PassTiming.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

// What a street's verge of grass costs the grass pass at 4K and the shipped 0.667 render scale: two
// strips of grass either side of a 71 m street, 82,176 clumps -- the count animal-run's baked
// street grass carries. Not a test of behaviour: run by hand, `just run bgl_extended_tests --
// "[.grasscost]"`, and read the numbers off the warnings it prints. The baked grass it replaces cost
// 0.82 ms of Forward World on the same machine class (docs/plans/gpu-grass.md, Context).

namespace
{
	constexpr uint32_t c_Width       = 3840;
	constexpr uint32_t c_Height      = 2160;
	constexpr float    c_RenderScale = 0.667f;

	constexpr float c_StreetLength = 71.0f;
	constexpr float c_VergeInner   = 3.0f;
	constexpr float c_VergeOuter   = 7.0f;
	constexpr float c_Spacing      = 0.0826f;

	/** Both verges, in 8 x 8 patches so a run of 64 clumps is one patch, jittered by a fixed LCG. */
	assetlib::BGrassFields
	MakeVerges()
	{
		auto grass  = assetlib::BGrassFields();
		grass.looks = { "unused.bgrass" };
		grass.names = { "Verge" };

		uint32_t   state  = 12345u;
		const auto jitter = [&state]() {
			state = state * 1664525u + 1013904223u;
			return (static_cast<float>(state >> 8u) / 16777216.0f - 0.5f) * c_Spacing;
		};

		const auto across =
			static_cast<uint32_t>((c_VergeOuter - c_VergeInner) / c_Spacing) / 8u * 8u;
		const auto along = static_cast<uint32_t>(c_StreetLength / c_Spacing) / 8u * 8u;

		auto field = assetlib::GrassField{ .mesh = 0, .look = 0, .firstChunk = 0, .chunkCount = 0 };
		for (const float side : { -1.0f, 1.0f })
		{
			for (uint32_t tileZ = 0; tileZ < along; tileZ += 8u)
			{
				for (uint32_t tileX = 0; tileX < across; tileX += 8u)
				{
					const auto first = static_cast<uint32_t>(grass.clumps.size());
					glm::vec3  lo(1e30f);
					glm::vec3  hi(-1e30f);
					for (uint32_t z = tileZ; z < tileZ + 8u; ++z)
					{
						for (uint32_t x = tileX; x < tileX + 8u; ++x)
						{
							const glm::vec3 position(
								side *
									(c_VergeInner + static_cast<float>(x) * c_Spacing + jitter()),
								0.0f,
								static_cast<float>(z) * c_Spacing + jitter());
							grass.clumps.push_back(
								assetlib::GrassClump{ .position    = position,
							                          .heightScale = 1.0f,
							                          .normal      = glm::vec3(0.0f, 1.0f, 0.0f),
							                          .color       = glm::u8vec4(255) });
							lo = glm::min(lo, position);
							hi = glm::max(hi, position);
						}
					}
					grass.chunks.push_back(
						assetlib::GrassChunk{ .boundingCenter = (lo + hi) * 0.5f,
					                          .boundingRadius = glm::distance(lo, hi) * 0.5f,
					                          .firstClump     = first,
					                          .clumpCount     = 64u,
					                          .maxHeightScale = 1.0f });
					++field.chunkCount;
				}
			}
		}
		grass.fields = { field };
		return grass;
	}

	/** The median of each named row over a handful of frames after a warm-up. */
	[[nodiscard]] double
	MedianMs(
		bgl::IGraphics&             gfx,
		const bgl::RenderTargetRef& target,
		const bgl::RenderJob&       job,
		const std::string&          row)
	{
		std::vector<double> samples;
		for (int frame = 0; frame < 12; ++frame)
		{
			gfx.DrawFrame(target, job);
			gfx.WaitIdle();
			if (frame < 4)
			{
				continue;
			}
			for (const bgl::PassTiming& timing : gfx.GetPassTimings(target).passes)
			{
				if (timing.name == row)
				{
					samples.push_back(timing.milliseconds);
				}
			}
		}
		REQUIRE(!samples.empty());
		std::ranges::sort(samples);
		return samples[samples.size() / 2];
	}
}

TEST_CASE("what a street's verge of grass costs the grass pass at 4K", "[.grasscost]")
{
	auto opts           = bgl::GraphicsOptions();
	opts.shaderCacheDir = bgl::test::ShaderCacheDir();
	auto gfx            = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc        = bgl::RenderTargetDesc();
	targetDesc.width       = static_cast<int>(c_Width);
	targetDesc.height      = static_cast<int>(c_Height);
	targetDesc.headless    = true;
	targetDesc.taaEnabled  = true;
	targetDesc.renderScale = c_RenderScale;
	auto target            = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);
	target->SetGpuTimingEnabled(true);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 128;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 100000;
	sceneDesc.initialIndices              = 4000;
	sceneDesc.initialPbrMaterials         = 8;
	auto scene                            = gfx->CreateScene(sceneDesc);
	auto view                             = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto groundDesc            = bgl::PbrMaterialDesc();
	groundDesc.baseColorFactor = glm::vec4(0.35f, 0.3f, 0.25f, 1.0f);
	groundDesc.metallicFactor  = 0.0f;
	const auto ground          = scene->CreatePbrMaterial(groundDesc);

	auto grassDesc            = groundDesc;
	grassDesc.baseColorFactor = glm::vec4(0.2f, 0.55f, 0.15f, 1.0f);
	const auto green          = scene->CreatePbrMaterial(grassDesc);

	// Flat, 20 m wide and as long as the street, its middle under the verges.
	const glm::mat4 flat =
		glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, c_StreetLength * 0.5f)) *
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	const auto streetGeom = scene->AddPlaneGeom(1, 1, 20.0f, c_StreetLength, ground);
	static_cast<void>(view->CreateStaticMeshInstance(streetGeom, flat));

	// The grass rides its own geom placed at the origin, so its clumps sit where they were generated.
	const auto vergeGeom = scene->AddPlaneGeom(1, 1, 0.01f, 0.01f, ground);
	static_cast<void>(view->CreateStaticMeshInstance(vergeGeom, glm::mat4(1.0f)));

	auto look                                   = bgl::GrassDesc();
	look.material                               = green;
	look.blade.minHeight                        = 0.25f;
	look.blade.maxHeight                        = 0.45f;
	look.blade.rootWidth                        = 0.03f;
	look.blade.nearSegments                     = 3;
	look.blade.farSegments                      = 1;
	look.clump.bladesPerClump                   = 2;
	look.clump.radius                           = 0.06f;
	look.density.fadeStart                      = 10.0f;
	look.density.fadeEnd                        = 60.0f;
	const std::array<bgl::GrassHandle, 1> looks = { scene->CreateGrass(look) };

	const assetlib::BGrassFields verges = MakeVerges();
	scene->AttachGrass(vergeGeom, verges, 0, looks);

	// A runner's eye down the street.
	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 1.5f, -2.0f),
			glm::vec3(0.0f, 1.0f, 20.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), static_cast<float>(c_Width) / c_Height, 0.1f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	const double grassMs = MedianMs(*gfx, target, job, "Forward Grass 0");
	const double worldMs = MedianMs(*gfx, target, job, "Forward World 0");

	WARN(
		"verge of " << verges.clumps.size() << " clumps in " << verges.chunks.size()
					<< " chunks: Forward Grass 0 " << grassMs << " ms, Forward World 0 " << worldMs
					<< " ms");
	CHECK(grassMs > 0.0);
}
