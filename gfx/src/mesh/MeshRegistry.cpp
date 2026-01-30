#include "mesh/MeshRegistry.h"
#include "BindingSlots.h"

namespace gfx
{
	void
	MeshRegistry::Init(nvrhi::DeviceHandle device)
	{
		m_meshInfos.Init(device, CPUAppendBufferDesc{}.SetName("Mesh Info Structured Buffer"));
		m_meshInstances.Init(
			device,
			CPUAppendBufferDesc{}
				.SetName("Mesh Instance Structured Buffer")
				.SetUseRedirectTableOnGPU(false));

		m_vertices.Init(
			device,
			CPUUploadBufferDesc{}.SetName("Vertex Structured Buffer").SetIsVertexBuffer());
		m_indices.Init(
			device,
			CPUUploadBufferDesc{}.SetName("Index Structured Buffer").SetIsIndexBuffer());

		m_meshlets.Init(device, CPUUploadBufferDesc{}.SetName("Meshlet Structured Buffer"));
		m_vertexMap.Init(device, CPUUploadBufferDesc{}.SetName("Vertex Map Structured Buffer"));

		// For the null mesh info
		m_meshInfoMeta.push_back({});
	}

	MeshInfo::ID
	MeshRegistry::GetMeshInfoIDByName(std::string_view nameId) const
	{
		if (auto it = m_infoNameMap.find(nameId.data()); it != m_infoNameMap.end())
		{
			return it->second;
		}
		return MeshInfo::ID{ 0u };
	}

	void
	MeshRegistry::RemoveMeshInstance(MeshInstance::ID id)
	{
		const MeshInstance& instance = m_meshInstances.At(id);
		MeshInfo::ID        infoID   = instance.infoID;

		m_meshInstances.Erase(id);

		if (infoID < m_meshInfoMeta.size())
		{
			auto& meta = m_meshInfoMeta[infoID];
			if (meta.refCount > 0)
			{
				meta.refCount--;
				if (meta.refCount == 0)
				{
					RemoveMeshInfo(infoID);
				}
			}
		}
	}

	MeshInfo::ID
	MeshRegistry::AddInfo(
		std::string_view nameId,
		const MeshInfo&  info,
		uint32_t         vOffset,
		uint32_t         vCount)
	{
		if (auto it = m_infoNameMap.find(nameId.data()); it != m_infoNameMap.end())
		{
			return it->second;
		}

		MeshInfo::ID id = m_meshInfos.EmplaceBack(info);

		if (id >= m_meshInfoMeta.size())
		{
			m_meshInfoMeta.resize(id + 1);
		}

		m_meshInfoMeta[id]                 = { vOffset, vCount, 0 };
		m_infoNameMap[std::string(nameId)] = id;

		return id;
	}

	MeshInstance::ID
	MeshRegistry::AddInstance(const MeshInstance& instance)
	{
		assert(instance.infoID < m_meshInfoMeta.size());
		auto id = m_meshInstances.EmplaceBack(instance);

		++m_meshInfoMeta[instance.infoID].refCount;

		return id;
	}

	void
	MeshRegistry::RemoveMeshInfo(MeshInfo::ID id)
	{
		const MeshInfo& info = m_meshInfos.At(id);
		const auto&     meta = m_meshInfoMeta[id];

		for (uint32_t i = 0; i < info.meshletCount; ++i)
		{
			const Meshlet& m = m_meshlets.At(info.meshletSegment, i);

			m_indices.Erase(m.indexSegment);
			m_vertexMap.Erase(m.vertexMapSegment);
		}

		m_meshlets.Erase(info.meshletSegment);
		m_vertices.Erase(meta.vertexSegment);

		m_meshInfos.Erase(id);

		for (auto it = m_infoNameMap.begin(); it != m_infoNameMap.end(); ++it)
		{
			if (it->second == id)
			{
				m_infoNameMap.erase(it);
				break;
			}
		}

		// Clear metadata but keep size (to keep indices stable)
		m_meshInfoMeta[id] = {};
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
			.addItem(m_meshlets.GetBindingSetItemSRV(SRV::MeshletBuffer))
			.addItem(m_meshInfos.GetRedirectTableBindingSetItemSRV(SRV::MeshInfoRedirectBuffer))
			.addItem(m_vertexMap.GetRedirectTableBindingSetItemSRV(SRV::VertexMapRedirectBuffer))
			.addItem(m_indices.GetRedirectTableBindingSetItemSRV(SRV::IndexRedirectBuffer))
			.addItem(m_vertices.GetRedirectTableBindingSetItemSRV(SRV::VertexRedirectBuffer))
			.addItem(m_meshlets.GetRedirectTableBindingSetItemSRV(SRV::MeshletRedirectBuffer));
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
			.addItem(m_meshlets.GetBindingLayoutItemSRV(SRV::MeshletBuffer))
			.addItem(m_meshInfos.GetRedirectTableBindingLayoutItemSRV(SRV::MeshInfoRedirectBuffer))
			.addItem(m_vertexMap.GetRedirectTableBindingLayoutItemSRV(SRV::VertexMapRedirectBuffer))
			.addItem(m_indices.GetRedirectTableBindingLayoutItemSRV(SRV::IndexRedirectBuffer))
			.addItem(m_vertices.GetRedirectTableBindingLayoutItemSRV(SRV::VertexRedirectBuffer))
			.addItem(m_meshlets.GetRedirectTableBindingLayoutItemSRV(SRV::MeshletRedirectBuffer));
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
