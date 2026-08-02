#pragma once
#include "convert_d3d12.h"
#include "resource/Srv.h"
#include "resource/Texture.h"

namespace bgl
{
	class Srv final
	{
	public:
		Srv() = default;
		Srv(ID3D12Device*         device,
		    TextureHandle         textureHandle,
		    ID3D12Resource*       resource,
		    ID3D12DescriptorHeap* descriptorHeap,
		    uint32_t              descriptorIndex,
		    const SrvDesc&        desc);

		Srv(const Srv&)     = delete;
		Srv(Srv&&) noexcept = default;

		Srv&
		operator=(const Srv&) = delete;

		Srv&
		operator=(Srv&&) noexcept = default;

		[[nodiscard]]
		const SrvDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]]
		uint32_t
		GetDescriptorIndex() const noexcept
		{
			return m_DescriptorIndex;
		}

		[[nodiscard]]
		bool
		IsNull() const noexcept
		{
			return m_DescriptorIndex == 0xFFFFFFFF;
		}

		[[nodiscard]]
		TextureHandle
		GetTextureHandle() const noexcept
		{
			return m_TextureHandle;
		}

	private:
		SrvDesc       m_Desc;
		uint32_t      m_DescriptorIndex = 0xFFFFFFFF;
		TextureHandle m_TextureHandle   = {};
	};
}
