#include <assetlib/AssetStore.h>
#include <assetlib/codecs.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/ImageData.h>

#include "bmesh_texture.h"
#include <assetlib_structs/VkFormat.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "MountAt.h"
#include "mounted_io.h"

using namespace assetlib;

// ADR-7 of the surface-material-panel plan: a surface slot composited from channel routes across
// two textures, in the shape that motivated it -- angelica's ORM, AO in one map's R and
// roughness/metallic in another's G/B, with no single texture to bind whole.

namespace
{
	// A scratch directory that cleans up after itself.
	struct BakeDir
	{
		std::filesystem::path path;

		explicit BakeDir(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}
		~BakeDir() { std::filesystem::remove_all(path); }
	};

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2 whose every texel is `rgba`.
	void
	WriteSource(const std::filesystem::path& path, uint32_t size, std::array<uint8_t, 4> rgba)
	{
		std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4);
		for (size_t t = 0; t < static_cast<size_t>(size) * size; ++t)
			for (size_t c = 0; c < 4; ++c) pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);

		writeKTX2(rgba8ToImage(pixels, size, size), path, false, Ktx2Compression::kNone);
	}

	// The angelica shape: AO from ao.ktx2's R, roughness and metallic from mr.ktx2's G and B,
	// alpha unrouted. `ao.ktx2` is 16x16 and `mr.ktx2` 8x8, so the composite also proves the map
	// sizes to its largest source.
	BMaterial
	RoutedMaterial()
	{
		BMaterial mat;
		mat.shadingModel = ShadingModel::kPbrSurface;
		mat.surface.name = "Rim";

		auto& baseColor   = mat.surface.textures.emplace_back();
		baseColor.name    = "baseColor";
		baseColor.texture = "albedo.ktx2";

		auto& orm     = mat.surface.textures.emplace_back();
		orm.name      = "orm";
		orm.routes[0] = { "ao.ktx2", 0 };
		orm.routes[1] = { "mr.ktx2", 1 };
		orm.routes[2] = { "mr.ktx2", 2 };
		return mat;
	}

	void
	WriteRoutedSources(const std::filesystem::path& root)
	{
		WriteSource(root / "albedo.ktx2", 4, { { 255, 0, 255, 255 } });
		WriteSource(root / "ao.ktx2", 16, { { 200, 7, 9, 255 } });
		WriteSource(root / "mr.ktx2", 8, { { 3, 60, 90, 255 } });
	}
}

TEST_CASE(
	"a routed slot bakes to one packed BC7 map and the whole binding stays untouched",
	"[bmaterial][bake][surface]")
{
	const BakeDir dir("bernini_surface_slot_bake");
	WriteRoutedSources(dir.path);

	BMaterial mat = RoutedMaterial();
	REQUIRE_NOTHROW(StoreAt(dir.path).BakeMaterial(mat));

	const SurfaceTextureBinding& baseColor = mat.surface.textures[0];
	CHECK(baseColor.texture == "albedo.ktx2");
	CHECK(baseColor.baked.empty());
	CHECK(baseColor.bakeToken == 0);

	const SurfaceTextureBinding& orm = mat.surface.textures[1];
	REQUIRE_FALSE(orm.baked.empty());
	CHECK(orm.baked.find("slot_") != std::string::npos);
	CHECK(orm.bakeToken != 0);

	// Linear data, block-compressed: the format ADR-7 fixes for a slot's map.
	CHECK(loadKTX2(dir.path / orm.baked).vkFormat == VkFormat::BC7_UNORM_BLOCK);

	SECTION("and the bake is not stale, however many times it is asked")
	{
		CHECK_FALSE(StoreAt(dir.path).BakeIsStale(mat));

		BMaterial again = mat;
		REQUIRE_NOTHROW(StoreAt(dir.path).BakeMaterial(again));
		CHECK(again.surface.textures[1].baked == orm.baked);
	}

	SECTION("an edited source reports the bake stale, and rebaking renames the map")
	{
		WriteSource(dir.path / "ao.ktx2", 16, { { 10, 7, 9, 255 } });
		CHECK(StoreAt(dir.path).BakeIsStale(mat));
		CHECK(StoreAt(dir.path).SurfaceSlotBakeIsStale(mat.surface.textures[1]));

		BMaterial again = mat;
		REQUIRE_NOTHROW(StoreAt(dir.path).BakeMaterial(again));
		CHECK(again.surface.textures[1].baked != orm.baked);
		CHECK_FALSE(StoreAt(dir.path).BakeIsStale(again));
	}

	SECTION("a slot bound whole is never stale -- there is no bake to have gone")
	{
		CHECK_FALSE(StoreAt(dir.path).SurfaceSlotBakeIsStale(mat.surface.textures[0]));
	}

	SECTION("de-routing the slot clears its provenance and its map")
	{
		// The last routed slot, deliberately: the re-bake then has nothing to stamp at all, which
		// is the path that once skipped this bookkeeping.
		BMaterial rerouted = mat;
		for (ChannelRoute& route : rerouted.surface.textures[1].routes) route = {};

		REQUIRE_NOTHROW(StoreAt(dir.path).BakeMaterial(rerouted));

		const SurfaceTextureBinding& cleared = rerouted.surface.textures[1];
		CHECK(cleared.baked.empty());
		CHECK(cleared.bakeToken == 0);
		CHECK(cleared.routeStamps[0] == SourceStamp{});

		// Nothing left of the object form: the document collapses back to the shorthand.
		const auto        bytes = AssetCodec<BMaterial>::Serialize(rerouted);
		const std::string out(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		CHECK(out.find("\"routes\"") == std::string::npos);
		CHECK(out.find("slot_") == std::string::npos);
	}
}

TEST_CASE(
	"ComposeSurfaceSlot lands each channel where its route said",
	"[bmaterial][bake][surface]")
{
	const BakeDir dir("bernini_surface_slot_compose");
	WriteRoutedSources(dir.path);

	const BMaterial mat = RoutedMaterial();
	const ImageData map = StoreAt(dir.path).ComposeSurfaceSlot(mat, "orm");

	// Sized to the largest routed source, not the smallest.
	CHECK(map.width == 16);
	CHECK(map.height == 16);
	CHECK(map.vkFormat == VkFormat::R8G8B8A8_UNORM);

	// R takes ao.R, G takes mr.G, B takes mr.B; the unrouted A is white, so the factor alone
	// drives it. Flat sources survive the 8->16 resample exactly.
	REQUIRE_FALSE(map.subresources.empty());
	const std::byte* texel = map.pixels.data() + map.subresources.front().offset;
	CHECK(std::to_integer<uint8_t>(texel[0]) == 200);
	CHECK(std::to_integer<uint8_t>(texel[1]) == 60);
	CHECK(std::to_integer<uint8_t>(texel[2]) == 90);
	CHECK(std::to_integer<uint8_t>(texel[3]) == 255);

	SECTION("a slot that routes nothing, or is not declared, is the caller's error")
	{
		CHECK_THROWS_AS(StoreAt(dir.path).ComposeSurfaceSlot(mat, "baseColor"), std::runtime_error);
		CHECK_THROWS_AS(
			StoreAt(dir.path).ComposeSurfaceSlot(mat, "no-such-slot"),
			std::runtime_error);
	}
}

TEST_CASE("a routed slot round-trips through the document", "[bmaterial][surface]")
{
	const BakeDir dir("bernini_surface_slot_roundtrip");
	WriteRoutedSources(dir.path);

	BMaterial mat = RoutedMaterial();
	REQUIRE_NOTHROW(StoreAt(dir.path).BakeMaterial(mat));

	const auto        bytes = AssetCodec<BMaterial>::Serialize(mat);
	const std::string out(reinterpret_cast<const char*>(bytes.data()), bytes.size());

	// The whole binding keeps the shorthand every pre-ADR-7 document used; only the routed slot
	// grows the object form.
	CHECK(out.find("\"baseColor\": \"albedo.ktx2\"") != std::string::npos);
	CHECK(out.find("\"routes\"") != std::string::npos);

	const BMaterial back = AssetCodec<BMaterial>::Deserialize(bytes);
	REQUIRE(back.surface.textures.size() == 2);

	const SurfaceTextureBinding& orm = back.surface.textures[1];
	CHECK(orm.routes[0].texture == "ao.ktx2");
	CHECK(orm.routes[0].channel == 0);
	CHECK(orm.routes[1].texture == "mr.ktx2");
	CHECK(orm.routes[1].channel == 1);
	CHECK(orm.routes[2].texture == "mr.ktx2");
	CHECK(orm.routes[2].channel == 2);
	CHECK(orm.routes[3].texture.empty());
	CHECK(orm.routeStamps[0] == mat.surface.textures[1].routeStamps[0]);
	CHECK(orm.baked == mat.surface.textures[1].baked);
	CHECK(orm.bakeToken == mat.surface.textures[1].bakeToken);

	// And the load-side verdicts read the same off the round-tripped struct.
	CHECK_FALSE(StoreAt(dir.path).BakeIsStale(back));
	CHECK_FALSE(StoreAt(dir.path).DrawsLoose(back));
}

TEST_CASE("unknown keys inside a routed slot survive the round-trip", "[bmaterial][surface]")
{
	// A sibling branch's field inside a slot object or one of its routes -- the same rule the
	// PBR `routes`/`baked` nesting keeps, now that a `textures` member can nest.
	const std::string_view text = R"({
	"shadingModel": "pbrSurface",
	"surface": "Rim",
	"textures": {
		"baseColor": "albedo.ktx2",
		"orm": {
			"futureSlotKey": true,
			"routes": { "r": { "texture": "ao.ktx2", "channel": 0, "futureRouteKey": 7 } }
		}
	}
}
)";

	const BMaterial material =
		AssetCodec<BMaterial>::Deserialize(std::as_bytes(std::span(text.data(), text.size())));
	REQUIRE(material.surface.textures.size() == 2);
	CHECK(material.surface.textures[1].routes[0].texture == "ao.ktx2");

	const auto        resaved = AssetCodec<BMaterial>::Serialize(material);
	const std::string out(reinterpret_cast<const char*>(resaved.data()), resaved.size());
	CHECK(out.find("\"futureSlotKey\"") != std::string::npos);
	CHECK(out.find("\"futureRouteKey\"") != std::string::npos);
	CHECK(out.find("\"baseColor\": \"albedo.ktx2\"") != std::string::npos);

	// And the round of the round-trip: the second read still holds everything together.
	const BMaterial again = AssetCodec<BMaterial>::Deserialize(resaved);
	CHECK(again.surface.textures[1].routes[0].texture == "ao.ktx2");
	CHECK(AssetCodec<BMaterial>::Serialize(again) == resaved);
}

TEST_CASE(
	"stripAuthoringData strips a baked slot's routes and refuses an unbaked one",
	"[bmaterial][surface]")
{
	const BakeDir dir("bernini_surface_slot_strip");
	WriteRoutedSources(dir.path);

	BMaterial unbaked = RoutedMaterial();
	CHECK_THROWS_AS(stripAuthoringData(unbaked), std::runtime_error);

	// Refused whole: the material must come out untouched, not half-stripped.
	CHECK(slotIsRouted(unbaked.surface.textures[1]));

	BMaterial baked = RoutedMaterial();
	REQUIRE_NOTHROW(StoreAt(dir.path).BakeMaterial(baked));
	REQUIRE_NOTHROW(stripAuthoringData(baked));

	const SurfaceTextureBinding& orm = baked.surface.textures[1];
	CHECK_FALSE(slotIsRouted(orm));
	CHECK(orm.routeStamps[0] == SourceStamp{});
	CHECK_FALSE(orm.baked.empty());
	CHECK(baked.surface.textures[0].texture == "albedo.ktx2");
}
