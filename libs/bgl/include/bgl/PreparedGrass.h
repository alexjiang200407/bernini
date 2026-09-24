#pragma once

#include <bgl/api.h>
#include <cstdint>
#include <memory>

namespace assetlib
{
	struct BGrassFields;
}

namespace bgl
{
	/**
	 * The CPU half of IScene::AttachGrass: the grass fields growing on one mesh, copied out of
	 * their pools and checked. Opaque and move-only; produced by CookGrass, consumed by AttachGrass.
	 */
	class PreparedGrass
	{
	public:
		BGL_API
		PreparedGrass() noexcept;

		BGL_API ~PreparedGrass();

		BGL_API
		PreparedGrass(PreparedGrass&&) noexcept;

		BGL_API PreparedGrass&
		operator=(PreparedGrass&&) noexcept;

		PreparedGrass(const PreparedGrass&) = delete;

		PreparedGrass&
		operator=(const PreparedGrass&) = delete;

		struct Impl;

	private:
		friend class Scene;

		friend BGL_API PreparedGrass
		CookGrass(const assetlib::BGrassFields& fields, uint32_t meshIndex);

		std::unique_ptr<Impl> m_Impl;
	};

	/**
	 * Copies the fields of `fields` growing on mesh `meshIndex` out of their pools. The ranges come
	 * from a file, so each is checked before it is read. Pure CPU over `fields` alone, so it may
	 * run on any thread, as CookStaticMesh may.
	 *
	 * A mesh with no fields cooks to empty grass, which AttachGrass accepts.
	 *
	 * @throws SceneError if a field on this mesh has no chunks or more than one dispatch can
	 *         launch, a chunk holds no clumps or more than `assetlib::c_GrassClumpsPerChunk`, or a
	 *         range lies outside its pool.
	 */
	[[nodiscard]] BGL_API PreparedGrass
	CookGrass(const assetlib::BGrassFields& fields, uint32_t meshIndex);
}
