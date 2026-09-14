#pragma once

#include <bgl/types/BlobShadowDesc.h>
#include <core/glm.h>

namespace editor
{
	/**
	 * The blob shadow a previewed rig of these world-space bounds wears: a disc of half the
	 * narrower horizontal extent -- the body's width, not the clip union's stride reach -- fully
	 * faded one bounds-height off the ground. Both floored at 0.1 so a degenerate bounds still
	 * yields a desc ISceneView::SetBlobShadow accepts.
	 */
	[[nodiscard]] bgl::BlobShadowDesc
	BlobShadowForBounds(const glm::vec3& aabbMin, const glm::vec3& aabbMax) noexcept;
}
