#include "mesh/MeshFactory.h"
#include "mesh/MeshRegistry.h"

namespace gfx
{
	MeshFactory::MeshFactory(nvrhi::DeviceHandle device, MeshRegistry& registry) :
		m_device{ device }, m_registry{ registry }
	{
		m_cubeInfoID   = CreateCubeInfo(m_registry);
		m_sphereInfoID = CreateSphereInfo(m_registry);
	}

	DrawInstance::ID
	MeshFactory::CreateCubeInstance(glm::mat4 modelTransform) const
	{
		return m_registry.AddStaticMeshInstance(
			StaticMeshInstance{ .infoID = m_cubeInfoID, .modelTransform = modelTransform });
	}

	DrawInstance::ID
	MeshFactory::CreateSphereInstance(glm::mat4 modelTransform) const
	{
		return m_registry.AddStaticMeshInstance(
			StaticMeshInstance{ .infoID = m_sphereInfoID, .modelTransform = modelTransform });
	}
}
