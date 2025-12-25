#include "mesh/Mesh.h"

namespace gfx
{
	class MeshFactory
	{
	public:
		MeshFactory(nvrhi::DeviceHandle device);

		std::shared_ptr<Mesh>
		CreateCube(std::string_view vertexShaderPath) const;

		std::shared_ptr<Mesh>
		CreateSphere(std::string_view vertexShaderPath) const;

	private:
		std::shared_ptr<Mesh::SharedData>
		CreateCubeSharedData() const;

		std::shared_ptr<Mesh::SharedData>
		CreateSphereSharedData() const;

	private:
		nvrhi::DeviceHandle               m_device;
		std::shared_ptr<Mesh::SharedData> m_cubeSharedData;
		std::shared_ptr<Mesh::SharedData> m_sphereSharedData;
	};

}
