#pragma once
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include <bgl/IScene.h>

namespace bgl
{
	class Scene : public core::RefCounter<IScene>
	{
	public:
		Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager);
		Scene(const Scene&) noexcept = delete;
		Scene(Scene&&) noexcept      = delete;

		Scene&
		operator=(const Scene&) noexcept = delete;

		Scene&
		operator=(Scene&&) noexcept = delete;

		const SceneDesc&
		GetDesc() const override
		{
			return m_Desc;
		}

	private:
		SceneDesc                         m_Desc;
		BufferHandle                      m_Instances;
		BufferHandle                      m_Meshlets;
		BufferHandle                      m_Vertices;
		BufferHandle                      m_Indices;
		core::SharedRef<IResourceManager> m_ResourceManager;
	};
}
