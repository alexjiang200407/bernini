#include "resource/ImageMemory_vulkan.h"
#include "native_device_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <bgpu/GpuContext.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vulkan/vk_enum_string_helper.h>

namespace bgpu
{
	namespace
	{
		// Every image a manager made, so an importer can add a reference to one it is handed as a
		// bare VkImage.
		struct Registry
		{
			std::mutex                                mutex;
			std::unordered_map<VkImage, ImageMemory*> images;
		};

		Registry&
		GetRegistry() noexcept
		{
			static auto g_Registry = Registry();
			return g_Registry;
		}
	}

	ImageMemory::ImageMemory(
		GpuContextRef            context,
		const VkImageCreateInfo& info,
		const std::string_view   debugName) : m_Context(std::move(context))
	{
		core::ensure(m_Context != nullptr, "An image needs a GPU context");

		const VulkanHandles handles = GetVulkanHandles(*m_Context);
		m_Device                    = handles.device;

		if (const VkResult created = vkCreateImage(m_Device, &info, nullptr, &m_Image);
		    created != VK_SUCCESS)
		{
			core::throw_runtime_error("vkCreateImage failed: {}", string_VkResult(created));
		}

		try
		{
			auto requirements = VkMemoryRequirements();
			vkGetImageMemoryRequirements(m_Device, m_Image, &requirements);

			auto allocation            = VkMemoryAllocateInfo();
			allocation.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocation.allocationSize  = requirements.size;
			allocation.memoryTypeIndex = FindMemoryType(
				handles.physicalDevice,
				requirements.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				0);
			if (const VkResult allocated =
			        vkAllocateMemory(m_Device, &allocation, nullptr, &m_Memory);
			    allocated != VK_SUCCESS)
			{
				core::throw_runtime_error(
					"vkAllocateMemory of {} bytes failed: {}",
					requirements.size,
					string_VkResult(allocated));
			}
			m_AllocationSize = requirements.size;

			EnsureVk(vkBindImageMemory(m_Device, m_Image, m_Memory, 0), "vkBindImageMemory");
		}
		catch (...)
		{
			Destroy();
			throw;
		}

		SetVkDebugName(
			m_Device,
			VK_OBJECT_TYPE_IMAGE,
			reinterpret_cast<uint64_t>(m_Image),
			debugName);

		Registry&             registry = GetRegistry();
		const std::lock_guard lock(registry.mutex);
		registry.images.emplace(m_Image, this);
	}

	ImageMemory::~ImageMemory() noexcept
	{
		{
			Registry&             registry = GetRegistry();
			const std::lock_guard lock(registry.mutex);
			registry.images.erase(m_Image);
		}
		Destroy();
	}

	core::SharedRef<ImageMemory>
	ImageMemory::Find(const VkImage image) noexcept
	{
		Registry&             registry = GetRegistry();
		const std::lock_guard lock(registry.mutex);
		const auto            found = registry.images.find(image);
		if (found == registry.images.end())
			return nullptr;

		// The entry is not a reference: its last owner may have let go, with the destructor now
		// waiting on this lock to erase it. A reference is taken only while one is still held.
		ImageMemory* const memory = found->second;
		if (!memory->TryAddRef())
			return nullptr;
		auto reference = core::SharedRef<ImageMemory>(memory);
		memory->Release();
		return reference;
	}

	void
	ImageMemory::Destroy() noexcept
	{
		if (m_Image != VK_NULL_HANDLE)
			vkDestroyImage(m_Device, m_Image, nullptr);
		if (m_Memory != VK_NULL_HANDLE)
			vkFreeMemory(m_Device, m_Memory, nullptr);

		m_Image  = VK_NULL_HANDLE;
		m_Memory = VK_NULL_HANDLE;
	}
}
