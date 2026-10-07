#include "resource/Sampler_vulkan.h"
#include "convert_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/resource/Sampler.h>
#include <core/err/util.h>
#include <utility>
#include <vulkan/vk_enum_string_helper.h>

namespace bgpu
{
	Sampler::Sampler(const VkDevice device, const SamplerDesc& desc) :
		m_Desc(desc), m_Device(device)
	{
		auto       reduction = VkSamplerReductionModeCreateInfo();
		const auto info      = ConvertSamplerDesc(desc, reduction);
		if (const VkResult created = vkCreateSampler(m_Device, &info, nullptr, &m_Sampler);
		    created != VK_SUCCESS)
		{
			core::throw_runtime_error("vkCreateSampler failed: {}", string_VkResult(created));
		}
	}

	Sampler::~Sampler() noexcept
	{
		if (m_Sampler != VK_NULL_HANDLE)
			vkDestroySampler(m_Device, m_Sampler, nullptr);
	}

	Sampler&
	Sampler::operator=(Sampler&& other) noexcept
	{
		if (this != &other)
		{
			if (m_Sampler != VK_NULL_HANDLE)
				vkDestroySampler(m_Device, m_Sampler, nullptr);
			m_Desc    = other.m_Desc;
			m_Device  = std::exchange(other.m_Device, VK_NULL_HANDLE);
			m_Sampler = std::exchange(other.m_Sampler, VK_NULL_HANDLE);
		}
		return *this;
	}
}
