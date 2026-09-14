#pragma once
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <bgl/Camera.h>
#include <bgl/glm.h>
#include <cstdint>

namespace headless
{
	/** A box holding nothing, which the first GrowBounds replaces. */
	[[nodiscard]] assetlib::Bounds
	EmptyBounds() noexcept;

	/** Grows `bounds` to hold `local`'s eight corners placed by `transform`. */
	void
	GrowBounds(
		assetlib::Bounds&       bounds,
		const glm::mat4&        transform,
		const assetlib::Bounds& local) noexcept;

	/** The box around every submesh of mesh entry `meshIndex`, in the entry's own space. */
	[[nodiscard]] assetlib::Bounds
	MeshEntryBounds(const assetlib::BMesh& mesh, uint32_t meshIndex);

	/**
	 * Where an instance of node `node`'s mesh entry stands: the node's world transform, or identity
	 * for a skinned entry -- its positions are already in rig space, which full weight on the bone
	 * undoes, so applying the node's transform again would place it twice.
	 */
	[[nodiscard]] glm::mat4
	InstanceTransform(const assetlib::BMesh& mesh, uint32_t node);

	/**
	 * A camera framing `bounds` on its bounding sphere, so a model fills the frame the same way
	 * whatever scale it was authored at.
	 */
	[[nodiscard]] bgl::Camera
	FrameBounds(const assetlib::Bounds& bounds, uint32_t width, uint32_t height);
}
