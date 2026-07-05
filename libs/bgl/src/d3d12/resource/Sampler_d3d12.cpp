#include "resource/Sampler_d3d12.h"

namespace bgl
{
	Sampler::Sampler(
		ID3D12Device*         device,
		ID3D12DescriptorHeap* samplerHeap,
		uint32_t              descriptorIndex,
		const SamplerDesc&    desc) : m_Desc(desc), m_DescriptorIndex(descriptorIndex)
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(samplerHeap != nullptr, "Sampler heap cannot be null");

		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(samplerHeap->GetDesc().Type);

		m_CpuHandle = samplerHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += static_cast<uint64_t>(descriptorIndex) * descriptorSize;
	}
}
