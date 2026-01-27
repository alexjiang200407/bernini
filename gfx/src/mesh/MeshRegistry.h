#pragma once
#include "buffer/CPUUploadBuffer.h"
#include "mesh/Mesh.h"
#include "mesh/Vertex.h"

namespace gfx
{
	class MeshRegistry final
	{
	private:
		struct MeshInfoMetadata
		{
			uint32_t vertexOffset = 0u;
			uint32_t vertexCount  = 0u;
			uint32_t refCount     = 0u;
		};

	public:
		MeshRegistry()  = default;
		~MeshRegistry() = default;

	public:
		void
		Init(nvrhi::DeviceHandle device);

		void
		AttachBindingSetItems(nvrhi::BindingSetDesc& bindingSet);

		void
		AttachBindingLayoutItems(nvrhi::BindingLayoutDesc& bindingLayout);

		[[nodiscard]] bool
		Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device);

		uint32_t
		GetInstancesCount() const noexcept
		{
			// First is null instance
			return m_meshInstances.Size() - 1;
		}

		MeshInfo::ID
		GetMeshInfoIDByName(std::string_view nameId) const;

		void
		RemoveMeshInstance(MeshInstance::ID id);

	private:
		void
		RemoveMeshInfo(MeshInfo::ID id);

		MeshInstance::ID
		AddInstance(const MeshInstance& instance);

		MeshInfo::ID
		AddInfo(std::string_view nameId, const MeshInfo& info, uint32_t vOffset, uint32_t vCount);

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
		CPUUploadBuffer<MeshInstance>                 m_meshInstances;
		CPUUploadBuffer<MeshInfo>                     m_meshInfos;
		CPUUploadBuffer<Meshlet>                      m_meshlets;
		CPUUploadBuffer<Vertex>                       m_vertices;
		CPUUploadBuffer<uint32_t>                     m_indices;
		CPUUploadBuffer<uint32_t>                     m_vertexMap;
		std::unordered_map<std::string, MeshInfo::ID> m_infoNameMap;
		std::vector<MeshInfoMetadata>                 m_meshInfoMeta;

		friend class MeshFactory;
	};
}
