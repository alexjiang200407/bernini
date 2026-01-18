#pragma once
#include "buffer/StructuredUploadBuffer.h"
#include "mesh/Mesh.h"
#include "mesh/Vertex.h"

namespace gfx
{
	class MeshRegistry final
	{
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
			return m_meshInstances.Size();
		}

		const StructuredUploadBuffer<Vertex>&
		GetVertices() const noexcept
		{
			return m_vertices;
		}

		const StructuredUploadBuffer<uint32_t>&
		GetIndices() const noexcept
		{
			return m_indices;
		}

		const StructuredUploadBuffer<Mesh::Instance>&
		GetInstances() const noexcept
		{
			return m_meshInstances;
		}

		const size_t
		GetMeshInfosCount() const noexcept
		{
			return m_meshInfos.Size();
		}

	private:
		Mesh::InstanceID
		AddInstance(Mesh::InfoID id, glm::mat4 modelTransform = {})
		{
			return m_meshInstances.Emplace(id, modelTransform);
		}

		Mesh::InfoID
		AddInfo(uint32_t indexStart, uint32_t indexCount, uint32_t vertexStart)
		{
			return m_meshInfos.Emplace(indexStart, indexCount, vertexStart, 0 /* materialID */);
		}

		void
		AddVertex(glm::vec3 position, glm::vec3 normal, glm::vec2 uv)
		{
			m_vertices.Emplace(position, normal, uv);
		}

		void
		AddIndex(uint32_t idx)
		{
			m_indices.Emplace(idx);
		}

	private:
		StructuredUploadBuffer<Mesh::Instance> m_meshInstances;
		StructuredUploadBuffer<Mesh::Info>     m_meshInfos;
		StructuredUploadBuffer<Vertex>         m_vertices;
		StructuredUploadBuffer<uint32_t>       m_indices;

		friend class MeshFactory;
	};
}
