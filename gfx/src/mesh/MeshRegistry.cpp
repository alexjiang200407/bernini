#include "mesh/MeshRegistry.h"
#include "BindingSlots.h"

namespace gfx
{
	void
	MeshRegistry::Init(nvrhi::DeviceHandle device)
	{
		m_meshInfos.Init(device, CPUUploadBufferDesc{}.SetName("Mesh Info Structured Buffer"));
		m_vertices.Init(
			device,
			CPUUploadBufferDesc{}
				.SetStartingCapacity(1024)
				.SetName("Vertex Structured Buffer")
				.SetIsVertexBuffer());
		m_indices.Init(
			device,
			CPUUploadBufferDesc{}
				.SetStartingCapacity(2048)
				.SetName("Index Structured Buffer")
				.SetIsIndexBuffer());
		m_meshInstances.Init(
			device,
			CPUUploadBufferDesc{}.SetStartingCapacity(1024).SetName(
				"Mesh Instance Structured Buffer"));
		m_meshlets.Init(
			device,
			CPUUploadBufferDesc{}.SetStartingCapacity(2048).SetName("Meshlet Structured Buffer"));
		m_vertexMap.Init(
			device,
			CPUUploadBufferDesc{}.SetStartingCapacity(1024).SetName(
				"Vertex Map Structured Buffer"));
	}

	void
	MeshRegistry::AttachBindingSetItems(nvrhi::BindingSetDesc& bindingSet)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		bindingSet.addItem(m_meshInstances.GetBindingSetItemSRV(SRV::InstanceBuffer))
			.addItem(m_meshInfos.GetBindingSetItemSRV(SRV::MeshInfo))
			.addItem(m_indices.GetBindingSetItemSRV(SRV::IndexBuffer))
			.addItem(m_vertices.GetBindingSetItemSRV(SRV::VertexBuffer))
			.addItem(m_vertexMap.GetBindingSetItemSRV(SRV::VertexMap))
			.addItem(m_meshlets.GetBindingSetItemSRV(SRV::MeshletBuffer));
	}

	void
	MeshRegistry::AttachBindingLayoutItems(nvrhi::BindingLayoutDesc& bindingLayout)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		bindingLayout.addItem(m_meshInstances.GetBindingLayoutItemSRV(SRV::InstanceBuffer))
			.addItem(m_meshInfos.GetBindingLayoutItemSRV(SRV::MeshInfo))
			.addItem(m_indices.GetBindingLayoutItemSRV(SRV::IndexBuffer))
			.addItem(m_vertices.GetBindingLayoutItemSRV(SRV::VertexBuffer))
			.addItem(m_vertexMap.GetBindingLayoutItemSRV(SRV::VertexMap))
			.addItem(m_meshlets.GetBindingLayoutItemSRV(SRV::MeshletBuffer));
	}

	bool
	MeshRegistry::Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
	{
		auto ret = m_meshInfos.Update(cmdList, device);
		ret |= m_vertices.Update(cmdList, device);
		ret |= m_indices.Update(cmdList, device);
		ret |= m_meshInstances.Update(cmdList, device);
		ret |= m_meshlets.Update(cmdList, device);
		ret |= m_vertexMap.Update(cmdList, device);
		return ret;
	}
}
