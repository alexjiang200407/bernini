#pragma once
#include "resource/Rtv.h"
#include "resource/Texture.h"

namespace bgl
{
	// A render-target view: a WebGPU texture view over the attachment, plus the texture handle it
	// was made from so GetRtvTexture can return it.
	class Rtv final
	{
	public:
		Rtv() = default;

		Rtv(wgpu::TextureView view, TextureHandle textureHandle) :
			m_View(std::move(view)), m_TextureHandle(textureHandle)
		{}

		Rtv(const Rtv&)     = delete;
		Rtv(Rtv&&) noexcept = default;
		Rtv&
		operator=(const Rtv&) = delete;
		Rtv&
		operator=(Rtv&&) noexcept = default;

		[[nodiscard]] const wgpu::TextureView&
		GetView() const noexcept
		{
			return m_View;
		}

		[[nodiscard]] TextureHandle
		GetTextureHandle() const noexcept
		{
			return m_TextureHandle;
		}

	private:
		wgpu::TextureView m_View;
		TextureHandle     m_TextureHandle = {};
	};
}
