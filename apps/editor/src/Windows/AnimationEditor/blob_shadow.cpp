#include "blob_shadow.h"

#include <algorithm>
#include <bgl/types/BlobShadowDesc.h>
#include <core/glm.h>

namespace editor
{
	bgl::BlobShadowDesc
	BlobShadowForBounds(const glm::vec3& aabbMin, const glm::vec3& aabbMax) noexcept
	{
		const glm::vec3 extent = aabbMax - aabbMin;

		// The narrower horizontal extent: the bounds are the union of every clip's posed box, so
		// the long axis carries stride reach and tail, and a disc spanning it dwarfs the rig. The
		// narrow axis stays close to the body's width, which is what a contact disc should read.
		auto desc       = bgl::BlobShadowDesc();
		desc.radius     = std::max(0.1f, std::min(extent.x, extent.z) * 0.5f);
		desc.fadeHeight = std::max(0.1f, extent.y);
		return desc;
	}
}
