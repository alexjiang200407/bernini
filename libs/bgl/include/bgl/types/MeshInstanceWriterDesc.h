#pragma once
#include <bgl/GeomType.h>
#include <string>
#include <utility>

namespace bgl
{
	/**
	 * A kernel that places a block's instances, as a Slang type conforming to one of
	 * `bgl.MeshInstanceWriter`'s writer interfaces. Both are names in Slang source, compiled when
	 * the writer is created; the module is resolved on the GPU context's search paths, as a game
	 * surface's is.
	 */
	struct MeshInstanceWriterDesc
	{
		std::string slangModuleName;
		std::string slangTypeName;

		// The geom whose blocks it places, and so the interface it conforms to: kStaticMesh,
		// IMeshInstanceWriter; kSkinnedMesh, ISkinnedMeshInstanceWriter, whose block also takes
		// each slot's playback offset.
		GeomType geomType = GeomType::kStaticMesh;

		template <typename Self>
		Self&&
		SetSlangModuleName(this Self&& self, std::string value) noexcept
		{
			self.slangModuleName = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSlangTypeName(this Self&& self, std::string value) noexcept
		{
			self.slangTypeName = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetGeomType(this Self&& self, GeomType value) noexcept
		{
			self.geomType = value;
			return std::forward<Self>(self);
		}
	};
}
