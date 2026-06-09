#include "resource/Texture_d3d12.h"

namespace bgl
{
	Texture::Texture(
		ID3D12Device*         device,
		ID3D12DescriptorHeap* descriptorHeap,
		uint32_t              descriptorIndex,
		const TextureDesc&    desc) : m_Desc(desc), m_DescriptorIndex(descriptorIndex)
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(descriptorHeap != nullptr, "Descriptor heap cannot be null");
		gassert(desc.width > 0, "Texture width must be greater than zero");
		gassert(desc.height > 0, "Texture height must be greater than zero");
		gassert(desc.format != Format::UNKNOWN, "Texture format cannot be UNKNOWN");

		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += descriptorIndex * descriptorSize;
	}

	Texture::Texture(
		ID3D12Device*               device,
		ID3D12DescriptorHeap*       descriptorHeap,
		uint32_t                    descriptorIndex,
		wrl::ComPtr<ID3D12Resource> texture,
		const TextureDesc&          desc) :
		m_Desc(desc), m_DescriptorIndex(descriptorIndex), m_Texture(std::move(texture))
	{
		auto           d3d12Desc = m_Texture->GetDesc();
		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += descriptorIndex * descriptorSize;
	}
}
