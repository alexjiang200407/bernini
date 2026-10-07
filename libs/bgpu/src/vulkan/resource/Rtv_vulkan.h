#pragma once
#include "resource/ImageView_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <utility>

namespace bgpu
{
	/** A texture's colour target view: its image view, the subresources it covers, and its texture. */
	class Rtv final
	{
	public:
		Rtv() = default;
		Rtv(ImageView                      view,
		    const VkImageSubresourceRange& range,
		    const TextureHandle            texture,
		    RtvDesc                        desc) :
			m_Desc(std::move(desc)), m_View(std::move(view)), m_Range(range), m_Texture(texture)
		{}

		[[nodiscard]] const RtvDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] VkImageView
		GetVkImageView() const noexcept
		{
			return m_View.GetVkImageView();
		}

		[[nodiscard]] const VkImageSubresourceRange&
		GetRange() const noexcept
		{
			return m_Range;
		}

		[[nodiscard]] TextureHandle
		GetTextureHandle() const noexcept
		{
			return m_Texture;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_View.GetVkImageView() == VK_NULL_HANDLE;
		}

	private:
		RtvDesc                 m_Desc;
		ImageView               m_View;
		VkImageSubresourceRange m_Range = {};
		TextureHandle           m_Texture;
	};
}
