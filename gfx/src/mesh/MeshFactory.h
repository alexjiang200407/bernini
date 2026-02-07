#include "mesh/Mesh.h"

namespace gfx
{
	class MeshRegistry;

	class MeshFactory
	{
	public:
		MeshFactory(nvrhi::DeviceHandle device, MeshRegistry& registry);

		[[nodiscard]]
		StaticMeshInstance::ID
		CreateCubeInstance(glm::mat4 modelTransform = {}) const;

		[[nodiscard]]
		StaticMeshInstance::ID
		CreateSphereInstance(glm::mat4 modelTransform = {}) const;

	private:
		static StaticMeshInfo::ID
		CreateCubeInfo(MeshRegistry& registry);

		static StaticMeshInfo::ID
		CreateSphereInfo(MeshRegistry& registry);

	private:
		StaticMeshInfo::ID  m_cubeInfoID   = 0u;
		StaticMeshInfo::ID  m_sphereInfoID = 0u;
		nvrhi::DeviceHandle m_device;
		MeshRegistry&       m_registry;
	};

}
