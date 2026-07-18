#include "resource/Buffer_d3d12.h"
#include "ResourceManager_d3d12.h"

namespace bgl
{
	Buffer::Buffer(
		ID3D12Device*         device,
		ID3D12DescriptorHeap* descriptorHeap,
		uint32_t              descriptorIndex,
		const BufferDesc&     desc) : m_Desc(desc)
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(descriptorHeap != nullptr, "Descriptor heap cannot be null");

		wrl::ComPtr<ID3D12Device10> device10;
		device->QueryInterface(IID_PPV_ARGS(&device10)) >> d3d12ErrChecker;

		gassert(desc.byteSize > 0, "Buffer byte size must be greater than zero");

		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += static_cast<uint64_t>(descriptorIndex) * descriptorSize;

		D3D12_HEAP_PROPERTIES heapProps = {};
		D3D12_RESOURCE_DESC1  resDesc   = {};
		D3D12_HEAP_FLAGS      heapFlags = D3D12_HEAP_FLAG_NONE;

		if (desc.isUav)
			resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		// Setup raw buffer resource description
		resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width            = desc.byteSize;
		resDesc.Height           = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels        = 1;
		resDesc.Format           = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		// Create the resource via CreateCommittedResource3
		device10->CreateCommittedResource3(
			&heapProps,
			heapFlags,
			&resDesc,
			D3D12_BARRIER_LAYOUT_UNDEFINED,
			nullptr,
			nullptr,
			0,
			nullptr,
			IID_PPV_ARGS(&m_Buffer)) >>
			d3d12ErrChecker;

		std::wstring wName(desc.debugName.begin(), desc.debugName.end());
		m_Buffer->SetName(wName.c_str());
	}

}
