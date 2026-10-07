#include "resource/BindlessTable_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <array>
#include <core/err/util.h>
#include <cstdint>

namespace bgpu
{
	namespace
	{
		constexpr VkShaderStageFlags c_Stages =
			VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_TASK_BIT_EXT |
			VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;

		// What a resource descriptor may hold: a buffer handle's storage buffer or a texture
		// handle's sampled image. The RHI's textures have no UAV, so no storage image.
		constexpr auto c_ResourceTypes =
			std::to_array({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE });

		// The mutable type list for each binding, in binding order: the sampler binding has none.
		struct MutableTypes
		{
			std::array<VkMutableDescriptorTypeListEXT, 2> lists = {};
			VkMutableDescriptorTypeCreateInfoEXT          info  = {};

			MutableTypes() noexcept
			{
				lists[1].descriptorTypeCount = static_cast<uint32_t>(c_ResourceTypes.size());
				lists[1].pDescriptorTypes    = c_ResourceTypes.data();

				info.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT;
				info.mutableDescriptorTypeListCount = static_cast<uint32_t>(lists.size());
				info.pMutableDescriptorTypeLists    = lists.data();
			}

			MutableTypes(const MutableTypes&) = delete;
			MutableTypes(MutableTypes&&)      = delete;
			MutableTypes&
			operator=(const MutableTypes&) = delete;
			MutableTypes&
			operator=(MutableTypes&&) = delete;
		};
	}

	VkDescriptorSetLayout
	BindlessTable::CreateSetLayout(const VkDevice device) noexcept
	{
		auto bindings = std::array<VkDescriptorSetLayoutBinding, 2>();

		bindings[0].binding         = c_SamplerBinding;
		bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
		bindings[0].descriptorCount = c_SamplerCapacity;
		bindings[0].stageFlags      = c_Stages;

		bindings[1].binding         = c_ResourceBinding;
		bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
		bindings[1].descriptorCount = c_Capacity;
		bindings[1].stageFlags      = c_Stages;

		// Partially bound: a freed index is never rewritten. Updated after bind: a create writes its
		// descriptor while the set is bound in work still in flight.
		const VkDescriptorBindingFlags bindless =
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		const auto bindingFlags = std::to_array({ bindless, bindless });

		auto mutableTypes = MutableTypes();

		auto flags          = VkDescriptorSetLayoutBindingFlagsCreateInfo();
		flags.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flags.pNext         = &mutableTypes.info;
		flags.bindingCount  = static_cast<uint32_t>(bindingFlags.size());
		flags.pBindingFlags = bindingFlags.data();

		auto info         = VkDescriptorSetLayoutCreateInfo();
		info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.pNext        = &flags;
		info.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		info.bindingCount = static_cast<uint32_t>(bindings.size());
		info.pBindings    = bindings.data();

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

		auto sizes               = std::array<VkDescriptorPoolSize, 2>();
		sizes[0].type            = VK_DESCRIPTOR_TYPE_SAMPLER;
		sizes[0].descriptorCount = c_SamplerCapacity;
		sizes[1].type            = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
		sizes[1].descriptorCount = c_Capacity;

		// A pool's mutable sizes are matched to the layout's by the type lists, in pool-size order.
		auto mutableTypes = MutableTypes();

		auto poolInfo          = VkDescriptorPoolCreateInfo();
		poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.pNext         = &mutableTypes.info;
		poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets       = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
		poolInfo.pPoolSizes    = sizes.data();
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
		write.dstBinding      = c_ResourceBinding;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo     = &bufferInfo;
		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	}

	void
	BindlessTable::WriteImage(const uint32_t index, const VkImageView view) noexcept
	{
		auto imageInfo        = VkDescriptorImageInfo();
		imageInfo.imageView   = view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		auto write            = VkWriteDescriptorSet();
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = m_Set;
		write.dstBinding      = c_ResourceBinding;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo      = &imageInfo;
		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	}

	void
	BindlessTable::WriteSampler(const uint32_t index, const VkSampler sampler) noexcept
	{
		core::ensure(index < c_SamplerCapacity, "Sampler index {} is past the table", index);

		auto imageInfo    = VkDescriptorImageInfo();
		imageInfo.sampler = sampler;

		auto write            = VkWriteDescriptorSet();
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = m_Set;
		write.dstBinding      = c_SamplerBinding;
		write.dstArrayElement = index;
		write.descriptorCount = 1;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
		write.pImageInfo      = &imageInfo;
		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	}
}
