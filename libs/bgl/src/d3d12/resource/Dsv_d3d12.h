#pragma once
#include "resource/Dsv.h"
#include "resource/Texture.h"

namespace bgl
{
	class Dsv final
	{
	public:
		Dsv() = default;
		Dsv(ID3D12Device*         device,
		    TextureHandle         textureHandle,
		    ID3D12DescriptorHeap* descriptorHeap,
		    uint32_t              descriptorIndex,
		    const DsvDesc&        desc);

		Dsv(const Dsv&)     = delete;
		Dsv(Dsv&&) noexcept = default;

		Dsv&
		operator=(const Dsv&) = delete;

		Dsv&
		operator=(Dsv&&) noexcept = default;

		[[nodiscard]]
		const DsvDesc&
		GetDesc() const noexcept
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
		DsvDesc                     m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle       = {};
		TextureHandle               m_TextureHandle   = {};
	};
}
