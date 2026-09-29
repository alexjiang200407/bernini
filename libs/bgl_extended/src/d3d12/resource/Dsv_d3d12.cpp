#include "Dsv_d3d12.h"
#include <core/err/util.h>

namespace bgl
{
	Dsv::Dsv(
		ID3D12Device*         device,
		TextureHandle         textureHandle,
		ID3D12DescriptorHeap* descriptorHeap,
		uint32_t              descriptorIndex,
		const DsvDesc&        desc) :
		m_Desc(desc), m_DescriptorIndex(descriptorIndex), m_TextureHandle(textureHandle)
	{
		core::ensure(device != nullptr, "Device cannot be null");
		core::ensure(descriptorHeap != nullptr, "Descriptor heap cannot be null");

		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += static_cast<size_t>(descriptorIndex) * descriptorSize;
	}
}
