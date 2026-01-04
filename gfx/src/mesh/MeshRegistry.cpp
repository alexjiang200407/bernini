#include "mesh/MeshRegistry.h"
#include "BindingSlots.h"

namespace gfx
{
	void
	MeshRegistry::Init(nvrhi::DeviceHandle device)
	{
		m_meshInfos.Init(device, StructuredBufferDesc{}.SetName("Mesh Info Structured Buffer"));
		m_vertices.Init(
			device,
			StructuredBufferDesc{}.SetName("Vertex Structured Buffer").SetIsVertexBuffer());
		m_indices.Init(
			device,
			StructuredBufferDesc{}.SetName("Index Structured Buffer").SetIsIndexBuffer());
		m_meshInstances.Init(
			device,
			StructuredBufferDesc{}.SetName("Mesh Instance Structured Buffer"));
	}

	void
	MeshRegistry::AttachBindingSetItems(nvrhi::BindingSetDesc& bindingSet)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		bindingSet.addItem(m_meshInstances.GetBindingSetItem(SRV::MeshInstance))
			.addItem(m_meshInfos.GetBindingSetItem(SRV::MeshInfo))
			.addItem(m_indices.GetBindingSetItem(SRV::IndexBuffer))
			.addItem(m_vertices.GetBindingSetItem(SRV::VertexBuffer));
	}

	void
	MeshRegistry::AttachBindingLayoutItems(nvrhi::BindingLayoutDesc& bindingLayout)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		bindingLayout.addItem(m_meshInstances.GetBindingLayoutItem(SRV::MeshInstance))
			.addItem(m_meshInfos.GetBindingLayoutItem(SRV::MeshInfo))
			.addItem(m_indices.GetBindingLayoutItem(SRV::IndexBuffer))
			.addItem(m_vertices.GetBindingLayoutItem(SRV::VertexBuffer));
	}

	bool
	MeshRegistry::Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
	{
		auto ret = m_meshInfos.Update(cmdList, device);
		ret |= m_vertices.Update(cmdList, device);
		ret |= m_indices.Update(cmdList, device);
		ret |= m_meshInstances.Update(cmdList, device);
		return ret;
	}
}
