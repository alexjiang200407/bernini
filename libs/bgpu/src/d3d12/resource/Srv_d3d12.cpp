#include "resource/Srv_d3d12.h"
#include <core/err/util.h>

namespace bgpu
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
		core::ensure(device != nullptr, "Device cannot be null");
		core::ensure(resource != nullptr, "Resource cannot be null");
		core::ensure(descriptorHeap != nullptr, "Descriptor heap cannot be null");

		const uint32_t descriptorSize =
			device->GetDescriptorHandleIncrementSize(descriptorHeap->GetDesc().Type);

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
			descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		cpuHandle.ptr += static_cast<size_t>(descriptorIndex) * descriptorSize;

		const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = ConvertSrvDesc(desc);
		device->CreateShaderResourceView(resource, &srvDesc, cpuHandle);
	}
}
