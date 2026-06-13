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

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.MipLevels           = static_cast<uint16_t>(desc.mipLevels);
		textureDesc.Format              = ConvertFormat(desc.format);
		textureDesc.Width               = desc.width;
		textureDesc.Height              = desc.height;
		textureDesc.Flags               = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		textureDesc.SampleDesc.Count   = desc.sampleCount;
		textureDesc.SampleDesc.Quality = desc.sampleQuality;
		textureDesc.Dimension          = ConvertResourceDimension(desc.dimension);

		if (desc.dimension == TextureDimension::kTexture3D)
		{
			gassert(desc.arraySize == 1, "3D Textures cannot have an array size!");
			textureDesc.DepthOrArraySize = static_cast<UINT16>(desc.depth);
		}
		else
		{
			gassert(desc.depth == 1, "2D/1D Textures cannot have a depth greater than 1!");
			textureDesc.DepthOrArraySize = static_cast<UINT16>(desc.arraySize);
		}

		D3D12_CLEAR_VALUE  clearValue;
		D3D12_CLEAR_VALUE* pClearValue = nullptr;

		if (desc.clearValue.format != Format::UNKNOWN)
		{
			clearValue  = ConvertClearValue(desc.clearValue);
			pClearValue = &clearValue;
		}

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
		device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			pClearValue,
			IID_PPV_ARGS(&m_Texture));

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += static_cast<uint64_t>(descriptorIndex) * descriptorSize;
	}

	Texture::Texture(
		ID3D12Device*               device,
		ID3D12DescriptorHeap*       descriptorHeap,
		uint32_t                    descriptorIndex,
		wrl::ComPtr<ID3D12Resource> texture,
		const TextureDesc&          desc) :
		m_Desc(desc), m_DescriptorIndex(descriptorIndex), m_Texture(std::move(texture))
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(descriptorHeap != nullptr, "Descriptor heap cannot be null");
		gassert(desc.width > 0, "Texture width must be greater than zero");
		gassert(desc.height > 0, "Texture height must be greater than zero");
		gassert(desc.format != Format::UNKNOWN, "Texture format cannot be UNKNOWN");

		auto           d3d12Desc = m_Texture->GetDesc();
		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += static_cast<uint64_t>(descriptorIndex) * descriptorSize;
	}
}
