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
		gassert(desc.width > 0, "Texture width must be greater than zero");
		gassert(desc.height > 0, "Texture height must be greater than zero");
		gassert(desc.format != Format::UNKNOWN, "Texture format cannot be UNKNOWN");

		// RTV/DSV-only textures pass a null heap: they need no shader-visible
		// descriptor, so no CPU handle is computed.
		if (descriptorHeap != nullptr)
		{
			const uint32_t descriptorSize =
				device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);
			m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
			m_CpuHandle.ptr += static_cast<uint64_t>(descriptorIndex) * descriptorSize;
		}

		D3D12_RESOURCE_DESC1 textureDesc = {};
		textureDesc.MipLevels            = static_cast<uint16_t>(desc.mipLevels);
		textureDesc.Format               = ConvertFormat(desc.format);
		textureDesc.Width                = desc.width;
		textureDesc.Height               = desc.height;
		textureDesc.Flags                = D3D12_RESOURCE_FLAG_NONE;

		if (desc.usage.any(TextureUsageFlag::kDepthStencil))
		{
			textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}

		if (desc.usage.any(TextureUsageFlag::kRenderTarget))
		{
			textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}

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

		// D3D12 only permits an optimized clear value on render-target / depth-stencil
		// resources; passing one for a sample-only (SRV) texture is invalid.
		D3D12_CLEAR_VALUE  clearValue;
		D3D12_CLEAR_VALUE* pClearValue = nullptr;

		const auto isRenderable = desc.usage.any(TextureUsageFlag::kRenderTarget) ||
		                          desc.usage.any(TextureUsageFlag::kDepthStencil);

		if (isRenderable && desc.format != Format::UNKNOWN)
		{
			clearValue  = ConvertClearValue(desc.format, desc.clearValue);
			pClearValue = &clearValue;
		}

		wrl::ComPtr<ID3D12Device10> device10;
		device->QueryInterface(IID_PPV_ARGS(&device10)) >> d3d12ErrChecker;
		auto initialLayout = ConvertBarrierLayout(desc.initalLayout);

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		device10->CreateCommittedResource3(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			initialLayout,
			pClearValue,
			nullptr,
			0,
			nullptr,
			IID_PPV_ARGS(&m_Texture)) >>
			d3d12ErrChecker;
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
		gassert(desc.width > 0, "Texture width must be greater than zero");
		gassert(desc.height > 0, "Texture height must be greater than zero");
		gassert(desc.format != Format::UNKNOWN, "Texture format cannot be UNKNOWN");

		// RTV/DSV-only textures pass a null heap: they need no shader-visible
		// descriptor, so no CPU handle is computed.
		if (descriptorHeap != nullptr)
		{
			const uint32_t descriptorSize =
				device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);
			m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
			m_CpuHandle.ptr += static_cast<uint64_t>(descriptorIndex) * descriptorSize;
		}
	}
}
