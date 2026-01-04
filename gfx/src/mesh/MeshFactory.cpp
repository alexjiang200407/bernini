#include "mesh/MeshFactory.h"
#include "mesh/MeshRegistry.h"

namespace gfx
{
	MeshFactory::MeshFactory(nvrhi::DeviceHandle device, MeshRegistry& registry) :
		m_device{ device }
	{
		m_cubeInfoID   = CreateCubeInfo(registry);
		m_sphereInfoID = CreateSphereInfo(registry);
	}

	Mesh::InstanceID
	MeshFactory::CreateCubeInstance(MeshRegistry& registry, glm::mat4 modelTransform) const
	{
		return registry.AddInstance(m_cubeInfoID, modelTransform);
	}
	Mesh::InstanceID
	MeshFactory::CreateSphereInstance(MeshRegistry& registry, glm::mat4 modelTransform) const
	{
		return registry.AddInstance(m_sphereInfoID, modelTransform);
	}
}
