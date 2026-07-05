#pragma once
#include "resource/Sampler.h"
#include "util_d3d12.h"

namespace bgl
{
	// A sampler is descriptor-heap-only: unlike Buffer/Texture there is no backing
	// ID3D12Resource. The object just owns its slot in the shader-visible sampler
	// heap; ResourceManager writes the actual D3D12_SAMPLER_DESC into it.
	class Sampler final
	{
	public:
		Sampler() = default;

		Sampler(
			ID3D12Device*         device,
			ID3D12DescriptorHeap* samplerHeap,
			uint32_t              descriptorIndex,
			const SamplerDesc&    desc);

		~Sampler() noexcept = default;

		Sampler(const Sampler&)     = delete;
		Sampler(Sampler&&) noexcept = default;

		Sampler&
		operator=(const Sampler&) = delete;

		Sampler&
		operator=(Sampler&&) noexcept = default;

		[[nodiscard]]
		const SamplerDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]]
		D3D12_CPU_DESCRIPTOR_HANDLE
		GetCpuHandle() const noexcept
		{
			return m_CpuHandle;
		}

		[[nodiscard]]
		bool
		IsNull() const noexcept
		{
			return m_DescriptorIndex == 0xFFFFFFFF;
		}

	private:
		SamplerDesc                 m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle       = {};
	};
}
