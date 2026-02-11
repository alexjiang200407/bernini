#pragma once
#include "buffer/AppendBuffer.h"
#include "buffer/SegmentBuffer.h"
#include "types/DrawInstance.h"
#include "types/Mesh.h"
#include "types/PBRMaterial.h"
#include "types/Vertex.h"

namespace gfx
{
	class SceneData final
	{
	private:
		struct MaterialExtraData
		{
			LayerType layerType = LayerType::kInvalid;
		};

	public:
		SceneData()  = default;
		~SceneData() = default;

	public:
		void
		Init(nvrhi::DeviceHandle device);

		void
		AttachBindingSetItems(nvrhi::BindingSetDesc& bindingSet) const;

		static void
		AttachBindingLayoutItems(nvrhi::BindingLayoutDesc& bindingLayout);

		[[nodiscard]] bool
		Upload(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device);

		uint32_t
		GetInstancesCount() const noexcept
		{
			// First is null instance
			return m_drawInstances.Size() - 1;
		}

	private:
		void
		RemoveStaticMeshInstance(StaticMeshInstance::ID id) noexcept;

		void
		RemoveStaticMeshInfo(StaticMeshInfo::ID id);

		StaticMeshInstance::ID
		AddStaticMeshInstance(StaticMeshInstance&& instance);

		StaticMeshInfo::ID
		AddStaticMeshInfo(StaticMeshInfo&& info);

		PBRMaterial::ID
		AddPBRMaterial(PBRMaterial&& material, LayerType layerType);

		void
		AttachPBRMaterial(DrawInstance::ID instanceId, PBRMaterial::ID matId);

		void
		RemoveDrawInstance(DrawInstance::ID id);

		Meshlet::ID
		AddMeshlets(std::span<const Meshlet> meshlets)
		{
			return m_meshlets.EmplaceRange(meshlets);
		}

		uint32_t
		AddVertices(std::span<const Vertex> vertices)
		{
			return m_vertices.EmplaceRange(vertices);
		}

		uint32_t
		AddIndices(std::span<const uint32_t> indices)
		{
			return m_indices.EmplaceRange(indices);
		}

		uint32_t
		AddVertexMapIdx(std::span<const uint32_t> mapIndices)
		{
			return m_vertexMap.EmplaceRange(mapIndices);
		}

	private:
		using PBRMaterialBuffer = AppendBuffer<PBRMaterial, MaterialExtraData>;

		AppendBuffer<DrawInstance>       m_drawInstances;
		AppendBuffer<StaticMeshInstance> m_staticMeshInstances;
		AppendBuffer<StaticMeshInfo>     m_staticMeshInfos;
		PBRMaterialBuffer                m_pbrMaterials;
		SegmentBuffer<Meshlet>           m_meshlets;
		SegmentBuffer<Vertex>            m_vertices;
		SegmentBuffer<uint32_t>          m_indices;
		SegmentBuffer<uint32_t>          m_vertexMap;

		friend class Scene;
	};
}
