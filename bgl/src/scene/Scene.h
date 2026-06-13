#pragma once
#include "resource/Buffer.h"

namespace bgl
{
	class IResourceManager;

	struct SceneDesc
	{
		uint32_t maxInstances = 0;
		uint32_t maxMeshlets  = 0;
		uint32_t maxVertices  = 0;
		uint32_t maxIndices   = 0;
	};

	class Scene
	{
	private:
		SceneDesc                         m_Desc;
		BufferHandle                      m_Instances;
		BufferHandle                      m_Meshlets;
		BufferHandle                      m_Vertices;
		BufferHandle                      m_Indices;
		core::SharedRef<IResourceManager> m_ResourceManager;
	};
}
