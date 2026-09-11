#pragma once
#include <core/glm.h>

namespace assetlib
{
	struct Transform;

	/**
	 * Translation, then rotation, then scale, composed into one matrix.
	 *
	 * Here and not beside `Transform`: `assetlib_structs` is data, so a question about a container
	 * is answered by the library that holds the answers. See the root CLAUDE.md.
	 */
	[[nodiscard]] glm::mat4
	toMatrix(const Transform& transform) noexcept;
}
