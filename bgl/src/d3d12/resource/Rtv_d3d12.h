#pragma once
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "util.h"

namespace bgl
{
	class Rtv final
	{
	public:
		Rtv() = default;
		Rtv(ID3D12Device*         device,
		    TextureHandle         textureHandle,
		    ID3D12DescriptorHeap* descriptorHeap,
		    uint32_t              descriptorIndex,
		    const RtvDesc&        desc);

		Rtv(const Rtv&)     = delete;
		Rtv(Rtv&&) noexcept = default;

		Rtv&
		operator=(const Rtv&) = delete;

		Rtv&
		operator=(Rtv&&) noexcept = default;

		[[nodiscard]]
		const RtvDesc&
		GetDesc() const
		{
			return m_Desc;
		}

		[[nodiscard]]
		D3D12_CPU_DESCRIPTOR_HANDLE
		GetCpuHandle() const noexcept
		{
			return m_CpuHandle;
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
		RtvDesc                     m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle       = {};
		TextureHandle               m_TextureHandle   = {};
	};
}
