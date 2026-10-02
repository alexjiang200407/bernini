#pragma once
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/RigHandle.h>
#include <core/glm.h>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace bgl
{
	/**
	 * One mesh of a loaded BMesh, as skinned geometry against a rig -- see
	 * IScene::AddSkinnedMeshGeom. The mesh and the materials are borrowed for the call alone.
	 */
	struct SkinnedMeshGeomDesc
	{
		// A BMesh loaded from disk, carrying skin binding on every submesh.
		const assetlib::BMesh* mesh = nullptr;

		// Index into `mesh->meshes`.
		uint32_t meshIndex = 0;

		// Parallel to `mesh->materials`, resolved by the caller.
		std::span<const MaterialHandle> materials;

		// The rig the mesh's joint indices address, from IScene::AddRig.
		RigHandle rig;

		// A box holding the mesh in every pose of every clip, in model space. Inverted until set,
		// so a desc that never names one is refused rather than culled by an empty box.
		assetlib::Bounds posedBounds = { glm::vec3(std::numeric_limits<float>::max()),
			                             glm::vec3(std::numeric_limits<float>::lowest()) };

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

		template <typename Self>
		Self&&
		SetRig(this Self&& self, RigHandle rig) noexcept
		{
			self.rig = rig;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPosedBounds(this Self&& self, const assetlib::Bounds& posedBounds) noexcept
		{
			self.posedBounds = posedBounds;
			return std::forward<Self>(self);
		}
	};
}
