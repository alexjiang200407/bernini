#pragma once
#include <assetlib_structs/BMesh.h>
#include <bgl/types/MaterialHandle.h>
#include <cstdint>
#include <span>
#include <utility>

namespace bgl
{
	/**
	 * One mesh of a loaded BMesh, as static-mesh geometry -- see IScene::AddStaticMeshGeom. The
	 * mesh and the materials are borrowed for the call alone.
	 */
	struct StaticMeshGeomDesc
	{
		// A BMesh loaded from disk (see assetlib::load).
		const assetlib::BMesh* mesh = nullptr;

		// Index into `mesh->meshes`.
		uint32_t meshIndex = 0;

		// Parallel to `mesh->materials`, resolved by the caller. A submesh whose material index is
		// out of range (e.g. the source had none) is left unlit.
		std::span<const MaterialHandle> materials;

		template <typename Self>
		Self&&
		SetMesh(this Self&& self, const assetlib::BMesh* mesh) noexcept
		{
			self.mesh = mesh;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMeshIndex(this Self&& self, uint32_t meshIndex) noexcept
		{
			self.meshIndex = meshIndex;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaterials(this Self&& self, std::span<const MaterialHandle> materials) noexcept
		{
			self.materials = materials;
			return std::forward<Self>(self);
		}
	};
}
