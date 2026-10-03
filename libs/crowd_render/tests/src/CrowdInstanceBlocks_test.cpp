// CrowdInstanceBlocks against a real renderer and crowd on one context: what it refuses, and that
// frames bracketed by it keep the crowd stepping past its ring -- each frame hands back the ticks
// no later frame reads.
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <crowd_render/CrowdInstanceBlocks.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace
{
	struct Fixture
	{
		bgpu::GpuContextRef context;
		bgl::GraphicsRef    graphics;
		bgl::SceneRef       scene;
		bgl::SceneViewRef   view;
		bgl::GeomHandle     box;

		Fixture()
		{
			auto contextDesc             = bgpu::GpuContextDesc();
			contextDesc.enableDebugLayer = true;
			context                      = bgpu::CreateGpuContext(contextDesc);
			graphics                     = bgl::CreateGraphics(context, bgl::GraphicsOptions());
			scene                        = graphics->CreateScene(bgl::SceneDesc());
			view                         = graphics->CreateSceneView(scene, 8);
			box                          = scene->AddCubeGeom(
				scene->CreatePbrMaterial({ .baseColorFactor = glm::vec4(1.0f) }));
		}

		crowd::CrowdRef
		CreateCrowd(uint32_t renderRingTicks) const
		{
			auto desc            = crowd::CrowdDesc();
			desc.agentTypes      = { crowd::AgentType{ .radius         = 0.3f,
				                                       .preferredSpeed = 1.2f,
				                                       .maxSpeed       = 1.5f,
				                                       .mass           = 80.0f } };
			desc.maxAgents       = 64;
			desc.maxGroups       = 2;
			desc.renderRingTicks = renderRingTicks;
			return crowd::CreateCrowd(context, desc);
		}

		crowd_render::CrowdInstanceBlocksDesc
		Desc(crowd::CrowdRef crowd) const
		{
			return crowd_render::CrowdInstanceBlocksDesc()
			    .SetCrowd(std::move(crowd))
			    .SetGraphics(graphics)
			    .SetView(view)
			    .AddType(crowd_render::AgentTypeMeshDesc().AddGeom(box));
		}
	};
}

TEST_CASE("CrowdInstanceBlocks refuses what it cannot draw", "[crowd_render]")
{
	Fixture f;

	CHECK_THROWS_AS(
		crowd_render::CrowdInstanceBlocks(crowd_render::CrowdInstanceBlocksDesc()),
		std::runtime_error);
	CHECK_THROWS_AS(
		crowd_render::CrowdInstanceBlocks(f.Desc(f.CreateCrowd(0))),
		std::runtime_error);

	auto noTypes  = f.Desc(f.CreateCrowd(5));
	noTypes.types = {};
	CHECK_THROWS_AS(crowd_render::CrowdInstanceBlocks(noTypes), std::runtime_error);

	auto noGeom           = f.Desc(f.CreateCrowd(5));
	noGeom.types[0].geoms = {};
	CHECK_THROWS_WITH(
		crowd_render::CrowdInstanceBlocks(noGeom),
		Catch::Matchers::ContainsSubstring("names no geom"));

	// The contract's refusal, until a block is made per geom.
	auto twoGeoms = f.Desc(f.CreateCrowd(5));
	twoGeoms.types[0].AddGeom(f.box);
	CHECK_THROWS_WITH(
		crowd_render::CrowdInstanceBlocks(twoGeoms),
		Catch::Matchers::ContainsSubstring("not drawn yet"));

	auto blocks = crowd_render::CrowdInstanceBlocks(f.Desc(f.CreateCrowd(5)));
	CHECK_THROWS_AS(blocks.PrepareFrame(-0.1f), std::runtime_error);
	CHECK_THROWS_AS(blocks.PrepareFrame(1.5f), std::runtime_error);
	CHECK_NOTHROW(blocks.PrepareFrame(0.0f));
}

TEST_CASE("A crowd drawn by its blocks steps past its ring", "[crowd_render][render]")
{
	constexpr uint32_t c_Ring = 5;

	Fixture f;
	auto    crowd = f.CreateCrowd(c_Ring);

	auto orders      = crowd::GroupOrders();
	orders.goal      = glm::vec2(4.0f, 0.0f);
	orders.facing    = glm::vec2(1.0f, 0.0f);
	orders.formation = { .frontage = 4, .spacing = 1.0f };
	crowd->CreateGroup({ .agentType = 0, .agentCount = 16, .orders = orders });

	f.view->SetDirectionalLight({ .direction = glm::vec3(0.0f, -1.0f, 0.0f), .intensity = 3.0f });

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
	targetDesc.headless = true;
	auto target         = f.graphics->CreateRenderTarget(targetDesc);

	auto job     = bgl::RenderJob();
	job.view     = f.view;
	job.camera   = bgl::Camera()
	                   .LookAt(glm::vec3(0.0f, 20.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0))
	                   .Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	job.viewport = bgl::Viewport(64.0f, 64.0f);

	{
		auto blocks = crowd_render::CrowdInstanceBlocks(f.Desc(crowd));

		// Three times the ring: without the releases the crowd would stop at c_Ring ticks.
		for (uint32_t frame = 0; frame < 3 * c_Ring; ++frame)
		{
			INFO("frame " << frame);
			REQUIRE(crowd->CanStep());
			crowd->Step();
			crowd->Wait();

			blocks.PrepareFrame(0.5f);
			f.graphics->DrawFrame(target, job);
			blocks.FinishFrame();

			const uint64_t drawn = crowd->GetCompletedTick();
			CHECK(crowd->GetReleasedRenderTick() == (drawn > 3 ? drawn - 3 : 0));
		}
		CHECK(crowd->GetCompletedTick() == 3 * c_Ring);
		f.graphics->WaitIdle();
	}
	crowd->Wait();
}
