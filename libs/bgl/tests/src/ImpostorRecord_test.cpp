#include <array>
#include <assetlib_structs/Mesh.h>
#include <bgl/LodLevel.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/Geom.h>
#include <bgl/idl/Impostor.h>
#include <bgl/idl/InstanceVisibility.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The impostor's CPU<->GPU agreement: the record a geom names, and the visibility bit its tier sets.
// What a fake host cannot prove -- that the stage samples these handles, that the cull sets the bit
// past the last level -- is the impostor stage's and the cull's render cases to pin.

TEST_CASE("an impostor record survives a copy through upload bytes", "[impostor][idl]")
{
	auto record             = bgl::idl::Impostor();
	record.albedo.bits      = { 11u, 12u };
	record.normalDepth.bits = { 21u, 22u };
	record.sphere           = glm::vec4(0.5f, 2.0f, -0.25f, 3.0f);
	record.framesPerSide    = assetlib::c_ImpostorFramesPerSide;

	auto bytes = std::array<std::byte, sizeof(bgl::idl::Impostor)>();
	std::memcpy(bytes.data(), &record, sizeof(record));
	auto read = bgl::idl::Impostor();
	std::memcpy(&read, bytes.data(), sizeof(read));

	CHECK(read.albedo.bits == record.albedo.bits);
	CHECK(read.normalDepth.bits == record.normalDepth.bits);
	CHECK(read.sphere == record.sphere);
	CHECK(read.framesPerSide == 8u);

	// Handles first, as the typed view of a raw-loaded record indexes them.
	CHECK(offsetof(bgl::idl::Impostor, albedo) == 0);
	CHECK(offsetof(bgl::idl::Impostor, normalDepth) == 8);
}

TEST_CASE("a geom names no impostor until one is uploaded for it", "[impostor][idl]")
{
	CHECK(bgl::idl::Geom().impostor.Null());
	CHECK(bgl::idl::Geom().impostorMinPixels == 0.0f);
}

TEST_CASE("forcing the impostor tier is no level and not the unforced value", "[impostor][idl]")
{
	CHECK(bgl::idl::cLodForceImpostor != bgl::idl::cLodForceNone);
	CHECK(bgl::idl::cLodForceImpostor >= bgl::cMaxMeshLods);
}

TEST_CASE("the impostor's visibility bit is one no other draw sets", "[impostor][idl]")
{
	constexpr uint32_t c_Others = bgl::idl::cVisibleCurrentBit | bgl::idl::cVisibleOutgoingBit |
	                              bgl::idl::cVisibleDissolvingBit;
	CHECK((bgl::idl::cVisibleImpostorBit & c_Others) == 0u);
	CHECK((bgl::idl::cVisibleImpostorBit & (bgl::idl::cVisibleImpostorBit - 1u)) == 0u);
}
