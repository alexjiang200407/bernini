#pragma once
#include "resource/Dsv.h"
#include "resource/Texture.h"

namespace bgl
{
	// A depth-stencil view: a WebGPU texture view over the depth attachment, plus the texture handle
	// it was made from so GetDsvTexture can return it.
	class Dsv final
	{
	public:
		Dsv() = default;

		Dsv(wgpu::TextureView view, TextureHandle textureHandle, bool hasStencil) :
			m_View(std::move(view)), m_TextureHandle(textureHandle), m_HasStencil(hasStencil)
		{}

		Dsv(const Dsv&)     = delete;
		Dsv(Dsv&&) noexcept = default;
		Dsv&
		operator=(const Dsv&) = delete;
		Dsv&
		operator=(Dsv&&) noexcept = default;

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

		[[nodiscard]] bool
		HasStencil() const noexcept
		{
			return m_HasStencil;
		}

	private:
		wgpu::TextureView m_View;
		TextureHandle     m_TextureHandle = {};
		bool              m_HasStencil    = false;
	};
}
