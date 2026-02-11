#pragma once
#include "GfxBase.h"
#include "scene/SceneData.h"
#include "types/PBRMaterial.h"

namespace gfx
{
	class Scene final : public GfxBase
	{
	public:
		Scene(nvrhi::DeviceHandle device) { m_data.Init(device); }

		[[nodiscard]] StaticMeshInfo::ID
		CreateCubeMesh();

		[[nodiscard]] StaticMeshInfo::ID
		CreateSphereMesh();

		[[nodiscard]] DrawInstance::ID
		CreateStaticMeshInstance(StaticMeshInfo::ID infoId, glm::mat4 modelTransform);

		void
		RemoveMeshInstance(DrawInstance::ID id)
		{
			m_data.RemoveDrawInstance(id);
		}

		bool
		RemoveMeshInstanceNoExcept(DrawInstance::ID id) noexcept;

		void
		RemoveStaticMesh(StaticMeshInfo::ID id)
		{
			m_data.RemoveStaticMeshInfo(id);
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

		PBRMaterial::ID
		CreatePBRMaterial(PBRMaterial&& material, LayerType layerType);

		void
		AttachPBRMaterial(DrawInstance::ID instanceId, PBRMaterial::ID matId)
		{
			m_data.AttachPBRMaterial(instanceId, matId);
		}

	private:
		[[nodiscard]]
		StaticMeshInfo::ID
		CreateCubeInfo(SceneData& sceneData);

		[[nodiscard]]
		StaticMeshInfo::ID
		CreateSphereInfo(SceneData& sceneData);

	private:
		SceneData m_data;
	};

}
