#pragma once
#include "resource/Dsv.h"
#include "resource/Texture.h"

namespace bgl
{
	// Metal has no standalone depth-stencil view, for the same reason it has no render-target view: a
	// render pass attaches the texture directly. So a Metal Dsv remembers which texture it targets,
	// and the attach/clear happens against that texture at pass time.
	class Dsv
	{
	public:
		Dsv() = default;

		Dsv(const DsvDesc& desc, TextureHandle texture) : m_Desc(desc), m_Texture(texture) {}

		[[nodiscard]] const DsvDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] TextureHandle
		GetTextureHandle() const noexcept
		{
			return m_Texture;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Texture.IsNull();
		}

	private:
		DsvDesc       m_Desc;
		TextureHandle m_Texture;
	};
}
