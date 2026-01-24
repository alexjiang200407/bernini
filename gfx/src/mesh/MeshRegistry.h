#pragma once
#include "buffer/CPUUploadBuffer.h"
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

		const CPUUploadBuffer<Vertex>&
		GetVertices() const noexcept
		{
			return m_vertices;
		}

		const CPUUploadBuffer<uint32_t>&
		GetIndices() const noexcept
		{
			return m_indices;
		}

		const CPUUploadBuffer<MeshInstance>&
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
		MeshInstance::ID
		AddInstance(const MeshInstance& instance)
		{
			return m_meshInstances.Emplace(instance);
		}

		MeshInstance::ID
		AddInstance(MeshInstance&& instance)
		{
			return m_meshInstances.Emplace(std::move(instance));
		}

		MeshInfo::ID
		AddInfo(const MeshInfo& info)
		{
			return m_meshInfos.Emplace(info);
		}

		MeshInfo::ID
		AddInfo(MeshInfo&& info)
		{
			return m_meshInfos.Emplace(std::move(info));
		}

		Meshlet::ID
		AddMeshlet(const Meshlet& meshlet)
		{
			return m_meshlets.Emplace(meshlet);
		}

		Meshlet::ID
		AddMeshlet(Meshlet&& meshlet)
		{
			return m_meshlets.Emplace(std::move(meshlet));
		}

		void
		AddVertex(const Vertex& vertex)
		{
			m_vertices.Emplace(vertex);
		}

		void
		AddVertex(Vertex&& vertex)
		{
			m_vertices.Emplace(std::move(vertex));
		}

		void
		AddIndex(uint32_t idx)
		{
			m_indices.Emplace(idx);
		}

		void
		AddVertexMapIdx(uint32_t idx)
		{
			m_vertexMap.Emplace(idx);
		}

	private:
		CPUUploadBuffer<MeshInstance> m_meshInstances;
		CPUUploadBuffer<MeshInfo>     m_meshInfos;
		CPUUploadBuffer<Meshlet>      m_meshlets;
		CPUUploadBuffer<Vertex>       m_vertices;
		CPUUploadBuffer<uint32_t>     m_indices;
		CPUUploadBuffer<uint32_t>     m_vertexMap;

		friend class MeshFactory;
	};
}
