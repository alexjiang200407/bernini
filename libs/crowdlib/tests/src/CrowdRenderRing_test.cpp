// The render ring's contract, run against the fake and the GPU crowd: what it refuses, what it
// describes, where a tick's records sit and for how long, and that a crowd never steps over records
// its reader has not released. A release needs a reader's queue point: the fake takes one from a
// queue of its own, and the GPU crowd from a renderer beside it on the same context, which is how
// a reader releases. What the records hold is the crowd's step's (docs/crowdlib.md).
#include "FakeCrowd.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/ref/SharedRef.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/RenderAgent.h>
#include <crowdlib/RenderTick.h>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace
{
	struct FakeCrowdFactory
	{
		static crowd::CrowdRef
		Create(crowd::CrowdDesc desc)
		{
			return core::SharedRef<crowd::test::FakeCrowd>::Make(std::move(desc));
		}
	};

	// On a context of its own, kept until the crowd is released: one context is live per process.
	struct GpuCrowdFactory
	{
		static crowd::CrowdRef
		Create(crowd::CrowdDesc desc)
		{
			auto contextDesc             = bgpu::GpuContextDesc();
			contextDesc.enableDebugLayer = true;
			return crowd::CreateCrowd(bgpu::CreateGpuContext(contextDesc), std::move(desc));
		}
	};

	using CrowdFactories = std::tuple<FakeCrowdFactory, GpuCrowdFactory>;

	constexpr uint32_t c_MaxAgents = 64;
	constexpr uint32_t c_Agents    = 20;

	// maxTicksInFlight 2, so the shortest ring holds 5 ticks.
	crowd::CrowdDesc
	MakeDesc(uint32_t renderRingTicks)
	{
		auto desc            = crowd::CrowdDesc();
		desc.agentTypes      = { crowd::AgentType{ .radius         = 0.3f,
			                                       .preferredSpeed = 1.2f,
			                                       .maxSpeed       = 1.5f,
			                                       .mass           = 80.0f } };
		desc.maxAgents       = c_MaxAgents;
		desc.maxGroups       = 2;
		desc.renderRingTicks = renderRingTicks;
		return desc;
	}

	void
	AddGroup(crowd::ICrowd& crowd)
	{
		auto orders      = crowd::GroupOrders();
		orders.goal      = glm::vec2(10.0f, 0.0f);
		orders.facing    = glm::vec2(1.0f, 0.0f);
		orders.formation = { .frontage = 5, .spacing = 1.0f };
		crowd.CreateGroup({ .agentType = 0, .agentCount = c_Agents, .orders = orders });
	}

	// Steps and waits until the ring stops it, and returns how many ticks that was.
	uint64_t
	StepUntilBlocked(crowd::ICrowd& crowd)
	{
		while (crowd.CanStep())
		{
			crowd.Step();
			crowd.Wait();
		}
		return crowd.GetSubmittedTick();
	}
}

TEMPLATE_LIST_TEST_CASE(
	"A render ring shorter than a reader needs is refused",
	"[crowd][render_ring]",
	CrowdFactories)
{
	CHECK_THROWS_AS(TestType::Create(MakeDesc(4)), std::runtime_error);
	CHECK_NOTHROW(TestType::Create(MakeDesc(5)));
}

TEMPLATE_LIST_TEST_CASE(
	"A crowd without a render ring refuses its calls",
	"[crowd][render_ring]",
	CrowdFactories)
{
	auto crowd = TestType::Create(MakeDesc(0));

	CHECK_THROWS_AS((void)crowd->GetRenderRing(), std::runtime_error);
	CHECK_THROWS_AS((void)crowd->GetRenderTick(1), std::runtime_error);
	CHECK_THROWS_AS(crowd->ReleaseRenderReads(0, bgpu::QueuePoint()), std::runtime_error);
	CHECK(crowd->GetReleasedRenderTick() == 0);

	// Without a ring, nothing but the ticks in flight holds a Step back.
	AddGroup(*crowd);
	for (int i = 0; i < 8; ++i)
	{
		REQUIRE(crowd->CanStep());
		crowd->Step();
		crowd->Wait();
	}
}

TEMPLATE_LIST_TEST_CASE(
	"A tick's records sit in its ring slot, and the ring does not step over them",
	"[crowd][render_ring]",
	CrowdFactories)
{
	constexpr uint32_t c_Ring = 6;

	auto crowd = TestType::Create(MakeDesc(c_Ring));
	AddGroup(*crowd);

	const bgpu::NativeBufferDesc ring = crowd->GetRenderRing();
	CHECK(ring.buffer.stride == sizeof(crowd::RenderAgent));
	CHECK(ring.buffer.elementCount == c_Ring * c_MaxAgents);
	CHECK_FALSE(ring.buffer.isUav);

	CHECK_FALSE(crowd->GetRenderTick(0).has_value());
	CHECK_FALSE(crowd->GetRenderTick(1).has_value());
	CHECK_THROWS_AS(crowd->ReleaseRenderReads(0, bgpu::QueuePoint()), std::runtime_error);

	// Nothing released, so the ring holds every tick it was given and refuses the next.
	CHECK(StepUntilBlocked(*crowd) == c_Ring);
	CHECK_THROWS_WITH(crowd->Step(), Catch::Matchers::ContainsSubstring("not released tick 1"));

	for (uint64_t tick = 1; tick <= c_Ring; ++tick)
	{
		const std::optional<crowd::RenderTick> held = crowd->GetRenderTick(tick);
		REQUIRE(held.has_value());
		CHECK(held->tick == tick);
		CHECK(held->firstRecord == (tick % c_Ring) * c_MaxAgents);
		CHECK(held->agentCount == c_Agents);
	}
	CHECK_FALSE(crowd->GetRenderTick(c_Ring + 1).has_value());
}

TEST_CASE("A release frees the ticks it names, in order", "[crowd][render_ring][fake]")
{
	constexpr uint32_t c_Ring = 5;

	// The fake's reader: a queue of its own on the one live context.
	auto       context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	auto       device  = bgpu::CreateDevice(context);
	auto       queue   = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	const auto done    = bgpu::QueuePoint{ queue, 0 };

	auto crowd = core::SharedRef<crowd::test::FakeCrowd>::Make(MakeDesc(c_Ring));
	AddGroup(*crowd);
	REQUIRE(StepUntilBlocked(*crowd) == c_Ring);

	CHECK_THROWS_AS(crowd->ReleaseRenderReads(c_Ring + 1, done), std::runtime_error);

	crowd->ReleaseRenderReads(2, done);
	CHECK(crowd->GetReleasedRenderTick() == 2);
	CHECK_THROWS_AS(crowd->ReleaseRenderReads(1, done), std::runtime_error);

	// Two ticks released, two more steps, and the two oldest are gone.
	CHECK(StepUntilBlocked(*crowd) == c_Ring + 2);
	CHECK_FALSE(crowd->GetRenderTick(1).has_value());
	CHECK_FALSE(crowd->GetRenderTick(2).has_value());
	CHECK(crowd->GetRenderTick(3).has_value());
	CHECK(crowd->GetRenderTick(c_Ring + 2).has_value());
}

// The real reader: a renderer on the crowd's context, releasing with the point its last frame
// passes. The Step after the release waits for that point on the crowd's queue, on the GPU.
TEST_CASE(
	"A crowd steps past ticks a renderer released, and times them",
	"[crowd][render_ring][render]")
{
	constexpr uint32_t c_Ring = 5;

	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	auto context                 = bgpu::CreateGpuContext(contextDesc);

	auto gfx   = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	auto crowd = crowd::CreateCrowd(context, MakeDesc(c_Ring));
	AddGroup(*crowd);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 32;
	targetDesc.height   = 32;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	auto scene          = gfx->CreateScene(bgl::SceneDesc());
	auto job            = bgl::RenderJob();
	job.view            = gfx->CreateSceneView(scene, 8);
	job.camera          = bgl::Camera().Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	job.viewport        = bgl::Viewport(32.0f, 32.0f);

	REQUIRE(StepUntilBlocked(*crowd) == c_Ring);

	const std::optional<crowd::RenderTick> first = crowd->GetRenderTick(1);
	REQUIRE(first.has_value());
	CHECK_FALSE(first->written.IsNull());
	CHECK(first->written.queue->IsFenceComplete(first->written.value));

	const std::optional<float> ms = crowd->GetTickGpuMilliseconds(c_Ring);
	REQUIRE(ms.has_value());
	CHECK(*ms >= 0.0f);
	CHECK_FALSE(crowd->GetTickGpuMilliseconds(c_Ring + 1).has_value());

	gfx->DrawFrame(target, job);
	crowd->ReleaseRenderReads(1, gfx->GetLastFrameDone());
	REQUIRE(crowd->CanStep());
	crowd->Step();
	crowd->Wait();
	CHECK_FALSE(crowd->GetRenderTick(1).has_value());
	CHECK(crowd->GetRenderTick(c_Ring + 1).has_value());

	gfx->WaitIdle();
}
