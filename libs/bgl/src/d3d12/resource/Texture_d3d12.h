#pragma once
#include "resource/Texture.h"
#include "util_d3d12.h"

namespace bgl
{
	class Texture final
	{
	public:
		Texture() = default;

		Texture(
			ID3D12Device*         device,
			ID3D12DescriptorHeap* descriptorHeap,
			uint32_t              descriptorIndex,
			const TextureDesc&    desc);

		/**
		 * Assumes that desc is correct.
		 */
		Texture(
			ID3D12Device*               device,
			ID3D12DescriptorHeap*       descriptorHeap,
			uint32_t                    descriptorIndex,
			wrl::ComPtr<ID3D12Resource> texture,
			const TextureDesc&          desc);

		Texture(const Texture&) = delete;

		Texture(Texture&&) noexcept = default;

		Texture&
		operator=(const Texture&) = delete;

		Texture&
		operator=(Texture&&) noexcept = default;

		[[nodiscard]]
		const TextureDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]]
		ID3D12Resource*
		GetD3D12Resource() const noexcept
		{
			return m_Texture.Get();
		}

		[[nodiscard]]
		wrl::ComPtr<ID3D12Resource>
		GetD3D12ResourceCopy() const noexcept
		{
			return m_Texture;
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
			return m_Texture == nullptr;
		}

	private:
		TextureDesc                 m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle       = {};
		wrl::ComPtr<ID3D12Resource> m_Texture;
	};
}
