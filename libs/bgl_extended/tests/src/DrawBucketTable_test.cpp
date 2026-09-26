#include "gfx/DrawBucketTable.h"
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl_common/idl/DrawBucket.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using bgl::DrawBucketTable;
using bgl::GeometryStage;
using bgl::LayerType;
using bgl::MaterialHandle;
using bgl::MaterialType;

TEST_CASE("a bucket id names one (geom, material, layer) and nothing else", "[drawbucket]")
{
	DrawBucketTable table;

	// The unlit fallback exists before anything resolves: it is what a demand past the ceiling
	// clamps to, so it can never itself be past the ceiling.
	REQUIRE(table.Count() == 1);
	CHECK(table.Desc(0).geom == GeometryStage::kStaticMesh);
	CHECK(table.Desc(0).material == MaterialType::kNull);
	CHECK(table.Desc(0).layer == LayerType::kOpaque);
	CHECK_FALSE(table.Transparent(0));

	// Re-resolving the seed's own key allocates nothing.
	CHECK(table.Resolve(GeometryStage::kStaticMesh, MaterialType::kNull, LayerType::kOpaque) == 0);
	CHECK(table.Count() == 1);

	// Ids are dense, in first-use order, and stable on re-resolve.
	const auto opaquePbr =
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kOpaque);
	const auto cutoutPbr =
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kMask);
	const auto skinnedPbr =
		table.Resolve(GeometryStage::kSkinnedMesh, MaterialType::kPBR, LayerType::kOpaque);

	CHECK(opaquePbr == 1);
	CHECK(cutoutPbr == 2);
	CHECK(skinnedPbr == 3);
	CHECK(table.Count() == 4);
	CHECK(
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kOpaque) ==
		opaquePbr);
	CHECK(table.Count() == 4);

	// The desc reads back exactly the key the id was allocated for.
	CHECK(table.Desc(skinnedPbr).geom == GeometryStage::kSkinnedMesh);
	CHECK(table.Desc(skinnedPbr).material == MaterialType::kPBR);
	CHECK(table.Desc(skinnedPbr).layer == LayerType::kOpaque);
}

TEST_CASE("only the blend layer is transparent, and the flags mirror it", "[drawbucket]")
{
	DrawBucketTable table;

	const auto blend =
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kBlend);
	const auto hashed =
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kHashed);

	CHECK(table.Transparent(blend));
	CHECK_FALSE(table.Transparent(hashed));

	// The GPU upload source agrees with the per-bucket accessor, and covers the whole ceiling so
	// an unallocated lane reads 0, never garbage.
	const auto flags = table.Flags();
	REQUIRE(flags.size() == bgl::idl::cMaxDrawBuckets);
	CHECK(flags[blend] == 1u);
	CHECK(flags[hashed] == 0u);
	CHECK(flags[table.Count()] == 0u);
}

TEST_CASE("a material handle resolves as its (type, layer); invalid falls to unlit", "[drawbucket]")
{
	DrawBucketTable table;

	auto handle         = MaterialHandle();
	handle.materialType = MaterialType::kPBR;
	handle.layerType    = LayerType::kMask;
	handle.byteOffset   = 640;

	const auto byHandle = table.Resolve(GeometryStage::kStaticMesh, handle);
	const auto byKey =
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kMask);
	CHECK(byHandle == byKey);

	// The arena offset is data, not identity: two records of one kind and layer share a bucket.
	handle.byteOffset = 1280;
	CHECK(table.Resolve(GeometryStage::kStaticMesh, handle) == byHandle);

	CHECK(table.Resolve(GeometryStage::kStaticMesh, MaterialHandle()) == 0);
}

TEST_CASE("an unshaded material has one bucket whatever its layer", "[drawbucket]")
{
	DrawBucketTable table;

	// No base color, so no alpha for a coverage or blend layer to read: every layer is the opaque
	// bucket. The coverage-twin lookup has no entry for these kinds, so a second bucket would be
	// one the depth pass could not build.
	for (const auto layer : { LayerType::kMask, LayerType::kBlend, LayerType::kHashed })
	{
		CHECK(table.Resolve(GeometryStage::kStaticMesh, MaterialType::kNull, layer) == 0);

		const auto assert = table.Resolve(GeometryStage::kStaticMesh, MaterialType::kAssert, layer);
		CHECK(
			assert ==
			table.Resolve(GeometryStage::kStaticMesh, MaterialType::kAssert, LayerType::kOpaque));
		CHECK_FALSE(table.Transparent(assert));
	}
	CHECK(table.Count() == 2);
}

TEST_CASE("a demand past the ceiling clamps to the unlit fallback", "[drawbucket]")
{
	// Ceiling 3: the seed plus two. Small so the clamp is reached in three resolves -- the clamp
	// logic is what is under test, not the ceiling's value.
	DrawBucketTable table(3);

	const auto a =
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kOpaque);
	const auto b = table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kMask);
	CHECK(a == 1);
	CHECK(b == 2);

	// The fourth distinct key is refused: reported, clamped to bucket 0, and never allocated.
	const auto over =
		table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kHashed);
	CHECK(over == 0);
	CHECK(table.Count() == 3);

	// A key allocated before the ceiling keeps resolving to its own bucket.
	CHECK(table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kMask) == b);

	// Refused again, still unallocated: a refusal never claims a slot.
	(void)table.Resolve(GeometryStage::kStaticMesh, MaterialType::kPBR, LayerType::kHashed);
	CHECK(table.Count() == 3);
}
