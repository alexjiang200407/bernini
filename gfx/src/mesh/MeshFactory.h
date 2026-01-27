#include "mesh/Mesh.h"

namespace gfx
{
	class MeshRegistry;

	class MeshFactory
	{
	public:
		MeshFactory(nvrhi::DeviceHandle device, MeshRegistry& registry);

		[[nodiscard]]
		MeshInstance::ID
		CreateCubeInstance(glm::mat4 modelTransform = {}) const;

		[[nodiscard]]
		MeshInstance::ID
		CreateSphereInstance(glm::mat4 modelTransform = {}) const;

	private:
		static MeshInfo::ID
		CreateCubeInfo(MeshRegistry& registry);

		static MeshInfo::ID
		CreateSphereInfo(MeshRegistry& registry);

	private:
		nvrhi::DeviceHandle m_device;
		MeshRegistry&       m_registry;
	};

}
