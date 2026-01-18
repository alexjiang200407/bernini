#include "mesh/Mesh.h"

namespace gfx
{
	class MeshRegistry;

	class MeshFactory
	{
	public:
		MeshFactory(nvrhi::DeviceHandle device, MeshRegistry& registry);

		[[nodiscard]]
		Mesh::InstanceID
		CreateCubeInstance(MeshRegistry& registry, glm::mat4 modelTransform = {}) const;

		[[nodiscard]]
		Mesh::InstanceID
		CreateSphereInstance(MeshRegistry& registry, glm::mat4 modelTransform = {}) const;

	private:
		static Mesh::InfoID
		CreateCubeInfo(MeshRegistry& registry);

		static Mesh::InfoID
		CreateSphereInfo(MeshRegistry& registry);

	private:
		nvrhi::DeviceHandle m_device;
		Mesh::InfoID        m_cubeInfoID;
		Mesh::InfoID        m_sphereInfoID;
	};

}
