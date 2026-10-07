#include "cmd/CommandQueue_vulkan.h"
#include "cmd/CommandList_vulkan.h"
#include "convert_vulkan.h"
#include "native_device_vulkan.h"
#include "resource/ResourceManager_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <algorithm>
#include <atomic>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/NativeVkQueue.h>
#include <bgpu/types/QueueType.h>
#include <core/containers/static_vector.h>
#include <core/err/util.h>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <span>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>

namespace bgpu
{
	CommandQueue::CommandQueue(GpuContextRef context, const QueueType type) :
		m_Context(std::move(context)), m_Device(GetVulkanHandles(*m_Context).device), m_Type(type)
	{
		const std::vector<uint32_t> families =
			QueueFamiliesFor(type, GetVulkanQueueFamilies(*m_Context));
		m_Queue = AcquireVulkanQueue(*m_Context, families);

		auto timeline          = VkSemaphoreTypeCreateInfo();
		timeline.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timeline.initialValue  = 0;

		auto info  = VkSemaphoreCreateInfo();
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		info.pNext = &timeline;
		EnsureVk(vkCreateSemaphore(m_Device, &info, nullptr, &m_Fence), "vkCreateSemaphore");

		m_Native = NativeVkQueue{ .queue      = m_Queue.queue,
			                      .family     = m_Queue.family,
			                      .timeline   = m_Fence,
			                      .submitLock = m_Queue.submitLock };

		auto poolInfo             = VkCommandPoolCreateInfo();
		poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = m_Queue.family;
		EnsureVk(
			vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_ProloguePool),
			"vkCreateCommandPool");
	}

	CommandQueue::~CommandQueue() noexcept
	{
		spdlog::trace("~CommandQueue");
		vkDestroyCommandPool(m_Device, m_ProloguePool, nullptr);
		vkDestroySemaphore(m_Device, m_Fence, nullptr);
		ReleaseVulkanQueue(*m_Context, m_Queue);
	}

	VkCommandBuffer
	CommandQueue::RecordPrologue(const std::span<const VkImageMemoryBarrier2> barriers) noexcept
	{
		const uint64_t completed = PollCurrentFenceValue();
		const uint64_t next      = m_NextFenceValue.load(std::memory_order_relaxed);

		auto reusable = std::ranges::find_if(m_Prologues, [completed](const Prologue& prologue) {
			return prologue.fenceValue <= completed;
		});
		if (reusable == m_Prologues.end())
		{
			auto allocation               = VkCommandBufferAllocateInfo();
			allocation.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocation.commandPool        = m_ProloguePool;
			allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocation.commandBufferCount = 1;

			auto prologue = Prologue();
			EnsureVk(
				vkAllocateCommandBuffers(m_Device, &allocation, &prologue.commandBuffer),
				"vkAllocateCommandBuffers");
			m_Prologues.push_back(prologue);
			reusable = std::prev(m_Prologues.end());
		}
		else
		{
			EnsureVk(vkResetCommandBuffer(reusable->commandBuffer, 0), "vkResetCommandBuffer");
		}
		reusable->fenceValue = next;

		auto begin  = VkCommandBufferBeginInfo();
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		EnsureVk(vkBeginCommandBuffer(reusable->commandBuffer, &begin), "vkBeginCommandBuffer");

		auto dependency                    = VkDependencyInfo();
		dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
		dependency.pImageMemoryBarriers    = barriers.data();
		vkCmdPipelineBarrier2(reusable->commandBuffer, &dependency);

		EnsureVk(vkEndCommandBuffer(reusable->commandBuffer), "vkEndCommandBuffer");
		return reusable->commandBuffer;
	}

	uint64_t
	CommandQueue::SubmitLocked(const std::span<const VkCommandBuffer> commandBuffers) noexcept
	{
		const uint64_t value = m_NextFenceValue.load(std::memory_order_relaxed);

		auto waits = std::vector<VkSemaphoreSubmitInfo>();
		waits.reserve(m_PendingWaits.size());
		for (const auto& [semaphore, waitValue] : m_PendingWaits)
		{
			auto wait      = VkSemaphoreSubmitInfo();
			wait.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
			wait.semaphore = semaphore;
			wait.value     = waitValue;
			wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			waits.push_back(wait);
		}
		m_PendingWaits.clear();

		auto signal      = VkSemaphoreSubmitInfo();
		signal.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signal.semaphore = m_Fence;
		signal.value     = value;
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		auto commands = std::vector<VkCommandBufferSubmitInfo>();
		commands.reserve(commandBuffers.size());
		for (const VkCommandBuffer commandBuffer : commandBuffers)
		{
			auto info          = VkCommandBufferSubmitInfo();
			info.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
			info.commandBuffer = commandBuffer;
			commands.push_back(info);
		}

		auto submit                     = VkSubmitInfo2();
		submit.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
		submit.pWaitSemaphoreInfos      = waits.data();
		submit.commandBufferInfoCount   = static_cast<uint32_t>(commands.size());
		submit.pCommandBufferInfos      = commands.data();
		submit.signalSemaphoreInfoCount = 1;
		submit.pSignalSemaphoreInfos    = &signal;

		{
			const std::lock_guard queueLock(*m_Queue.submitLock);
			EnsureVk(vkQueueSubmit2(m_Queue.queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2");
		}

		m_NextFenceValue.store(value + 1, std::memory_order_relaxed);
		return value;
	}

	uint64_t
	CommandQueue::ExecuteCommandList(ICommandList* commandList) noexcept
	{
		core::ensure(commandList != nullptr, "Command list is not initialized.");
		core::ensure(!commandList->IsOpen(), "A command list must be closed to execute it");
		core::ensure(
			commandList->GetType() == m_Type,
			"Command list type must match command queue type");

		auto*                 list = commandList->As<CommandList>();
		const std::lock_guard lock(m_FenceMutex);

		// Taken under the queue's lock, so a later submission on this queue cannot overtake them.
		auto barriers = std::vector<VkImageMemoryBarrier2>();
		list->GetResourceManager().TakeInitialLayouts(barriers);

		auto buffers = core::static_vector<VkCommandBuffer, 2>();
		if (!barriers.empty())
			buffers.push_back(RecordPrologue(barriers));
		buffers.push_back(list->GetVkCommandBuffer());

		const uint64_t value = SubmitLocked(buffers);
		list->Submitted(value);
		return value;
	}

	uint64_t
	CommandQueue::PollCurrentFenceValue() noexcept
	{
		uint64_t completed = 0;
		EnsureVk(
			vkGetSemaphoreCounterValue(m_Device, m_Fence, &completed),
			"vkGetSemaphoreCounterValue");

		// Max-update: concurrent pollers must never move the published value backwards.
		uint64_t previous = m_LastCompletedFenceValue.load(std::memory_order_relaxed);
		while (previous < completed && !m_LastCompletedFenceValue.compare_exchange_weak(
										   previous,
										   completed,
										   std::memory_order_relaxed))
		{}
		return std::max(previous, completed);
	}

	bool
	CommandQueue::IsFenceComplete(const uint64_t fenceValue) noexcept
	{
		if (fenceValue <= m_LastCompletedFenceValue.load(std::memory_order_relaxed))
			return true;
		return fenceValue <= PollCurrentFenceValue();
	}

	void
	CommandQueue::AddWait(const VkSemaphore semaphore, const uint64_t value) const noexcept
	{
		const std::lock_guard lock(m_FenceMutex);
		m_PendingWaits.push_back({ semaphore, value });
	}

	void
	CommandQueue::InsertWait(const uint64_t fenceValue) noexcept
	{
		AddWait(m_Fence, fenceValue);
	}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue* cq, const uint64_t fenceValue)
		const noexcept
	{
		core::ensure(cq != nullptr, "A wait needs the queue it waits on");
		AddWait(cq->As<CommandQueue>()->GetVkSemaphore(), fenceValue);
	}

	void
	CommandQueue::InsertWaitForQueue(ICommandQueue* otherQueue) const noexcept
	{
		core::ensure(otherQueue != nullptr, "A wait needs the queue it waits on");
		AddWait(
			otherQueue->As<CommandQueue>()->GetVkSemaphore(),
			otherQueue->GetNextFenceValue() - 1);
	}

	void
	CommandQueue::WaitForFenceCPUBlocking(const uint64_t fenceValue) noexcept
	{
		if (IsFenceComplete(fenceValue))
			return;

		auto info           = VkSemaphoreWaitInfo();
		info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		info.semaphoreCount = 1;
		info.pSemaphores    = &m_Fence;
		info.pValues        = &fenceValue;

		// Waited in slices so a wedged queue says so rather than hanging silently.
		constexpr uint64_t c_SliceNs        = 1'000'000'000;
		uint64_t           waitedSeconds    = 0;
		uint64_t           nextComplaintSec = 2;
		while (true)
		{
			const VkResult result = vkWaitSemaphores(m_Device, &info, c_SliceNs);
			if (result == VK_SUCCESS)
				break;
			if (result != VK_TIMEOUT)
				core::fatal("vkWaitSemaphores failed: {}", string_VkResult(result));

			if (++waitedSeconds >= nextComplaintSec)
			{
				spdlog::error(
					"CommandQueue: waited {}s for fence value {} (completed {}); the queue looks "
					"wedged",
					waitedSeconds,
					fenceValue,
					PollCurrentFenceValue());
				nextComplaintSec *= 2;
			}
		}

		uint64_t previous = m_LastCompletedFenceValue.load(std::memory_order_relaxed);
		while (previous < fenceValue && !m_LastCompletedFenceValue.compare_exchange_weak(
											previous,
											fenceValue,
											std::memory_order_relaxed))
		{}
	}

	void
	CommandQueue::Flush() noexcept
	{
		uint64_t value = 0;
		{
			const std::lock_guard lock(m_FenceMutex);
			value = SubmitLocked({});
		}
		WaitForFenceCPUBlocking(value);
	}

	double
	CommandQueue::GetTimestampFrequency() const noexcept
	{
		if (GetVulkanQueueFamilies(*m_Context)[m_Queue.family].timestampValidBits == 0)
			return 0.0;

		auto properties = VkPhysicalDeviceProperties();
		vkGetPhysicalDeviceProperties(GetVulkanHandles(*m_Context).physicalDevice, &properties);
		const double nanosecondsPerTick = properties.limits.timestampPeriod;
		return nanosecondsPerTick > 0.0 ? 1e9 / nanosecondsPerTick : 0.0;
	}

	NativeObject
	CommandQueue::GetNativeObject(const NativeObjectType type) const noexcept
	{
		if (type == NativeObjectType::kVkQueue)
			return { &m_Native };
		return {};
	}
}
