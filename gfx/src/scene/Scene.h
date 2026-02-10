#pragma once
#include "GfxBase.h"
#include "scene/SceneData.h"

namespace gfx
{
	class Scene final : public GfxBase
	{
	public:
		Scene(nvrhi::DeviceHandle device)
		{
			m_data.Init(device);
			m_cubeInfoID   = CreateCubeInfo(m_data);
			m_sphereInfoID = CreateSphereInfo(m_data);
		}

		[[nodiscard]] DrawInstance::ID
		CreateCube(glm::mat4 modelTransform);

		[[nodiscard]] DrawInstance::ID
		CreateSphere(glm::mat4 modelTransform);

		void
		RemoveInstance(DrawInstance::ID id)
		{
			m_data.RemoveDrawInstance(id);
		}

		const SceneData&
		GetData() const noexcept
		{
			return m_data;
		}

		SceneData&
		GetData() noexcept
		{
			return m_data;
		}

	private:
		[[nodiscard]]
		StaticMeshInfo::ID
		CreateCubeInfo(SceneData& sceneData);

		[[nodiscard]]
		StaticMeshInfo::ID
		CreateSphereInfo(SceneData& sceneData);

	private:
		SceneData          m_data;
		StaticMeshInfo::ID m_cubeInfoID   = 0u;
		StaticMeshInfo::ID m_sphereInfoID = 0u;
	};

}
