#include "resource/Rtv_d3d12.h"

namespace bgl
{
	Rtv::Rtv(
		ID3D12Device*         device,
		TextureHandle         textureHandle,
		ID3D12DescriptorHeap* descriptorHeap,
		uint32_t              descriptorIndex,
		const RtvDesc&        desc) :
		m_Desc(desc), m_TextureHandle(textureHandle), m_DescriptorIndex(descriptorIndex)
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(descriptorHeap != nullptr, "Descriptor heap cannot be null");

		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += descriptorIndex * descriptorSize;
	}
}
