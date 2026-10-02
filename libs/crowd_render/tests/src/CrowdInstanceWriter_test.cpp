// The crowd's writer run against a recording block, with the parameters the library plans for each
// frame: what a frame places, and that each agent's prevTransform is the transform it was placed at
// last frame -- across ticks that relayout the agents, and frames that stay on a tick.
#include "WriterFrame.h"
#include <bgl/glm.h>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/math.h>
#include <crowdlib/AgentType.h>
#include <crowdlib/CrowdDesc.h>
#include <crowdlib/GroupHandle.h>
#include <crowdlib/GroupOrders.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/RenderAgent.h>
#include <crowdlib/RenderTick.h>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace
{
	constexpr uint32_t c_MaxAgents = 64;

	// What the recording block kept of one slot.
	struct Placed
	{
		glm::vec4 position;
		glm::vec4 axis;
		glm::vec4 prevPosition;
		glm::vec4 prevAxis;
	};

	struct Frame
	{
		uint64_t                        tick = 0;
		std::vector<crowd::RenderAgent> records;

		// Each type's run of `records`: slot i of a type's block is record runs[type].firstRecordIndex + i.
		std::vector<crowd::RenderTypeRecords> runs;

		// One per type, each c_MaxAgents slots.
		std::vector<std::vector<Placed>> types;
	};

	/**
	 * The writer's harness: on a device of its own beside the crowd's, it imports the ring and runs
	 * CSRecordCrowdInstances once per agent type, then reads back the slots and the tick's records.
	 */
	class WriterHarness
	{
	public:
		WriterHarness(const bgpu::GpuContextRef& context, const crowd::ICrowd& crowd) :
			m_Device(bgpu::CreateDevice(context)),
			m_Rm(m_Device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly())),
			m_Queue(m_Device->CreateCommandQueue(bgpu::QueueType::kCompute)),
			m_Allocator(m_Device->CreateCommandAllocator(bgpu::QueueType::kCompute))
		{
			m_Rm->RegisterQueue(m_Queue.Get());

			const bgpu::NativeBufferDesc ring = crowd.GetRenderRing();
			m_Ring                            = m_Rm->ImportNativeBuffer(ring);
			REQUIRE(m_Rm->ValidBufferHandle(m_Ring));

			m_Slots = m_Rm->CreateComputeBuffer(
				bgpu::ComputeBufferDesc()
					.SetElement<glm::vec4>()
					.SetInitialCount(c_MaxAgents * 4)
					.SetDebugName("Recorded crowd slots"));

			auto slotsDesc      = bgpu::ReadbackBufferDesc();
			slotsDesc.byteSize  = c_MaxAgents * sizeof(Placed);
			slotsDesc.debugName = "Recorded crowd slots readback";
			m_SlotsReadback     = m_Rm->CreateReadbackBuffer(slotsDesc);

			auto ringDesc      = bgpu::ReadbackBufferDesc();
			ringDesc.byteSize  = uint64_t{ ring.buffer.elementCount } * ring.buffer.stride;
			ringDesc.debugName = "Render ring readback";
			m_RingReadback     = m_Rm->CreateReadbackBuffer(ringDesc);

			m_Kernel = m_Device->CreateComputeKernel(
				bgpu::ComputePipelineDesc()
					.SetShader(m_Device->CreateShader("crowd_render_tests.CSRecordCrowdInstances"))
					.SetDebugName("CSRecordCrowdInstances"));
			REQUIRE(m_Kernel.pipeline != nullptr);

			auto listDesc = bgpu::CommandListDesc();
			listDesc.type = bgpu::QueueType::kCompute;
			m_List        = m_Device->CreateCommandList(listDesc, m_Allocator, m_Rm);
		}

		WriterHarness(const WriterHarness&) = delete;
		WriterHarness&
		operator=(const WriterHarness&) = delete;

		~WriterHarness()
		{
			m_Queue->Flush();
			m_Rm->DestroyReadbackBuffer(m_SlotsReadback, false);
			m_Rm->DestroyReadbackBuffer(m_RingReadback, false);
			m_Rm->DestroyBuffer(m_Slots, false);
			m_Rm->DestroyBuffer(m_Ring, false);
			m_Rm->UnregisterQueue(m_Queue.Get());
		}

		/** Runs the writer for every type over `frame`, the crowd's latest completed tick. */
		Frame
		Run(const crowd::ICrowd&             crowd,
		    uint64_t                         tick,
		    const crowd_render::WriterFrame& frame,
		    const std::vector<glm::mat4>&    models)
		{
			const std::optional<crowd::RenderTick> written = crowd.GetRenderTick(tick);
			REQUIRE(written.has_value());

			auto result = Frame{ .tick = tick };
			for (uint32_t type = 0; type < models.size(); ++type)
			{
				m_Kernel["gUniforms"]["block"]["slots"]    = m_Slots;
				m_Kernel["gUniforms"]["block"]["capacity"] = c_MaxAgents;
				crowd_render::WriteWriterParams(
					m_Kernel["gUniforms"]["params"],
					frame,
					m_Ring,
					type,
					models[type]);

				m_Queue->InsertWaitForQueueFence(
					written->written.queue.Get(),
					written->written.value);
				m_Allocator->ResetAllocator();
				m_List->Open(m_Queue.Get(), m_Allocator.Get());
				auto state   = bgpu::ComputeState();
				state.kernel = &m_Kernel;
				m_List->SetComputeState(state);
				m_List->Dispatch(core::div_ceil(c_MaxAgents, 64u), 1, 1);
				m_List->Barrier(
					m_Slots,
					bgpu::BufferBarrierDesc()
						.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
						.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
						.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
						.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
				m_List->CopyBufferToReadback(m_SlotsReadback, m_Slots);
				m_List->CopyBufferToReadback(m_RingReadback, m_Ring);
				m_List->Close();
				m_Done = m_Queue->ExecuteCommandList(m_List.Get());
				m_Queue->WaitForFenceCPUBlocking(m_Done);

				auto placed = std::vector<Placed>(c_MaxAgents);
				std::memcpy(
					placed.data(),
					m_Rm->MapReadback(m_SlotsReadback),
					placed.size() * sizeof(Placed));
				m_Rm->UnmapReadback(m_SlotsReadback);
				result.types.push_back(std::move(placed));
			}

			const auto* ring =
				static_cast<const crowd::RenderAgent*>(m_Rm->MapReadback(m_RingReadback));
			for (const auto& run : written->types)
				result.runs.push_back(
					{ .firstRecordIndex = run.firstRecordIndex - written->firstRecordIndex,
				      .count            = run.count });
			result.records.assign(
				ring + written->firstRecordIndex,
				ring + written->firstRecordIndex + written->agentCount);
			m_Rm->UnmapReadback(m_RingReadback);
			return result;
		}

		/** The point past which every run so far has finished. */
		[[nodiscard]] bgpu::QueuePoint
		Done() const
		{
			return bgpu::QueuePoint{ m_Queue, m_Done };
		}

	private:
		bgpu::DeviceRef            m_Device;
		bgpu::ResourceManagerRef   m_Rm;
		bgpu::CommandQueueRef      m_Queue;
		bgpu::CommandAllocatorRef  m_Allocator;
		bgpu::CommandListRef       m_List;
		bgpu::ComputeKernel        m_Kernel;
		bgpu::BufferHandle         m_Ring;
		bgpu::BufferHandle         m_Slots;
		bgpu::ReadbackBufferHandle m_SlotsReadback;
		bgpu::ReadbackBufferHandle m_RingReadback;
		uint64_t                   m_Done = 0;
	};

	bool
	Near(const glm::vec4& a, const glm::vec4& b)
	{
		return glm::length(a - b) <= 1e-4f;
	}

	crowd::CrowdDesc
	MakeDesc()
	{
		auto desc            = crowd::CrowdDesc();
		desc.agentTypes      = { crowd::AgentType{ .radius         = 0.3f,
			                                       .preferredSpeed = 1.2f,
			                                       .maxSpeed       = 1.5f,
			                                       .mass           = 80.0f },
			                     crowd::AgentType{ .radius         = 0.8f,
			                                       .preferredSpeed = 4.0f,
			                                       .maxSpeed       = 6.0f,
			                                       .mass           = 500.0f } };
		desc.maxAgents       = c_MaxAgents;
		desc.maxGroups       = 4;
		desc.renderRingTicks = 6;
		return desc;
	}
}

TEST_CASE(
	"Every agent's prevTransform is where it was placed last frame, across a relayout",
	"[crowd_render][compute]")
{
	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	auto context                 = bgpu::CreateGpuContext(contextDesc);

	auto          crowd   = crowd::CreateCrowd(context, MakeDesc());
	WriterHarness harness = WriterHarness(context, *crowd);

	// A lift, so the model matrix is seen to apply: every placed agent stands at y = 1.
	const auto models =
		std::vector<glm::mat4>{ glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)),
		                        glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) };

	auto orders      = crowd::GroupOrders();
	orders.facing    = glm::vec2(1.0f, 0.0f);
	orders.formation = { .frontage = 4, .spacing = 1.0f };
	orders.goal      = glm::vec2(0.0f, 0.0f);
	const auto foot  = crowd->CreateGroup({ .agentType = 0, .agentCount = 12, .orders = orders });
	orders.goal      = glm::vec2(0.0f, 20.0f);
	const auto horse = crowd->CreateGroup({ .agentType = 1, .agentCount = 8, .orders = orders });

	auto step = [&] {
		crowd->Step();
		crowd->Wait();
	};
	step();
	orders.goal = glm::vec2(30.0f, -10.0f);
	crowd->SetOrders(foot, orders);
	crowd->SetOrders(horse, orders);
	step();

	std::optional<Frame> last;
	float                lastAlpha = 0.0f;
	uint32_t             checked   = 0;
	auto                 draw      = [&](float alpha) {
		const uint64_t tick = crowd->GetCompletedTick();
		const auto     frame =
			crowd_render::PlanWriterFrame(*crowd, tick, alpha, last ? last->tick : 0, lastAlpha);
		Frame now = harness.Run(*crowd, tick, frame, models);
		if (tick > 3)
			crowd->ReleaseRenderReads(tick - 3, harness.Done());

		for (uint32_t type = 0; type < now.types.size(); ++type)
		{
			for (uint32_t slot = 0; slot < c_MaxAgents; ++slot)
			{
				INFO("tick " << tick << ", type " << type << ", slot " << slot);
				const Placed& placed = now.types[type][slot];
				const auto&   run    = now.runs[type];
				const bool    shown  = slot < run.count;
				REQUIRE((placed.position.w == 1.0f) == shown);
				if (!shown)
					continue;
				CHECK(placed.position.y == 1.0f);

				// Where last frame placed the same agent: the same slot on the same tick, its
				// source a tick later, nowhere further back.
				const auto& record = now.records[run.firstRecordIndex + slot];
				CHECK(record.type == type);
				const Placed* before = nullptr;
				if (last && last->tick == tick)
				{
					before = &last->types[type][slot];
				}
				else if (last && last->tick + 1 == tick && record.source != crowd::c_RenderSpawned)
				{
					const auto& lastRun = last->runs[type];
					REQUIRE(record.source >= lastRun.firstRecordIndex);
					REQUIRE(record.source < lastRun.firstRecordIndex + lastRun.count);
					before = &last->types[type][record.source - lastRun.firstRecordIndex];
				}

				if (before != nullptr)
				{
					REQUIRE(before->position.w == 1.0f);
					CHECK(Near(placed.prevPosition, before->position));
					CHECK(Near(placed.prevAxis, before->axis));
					++checked;
				}
				else
				{
					CHECK(placed.prevPosition == placed.position);
					CHECK(placed.prevAxis == placed.axis);
				}
			}
		}
		last      = std::move(now);
		lastAlpha = alpha;
	};

	// Frames between ticks, on a tick and onto the next one, as the crowd marches.
	for (int i = 0; i < 3; ++i)
	{
		draw(0.25f);
		draw(0.75f);
		step();
	}

	// A split, a destroy, a merge that reorders a type's records, and a spawn.
	const auto split = crowd->SplitGroup(foot, 4);
	step();
	draw(0.5f);
	crowd->DestroyGroup(horse);
	step();
	draw(0.0f);
	draw(1.0f);
	// The front group merged into the split one goes behind it in its type's run: every record of
	// the type moves.
	crowd->MergeGroup(foot, split);
	step();
	draw(0.3f);
	orders.goal = glm::vec2(-5.0f, 5.0f);
	crowd->CreateGroup({ .agentType = 1, .agentCount = 5, .orders = orders });
	step();
	draw(0.6f);

	// Two ticks in one frame: last frame's pose is not followed that far, so nothing moves.
	step();
	step();
	draw(0.5f);

	CHECK(checked > 100);
}

TEST_CASE("A frame between ticks places each agent alpha of the way", "[crowd_render][compute]")
{
	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	auto context                 = bgpu::CreateGpuContext(contextDesc);

	auto          crowd   = crowd::CreateCrowd(context, MakeDesc());
	WriterHarness harness = WriterHarness(context, *crowd);
	const auto    models  = std::vector<glm::mat4>{ glm::mat4(1.0f), glm::mat4(1.0f) };

	auto orders      = crowd::GroupOrders();
	orders.facing    = glm::vec2(0.0f, 1.0f);
	orders.formation = { .frontage = 3, .spacing = 1.5f };
	const auto group = crowd->CreateGroup({ .agentType = 0, .agentCount = 9, .orders = orders });
	crowd->Step();
	orders.goal = glm::vec2(10.0f, 4.0f);
	crowd->SetOrders(group, orders);
	for (int i = 0; i < 4; ++i)
	{
		crowd->Step();
		crowd->Wait();
	}

	const uint64_t tick     = crowd->GetCompletedTick();
	const auto     previous = harness.Run(
		*crowd,
		tick - 1,
		crowd_render::PlanWriterFrame(*crowd, tick - 1, 1.0f, 0, 0.0f),
		models);
	const auto frame = harness.Run(
		*crowd,
		tick,
		crowd_render::PlanWriterFrame(*crowd, tick, 0.4f, 0, 0.0f),
		models);

	for (uint32_t slot = 0; slot < frame.records.size(); ++slot)
	{
		INFO("slot " << slot);
		const auto& now = frame.records[slot];
		REQUIRE(now.source != crowd::c_RenderSpawned);
		const auto& then     = previous.records[now.source];
		const auto  expected = glm::mix(then.position, now.position, 0.4f);
		CHECK(Near(frame.types[0][slot].position, glm::vec4(expected.x, 0.0f, expected.y, 1.0f)));

		// alpha 1 is the tick itself, facing included: +z of the placed transform is the facing.
		const Placed& atTick = previous.types[0][now.source];
		CHECK(Near(atTick.position, glm::vec4(then.position.x, 0.0f, then.position.y, 1.0f)));
		CHECK(Near(atTick.axis, glm::vec4(then.facing.x, 0.0f, then.facing.y, 0.0f)));
	}
}
