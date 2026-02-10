#include "scene/SceneData.h"
#include "BindingSlots.h"

namespace gfx
{
	void
	SceneData::Init(nvrhi::DeviceHandle device)
	{
		m_drawInstances.Init(
			device,
			AppendBufferDesc{}
				.SetName("Draw Instance Structured Buffer")
				.SetUseRedirectTableOnGPU(false));

		m_staticMeshInstances.Init(
			device,
			AppendBufferDesc{}.SetName("Static Mesh Instance Structured Buffer"));

		m_staticMeshInfos.Init(
			device,
			AppendBufferDesc{}.SetName("Static Mesh Info Structured Buffer"));

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

		m_staticMeshInfoMeta.push_back({});
	}

	DrawInstance::ID
	SceneData::AddStaticMeshInstance(StaticMeshInstance&& instance)
	{
		assert(instance.infoID < m_staticMeshInfoMeta.size());

		++m_staticMeshInfoMeta[instance.infoID].refCount;
		auto instanceId = m_staticMeshInstances.EmplaceBack(std::move(instance));

		DrawInstance drawInstance{};
		drawInstance.specId  = instanceId;
		drawInstance.sortKey = 0;

		return m_drawInstances.EmplaceBack(std::move(drawInstance));
	}

	StaticMeshInfo::ID
	SceneData::AddStaticMeshInfo(StaticMeshInfo&& info)
	{
		StaticMeshInfo::ID id = m_staticMeshInfos.EmplaceBack(std::move(info));

		if (id >= m_staticMeshInfoMeta.size())
		{
			m_staticMeshInfoMeta.resize(id + 1);
		}

		m_staticMeshInfoMeta[id] = { 0 };

		return id;
	}

	void
	SceneData::RemoveDrawInstance(DrawInstance::ID id)
	{
		auto drawInstance = m_drawInstances.At(id);

		switch (drawInstance.sortKey.GetGeomType())
		{
		case GeometryType::kStatic:
		{
			RemoveStaticMeshInstance(drawInstance.specId);
			break;
		}
		default:
			throw GfxException(
				GFX_RESULT_ERROR_UNSUPPORTED_FEATURE,
				"Unsupported Draw Type",
				"Unsupported Draw Type");
		}
	}

	void
	SceneData::RemoveStaticMeshInstance(StaticMeshInstance::ID id)
	{
		const StaticMeshInstance& instance = m_staticMeshInstances.At(id);
		StaticMeshInfo::ID        infoID   = instance.infoID;

		m_staticMeshInstances.Erase(id);

		if (infoID < m_staticMeshInfoMeta.size())
		{
			auto& meta = m_staticMeshInfoMeta[infoID];
			if (meta.refCount > 0)
			{
				meta.refCount--;
				if (meta.refCount == 0)
				{
					RemoveStaticMeshInfo(infoID);
				}
			}
		}
	}

	void
	SceneData::RemoveStaticMeshInfo(StaticMeshInfo::ID id)
	{
		const StaticMeshInfo& info = m_staticMeshInfos.At(id);

		m_indices.Erase(info.indexSegment);
		m_vertexMap.Erase(info.vertexMapSegment);
		m_vertices.Erase(info.vertexSegment);
		m_meshlets.Erase(info.meshletSegment);
		m_staticMeshInfos.Erase(id);

		m_staticMeshInfoMeta[id] = {};
	}

	void
	SceneData::AttachBindingSetItems(nvrhi::BindingSetDesc& bindingSet) const
	{
		namespace SRV = BindingSlots::SRV;

		bindingSet.addItem(m_drawInstances.GetBindingSetItem(SRV::InstanceBuffer))
			.addItem(m_staticMeshInstances.GetBindingSetItem(SRV::StaticMeshInstance))
			.addItem(m_staticMeshInfos.GetBindingSetItem(SRV::StaticMeshInfo))
			.addItem(m_indices.GetBindingSetItem(SRV::IndexBuffer))
			.addItem(m_vertices.GetBindingSetItem(SRV::VertexBuffer))
			.addItem(m_vertexMap.GetBindingSetItem(SRV::VertexMap))
			.addItem(m_meshlets.GetBindingSetItem(SRV::MeshletBuffer))
			.addItem(m_staticMeshInfos.GetRedirectTableBindingSetItem(SRV::MeshInfoRedirectBuffer))
			.addItem(m_vertexMap.GetRedirectTableBindingSetItem(SRV::VertexMapRedirectBuffer))
			.addItem(m_indices.GetRedirectTableBindingSetItem(SRV::IndexRedirectBuffer))
			.addItem(m_vertices.GetRedirectTableBindingSetItem(SRV::VertexRedirectBuffer))
			.addItem(m_meshlets.GetRedirectTableBindingSetItem(SRV::MeshletRedirectBuffer))
			.addItem(m_staticMeshInstances.GetRedirectTableBindingSetItem(
				SRV::StaticMeshInstanceRedirectBuffer));
	}

	void
	SceneData::AttachBindingLayoutItems(nvrhi::BindingLayoutDesc& bindingLayout)
	{
		namespace SRV = BindingSlots::SRV;

		bindingLayout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::InstanceBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::StaticMeshInstance))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::StaticMeshInfo))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::IndexBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::VertexBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::VertexMap))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::MeshletBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::MeshInfoRedirectBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::VertexMapRedirectBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::IndexRedirectBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::VertexRedirectBuffer))
			.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(SRV::MeshletRedirectBuffer))
			.addItem(
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
					SRV::StaticMeshInstanceRedirectBuffer));
	}

	bool
	SceneData::Upload(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
	{
		auto ret = m_drawInstances.Update(cmdList, device);
		ret |= m_staticMeshInstances.Update(cmdList, device);
		ret |= m_staticMeshInfos.Update(cmdList, device);
		ret |= m_vertices.Update(cmdList, device);
		ret |= m_indices.Update(cmdList, device);
		ret |= m_meshlets.Update(cmdList, device);
		ret |= m_vertexMap.Update(cmdList, device);
		return ret;
	}
}
