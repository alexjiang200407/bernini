#include "cmd/CommandQueue_vulkan.h"
#include "cmd/CommandList_vulkan.h"
#include "convert_vulkan.h"
#include "native_device_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <algorithm>
#include <atomic>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/types/QueueType.h>
#include <core/err/util.h>
#include <cstdint>
#include <mutex>
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
	}

	CommandQueue::~CommandQueue() noexcept
	{
		spdlog::trace("~CommandQueue");
		vkDestroySemaphore(m_Device, m_Fence, nullptr);
		ReleaseVulkanQueue(*m_Context, m_Queue);
	}

	uint64_t
	CommandQueue::SubmitLocked(const VkCommandBuffer commandBuffer) noexcept
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

		auto commands          = VkCommandBufferSubmitInfo();
		commands.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		commands.commandBuffer = commandBuffer;

		auto submit                     = VkSubmitInfo2();
		submit.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
		submit.pWaitSemaphoreInfos      = waits.data();
		submit.commandBufferInfoCount   = commandBuffer != VK_NULL_HANDLE ? 1 : 0;
		submit.pCommandBufferInfos      = &commands;
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
		const uint64_t        value = SubmitLocked(list->GetVkCommandBuffer());
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
			value = SubmitLocked(VK_NULL_HANDLE);
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
}
