#pragma once
#include "volk_vulkan.h"
#include <bgpu/resource/Sampler.h>
#include <utility>

namespace bgpu
{
	/** A VkSampler, destroyed with this, and the desc it was made from. */
	class Sampler final
	{
	public:
		Sampler() = default;

		/** @throws std::runtime_error when the driver cannot make another sampler. */
		Sampler(VkDevice device, const SamplerDesc& desc);
		~Sampler() noexcept;

		Sampler(const Sampler&) = delete;
		Sampler(Sampler&& other) noexcept :
			m_Desc(other.m_Desc), m_Device(std::exchange(other.m_Device, VK_NULL_HANDLE)),
			m_Sampler(std::exchange(other.m_Sampler, VK_NULL_HANDLE))
		{}
		Sampler&
		operator=(const Sampler&) = delete;
		Sampler&
		operator=(Sampler&& other) noexcept;

		[[nodiscard]] VkSampler
		GetVkSampler() const noexcept
		{
			return m_Sampler;
		}

		[[nodiscard]] const SamplerDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Sampler == VK_NULL_HANDLE;
		}

	private:
		SamplerDesc m_Desc;
		VkDevice    m_Device  = VK_NULL_HANDLE;
		VkSampler   m_Sampler = VK_NULL_HANDLE;
	};
}
