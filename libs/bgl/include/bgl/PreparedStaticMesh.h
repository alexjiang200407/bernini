#pragma once

#include <bgl/api.h>

namespace assetlib
{
	struct BMesh;
}

namespace bgl
{
	/**
	 * The CPU half of AddStaticMeshGeom: one mesh's submeshes flattened into the arrays the scene's
	 * buffers take. Opaque and move-only; produced by CookStaticMesh, consumed by the AddStaticMeshGeom
	 * overload that takes one.
	 */
	class PreparedStaticMesh
	{
	public:
		BGL_API
		PreparedStaticMesh() noexcept;

		BGL_API ~PreparedStaticMesh();

		BGL_API
		PreparedStaticMesh(PreparedStaticMesh&&) noexcept;

		BGL_API PreparedStaticMesh&
		operator=(PreparedStaticMesh&&) noexcept;

		PreparedStaticMesh(const PreparedStaticMesh&) = delete;

		PreparedStaticMesh&
		operator=(const PreparedStaticMesh&) = delete;

		struct Impl;

	private:
		friend class Scene;

		friend BGL_API PreparedStaticMesh
		CookStaticMesh(const assetlib::BMesh& mesh, uint32_t meshIndex);

		std::unique_ptr<Impl> m_Impl;
	};

	/**
	 * Flattens mesh `meshIndex` of a loaded BMesh into what AddStaticMeshGeom uploads: per submesh, the
	 * meshlet table and the remapped vertex and index streams.
	 *
	 * Pure CPU over the BMesh alone, so it may run on any thread -- the one bgl entry point that
	 * may. That is its reason to exist: this is the dominant cost of adding a large mesh, and fused
	 * into AddStaticMeshGeom it rode the render thread.
	 *
	 * @throws SceneError if `meshIndex` is out of range, a submesh has no geometry or more meshlets
	 *         than one dispatch can launch, or a submesh's data lies outside the mesh's buffers.
	 */
	[[nodiscard]] BGL_API PreparedStaticMesh
	CookStaticMesh(const assetlib::BMesh& mesh, uint32_t meshIndex);
}
