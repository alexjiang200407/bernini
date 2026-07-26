#pragma once
#include "resource/Dsv.h"
#include "resource/Texture.h"

namespace bgl
{
	// A depth-stencil view: a WebGPU texture view over the depth attachment, the texture handle it
	// was made from so GetDsvTexture can return it, and the descriptor it was built from so pipeline
	// creation can read the depth format back -- the D3D12 Dsv keeps the same trio. Whether the
	// format carries a stencil aspect is derived from GetDesc().format at clear time.
	class Dsv final
	{
	public:
		Dsv() = default;

		Dsv(wgpu::TextureView view, TextureHandle textureHandle, const DsvDesc& desc) :
			m_View(std::move(view)), m_TextureHandle(textureHandle), m_Desc(desc)
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

		[[nodiscard]] const DsvDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

	private:
		wgpu::TextureView m_View;
		TextureHandle     m_TextureHandle = {};
		DsvDesc           m_Desc;
	};
}
