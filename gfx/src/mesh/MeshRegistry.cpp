#include "mesh/MeshRegistry.h"
#include "BindingSlots.h"

namespace gfx
{
	void
	MeshRegistry::Init(nvrhi::DeviceHandle device)
	{
		m_meshInfos.Init(device, AppendBufferDesc{}.SetName("Mesh Info Structured Buffer"));
		m_meshInstances.Init(
			device,
			AppendBufferDesc{}
				.SetName("Mesh Instance Structured Buffer")
				.SetUseRedirectTableOnGPU(false));

		m_vertices.Init(
			device,
			SegmentBufferDesc{}.SetName("Vertex Structured Buffer").SetIsVertexBuffer());
		m_indices.Init(
			device,
			SegmentBufferDesc{}.SetName("Index Structured Buffer").SetIsIndexBuffer());

		m_meshlets.Init(
			device,
			SegmentBufferDesc{}.SetStartingCapacity(4096).SetName("Meshlet Structured Buffer"));
		m_vertexMap.Init(
			device,
			SegmentBufferDesc{}.SetStartingCapacity(10'000).SetName(
				"Vertex Map Structured Buffer"));

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
	MeshRegistry::AddInfo(std::string_view nameId, MeshInfo&& info)
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

		m_meshInfoMeta[id]                 = { 0 };
		m_infoNameMap[std::string(nameId)] = id;

		return id;
	}

	MeshInstance::ID
	MeshRegistry::AddInstance(MeshInstance&& instance)
	{
		assert(instance.infoID < m_meshInfoMeta.size());

		++m_meshInfoMeta[instance.infoID].refCount;
		auto id = m_meshInstances.EmplaceBack(std::move(instance));

		return id;
	}

	void
	MeshRegistry::RemoveMeshInfo(MeshInfo::ID id)
	{
		const MeshInfo& info = m_meshInfos.At(id);
		const auto&     meta = m_meshInfoMeta[id];

		m_indices.Erase(info.indexSegment);
		m_vertexMap.Erase(info.vertexSegment);
		m_meshlets.Erase(info.meshletSegment);
		m_meshInfos.Erase(id);

		// TODO: optimize this
		for (auto it = m_infoNameMap.begin(); it != m_infoNameMap.end(); ++it)
		{
			if (it->second == id)
			{
				m_infoNameMap.erase(it);
				break;
			}
		}

		m_meshInfoMeta[id] = {};
	}

	void
	MeshRegistry::AttachBindingSetItems(nvrhi::BindingSetDesc& bindingSet)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		bindingSet.addItem(m_meshInstances.GetBindingSetItem(SRV::InstanceBuffer))
			.addItem(m_meshInfos.GetBindingSetItem(SRV::MeshInfo))
			.addItem(m_indices.GetBindingSetItem(SRV::IndexBuffer))
			.addItem(m_vertices.GetBindingSetItem(SRV::VertexBuffer))
			.addItem(m_vertexMap.GetBindingSetItem(SRV::VertexMap))
			.addItem(m_meshlets.GetBindingSetItem(SRV::MeshletBuffer))
			.addItem(m_meshInfos.GetRedirectTableBindingSetItem(SRV::MeshInfoRedirectBuffer))
			.addItem(m_vertexMap.GetRedirectTableBindingSetItem(SRV::VertexMapRedirectBuffer))
			.addItem(m_indices.GetRedirectTableBindingSetItem(SRV::IndexRedirectBuffer))
			.addItem(m_vertices.GetRedirectTableBindingSetItem(SRV::VertexRedirectBuffer))
			.addItem(m_meshlets.GetRedirectTableBindingSetItem(SRV::MeshletRedirectBuffer));
	}

	void
	MeshRegistry::AttachBindingLayoutItems(nvrhi::BindingLayoutDesc& bindingLayout)
	{
		namespace SRV = BindingSlots::SRV;
		namespace UAV = BindingSlots::UAV;

		bindingLayout.addItem(m_meshInstances.GetBindingLayoutItem(SRV::InstanceBuffer))
			.addItem(m_meshInfos.GetBindingLayoutItem(SRV::MeshInfo))
			.addItem(m_indices.GetBindingLayoutItem(SRV::IndexBuffer))
			.addItem(m_vertices.GetBindingLayoutItem(SRV::VertexBuffer))
			.addItem(m_vertexMap.GetBindingLayoutItem(SRV::VertexMap))
			.addItem(m_meshlets.GetBindingLayoutItem(SRV::MeshletBuffer))
			.addItem(m_meshInfos.GetRedirectTableBindingLayoutItem(SRV::MeshInfoRedirectBuffer))
			.addItem(m_vertexMap.GetRedirectTableBindingLayoutItem(SRV::VertexMapRedirectBuffer))
			.addItem(m_indices.GetRedirectTableBindingLayoutItem(SRV::IndexRedirectBuffer))
			.addItem(m_vertices.GetRedirectTableBindingLayoutItem(SRV::VertexRedirectBuffer))
			.addItem(m_meshlets.GetRedirectTableBindingLayoutItem(SRV::MeshletRedirectBuffer));
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
