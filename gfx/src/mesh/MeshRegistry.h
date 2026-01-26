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
			// First is null instance
			return m_meshInstances.Size() - 1;
		}

		void
		RemoveMeshInstance()
		{}

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
		CPUUploadBuffer<MeshInstance> m_meshInstances;
		CPUUploadBuffer<MeshInfo>     m_meshInfos;
		CPUUploadBuffer<Meshlet>      m_meshlets;
		CPUUploadBuffer<Vertex>       m_vertices;
		CPUUploadBuffer<uint32_t>     m_indices;
		CPUUploadBuffer<uint32_t>     m_vertexMap;

		friend class MeshFactory;
	};
}
