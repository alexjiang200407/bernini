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
		wgpu::Texture m_Texture;
		TextureDesc   m_Desc;
	};
}
