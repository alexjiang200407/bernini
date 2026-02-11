#include "scene/SceneData.h"
#include "BindingSlots.h"
#include <core/ScopeGuard.h>

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

		m_pbrMaterials.Init(device, AppendBufferDesc{}.SetName("PBR Material Structured Buffer"));
	}

	DrawInstance::ID
	SceneData::AddStaticMeshInstance(StaticMeshInstance&& instance)
	{
		assert(m_staticMeshInfos.IsValid(instance.infoID));

		auto instanceId = m_staticMeshInstances.EmplaceBack(std::move(instance));

		DrawInstance drawInstance{};
		drawInstance.geomSpecId = instanceId;
		drawInstance.sortKey    = 0;

		return m_drawInstances.EmplaceBack(std::move(drawInstance));
	}

	StaticMeshInfo::ID
	SceneData::AddStaticMeshInfo(StaticMeshInfo&& info)
	{
		auto id = m_staticMeshInfos.EmplaceBack(std::move(info));
		return id;
	}

	void
	SceneData::AttachPBRMaterial(DrawInstance::ID instanceId, PBRMaterial::ID matId)
	{
		auto& instance     = m_drawInstances.At(instanceId);
		auto& material     = m_pbrMaterials.At(matId);
		auto& matExtraData = m_pbrMaterials.GetExtraData(matId);

		instance.sortKey.SetMaterialType(MaterialType::kPBR);
		instance.sortKey.SetLayerType(matExtraData.layerType);
		if (instance.sortKey.UpdatePSO(MaterialType::kPBR) == PSO::kInvalid)
		{
			logger::error(
				"Failed to update PSO for DrawInstance {} with MaterialType::kPBR",
				instanceId);

			THROW_GFX_ERROR("Invalid PSO", "User created a material that has no registered PSO");
		}
		instance.materialSpecId = matId;
	}

	PBRMaterial::ID
	SceneData::AddPBRMaterial(PBRMaterial&& material, LayerType layerType)
	{
		auto  id            = m_pbrMaterials.EmplaceBack(std::move(material));
		auto& extraData     = m_pbrMaterials.GetExtraData(id);
		extraData.layerType = layerType;
		return id;
	}

	void
	SceneData::RemoveDrawInstance(DrawInstance::ID id)
	{
		auto drawInstance = m_drawInstances.At(id);
		gassert(m_drawInstances.IsValid(id), "Invalid Draw Instance");

		switch (drawInstance.sortKey.GetGeomType())
		{
		case GeometryType::kStatic:
		{
			RemoveStaticMeshInstance(drawInstance.geomSpecId);
			break;
		}
		default:
			gfatal("Unsupported Draw Type", "Unsupported Draw Type");
			break;
		}

		m_drawInstances.Erase(id);
	}

	void
	SceneData::RemoveStaticMeshInstance(StaticMeshInstance::ID id) noexcept
	{
		m_staticMeshInstances.Erase(id);
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
