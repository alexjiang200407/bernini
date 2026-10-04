// The render ring's contract, run against the fake and the GPU crowd: what it refuses, what it
// describes, where a tick's records sit and for how long, and that a crowd never steps over records
// its reader has not released. A release needs a reader's queue point: the fake takes one from a
// queue of its own, and the GPU crowd from a renderer beside it on the same context, which is how
// a reader releases. The GPU crowd's records are read back by a reader of the test's own: what each
// holds, and that a Step waits for its reader before overwriting one.
#include "FakeCrowd.h"
#include <algorithm>
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <core/ref/SharedRef.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/RenderAgent.h>
#include <crowdlib/RenderTick.h>
#include <crowdlib/debug/AgentReadback.h>
#include <crowdlib/debug/CrowdReadback.h>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

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
		CHECK(held->firstRecordIndex == (tick % c_Ring) * c_MaxAgents);
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

namespace
{
	/**
	 * A reader as a renderer is one: a device and queue of its own on the crowd's context, the ring
	 * imported once, and every read a copy of the whole ring on its queue.
	 */
	class RingReader
	{
	public:
		RingReader(const bgpu::GpuContextRef& context, const crowd::ICrowd& crowd) :
			m_Device(bgpu::CreateDevice(context)),
			m_Rm(m_Device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly())),
			m_Queue(m_Device->CreateCommandQueue(bgpu::QueueType::kCompute)),
			m_Allocator(m_Device->CreateCommandAllocator(bgpu::QueueType::kCompute))
		{
			m_Rm->RegisterQueue(m_Queue.Get());
			m_Ring = m_Rm->ImportNativeBuffer(crowd.GetRenderRing());
			REQUIRE(m_Rm->ValidBufferHandle(m_Ring));

			const auto& ring = crowd.GetRenderRing().buffer;
			auto        desc = bgpu::ReadbackBufferDesc();
			desc.byteSize    = uint64_t{ ring.elementCount } * ring.stride;
			desc.debugName   = "Render ring readback";
			m_Readback       = m_Rm->CreateReadbackBuffer(desc);

			auto listDesc = bgpu::CommandListDesc();
			listDesc.type = bgpu::QueueType::kCompute;
			m_List        = m_Device->CreateCommandList(listDesc, m_Allocator, m_Rm);
		}

		RingReader(const RingReader&) = delete;
		RingReader&
		operator=(const RingReader&) = delete;

		~RingReader()
		{
			m_Queue->Flush();
			m_Rm->DestroyReadbackBuffer(m_Readback, false);
			m_Rm->DestroyBuffer(m_Ring, false);
			m_Rm->UnregisterQueue(m_Queue.Get());
		}

		/** The point the reader's next copy passes, before it is submitted. */
		[[nodiscard]] bgpu::QueuePoint
		NextPoint() const
		{
			return bgpu::QueuePoint{ m_Queue, m_Queue->GetNextFenceValue() };
		}

		/** Copies the ring after `tick` is written, and returns the point the copy passes. */
		bgpu::QueuePoint
		Submit(const crowd::RenderTick& tick)
		{
			m_Queue->InsertWaitForQueueFence(tick.written.queue.Get(), tick.written.value);
			m_Allocator->ResetAllocator();
			m_List->Open(m_Queue.Get(), m_Allocator.Get());
			m_List->CopyBufferToReadback(m_Readback, m_Ring);
			m_List->Close();
			return bgpu::QueuePoint{ m_Queue, m_Queue->ExecuteCommandList(m_List.Get()) };
		}

		/** `tick`'s records as the copy that passed `copied` saw them. */
		std::vector<crowd::RenderAgent>
		Records(const crowd::RenderTick& tick, const bgpu::QueuePoint& copied)
		{
			m_Queue->WaitForFenceCPUBlocking(copied.value);
			const auto* mapped =
				static_cast<const crowd::RenderAgent*>(m_Rm->MapReadback(m_Readback));
			REQUIRE(mapped != nullptr);
			auto records = std::vector<crowd::RenderAgent>(
				mapped + tick.firstRecordIndex,
				mapped + tick.firstRecordIndex + tick.agentCount);
			m_Rm->UnmapReadback(m_Readback);
			return records;
		}

	private:
		bgpu::DeviceRef            m_Device;
		bgpu::ResourceManagerRef   m_Rm;
		bgpu::CommandQueueRef      m_Queue;
		bgpu::CommandAllocatorRef  m_Allocator;
		bgpu::CommandListRef       m_List;
		bgpu::BufferHandle         m_Ring;
		bgpu::ReadbackBufferHandle m_Readback;
	};

	bgpu::GpuContextRef
	CreateDebugContext()
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		return bgpu::CreateGpuContext(desc);
	}

	/**
	 * Checks that every record of `current` not spawned follows `source` to its agent a tick
	 * before: a distinct record, of the same type and id, no further than one tick's step away;
	 * that every id is distinct; and that a spawned record's id is none of the previous tick's.
	 * Returns how many records moved to another index, so a test can show the layout changed.
	 */
	uint32_t
	CheckSources(
		const std::vector<crowd::RenderAgent>& previous,
		const std::vector<crowd::RenderAgent>& current,
		float                                  maxStep)
	{
		auto     claimed = std::vector<bool>(previous.size(), false);
		uint32_t moved   = 0;
		auto     ids     = std::set<uint32_t>();
		auto     before  = std::set<uint32_t>();
		for (const auto& record : previous) before.insert(record.id);
		for (uint32_t index = 0; index < current.size(); ++index)
		{
			const auto& record = current[index];
			CHECK(ids.insert(record.id).second);
			if (record.source == crowd::c_RenderSpawned)
			{
				CHECK_FALSE(before.contains(record.id));
				continue;
			}
			REQUIRE(record.source < previous.size());
			CHECK_FALSE(claimed[record.source]);
			claimed[record.source] = true;

			const auto& then = previous[record.source];
			CHECK(record.type == then.type);
			CHECK(record.id == then.id);
			CHECK(glm::length(record.position - then.position) <= maxStep);
			moved += record.source != index ? 1 : 0;
		}
		return moved;
	}
}

// What the step writes: each agent's position and facing as its debug readback has them, grouped
// by type, and its source, which keeps finding the same agent while groups split, are destroyed,
// merge and spawn, each of which moves agents to other indices.
TEST_CASE(
	"Each render record holds its agent, and its source is that agent a tick before",
	"[crowd][render_ring][compute]")
{
	auto context = CreateDebugContext();

	auto desc = MakeDesc(6);
	desc.agentTypes.push_back(
		crowd::AgentType{ .radius         = 0.4f,
	                      .preferredSpeed = 1.0f,
	                      .maxSpeed       = 2.0f,
	                      .mass           = 90.0f });
	desc.maxGroups          = 4;
	desc.debugAgentReadback = true;
	const float maxStep     = 2.0f * desc.tickSeconds + 1e-4f;

	auto       crowd  = crowd::CreateCrowd(context, desc);
	RingReader reader = RingReader(context, *crowd);

	auto orders      = crowd::GroupOrders();
	orders.facing    = glm::vec2(1.0f, 0.0f);
	orders.formation = { .frontage = 4, .spacing = 1.0f };
	orders.goal      = glm::vec2(0.0f, 0.0f);
	const auto first = crowd->CreateGroup({ .agentType = 0, .agentCount = 12, .orders = orders });
	orders.goal      = glm::vec2(0.0f, 20.0f);
	const auto other = crowd->CreateGroup({ .agentType = 1, .agentCount = 8, .orders = orders });

	// Steps once and reads the tick's records, releasing them at once: they are copied out.
	auto step = [&] {
		const uint64_t tick = crowd->Step();
		crowd->Wait();
		const std::optional<crowd::RenderTick> written = crowd->GetRenderTick(tick);
		REQUIRE(written.has_value());
		const bgpu::QueuePoint copied  = reader.Submit(*written);
		auto                   records = reader.Records(*written, copied);
		crowd->ReleaseRenderReads(tick, copied);

		// Grouped by type: each type's run holds that type's agents and no other.
		REQUIRE(written->types.size() == 2);
		uint32_t next = written->firstRecordIndex;
		for (uint32_t type = 0; type < written->types.size(); ++type)
		{
			const crowd::RenderTypeRecords& run = written->types[type];
			CHECK(run.firstRecordIndex == next);
			next += run.count;
			for (uint32_t index = 0; index < run.count; ++index)
				CHECK(
					records[run.firstRecordIndex - written->firstRecordIndex + index].type == type);
		}
		CHECK(next == written->firstRecordIndex + written->agentCount);

		// The same agents the debug readback has, in another order.
		const std::optional<crowd::debug::CrowdReadback> agents = crowd->ReadDebugAgents();
		REQUIRE(agents.has_value());
		REQUIRE(agents->tick == tick);
		REQUIRE(records.size() == agents->agents.size());
		auto key = [](glm::vec2 position, glm::vec2 facing) {
			return std::array<float, 4>{ { position.x, position.y, facing.x, facing.y } };
		};
		auto fromRecords = std::vector<std::array<float, 4>>();
		auto fromAgents  = std::vector<std::array<float, 4>>();
		for (uint32_t index = 0; index < records.size(); ++index)
		{
			fromRecords.push_back(key(records[index].position, records[index].facing));
			fromAgents.push_back(key(agents->agents[index].position, agents->agents[index].facing));
		}
		std::ranges::sort(fromRecords);
		std::ranges::sort(fromAgents);
		CHECK(fromRecords == fromAgents);
		return records;
	};
	auto count = [](const std::vector<crowd::RenderAgent>& records, auto&& matches) {
		return std::count_if(records.begin(), records.end(), matches);
	};
	auto spawned = [](const crowd::RenderAgent& record) {
		return record.source == crowd::c_RenderSpawned;
	};
	auto ofType = [](uint32_t type) {
		return [type](const crowd::RenderAgent& record) { return record.type == type; };
	};

	auto previous = step();
	CHECK(count(previous, spawned) == 20);
	CHECK(count(previous, ofType(0)) == 12);
	CHECK(count(previous, ofType(1)) == 8);

	// Marching, so a source that found the wrong agent would be a step or more away.
	orders.goal = glm::vec2(30.0f, 0.0f);
	crowd->SetOrders(first, orders);
	crowd->SetOrders(other, orders);
	for (int i = 0; i < 3; ++i)
	{
		auto current = step();
		CHECK(count(current, spawned) == 0);
		CHECK(CheckSources(previous, current, maxStep) == 0);
		previous = std::move(current);
	}

	// The split's agents move to the end of the agents, past the other type's group, but their
	// records stay at the end of their type's run; destroying the other type's group moves none.
	const auto split = crowd->SplitGroup(first, 4);
	auto       after = step();
	CHECK(CheckSources(previous, after, maxStep) == 0);
	previous = std::move(after);

	crowd->DestroyGroup(other);
	after = step();
	CHECK(after.size() == 12);
	CHECK(count(after, ofType(0)) == 12);
	CHECK(CheckSources(previous, after, maxStep) == 0);
	previous = std::move(after);

	// The front group merged into the split one goes behind it: every record moves.
	crowd->MergeGroup(first, split);
	after = step();
	CHECK(count(after, spawned) == 0);
	CHECK(CheckSources(previous, after, maxStep) > 0);
	previous = std::move(after);

	orders.goal = glm::vec2(0.0f, -20.0f);
	crowd->CreateGroup({ .agentType = 1, .agentCount = 5, .orders = orders });
	after = step();
	CHECK(after.size() == 17);
	CHECK(count(after, spawned) == 5);
	CHECK(count(after, ofType(1)) == 5);
	CheckSources(previous, after, maxStep);
}

// The reader releases tick 1 at a point its queue has not reached. The Step that overwrites tick
// 1's slot is submitted, and the crowd's queue holds it there: the reader's copy, made after,
// still finds tick 1's records, and only once it passes does the crowd's tick complete.
TEST_CASE(
	"A Step overwrites a released tick only after its reader's point passes",
	"[crowd][render_ring][compute]")
{
	constexpr uint32_t c_Ring = 5;

	auto       context = CreateDebugContext();
	auto       crowd   = crowd::CreateCrowd(context, MakeDesc(c_Ring));
	RingReader reader  = RingReader(context, *crowd);
	AddGroup(*crowd);

	REQUIRE(StepUntilBlocked(*crowd) == c_Ring);
	const std::optional<crowd::RenderTick> first = crowd->GetRenderTick(1);
	REQUIRE(first.has_value());

	const bgpu::QueuePoint later = reader.NextPoint();
	crowd->ReleaseRenderReads(1, later);
	REQUIRE(crowd->CanStep());
	const uint64_t overwriting = crowd->Step();
	REQUIRE(overwriting == c_Ring + 1);

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	CHECK(crowd->GetCompletedTick() == c_Ring);

	const bgpu::QueuePoint copied = reader.Submit(*first);
	REQUIRE(copied.value == later.value);
	const auto held = reader.Records(*first, copied);
	CHECK(std::ranges::all_of(held, [](const crowd::RenderAgent& record) {
		return record.source == crowd::c_RenderSpawned;
	}));

	crowd->Wait();
	CHECK(crowd->GetCompletedTick() == overwriting);
	const std::optional<crowd::RenderTick> sixth = crowd->GetRenderTick(overwriting);
	REQUIRE(sixth.has_value());
	REQUIRE(sixth->firstRecordIndex == first->firstRecordIndex);
	const auto overwritten = reader.Records(*sixth, reader.Submit(*sixth));
	CHECK(std::ranges::none_of(overwritten, [](const crowd::RenderAgent& record) {
		return record.source == crowd::c_RenderSpawned;
	}));
}

TEMPLATE_LIST_TEST_CASE(
	"A tick's records run by agent type, in the crowd's type order",
	"[crowd][render_ring]",
	CrowdFactories)
{
	auto desc = MakeDesc(5);
	desc.agentTypes.push_back(
		crowd::AgentType{ .radius         = 0.8f,
	                      .preferredSpeed = 4.0f,
	                      .maxSpeed       = 6.0f,
	                      .mass           = 500.0f });
	desc.maxGroups = 4;
	auto crowd     = TestType::Create(desc);

	auto orders      = crowd::GroupOrders();
	orders.facing    = glm::vec2(1.0f, 0.0f);
	orders.formation = { .frontage = 4, .spacing = 1.0f };

	// Created out of type order, with a type-0 group on each side of the type-1 one.
	const auto horse = crowd->CreateGroup({ .agentType = 1, .agentCount = 6, .orders = orders });
	crowd->CreateGroup({ .agentType = 0, .agentCount = 9, .orders = orders });
	crowd->CreateGroup({ .agentType = 1, .agentCount = 4, .orders = orders });
	crowd->Step();
	crowd->Wait();

	const std::optional<crowd::RenderTick> first = crowd->GetRenderTick(1);
	REQUIRE(first.has_value());
	REQUIRE(first->types.size() == 2);
	CHECK(first->types[0].firstRecordIndex == first->firstRecordIndex);
	CHECK(first->types[0].count == 9);
	CHECK(first->types[1].firstRecordIndex == first->firstRecordIndex + 9);
	CHECK(first->types[1].count == 10);

	// Destroying a type-1 group shortens that type's run; the runs stay back to back.
	crowd->DestroyGroup(horse);
	crowd->Step();
	crowd->Wait();
	const std::optional<crowd::RenderTick> second = crowd->GetRenderTick(2);
	REQUIRE(second.has_value());
	REQUIRE(second->types.size() == 2);
	CHECK(second->types[0].count == 9);
	CHECK(second->types[1].firstRecordIndex == second->firstRecordIndex + 9);
	CHECK(second->types[1].count == 4);
	CHECK(second->agentCount == 13);
}
