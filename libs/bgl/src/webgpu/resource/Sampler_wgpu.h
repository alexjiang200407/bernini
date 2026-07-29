#pragma once
#include "resource/Sampler.h"

namespace bgl
{
	// Unlike the D3D12 Sampler, which owns only a slot in the sampler heap, this owns the WebGPU
	// object itself: WebGPU has no descriptor heap and a bind group takes the sampler directly.
	class Sampler final
	{
	public:
		Sampler() = default;

		Sampler(const wgpu::Device& device, const SamplerDesc& desc);

		Sampler(const Sampler&)     = delete;
		Sampler(Sampler&&) noexcept = default;

		Sampler&
		operator=(const Sampler&) = delete;

		Sampler&
		operator=(Sampler&&) noexcept = default;

		[[nodiscard]] const wgpu::Sampler&
		GetHandle() const noexcept
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
			return m_Sampler == nullptr;
		}

	private:
		wgpu::Sampler m_Sampler;
		SamplerDesc   m_Desc;
	};
}
