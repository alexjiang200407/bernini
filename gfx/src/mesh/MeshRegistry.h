#pragma once
#include "buffer/StructuredBufferSRV.h"
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

		const StructuredBufferSRV<Vertex>&
		GetVertices() const noexcept
		{
			return m_vertices;
		}

		const StructuredBufferSRV<uint32_t>&
		GetIndices() const noexcept
		{
			return m_indices;
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
		StructuredBufferSRV<Mesh::Instance> m_meshInstances;
		StructuredBufferSRV<Mesh::Info>     m_meshInfos;
		StructuredBufferSRV<Vertex>         m_vertices;
		StructuredBufferSRV<uint32_t>       m_indices;

		friend class MeshFactory;
	};
}
