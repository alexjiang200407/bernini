#include "cmd/TimestampHeap_vulkan.h"
#include "native_device_vulkan.h"
#include "resource/ReadbackBuffer_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <bgpu/GpuContext.h>
#include <bgpu/resource/Readback.h>
#include <core/err/util.h>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace bgpu
{
	bool
	TimestampHeap::Supported(const GpuContext& context) noexcept
	{
		auto properties = VkPhysicalDeviceProperties();
		vkGetPhysicalDeviceProperties(GetVulkanHandles(context).physicalDevice, &properties);
		return properties.limits.timestampComputeAndGraphics == VK_TRUE;
	}

	TimestampHeap::TimestampHeap(GpuContextRef context, const uint32_t capacity) :
		m_Context(std::move(context)), m_Capacity(capacity)
	{
		core::ensure(capacity > 0, "A timestamp heap needs at least one slot");

		auto info       = VkQueryPoolCreateInfo();
		info.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType  = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = capacity;

		const VkDevice device = GetVulkanHandles(*m_Context).device;
		EnsureVk(vkCreateQueryPool(device, &info, nullptr, &m_Pool), "vkCreateQueryPool");
		SetVkDebugName(
			device,
			VK_OBJECT_TYPE_QUERY_POOL,
			reinterpret_cast<uint64_t>(m_Pool),
			"Timestamp Heap");

		auto rbDesc      = ReadbackBufferDesc();
		rbDesc.byteSize  = static_cast<uint64_t>(capacity) * sizeof(uint64_t);
		rbDesc.debugName = "Timestamp Readback";
		m_Readback       = ReadbackBuffer(m_Context, rbDesc);
		std::memset(const_cast<void*>(m_Readback.Map()), 0, rbDesc.byteSize);
	}

	TimestampHeap::~TimestampHeap() noexcept
	{
		vkDestroyQueryPool(GetVulkanHandles(*m_Context).device, m_Pool, nullptr);
	}

	void
	TimestampHeap::Read(const uint32_t first, const std::span<uint64_t> out) const noexcept
	{
		core::ensure(first + out.size() <= m_Capacity, "Timestamp read outside the heap");
		if (out.empty())
			return;

		const auto* mapped = static_cast<const uint64_t*>(m_Readback.Map());
		std::memcpy(out.data(), mapped + first, out.size() * sizeof(uint64_t));
	}
}
