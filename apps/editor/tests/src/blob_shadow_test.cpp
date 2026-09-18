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

TEST_CASE("The preview's shadows clear the floor the rig stands on", "[blobshadow]")
{
	// The rig's origin and planted soles are exactly at floor height: with no slack the depth
	// buffer refuses half the floor, and the orbiting camera reshuffles which half every frame.
	const glm::vec3 lo(-0.5f, 0.0f, -1.0f);
	const glm::vec3 hi(0.5f, 4.0f, 1.0f);

	CHECK(editor::BlobShadowForBounds(lo, hi).casterLift == 0.2f);
	CHECK(editor::FootShadowForBounds(lo, hi).maxReceiverRise == 0.2f);

	// A lift far short of the fade height, so the disc is barely weakened by it.
	CHECK(editor::BlobShadowForBounds(lo, hi).casterLift < 0.1f * 4.0f);

	CHECK(editor::BlobShadowForBounds(glm::vec3(0.0f), glm::vec3(0.0f)).casterLift > 0.0f);
	CHECK(editor::FootShadowForBounds(glm::vec3(0.0f), glm::vec3(0.0f)).maxReceiverRise > 0.0f);
}

TEST_CASE("A degenerate bounds still yields a desc SetBlobShadow accepts", "[blobshadow]")
{
	const auto desc = editor::BlobShadowForBounds(glm::vec3(0.0f), glm::vec3(0.0f));

	// SetBlobShadow refuses a non-positive radius or fade height, so the floor is what keeps a
	// point-sized load from throwing out of the preview.
	CHECK(desc.radius > 0.0f);
	CHECK(desc.fadeHeight > 0.0f);
}

TEST_CASE("The preview's foot shadows are sized by the rig they stand under", "[blobshadow]")
{
	// The same 1 x 4 x 2 rig: a tenth of its width across, faded a fifth of its height up.
	const auto desc =
		editor::FootShadowForBounds(glm::vec3(-0.5f, 0.0f, -1.0f), glm::vec3(0.5f, 4.0f, 1.0f));

	CHECK(desc.radius == 0.1f);
	CHECK(desc.fadeHeight == 0.8f);
}

TEST_CASE("A degenerate bounds still yields foot shadows SetBlobShadow accepts", "[blobshadow]")
{
	const auto desc = editor::FootShadowForBounds(glm::vec3(0.0f), glm::vec3(0.0f));

	CHECK(desc.radius > 0.0f);
	CHECK(desc.fadeHeight > 0.0f);
}

TEST_CASE("A preview wears feet only where the instance has a pose to find them in", "[blobshadow]")
{
	auto disc      = bgl::BlobShadowDesc();
	disc.intensity = 0.6f;
	auto feet      = bgl::FootShadowDesc();
	feet.radius    = 0.2f;

	SECTION("both switches off wear nothing")
	{
		CHECK_FALSE(editor::PreviewBlobShadow(false, false, true, disc, feet).has_value());
	}

	SECTION("the disc alone is the disc as sized")
	{
		const auto desc = editor::PreviewBlobShadow(true, false, true, disc, feet);
		REQUIRE(desc.has_value());
		CHECK(desc->intensity == 0.6f);
		CHECK_FALSE(desc->feet.has_value());
	}

	SECTION("the feet alone keep a disc of zero intensity under them")
	{
		const auto desc = editor::PreviewBlobShadow(false, true, true, disc, feet);
		REQUIRE(desc.has_value());
		CHECK(desc->intensity == 0.0f);
		REQUIRE(desc->feet.has_value());
		CHECK(desc->feet->radius == 0.2f);
	}

	// SetBlobShadow throws on feet wherever HasFootIK is false -- a crowd-tier preview, a rig
	// without an avatar -- so the switch must fall back rather than reach it.
	SECTION("an instance without foot IK keeps the disc and drops the feet")
	{
		const auto desc = editor::PreviewBlobShadow(true, true, false, disc, feet);
		REQUIRE(desc.has_value());
		CHECK(desc->intensity == 0.6f);
		CHECK_FALSE(desc->feet.has_value());

		CHECK_FALSE(editor::PreviewBlobShadow(false, true, false, disc, feet).has_value());
	}
}
