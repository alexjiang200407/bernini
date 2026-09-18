#include "gfx/BucketTable.h"
#include "passes/bucket_config.h"
#include "types/RasterState.h"
#include "util/util.h"
#include <array>
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

// What a (geom, material kind, layer) resolves to and draws with, and the door predicate that
// decides which keys can exist. No device: this is what every door, every counting-sort bucket and
// every pass's kernel choice depends on, and it is worth pinning where it costs nothing to run.

namespace
{
	using namespace std::string_view_literals;

	bgl::MaterialHandle
	Handle(bgl::MaterialType type, bgl::LayerType layer)
	{
		auto handle         = bgl::MaterialHandle();
		handle.materialType = type;
		handle.layerType    = layer;
		return handle;
	}

	constexpr std::array<bgl::LayerType, 4> c_Layers = { {
		bgl::LayerType::kOpaque,
		bgl::LayerType::kMask,
		bgl::LayerType::kBlend,
		bgl::LayerType::kHashed,
	} };
}

TEST_CASE("every layer of a kPBR material binds to animated geometry", "[bucket]")
{
	for (const bgl::LayerType layer : c_Layers)
	{
		const auto pbr = Handle(bgl::MaterialType::kPBR, layer);

		CHECK(bgl::AcceptsMaterial(bgl::GeomType::kSkinnedMesh, pbr));
	}
}

TEST_CASE("animated geometry takes no unlit or loose material", "[bucket]")
{
	// No unlit variant to fall back to, so an unnamed material is a refusal rather than the flat
	// shading a static submesh gets.
	CHECK_FALSE(bgl::AcceptsMaterial(bgl::GeomType::kSkinnedMesh, bgl::MaterialHandle{}));

	// A loose material routes its channels rather than sampling a baked triplet, and the animated
	// geometry stage has no pixel shader that does the routing.
	for (const bgl::LayerType layer : c_Layers)
	{
		const auto loose = Handle(bgl::MaterialType::kLoosePbr, layer);

		CHECK_FALSE(bgl::AcceptsMaterial(bgl::GeomType::kSkinnedMesh, loose));
	}

	// Static geometry is the exception and takes anything, an invalid handle included: it resolves
	// to the unlit kNull bucket rather than failing to load.
	CHECK(bgl::AcceptsMaterial(bgl::GeomType::kStaticMesh, bgl::MaterialHandle{}));
}

TEST_CASE("every drawable key has a bucket of its own", "[bucket]")
{
	bgl::BucketTable table;

	// Every key a door admits: each tier's every layer of every kind it accepts. kNull and kAssert
	// collapse to their opaque bucket (BucketTable_test), so they are resolved once, opaque.
	std::set<uint32_t> seen;
	uint32_t           keys = 0;
	for (uint32_t kind = 0; kind < static_cast<uint32_t>(bgl::MaterialType::kCount); ++kind)
	{
		const auto material = static_cast<bgl::MaterialType>(kind);
		const bool unshaded =
			material == bgl::MaterialType::kNull || material == bgl::MaterialType::kAssert;

		for (const bgl::LayerType layer : c_Layers)
		{
			if (unshaded && layer != bgl::LayerType::kOpaque)
			{
				continue;
			}

			for (const auto geom : { bgl::GeomType::kStaticMesh, bgl::GeomType::kSkinnedMesh })
			{
				if (!bgl::AcceptsMaterial(geom, Handle(material, layer)))
				{
					continue;
				}

				INFO("kind " << kind << " layer " << static_cast<uint32_t>(layer));
				const uint32_t bucket = table.Resolve(geom, material, layer);

				// Distinct keys, distinct ids; and the desc reads back exactly the key.
				CHECK(seen.insert(bucket).second);
				CHECK(table.Desc(bucket).geom == geom);
				CHECK(table.Desc(bucket).material == material);
				CHECK(table.Desc(bucket).layer == layer);

				// Only a blended layer leaves the counting sort, whatever the tier or kind.
				CHECK(table.Transparent(bucket) == (layer == bgl::LayerType::kBlend));
				++keys;
			}
		}
	}

	// Dense: the ids are exactly 0..count-1, the unlit seed among them.
	CHECK(table.Count() == keys);
	CHECK(*seen.rbegin() == keys - 1);
}

// A bucket's kernels are functions of its desc, not of its id: the pixel program follows the
// material kind and the layer, the geometry program the tier, and the hardware cull the kind.
TEST_CASE("a bucket's programs follow its desc", "[bucket]")
{
	using bgl::BucketDesc;
	using bgl::GeomType;
	using bgl::LayerType;
	using bgl::MaterialType;

	const BucketDesc staticCutout = { GeomType::kStaticMesh, MaterialType::kPBR, LayerType::kMask };
	const BucketDesc skinnedCutout = { GeomType::kSkinnedMesh,
		                               MaterialType::kPBR,
		                               LayerType::kMask };

	// The two tiers differ only in their geometry stage: the pixel shader reads a vertex output
	// and a material offset, and neither says which tier filled them.
	CHECK(bgl::BucketPixelSrc(staticCutout) == "programs.forward.PBR_AlphaTest"sv);
	CHECK(bgl::BucketPixelSrc(skinnedCutout) == bgl::BucketPixelSrc(staticCutout));
	CHECK(bgl::BucketGeometrySrc(staticCutout) == "programs.forward.StaticMesh"sv);
	CHECK(bgl::BucketGeometrySrc(skinnedCutout) == "programs.forward.SkinnedMesh"sv);

	// The static depth pass evaluates coverage with the colour pass's own arithmetic.
	CHECK(
		bgl::BucketCoveragePixelSrc(staticCutout) == "programs.forward.DepthOnly_PBR_AlphaTest"sv);
	CHECK(
		bgl::BucketCoveragePixelSrc(
			{ GeomType::kStaticMesh, MaterialType::kLoosePbr, LayerType::kHashed }) ==
		"programs.forward.DepthOnly_PBR_Loose_HashedAlpha"sv);

	// Only the materialless kinds cull in hardware: every material bucket leaves back faces to the
	// mesh stage and the material's doubleSided flag.
	CHECK(
		bgl::BucketCullMode({ GeomType::kStaticMesh, MaterialType::kNull, LayerType::kOpaque }) ==
		bgl::RasterCullMode::kBack);
	CHECK(
		bgl::BucketCullMode({ GeomType::kStaticMesh, MaterialType::kAssert, LayerType::kOpaque }) ==
		bgl::RasterCullMode::kBack);
	CHECK(bgl::BucketCullMode(staticCutout) == bgl::RasterCullMode::kNone);
}

// Each reserved game slot is its own material kind, so its layers resolve to buckets of their own
// on both tiers, and draw with the slot's own programs -- never another slot's, never engine PBR's.
TEST_CASE("a game slot's layers resolve to its own programs, on both tiers", "[bucket]")
{
	using bgl::GeomType;
	using bgl::LayerType;

	for (uint32_t slot = 0; slot < bgl::cGameSlots; ++slot)
	{
		INFO("slot " << slot);

		const bgl::MaterialType kind = bgl::GameSlotKind(slot);
		CHECK(bgl::GameSlot(kind) == slot);

		const auto program = [&](std::string_view layerSuffix) {
			return "programs.forward.GameSlot" + std::to_string(slot) + std::string(layerSuffix);
		};

		for (const GeomType geom : { GeomType::kStaticMesh, GeomType::kSkinnedMesh })
		{
			CHECK(bgl::BucketPixelSrc({ geom, kind, LayerType::kOpaque }) == program(""));
			CHECK(bgl::BucketPixelSrc({ geom, kind, LayerType::kMask }) == program("_AlphaTest"));
			CHECK(
				bgl::BucketPixelSrc({ geom, kind, LayerType::kHashed }) == program("_HashedAlpha"));
		}

		CHECK(
			bgl::BucketCoveragePixelSrc({ GeomType::kStaticMesh, kind, LayerType::kMask }) ==
			"programs.forward.DepthOnly_GameSlot" + std::to_string(slot) + "_AlphaTest");

		// The skinned door is open for every layer a game surface can carry, hashed included: a
		// surface's tiers differ in nothing but the geometry stage.
		for (const LayerType layer : c_Layers)
		{
			CHECK(bgl::AcceptsMaterial(GeomType::kSkinnedMesh, Handle(kind, layer)));
		}
	}

	// A kind outside the slots is nobody's slot.
	CHECK_FALSE(bgl::GameSlot(bgl::MaterialType::kPBR).has_value());
	CHECK_FALSE(bgl::GameSlot(bgl::MaterialType::kCount).has_value());
}
