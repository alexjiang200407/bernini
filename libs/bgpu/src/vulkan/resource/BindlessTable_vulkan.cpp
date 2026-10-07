#include "resource/BindlessTable_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <core/err/util.h>
#include <cstdint>

namespace bgpu
{
	namespace
	{
		constexpr VkShaderStageFlags c_Stages =
			VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT |
			VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayout
	BindlessTable::CreateSetLayout(const VkDevice device) noexcept
	{
		auto binding            = VkDescriptorSetLayoutBinding();
		binding.binding         = c_BufferBinding;
		binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		binding.descriptorCount = c_Capacity;
		binding.stageFlags      = c_Stages;

		// Partially bound: a freed index is never rewritten. Updated after bind: a create writes its
		// descriptor while the set is bound in work still in flight.
		const VkDescriptorBindingFlags bindless =
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

		auto flags          = VkDescriptorSetLayoutBindingFlagsCreateInfo();
		flags.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flags.bindingCount  = 1;
		flags.pBindingFlags = &bindless;

		auto info         = VkDescriptorSetLayoutCreateInfo();
		info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.pNext        = &flags;
		info.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		info.bindingCount = 1;
		info.pBindings    = &binding;

		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		EnsureVk(
			vkCreateDescriptorSetLayout(device, &info, nullptr, &layout),
			"vkCreateDescriptorSetLayout");
		return layout;
	}

	BindlessTable::BindlessTable(const VkDevice device, const uint32_t indexCount) :
		m_Device(device), m_Indices(indexCount)
	{
		core::ensure(
			indexCount <= c_Capacity,
			"A resource manager asked for {} descriptors; the bindless table holds {}",
			indexCount,
			c_Capacity);

		m_Layout = CreateSetLayout(m_Device);

		auto size            = VkDescriptorPoolSize();
		size.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		size.descriptorCount = c_Capacity;

		auto poolInfo          = VkDescriptorPoolCreateInfo();
		poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets       = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes    = &size;
		EnsureVk(
			vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_Pool),
			"vkCreateDescriptorPool");

		auto setInfo               = VkDescriptorSetAllocateInfo();
		setInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		setInfo.descriptorPool     = m_Pool;
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts        = &m_Layout;
		EnsureVk(vkAllocateDescriptorSets(m_Device, &setInfo, &m_Set), "vkAllocateDescriptorSets");
	}

	BindlessTable::~BindlessTable() noexcept
	{
		vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
		vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
	}

	void
	BindlessTable::WriteBuffer(const uint32_t index, const VkBuffer buffer) noexcept
	{
		auto bufferInfo   = VkDescriptorBufferInfo();
		bufferInfo.buffer = buffer;
		bufferInfo.offset = 0;
		bufferInfo.range  = VK_WHOLE_SIZE;

		auto write            = VkWriteDescriptorSet();
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = m_Set;
		write.dstBinding      = c_BufferBinding;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo     = &bufferInfo;
		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	}
}
