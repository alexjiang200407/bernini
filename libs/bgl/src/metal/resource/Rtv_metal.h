#pragma once
#include "resource/Rtv.h"
#include "resource/Texture.h"

namespace bgl
{
	// Metal has no standalone render-target view: a render pass attaches the texture directly. So a
	// Metal Rtv just remembers which texture (and view desc) it targets; the attach/clear happens
	// against that texture at pass time.
	class Rtv
	{
	public:
		Rtv() = default;

		Rtv(const RtvDesc& desc, TextureHandle texture) : m_Desc(desc), m_Texture(texture) {}

		[[nodiscard]] const RtvDesc&
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
		RtvDesc       m_Desc;
		TextureHandle m_Texture;
	};
}
