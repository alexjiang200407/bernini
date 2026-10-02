#pragma once
#include <bgl/types/GeomHandle.h>
#include <core/glm.h>
#include <utility>

namespace bgl
{
	/** A placement of one static geom -- see ISceneView::CreateStaticMeshInstance. */
	struct StaticMeshInstanceDesc
	{
		GeomHandle geom;
		glm::mat4  transform = glm::mat4(1.0f);

		template <typename Self>
		Self&&
		SetGeom(this Self&& self, GeomHandle geom) noexcept
		{
			self.geom = geom;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTransform(this Self&& self, const glm::mat4& transform) noexcept
		{
			self.transform = transform;
			return std::forward<Self>(self);
		}
	};
}
