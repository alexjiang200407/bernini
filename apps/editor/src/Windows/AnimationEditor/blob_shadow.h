#pragma once

#include <bgl/types/BlobShadowDesc.h>
#include <core/glm.h>
#include <optional>

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

	/**
	 * The shadow each foot of a previewed rig of these bounds stands in: a capsule a tenth of the
	 * body's width across, fully faded a fifth of the rig's height up -- about where a stride
	 * carries a foot. Floored so a degenerate bounds still yields a desc SetBlobShadow accepts.
	 */
	[[nodiscard]] bgl::FootShadowDesc
	FootShadowForBounds(const glm::vec3& aabbMin, const glm::vec3& aabbMax) noexcept;

	/**
	 * What one previewed instance wears for the panel's two switches, or empty for nothing at all.
	 * The feet go only where `hasFootIK` -- SetBlobShadow refuses them anywhere else, so a
	 * crowd-tier preview or a rig without an avatar keeps the disc -- and a disc switched off
	 * under feet that are on is kept at zero intensity, which is how the feet are asked for alone.
	 */
	[[nodiscard]] std::optional<bgl::BlobShadowDesc>
	PreviewBlobShadow(
		bool                       disc,
		bool                       feet,
		bool                       hasFootIK,
		const bgl::BlobShadowDesc& discDesc,
		const bgl::FootShadowDesc& footDesc) noexcept;
}
