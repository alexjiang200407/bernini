#include "resource/BufferMemory_vulkan.h"
#include "native_device_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <bgpu/GpuContext.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <core/ref/WeakRef.h>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vulkan/vk_enum_string_helper.h>

namespace bgpu
{
	namespace
	{
		// Every device buffer a manager made, so an importer can add a reference to one it is handed
		// as a bare VkBuffer.
		struct Registry
		{
			std::mutex                                                mutex;
			std::unordered_map<VkBuffer, core::WeakRef<BufferMemory>> buffers;
		};

		Registry&
		GetRegistry() noexcept
		{
			static auto g_Registry = Registry();
			return g_Registry;
		}

		struct KindTraits
		{
			VkBufferUsageFlags    usage;
			VkMemoryPropertyFlags required;
			VkMemoryPropertyFlags preferred;
		};

		[[nodiscard]] KindTraits
		TraitsOf(const BufferMemoryKind kind) noexcept
		{
			switch (kind)
			{
			case BufferMemoryKind::kDevice:
				return {
					.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
					         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
					.required  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					.preferred = 0,
				};
			case BufferMemoryKind::kUpload:
				return {
					.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					.required =
						VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					.preferred = 0,
				};
			case BufferMemoryKind::kReadback:
				return {
					.usage     = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					.required  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
					.preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				};
			}
			core::fatal("Unknown buffer memory kind");
		}
	}

	BufferMemory::BufferMemory(
		GpuContextRef          context,
		const uint64_t         byteSize,
		const BufferMemoryKind kind,
		const std::string_view debugName) :
		m_Context(std::move(context)), m_ByteSize(byteSize), m_Kind(kind)
	{
		core::ensure(m_Context != nullptr, "A buffer needs a GPU context");
		core::ensure(byteSize > 0, "A buffer needs a byte size");

		const VulkanHandles handles = GetVulkanHandles(*m_Context);
		m_Device                    = handles.device;
		const KindTraits traits     = TraitsOf(kind);

		const VulkanSharing sharing = GetVulkanSharing(*m_Context);

		auto info                  = VkBufferCreateInfo();
		info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size                  = byteSize;
		info.usage                 = traits.usage;
		info.sharingMode           = sharing.mode;
		info.queueFamilyIndexCount = static_cast<uint32_t>(sharing.families.size());
		info.pQueueFamilyIndices   = sharing.families.data();

		if (const VkResult created = vkCreateBuffer(m_Device, &info, nullptr, &m_Buffer);
		    created != VK_SUCCESS)
		{
			core::throw_runtime_error("vkCreateBuffer failed: {}", string_VkResult(created));
		}

		try
		{
			auto requirements = VkMemoryRequirements();
			vkGetBufferMemoryRequirements(m_Device, m_Buffer, &requirements);

			const uint32_t memoryType = FindMemoryType(
				handles.physicalDevice,
				requirements.memoryTypeBits,
				traits.required,
				traits.preferred);

			auto allocation            = VkMemoryAllocateInfo();
			allocation.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocation.allocationSize  = requirements.size;
			allocation.memoryTypeIndex = memoryType;
			if (const VkResult allocated =
			        vkAllocateMemory(m_Device, &allocation, nullptr, &m_Memory);
			    allocated != VK_SUCCESS)
			{
				core::throw_runtime_error(
					"vkAllocateMemory of {} bytes failed: {}",
					requirements.size,
					string_VkResult(allocated));
			}

			EnsureVk(vkBindBufferMemory(m_Device, m_Buffer, m_Memory, 0), "vkBindBufferMemory");

			if (kind != BufferMemoryKind::kDevice)
			{
				auto properties = VkPhysicalDeviceMemoryProperties();
				vkGetPhysicalDeviceMemoryProperties(handles.physicalDevice, &properties);
				m_Coherent = (properties.memoryTypes[memoryType].propertyFlags &
				              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
				EnsureVk(
					vkMapMemory(m_Device, m_Memory, 0, VK_WHOLE_SIZE, 0, &m_Mapped),
					"vkMapMemory");
			}
		}
		catch (...)
		{
			Destroy();
			throw;
		}

		SetVkDebugName(
			m_Device,
			VK_OBJECT_TYPE_BUFFER,
			reinterpret_cast<uint64_t>(m_Buffer),
			debugName);

		if (kind == BufferMemoryKind::kDevice)
		{
			Registry&             registry = GetRegistry();
			const std::lock_guard lock(registry.mutex);
			registry.buffers.emplace(m_Buffer, core::WeakRef<BufferMemory>(this));
		}
	}

	BufferMemory::~BufferMemory() noexcept
	{
		if (m_Kind == BufferMemoryKind::kDevice)
		{
			Registry&             registry = GetRegistry();
			const std::lock_guard lock(registry.mutex);
			registry.buffers.erase(m_Buffer);
		}
		Destroy();
	}

	core::SharedRef<BufferMemory>
	BufferMemory::Find(const VkBuffer buffer) noexcept
	{
		Registry&             registry = GetRegistry();
		const std::lock_guard lock(registry.mutex);
		const auto            found = registry.buffers.find(buffer);
		if (found == registry.buffers.end())
			return nullptr;

		// Null when the last owner has let go and the destructor waits on this lock to erase it.
		return found->second.Lock();
	}

	void
	BufferMemory::InvalidateForRead() const noexcept
	{
		if (m_Coherent || m_Mapped == nullptr)
			return;

		auto range   = VkMappedMemoryRange();
		range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = m_Memory;
		range.offset = 0;
		range.size   = VK_WHOLE_SIZE;
		EnsureVk(
			vkInvalidateMappedMemoryRanges(m_Device, 1, &range),
			"vkInvalidateMappedMemoryRanges");
	}

	void
	BufferMemory::Destroy() noexcept
	{
		if (m_Mapped != nullptr)
			vkUnmapMemory(m_Device, m_Memory);
		if (m_Buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(m_Device, m_Buffer, nullptr);
		if (m_Memory != VK_NULL_HANDLE)
			vkFreeMemory(m_Device, m_Memory, nullptr);

		m_Mapped = nullptr;
		m_Buffer = VK_NULL_HANDLE;
		m_Memory = VK_NULL_HANDLE;
	}
}
