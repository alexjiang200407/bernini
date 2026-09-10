#include "util/util.h"
#include <array>
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl_common/idl/PsoType.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// The (geom, material type, layer) -> PsoType table, and the door predicate built on it. No device:
// this is the arithmetic every door and every counting-sort bucket depends on, and it is worth
// pinning where it costs nothing to run.

namespace
{
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

TEST_CASE("every layer of a kPBR material binds to animated geometry", "[pso]")
{
	for (const bgl::LayerType layer : c_Layers)
	{
		const auto pbr = Handle(bgl::MaterialType::kPBR, layer);

		CHECK(bgl::AcceptsMaterial(bgl::GeomType::kSkinnedMesh, pbr));
	}
}

TEST_CASE("animated geometry takes no unlit or loose material", "[pso]")
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

TEST_CASE("a layer resolves to its own tier's bucket", "[pso]")
{
	using bgl::GeomType;
	using bgl::GetPsoFromGeomAndMaterial;
	using bgl::LayerType;
	using bgl::MaterialType;
	using bgl::idl::PsoType;

	const auto pso = [](GeomType geom, LayerType layer) {
		return GetPsoFromGeomAndMaterial(geom, MaterialType::kPBR, layer);
	};

	CHECK(pso(GeomType::kSkinnedMesh, LayerType::kOpaque) == PsoType::kOpaque_SkinnedMesh_PBR);
	CHECK(pso(GeomType::kSkinnedMesh, LayerType::kMask) == PsoType::kAlphaTest_SkinnedMesh_PBR);
	CHECK(pso(GeomType::kSkinnedMesh, LayerType::kHashed) == PsoType::kHashedAlpha_SkinnedMesh_PBR);
	CHECK(pso(GeomType::kSkinnedMesh, LayerType::kBlend) == PsoType::kTransparent_SkinnedMesh_PBR);
}

TEST_CASE("only a blended layer leaves the counting sort", "[pso]")
{
	// Every other layer draws indirect off its bucket's prefix-sum range; a blended one is drawn
	// from the depth-sorted list instead, whatever tier it is on. Read off the layer rather than
	// off a second copy of IsTransparentPso's list, so a tier added without extending that list
	// fails here.
	for (const bgl::GeomType geom : { bgl::GeomType::kStaticMesh, bgl::GeomType::kSkinnedMesh })
	{
		for (const bgl::LayerType layer : c_Layers)
		{
			const uint32_t pso = bgl::SubmeshPso(geom, Handle(bgl::MaterialType::kPBR, layer));

			CHECK(bgl::IsTransparentPso(pso) == (layer == bgl::LayerType::kBlend));
		}
	}

	// The loose material type is static-only, and its blend bucket is the second one the
	// depth-sorted list has to carry.
	for (const bgl::LayerType layer : c_Layers)
	{
		const uint32_t pso = bgl::SubmeshPso(
			bgl::GeomType::kStaticMesh,
			Handle(bgl::MaterialType::kLoosePbr, layer));

		CHECK(bgl::IsTransparentPso(pso) == (layer == bgl::LayerType::kBlend));
	}
}

// Each reserved game slot has seven rows: an opaque, an alpha-test and a hashed row per tier, and
// one transparent row both tiers share. The rows are reached by arithmetic from kGameRowsStart, so
// what this checks is that the arithmetic agrees with the tier, the layer and the transparent
// predicate on both sides.
TEST_CASE("a game slot's layers resolve to its own rows, on both tiers", "[pso]")
{
	using bgl::GeomType;
	using bgl::LayerType;
	using bgl::idl::PsoType;

	constexpr std::array<LayerType, 4> c_SurfaceLayers = { {
		LayerType::kOpaque,
		LayerType::kMask,
		LayerType::kHashed,
		LayerType::kBlend,
	} };

	for (uint32_t slot = 0; slot < bgl::cGameSlots; ++slot)
	{
		const bgl::MaterialType kind = bgl::GameSlotKind(slot);
		CHECK(bgl::GameSlot(kind) == slot);

		const auto first =
			static_cast<uint32_t>(PsoType::kGameRowsStart) + slot * bgl::idl::cGameSlotRows;
		const auto pso = [&](GeomType geom, LayerType layer) {
			return static_cast<uint32_t>(bgl::GetPsoFromGeomAndMaterial(geom, kind, layer));
		};

		// A tier's rows are contiguous, so the skinned block starts cGameSlotTierRows along: the
		// offsets are what a reader follows rather than a list to keep in step.
		CHECK(pso(GeomType::kStaticMesh, LayerType::kOpaque) == first);
		CHECK(pso(GeomType::kStaticMesh, LayerType::kMask) == first + 1);
		CHECK(pso(GeomType::kStaticMesh, LayerType::kHashed) == first + 2);
		CHECK(
			pso(GeomType::kSkinnedMesh, LayerType::kOpaque) == first + bgl::idl::cGameSlotTierRows);
		CHECK(
			pso(GeomType::kSkinnedMesh, LayerType::kMask) ==
			first + bgl::idl::cGameSlotTierRows + 1);
		CHECK(
			pso(GeomType::kSkinnedMesh, LayerType::kHashed) ==
			first + bgl::idl::cGameSlotTierRows + 2);

		// One row for both tiers rather than one each: the blended pipeline's geometry stage
		// branches tier per instance, so a second row would name the same pipeline. Landing on the
		// same row is the claim -- landing on two transparent rows would pass a weaker check.
		CHECK(pso(GeomType::kStaticMesh, LayerType::kBlend) == first + bgl::idl::cGameSlotBlendRow);
		CHECK(
			pso(GeomType::kSkinnedMesh, LayerType::kBlend) ==
			pso(GeomType::kStaticMesh, LayerType::kBlend));
		CHECK(pso(GeomType::kStaticMesh, LayerType::kBlend) < bgl::idl::c_PsoCount);

		// Only the blend row leaves the counting sort, read off the layer rather than off a second
		// copy of IsTransparentPso's list.
		for (const GeomType geom : { GeomType::kStaticMesh, GeomType::kSkinnedMesh })
			for (const LayerType layer : c_SurfaceLayers)
			{
				const uint32_t bucket = bgl::SubmeshPso(geom, Handle(kind, layer));

				CHECK(bgl::IsTransparentPso(bucket) == (layer == LayerType::kBlend));
			}

		// The skinned door is open for every layer a game surface can carry, hashed included: a
		// surface's tiers differ in nothing but the geometry stage.
		for (const LayerType layer : c_SurfaceLayers)
			CHECK(bgl::AcceptsMaterial(GeomType::kSkinnedMesh, Handle(kind, layer)));
	}

	// A kind outside the slots is nobody's slot.
	CHECK_FALSE(bgl::GameSlot(bgl::MaterialType::kPBR).has_value());
	CHECK_FALSE(bgl::GameSlot(bgl::MaterialType::kCount).has_value());
}
