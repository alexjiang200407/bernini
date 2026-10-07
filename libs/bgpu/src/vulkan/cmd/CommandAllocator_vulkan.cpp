#include "cmd/CommandAllocator_vulkan.h"
#include "native_device_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <array>
#include <bgpu/GpuContext.h>
#include <core/err/util.h>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <utility>

namespace bgpu
{
	namespace
	{
		// Sized for a list's dispatches between resets; a full pool adds another rather than failing.
		constexpr uint32_t c_SetsPerPool    = 256;
		constexpr uint32_t c_BuffersPerPool = c_SetsPerPool * 4;
	}

	CommandAllocator::CommandAllocator(GpuContextRef context) :
		m_Context(std::move(context)), m_Device(GetVulkanHandles(*m_Context).device)
	{
		AddDescriptorPool();
	}

	CommandAllocator::~CommandAllocator() noexcept
	{
		spdlog::trace("~CommandAllocator");
		for (const auto& [family, pool] : m_CommandPools)
			vkDestroyCommandPool(m_Device, pool.pool, nullptr);
		for (const VkDescriptorPool pool : m_DescriptorPools)
			vkDestroyDescriptorPool(m_Device, pool, nullptr);
	}

	void
	CommandAllocator::ResetAllocator() noexcept
	{
		for (auto& [family, pool] : m_CommandPools)
		{
			EnsureVk(vkResetCommandPool(m_Device, pool.pool, 0), "vkResetCommandPool");
			pool.taken = 0;
		}
		for (const VkDescriptorPool pool : m_DescriptorPools)
			EnsureVk(vkResetDescriptorPool(m_Device, pool, 0), "vkResetDescriptorPool");
		m_CurrentDescriptorPool = 0;
	}

	VkCommandBuffer
	CommandAllocator::TakeCommandBuffer(const uint32_t family) noexcept
	{
		FamilyPool& pool = m_CommandPools[family];
		if (pool.pool == VK_NULL_HANDLE)
		{
			auto info             = VkCommandPoolCreateInfo();
			info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.queueFamilyIndex = family;
			EnsureVk(
				vkCreateCommandPool(m_Device, &info, nullptr, &pool.pool),
				"vkCreateCommandPool");
		}

		if (pool.taken == pool.buffers.size())
		{
			auto info               = VkCommandBufferAllocateInfo();
			info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.commandPool        = pool.pool;
			info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = 1;

			VkCommandBuffer buffer = VK_NULL_HANDLE;
			EnsureVk(
				vkAllocateCommandBuffers(m_Device, &info, &buffer),
				"vkAllocateCommandBuffers");
			pool.buffers.push_back(buffer);
		}
		return pool.buffers[pool.taken++];
	}

	VkDescriptorSet
	CommandAllocator::AllocateSet(const VkDescriptorSetLayout layout) noexcept
	{
		auto info               = VkDescriptorSetAllocateInfo();
		info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		info.descriptorSetCount = 1;
		info.pSetLayouts        = &layout;

		while (true)
		{
			info.descriptorPool = m_DescriptorPools[m_CurrentDescriptorPool];

			VkDescriptorSet set    = VK_NULL_HANDLE;
			const VkResult  result = vkAllocateDescriptorSets(m_Device, &info, &set);
			if (result == VK_SUCCESS)
				return set;

			core::ensure(
				result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL,
				"vkAllocateDescriptorSets failed");
			if (++m_CurrentDescriptorPool == m_DescriptorPools.size())
				AddDescriptorPool();
		}
	}

	void
	CommandAllocator::AddDescriptorPool() noexcept
	{
		auto size            = VkDescriptorPoolSize();
		size.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		size.descriptorCount = c_BuffersPerPool;

		auto info          = VkDescriptorPoolCreateInfo();
		info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.maxSets       = c_SetsPerPool;
		info.poolSizeCount = 1;
		info.pPoolSizes    = &size;

		VkDescriptorPool pool = VK_NULL_HANDLE;
		EnsureVk(vkCreateDescriptorPool(m_Device, &info, nullptr, &pool), "vkCreateDescriptorPool");
		m_DescriptorPools.push_back(pool);
	}
}
