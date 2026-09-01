#pragma once
#include "resource/Srv.h"
#include "resource/Texture.h"

namespace bgl
{
	// Metal has no descriptor heap: a shader reaches a texture by its native id, or the encoder finds
	// it by pool slot at dispatch. So a Metal Srv owns nothing and just remembers which texture it
	// views -- the same shape Rtv and Dsv already have here, and the seam a format or mip view would
	// need later.
	class Srv
	{
	public:
		Srv() = default;

		Srv(const SrvDesc& desc, TextureHandle texture) : m_Desc(desc), m_Texture(texture) {}

		[[nodiscard]] const SrvDesc&
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
		SrvDesc       m_Desc;
		TextureHandle m_Texture;
	};
}
