#include "mesh/MeshFactory.h"

namespace gfx
{
	MeshFactory::MeshFactory(nvrhi::DeviceHandle device) : m_device{ device }
	{
		m_cubeSharedData   = CreateCubeSharedData();
		m_sphereSharedData = CreateSphereSharedData();
	}

	std::shared_ptr<Mesh>
	MeshFactory::CreateCube(std::string_view vertexShaderPath) const
	{
		return std::shared_ptr<Mesh>{ new Mesh{ m_cubeSharedData, m_device, vertexShaderPath } };
	}

	std::shared_ptr<Mesh>
	MeshFactory::CreateSphere(std::string_view vertexShaderPath) const
	{
		return std::shared_ptr<Mesh>{ new Mesh{ m_sphereSharedData, m_device, vertexShaderPath } };
	}
}
