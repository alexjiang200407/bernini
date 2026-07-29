#pragma once
#include "resource/Texture.h"

namespace bgl
{
	class Texture final
	{
	public:
		Texture() = default;

		Texture(const wgpu::Device& device, const TextureDesc& desc);

		Texture(const Texture&)     = delete;
		Texture(Texture&&) noexcept = default;
		Texture&
		operator=(const Texture&) = delete;
		Texture&
		operator=(Texture&&) noexcept = default;

		[[nodiscard]] const wgpu::Texture&
		GetHandle() const noexcept
		{
			return m_Texture;
		}

		// The whole-resource view a shader binds, null unless the texture was created kSRV. Its
		// dimension comes from the desc, which is the only place a cube is distinguishable: WebGPU
		// stores one as a 2D texture with six layers.
		[[nodiscard]] const wgpu::TextureView&
		GetSampledView() const noexcept
		{
			return m_SampledView;
		}

		[[nodiscard]] const TextureDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Texture == nullptr;
		}

	private:
		wgpu::Texture     m_Texture;
		wgpu::TextureView m_SampledView;
		TextureDesc       m_Desc;
	};
}
