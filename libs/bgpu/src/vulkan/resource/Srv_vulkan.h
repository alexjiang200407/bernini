#pragma once
#include "resource/ImageView_vulkan.h"
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <cstdint>
#include <limits>
#include <utility>

namespace bgpu
{
	/** A texture's shader view: its image view and the bindless index it is written at. */
	class Srv final
	{
	public:
		Srv() = default;
		Srv(ImageView           view,
		    const TextureHandle texture,
		    const uint32_t      descriptorIndex,
		    SrvDesc             desc) :
			m_Desc(std::move(desc)), m_View(std::move(view)), m_Texture(texture),
			m_DescriptorIndex(descriptorIndex)
		{}

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

		[[nodiscard]] uint32_t
		GetDescriptorIndex() const noexcept
		{
			return m_DescriptorIndex;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_View.GetVkImageView() == VK_NULL_HANDLE;
		}

	private:
		SrvDesc       m_Desc;
		ImageView     m_View;
		TextureHandle m_Texture;
		uint32_t      m_DescriptorIndex = std::numeric_limits<uint32_t>::max();
	};
}
