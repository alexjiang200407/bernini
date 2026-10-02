#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <cstdint>
#include <utility>

namespace
{
	bgpu::GpuContextRef
	MakeContext()
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		return bgpu::CreateGpuContext(desc);
	}

	crowd::CrowdRef
	MakeMarchingCrowd(bgpu::GpuContextRef context)
	{
		auto desc       = crowd::CrowdDesc();
		desc.agentTypes = { crowd::AgentType{ .radius         = 0.3f,
			                                  .preferredSpeed = 1.2f,
			                                  .maxSpeed       = 1.5f,
			                                  .mass           = 80.0f } };
		desc.maxAgents  = 256;
		desc.maxGroups  = 2;
		auto crowd      = crowd::CreateCrowd(std::move(context), std::move(desc));

		auto orders      = crowd::GroupOrders();
		orders.goal      = glm::vec2(20.0f, 0.0f);
		orders.facing    = glm::vec2(1.0f, 0.0f);
		orders.formation = { .frontage = 10, .spacing = 1.0f };
		crowd->CreateGroup({ .agentType = 0, .agentCount = 100, .orders = orders });
		return crowd;
	}
}

// Two owners on one context: the renderer drawing on its queue and the crowd stepping on its own,
// each polled and neither waiting on the other.
TEST_CASE("A crowd steps on its own queue beside a renderer on the same context", "[render][crowd]")
{
	auto context = MakeContext();

	auto gfx = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	REQUIRE(gfx != nullptr);

	auto crowd = MakeMarchingCrowd(context);

	constexpr uint32_t c_Size = 64;

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = c_Size;
	targetDesc.height   = c_Size;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);
	view->CreateStaticMeshInstance(bgl::StaticMeshInstanceDesc().SetGeom(scene->AddCubeGeom()));

	auto camera = bgl::Camera();
	camera.LookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);

	auto renderJob     = bgl::RenderJob();
	renderJob.view     = view;
	renderJob.camera   = camera;
	renderJob.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));

	constexpr uint32_t c_Frames = 8;

	uint64_t steps = 0;
	for (uint32_t frame = 0; frame < c_Frames; ++frame)
	{
		gfx->DrawFrame(target, renderJob);

		if (crowd->CanStep())
		{
			CHECK(crowd->Step() == ++steps);
		}
	}

	crowd->Wait();
	gfx->WaitIdle();
	CHECK(steps > 0);
	CHECK(crowd->GetCompletedTick() == steps);
}

// One context is live per process, so a new one can be made only once the crowd has let go of its
// reference -- which it does after draining its queue.
TEST_CASE("A crowd releases its context when it is destroyed mid-flight", "[render][crowd]")
{
	auto context = MakeContext();
	auto crowd   = MakeMarchingCrowd(context);
	crowd->Step();

	crowd   = nullptr;
	context = nullptr;

	auto next = MakeContext();
	CHECK(next != nullptr);
}
