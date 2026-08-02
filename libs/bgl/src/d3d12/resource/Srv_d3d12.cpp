#include "resource/Srv_d3d12.h"

namespace bgl
{
	Srv::Srv(
		ID3D12Device*         device,
		TextureHandle         textureHandle,
		ID3D12Resource*       resource,
		ID3D12DescriptorHeap* descriptorHeap,
		uint32_t              descriptorIndex,
		const SrvDesc&        desc) :
		m_Desc(desc), m_DescriptorIndex(descriptorIndex), m_TextureHandle(textureHandle)
	{
		gassert(device != nullptr, "Device cannot be null");
		gassert(resource != nullptr, "Resource cannot be null");
		gassert(descriptorHeap != nullptr, "Descriptor heap cannot be null");

		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		m_CpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_CpuHandle.ptr += static_cast<size_t>(descriptorIndex) * descriptorSize;

		const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = ConvertSrvDesc(desc);
		device->CreateShaderResourceView(resource, &srvDesc, m_CpuHandle);
	}
}
