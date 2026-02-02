#include "mesh/MeshFactory.h"
#include "mesh/MeshRegistry.h"

namespace gfx
{
	MeshFactory::MeshFactory(nvrhi::DeviceHandle device, MeshRegistry& registry) :
		m_device{ device }, m_registry{ registry }
	{}

	MeshInstance::ID
	MeshFactory::CreateCubeInstance(glm::mat4 modelTransform) const
	{
		auto m_cubeInfoID = CreateCubeInfo(m_registry);
		return m_registry.AddInstance(
			MeshInstance{ .infoID = m_cubeInfoID, .modelTransform = modelTransform });
	}

	MeshInstance::ID
	MeshFactory::CreateSphereInstance(glm::mat4 modelTransform) const
	{
		auto m_sphereInfoID = CreateSphereInfo(m_registry);
		return m_registry.AddInstance(
			MeshInstance{ .infoID = m_sphereInfoID, .modelTransform = modelTransform });
	}
}
