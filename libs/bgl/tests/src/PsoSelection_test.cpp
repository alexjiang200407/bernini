#include "util/util.h"
#include <catch2/catch_test_macros.hpp>

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

		CHECK(bgl::AcceptsMaterial(bgl::GeomType::kVatMesh, pbr));
		CHECK(bgl::AcceptsMaterial(bgl::GeomType::kSkinnedMesh, pbr));
	}
}

TEST_CASE("animated geometry takes no material but kPBR", "[pso]")
{
	// No unlit variant to fall back to, so an unnamed material is a refusal rather than the flat
	// shading a static submesh gets.
	CHECK_FALSE(bgl::AcceptsMaterial(bgl::GeomType::kVatMesh, bgl::MaterialHandle{}));
	CHECK_FALSE(bgl::AcceptsMaterial(bgl::GeomType::kSkinnedMesh, bgl::MaterialHandle{}));

	// A loose material routes its channels rather than sampling a baked triplet, and neither
	// animated geometry stage has a pixel shader that does the routing.
	for (const bgl::LayerType layer : c_Layers)
	{
		const auto loose = Handle(bgl::MaterialType::kLoosePbr, layer);

		CHECK_FALSE(bgl::AcceptsMaterial(bgl::GeomType::kVatMesh, loose));
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

	CHECK(pso(GeomType::kVatMesh, LayerType::kOpaque) == PsoType::kOpaque_VatMesh_PBR);
	CHECK(pso(GeomType::kVatMesh, LayerType::kMask) == PsoType::kAlphaTest_VatMesh_PBR);
	CHECK(pso(GeomType::kVatMesh, LayerType::kHashed) == PsoType::kHashedAlpha_VatMesh_PBR);
	CHECK(pso(GeomType::kVatMesh, LayerType::kBlend) == PsoType::kTransparent_VatMesh_PBR);

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
	for (const bgl::GeomType geom :
	     { bgl::GeomType::kStaticMesh, bgl::GeomType::kVatMesh, bgl::GeomType::kSkinnedMesh })
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
