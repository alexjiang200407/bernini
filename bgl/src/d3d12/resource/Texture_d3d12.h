#pragma once
#include "resource/Texture.h"
#include "util_d3d12.h"

namespace bgl
{
	class Texture final
	{
	public:
		Texture() = default;

		Texture(ID3D12Device* device, uint32_t descriptorIndex, const TextureDesc& desc);

		/**
		 * Assumes that desc is correct.
		 */
		Texture(
			ID3D12Device*               device,
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
		GetDesc() const
		{
			return m_Desc;
		}

		[[nodiscard]]
		ID3D12Resource*
		GetD3D12Resource() const
		{
			return m_Texture.Get();
		}

		[[nodiscard]]
		wrl::ComPtr<ID3D12Resource>
		GetD3D12ResourceCopy() const
		{
			return m_Texture;
		}

		[[nodiscard]]
		bool
		IsNull() const
		{
			return m_Texture == nullptr;
		}

	private:
		TextureDesc                 m_Desc;
		uint32_t                    m_DescriptorIndex = 0xFFFFFFFF;
		wrl::ComPtr<ID3D12Resource> m_Texture;
	};
}
