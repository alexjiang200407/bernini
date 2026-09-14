#include "Windows/AnimationEditor/blob_shadow.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The preview's disc follows the rig's width, not its stride reach", "[blobshadow]")
{
	// A rig of 1 x 4 x 2: the 2 along z is the clip union's stride reach, so the disc takes the
	// 1 along x -- the body's width -- and fades out over the rig's own height.
	const auto desc =
		editor::BlobShadowForBounds(glm::vec3(-0.5f, 0.0f, -1.0f), glm::vec3(0.5f, 4.0f, 1.0f));

	CHECK(desc.radius == 0.5f);
	CHECK(desc.fadeHeight == 4.0f);
}

TEST_CASE("A degenerate bounds still yields a desc SetBlobShadow accepts", "[blobshadow]")
{
	const auto desc = editor::BlobShadowForBounds(glm::vec3(0.0f), glm::vec3(0.0f));

	// SetBlobShadow refuses a non-positive radius or fade height, so the floor is what keeps a
	// point-sized load from throwing out of the preview.
	CHECK(desc.radius > 0.0f);
	CHECK(desc.fadeHeight > 0.0f);
}
