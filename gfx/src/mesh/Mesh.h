#pragma once

namespace gfx
{

	class Mesh final
	{
	public:
		using InstanceID = uint32_t;
		using InfoID     = uint32_t;

		struct Instance final
		{
			InfoID    infoID;
			glm::mat4 modelTransform;
		};

	private:
		struct Info final
		{
			uint32_t startIndex;
			uint32_t indexCount;
			uint32_t baseVertex;
			uint32_t materialID;
		};

	private:
		InstanceID m_instanceID = 0;
		InfoID     m_infoID     = 0;

		friend class MeshRegistry;
	};
}
