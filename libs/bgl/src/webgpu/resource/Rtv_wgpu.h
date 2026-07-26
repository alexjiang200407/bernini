#pragma once
#include "resource/Rtv.h"
#include "resource/Texture.h"

namespace bgl
{
	// A render-target view: a WebGPU texture view over the attachment, the texture handle it was made
	// from so GetRtvTexture can return it, and the descriptor it was built from so pipeline creation
	// can read the target format back -- the D3D12 Rtv keeps the same trio.
	class Rtv final
	{
	public:
		Rtv() = default;

		Rtv(wgpu::TextureView view, TextureHandle textureHandle, const RtvDesc& desc) :
			m_View(std::move(view)), m_TextureHandle(textureHandle), m_Desc(desc)
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

		[[nodiscard]] const RtvDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

	private:
		wgpu::TextureView m_View;
		TextureHandle     m_TextureHandle = {};
		RtvDesc           m_Desc;
	};
}
