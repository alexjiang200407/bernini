#include <assetlib/asset_import.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <core/hash.h>

#include <catch2/catch_approx.hpp>

#include "MountAt.h"
#include "mounted_io.h"

#include <assetlib/AssetStore.h>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;

TEST_CASE("a BMaterial survives a serialize round-trip", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                 = "brushed_metal";
	mat.pbr.baseColorTexture = "albedo.ktx2";
	mat.pbr.normalTexture    = "";  // absent
	mat.pbr.ormTexture       = "orm.ktx2";
	mat.pbr.baseColorFactor  = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
	mat.pbr.metallicFactor   = 0.75f;
	mat.pbr.roughnessFactor  = 0.25f;

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.name == mat.name);
	REQUIRE(restored.shadingModel == ShadingModel::kPbr);
	REQUIRE(restored.pbr.baseColorTexture == mat.pbr.baseColorTexture);
	REQUIRE(restored.pbr.normalTexture.empty());
	REQUIRE(restored.pbr.ormTexture == mat.pbr.ormTexture);
	REQUIRE(restored.pbr.baseColorFactor.r == Catch::Approx(0.1f));
	REQUIRE(restored.pbr.baseColorFactor.a == Catch::Approx(1.0f));
	REQUIRE(restored.pbr.metallicFactor == Catch::Approx(0.75f));
	REQUIRE(restored.pbr.roughnessFactor == Catch::Approx(0.25f));
}

// Transmission is what separates a lens from a hair card, and both are AlphaMode::kBlend -- so a
// factor lost in the container is a material that reloads as the wrong one of the two, with the
// alpha mode still agreeing and nothing to say which was meant.
TEST_CASE("a blend material's transmission survives a round trip", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                   = "lens";
	mat.pbr.alphaMode          = AlphaMode::kBlend;
	mat.pbr.transmissionFactor = 0.85f;

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	CHECK(restored.pbr.alphaMode == AlphaMode::kBlend);
	CHECK(restored.pbr.transmissionFactor == Catch::Approx(0.85f));

	// The default is what every material baked before the factor re-bakes to, and it is the reading
	// blend has always had.
	BMaterial coverage;
	coverage.pbr.alphaMode = AlphaMode::kBlend;
	CHECK(deserializeMaterial(serializeMaterial(coverage)).pbr.transmissionFactor == 0.0f);
}

TEST_CASE("a material's specular factors survive a round trip", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                    = "fur";
	mat.pbr.specularFactor      = 0.0f;
	mat.pbr.specularColorFactor = glm::vec3(1.0f, 0.77f, 0.34f);

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	// 0 is the value the whole feature exists to carry, and it is also the value a zero-initialized
	// read would produce by accident -- so the tint is what proves the field was really stored.
	CHECK(restored.pbr.specularFactor == 0.0f);
	CHECK(restored.pbr.specularColorFactor.g == Catch::Approx(0.77f));
	CHECK(restored.pbr.specularColorFactor.b == Catch::Approx(0.34f));

	BMaterial  plain;
	const auto defaulted = deserializeMaterial(serializeMaterial(plain));
	CHECK(defaulted.pbr.specularFactor == 1.0f);
	CHECK(defaulted.pbr.specularColorFactor == glm::vec3(1.0f));
}

TEST_CASE("a Loose BMaterial round-trips its routes", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                = "packed";
	mat.pbr.metallicFactor  = 0.5f;
	mat.pbr.roughnessFactor = 0.4f;
	mat.pbr.routes[0]       = { "albedo.ktx2", 0 };  // base color R
	mat.pbr.routes[1]       = { "albedo.ktx2", 1 };  // base color G
	mat.pbr.routes[5]       = { "packed.ktx2", 2 };  // roughness from packed.B
	mat.pbr.routes[7]       = { "normal.ktx2", 0 };  // normal X

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.pbr.routes.size() == c_LooseChannelCount);
	REQUIRE(restored.pbr.routes[0].texture == "albedo.ktx2");
	REQUIRE(restored.pbr.routes[0].channel == 0);
	REQUIRE(restored.pbr.routes[1].channel == 1);
	REQUIRE(restored.pbr.routes[5].texture == "packed.ktx2");
	REQUIRE(restored.pbr.routes[5].channel == 2);
	REQUIRE(restored.pbr.routes[7].texture == "normal.ktx2");
	REQUIRE(restored.pbr.routes[2].texture.empty());  // an unrouted channel stays empty
	REQUIRE(restored.pbr.metallicFactor == Catch::Approx(0.5f));
}

TEST_CASE("a BMaterial round-trips its editor graph", "[bmaterial][io]")
{
	// The graph is an opaque blob to assetlib: it must survive byte-for-byte, embedded quotes,
	// braces, newlines and all.
	BMaterial mat;
	mat.name        = "graphed";
	mat.editorGraph = R"({"nodes":[{"id":0,"internal-data":{"model-name":"Texture"}}],"c":[]})"
					  "\n{\"trailing\":\"line\"}";

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.editorGraph == mat.editorGraph);
	REQUIRE(restored.name == "graphed");
}

TEST_CASE("a BMaterial with no editor graph round-trips an empty one", "[bmaterial][io]")
{
	// The exported/baked form: the authoring graph has been stripped.
	BMaterial mat;
	mat.pbr.baseColorTexture = "baked_basecolor.ktx2";

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.editorGraph.empty());
	REQUIRE(restored.pbr.baseColorTexture == "baked_basecolor.ktx2");
}

TEST_CASE("a BMaterial round-trips its bake provenance", "[bmaterial][io]")
{
	BMaterial mat;
	mat.pbr.routes[0]      = { "albedo.ktx2", 0 };
	mat.pbr.routeStamps[0] = { 4096, 0x0123456789abcdefull };
	// The hash uses the whole 64-bit range: one with the top bit set must not be sign-mangled on
	// the way through, which is what the field being unsigned buys.
	mat.pbr.routeStamps[8] = { 1, 0xffffffffffffffffull };

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.pbr.routeStamps[0].size == 4096);
	REQUIRE(restored.pbr.routeStamps[0].hash == 0x0123456789abcdefull);
	REQUIRE(restored.pbr.routeStamps[8].hash == 0xffffffffffffffffull);
	REQUIRE(restored.pbr.routeStamps[3] == SourceStamp{});  // unstamped routes stay zeroed
}

TEST_CASE("a BMaterial carries both its sources and its baked triplet", "[bmaterial][io]")
{
	// The coexistence the format exists for: the bake fills the triplet without discarding the
	// routes that produced it, so the material can still be reopened and re-baked.
	BMaterial mat;
	mat.pbr.baseColorTexture = "mat_basecolor.ktx2";
	mat.pbr.ormTexture       = "mat_orm.ktx2";
	mat.pbr.routes[0]        = { "src/albedo.ktx2", 0 };
	mat.pbr.routeStamps[0]   = { 64, 7 };

	const auto restored = deserializeMaterial(serializeMaterial(mat));

	REQUIRE(restored.pbr.baseColorTexture == "mat_basecolor.ktx2");
	REQUIRE(restored.pbr.routes[0].texture == "src/albedo.ktx2");
	REQUIRE(restored.pbr.routeStamps[0].size == 64);
}

TEST_CASE("stampOf measures a file and zeroes a missing one", "[bmaterial][bake]")
{
	const auto dir = std::filesystem::temp_directory_path() / "bernini_stamp_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	const auto write = [](const std::filesystem::path& path, std::string_view bytes) {
		std::ofstream out(path, std::ios::binary);
		out << bytes;
	};

	const auto file = dir / "src.bin";
	write(file, "hello");

	const SourceStamp stamp = stampOf(file);
	REQUIRE(stamp.size == 5);
	REQUIRE(stamp.hash == core::hash_bytes("hello", 5, core::hash_seed()));
	REQUIRE(stampOf(file) == stamp);  // stable across calls

	REQUIRE(stampOf(dir / "absent.bin") == SourceStamp{});

	// Two files with the same bytes stamp identically, wherever they sit: the stamp is a statement
	// about content and nothing else. This is what makes a bake reproducible across machines.
	const auto copy = dir / "elsewhere.bin";
	write(copy, "hello");
	REQUIRE(stampOf(copy) == stamp);

	std::filesystem::remove_all(dir);
}

// The bug the content stamp exists for. A git pull or checkout rewrites source mtimes without
// changing a byte; a stamp that noticed would report every bake stale and re-bake it, rewriting
// containers that are tracked in git.
TEST_CASE("A source whose mtime moved but whose bytes did not is not stale", "[bmaterial][bake]")
{
	const auto dir = std::filesystem::temp_directory_path() / "bernini_stamp_mtime_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	const auto write = [](const std::filesystem::path& path, std::string_view bytes) {
		std::ofstream out(path, std::ios::binary);
		out << bytes;
	};

	const auto source = dir / "albedo.ktx2";
	write(source, "aaaa");
	write(dir / "mat_basecolor.ktx2", "bbbb");

	BMaterial mat;
	mat.pbr.baseColorTexture = "mat_basecolor.ktx2";
	mat.pbr.routes[0]        = { "albedo.ktx2", 0 };
	mat.pbr.routeStamps[0]   = stampOf(source);

	REQUIRE_FALSE(bakeIsStale(mat, MountAt(dir)));

	std::filesystem::last_write_time(
		source,
		std::filesystem::last_write_time(source) + std::chrono::seconds(5));

	REQUIRE(stampOf(source) == mat.pbr.routeStamps[0]);
	REQUIRE_FALSE(bakeIsStale(mat, MountAt(dir)));
	REQUIRE_FALSE(drawsLoose(mat, MountAt(dir)));

	// The other half of the same rule: content that did change is still caught, even at the same
	// size, where an mtime stamp with one-second granularity could miss it.
	write(source, "aaab");
	std::filesystem::last_write_time(
		source,
		std::filesystem::last_write_time(source) + std::chrono::seconds(5));

	REQUIRE(bakeIsStale(mat, MountAt(dir)));

	std::filesystem::remove_all(dir);
}

TEST_CASE("bakeIsStale compares routed sources against their stamps", "[bmaterial][bake]")
{
	const auto dir = std::filesystem::temp_directory_path() / "bernini_stale_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	const auto write = [](const std::filesystem::path& path, std::string_view bytes) {
		std::ofstream out(path, std::ios::binary);
		out << bytes;
	};

	const auto source = dir / "albedo.ktx2";
	const auto baked  = dir / "mat_basecolor.ktx2";
	write(source, "aaaa");
	write(baked, "bbbb");

	BMaterial mat;
	mat.pbr.baseColorTexture = "mat_basecolor.ktx2";
	mat.pbr.routes[0]        = { "albedo.ktx2", 0 };

	SECTION("a material with no routes is never stale")
	{
		BMaterial imported;
		imported.pbr.baseColorTexture = "tex0.ktx2";
		REQUIRE_FALSE(bakeIsStale(imported, MountAt(dir)));
	}

	SECTION("routed but unstamped means it was never baked")
	{
		REQUIRE(bakeIsStale(mat, MountAt(dir)));
	}

	SECTION("a matching stamp is fresh")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		REQUIRE_FALSE(bakeIsStale(mat, MountAt(dir)));
	}

	SECTION("a source that changed size is stale")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		write(source, "aaaaaaaa");  // different size
		REQUIRE(bakeIsStale(mat, MountAt(dir)));
	}

	SECTION("a deleted baked map is stale, however fresh the sources are")
	{
		// The reason the draw-from choice is derived rather than stored: a material that claimed its
		// triplet here would bind the default white 1x1 instead, which on a cutout is a solid
		// silhouette rather than a visible error.
		mat.pbr.routeStamps[0] = stampOf(source);
		std::filesystem::remove(baked);
		REQUIRE(bakeIsStale(mat, MountAt(dir)));
	}

	SECTION("a deleted source is stale, not silently unchanged")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		std::filesystem::remove(source);
		REQUIRE(bakeIsStale(mat, MountAt(dir)));
	}

	SECTION("fresh sources but no bake output is stale")
	{
		mat.pbr.routeStamps[0]   = stampOf(source);
		mat.pbr.baseColorTexture = "";
		REQUIRE(bakeIsStale(mat, MountAt(dir)));
	}

	std::filesystem::remove_all(dir);
}

TEST_CASE("drawsLoose falls back to routes only when they are there", "[bmaterial][bake]")
{
	const auto dir = std::filesystem::temp_directory_path() / "bernini_draws_loose_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	const auto write = [](const std::filesystem::path& path, std::string_view bytes) {
		std::ofstream out(path, std::ios::binary);
		out << bytes;
	};

	const auto source = dir / "albedo.ktx2";
	const auto baked  = dir / "mat_basecolor.ktx2";
	write(source, "aaaa");
	write(baked, "bbbb");

	BMaterial mat;
	mat.pbr.baseColorTexture = "mat_basecolor.ktx2";
	mat.pbr.routes[0]        = { "albedo.ktx2", 0 };

	SECTION("a current bake draws its triplet")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		REQUIRE_FALSE(drawsLoose(mat, MountAt(dir)));
	}

	SECTION("an edited source draws the routes it drifted from")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		write(source, "aaaaaaaa");
		REQUIRE(drawsLoose(mat, MountAt(dir)));
	}

	SECTION("never baked draws its routes")
	{
		REQUIRE(drawsLoose(mat, MountAt(dir)));  // no stamp
	}

	// The regression: a shipped material keeps the routes it was composited from, but not the sources
	// themselves. Stale is the right rebake verdict; loose is not a thing it could draw.
	SECTION("a deleted source keeps the triplet rather than naming a file that is gone")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		std::filesystem::remove(source);

		REQUIRE(bakeIsStale(mat, MountAt(dir)));
		REQUIRE_FALSE(drawsLoose(mat, MountAt(dir)));
	}

	SECTION("a deleted baked map still draws its routes while they are readable")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		std::filesystem::remove(baked);
		REQUIRE(drawsLoose(mat, MountAt(dir)));
	}

	SECTION("neither representation readable falls back to the triplet")
	{
		mat.pbr.routeStamps[0] = stampOf(source);
		std::filesystem::remove(source);
		std::filesystem::remove(baked);
		REQUIRE_FALSE(drawsLoose(mat, MountAt(dir)));
	}

	std::filesystem::remove_all(dir);
}

TEST_CASE("deserializeMaterial rejects every version but the current one", "[bmaterial][io]")
{
	// Exactly one version is readable, deliberately: nothing has shipped, so an out-of-date file is
	// re-cooked rather than decoded by a second reader that would have to be kept correct forever. The
	// check has to be loud, because the alternative to rejecting an old stream is not "it still works"
	// -- it is reading its bytes with the current layout and producing a material made of garbage.
	//
	// v5 is named explicitly: it is the layout v6 replaced, and every asset in the repo used to be one.
	const auto streamOfVersion = [](uint16_t version) {
		std::vector<std::byte> bytes;
		const auto             putPod = [&](auto value) {
			const auto* p = reinterpret_cast<const std::byte*>(&value);
			bytes.insert(bytes.end(), p, p + sizeof(value));
		};
		const auto putStr = [&](const std::string& s) {
			putPod(static_cast<uint32_t>(s.size()));
			const auto* p = reinterpret_cast<const std::byte*>(s.data());
			bytes.insert(bytes.end(), p, p + s.size());
		};

		putPod(static_cast<uint32_t>(0x54414D42u));  // magic
		putPod(version);                             // versionMajor
		putPod(static_cast<uint16_t>(0));            // versionMinor

		// A plausible old body. It should never be decoded, whatever it holds.
		putPod(glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));
		putPod(0.6f);
		putPod(0.7f);
		putStr("old");
		putStr("base.ktx2");
		putStr("");
		putStr("orm.ktx2");

		return bytes;
	};

	REQUIRE_THROWS_AS(deserializeMaterial(streamOfVersion(1)), std::runtime_error);
	REQUIRE_THROWS_AS(deserializeMaterial(streamOfVersion(5)), std::runtime_error);
	REQUIRE_THROWS_AS(deserializeMaterial(streamOfVersion(7)), std::runtime_error);
}

TEST_CASE("deserializeMaterial rejects an unknown shading model", "[bmaterial][io]")
{
	// A tag the reader does not know is a payload it cannot decode: rejected, never guessed at.
	std::vector<std::byte> bytes;
	const auto             putPod = [&](auto value) {
		const auto* p = reinterpret_cast<const std::byte*>(&value);
		bytes.insert(bytes.end(), p, p + sizeof(value));
	};

	putPod(static_cast<uint32_t>(0x54414D42u));  // magic
	putPod(static_cast<uint16_t>(6));            // the current version...
	putPod(static_cast<uint16_t>(0));
	putPod(static_cast<uint32_t>(999));  // ...but a shading model from the future

	REQUIRE_THROWS_AS(deserializeMaterial(bytes), std::runtime_error);
}

TEST_CASE("deserializeMaterial rejects a stream with bad magic", "[bmaterial][io]")
{
	const std::array<std::byte, 8> garbage{};
	REQUIRE_THROWS_AS(deserializeMaterial(garbage), std::runtime_error);
}

TEST_CASE("saveMaterial / loadMaterial round-trips through a file", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                 = "leaf";
	mat.pbr.baseColorTexture = "tex0.ktx2";
	mat.pbr.baseColorFactor  = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
	mat.pbr.metallicFactor   = 0.0f;
	mat.pbr.roughnessFactor  = 0.9f;

	const auto path = std::filesystem::temp_directory_path() / "bmaterial_roundtrip_test.bmaterial";
	SaveAt(mat, path);
	const auto restored = LoadAt<BMaterial>(path);
	std::filesystem::remove(path);

	REQUIRE(restored.name == "leaf");
	REQUIRE(restored.shadingModel == ShadingModel::kPbr);
	REQUIRE(restored.pbr.baseColorTexture == "tex0.ktx2");
	REQUIRE(restored.pbr.normalTexture.empty());
	REQUIRE(restored.pbr.roughnessFactor == Catch::Approx(0.9f));
}

TEST_CASE("an import writes a loadable .bmesh and its textures, and no materials", "[bmesh][bake]")
{
	const std::filesystem::path glb = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(glb));

	const auto import = loadFromGltf(glb);
	REQUIRE(
		import.materials.size() >= 1);  // the glTF has materials; the bake must not carry them over

	const auto outDir = std::filesystem::temp_directory_path() / "bake_suzanne_test";
	std::filesystem::remove_all(outDir);

	// The import sequence an importer runs, minus the materials step neither runtime writes here.
	writeTextures(import, outDir);
	BMesh baked = toBMesh(import);
	static_cast<void>(generateTangents(baked));
	writeImportedMesh(AssetStore(outDir), baked, "suzanne.bmesh");

	REQUIRE(std::filesystem::exists(outDir / "suzanne.bmesh"));

	// The textures do come across: they are what a material, once authored, routes at.
	for (size_t i = 0; i < import.textures.size(); ++i)
		REQUIRE(std::filesystem::exists(outDir / ("tex" + std::to_string(i) + ".ktx2")));

	// The glTF's PBR materials do not. Nothing is written for them, and -- this is the part that used
	// to be wrong -- the container does not name files that were never written: every submesh comes out
	// unassigned rather than pointing at a matN.bmaterial that does not exist.
	REQUIRE_FALSE(std::filesystem::exists(outDir / "mat0.bmaterial"));

	const auto mesh = StoreAt(outDir).Load<BMesh>("suzanne.bmesh");
	REQUIRE(mesh.materials.empty());
	REQUIRE_FALSE(mesh.submeshes.empty());

	for (const Submesh& submesh : mesh.submeshes) REQUIRE(submesh.material == c_InvalidIndex);

	// suzanne.glb carries normals and UVs but no tangents, which is the ordinary case for a DCC
	// export -- and a mesh that reaches disk without one renders its normal map as nothing at all.
	// The bake derives it, so this is what stops the two importers drifting apart on that.
	for (const Submesh& submesh : mesh.submeshes) REQUIRE(hasTangent(submesh));

	std::filesystem::remove_all(outDir);
}

TEST_CASE("attachMaterial binds a material to an imported submesh", "[bmesh][bmaterial][attach]")
{
	// The other half of the contract: an import leaves the submeshes unassigned, and this is how they
	// get a material -- what the material editor calls when a material is saved.
	const std::filesystem::path glb = "assets/suzanne.glb";
	REQUIRE(std::filesystem::exists(glb));

	auto mesh = toBMesh(loadFromGltf(glb));
	REQUIRE(mesh.materials.empty());
	REQUIRE_FALSE(mesh.submeshes.empty());

	REQUIRE(attachMaterial(mesh, 0, "Materials/suzanne.bmaterial"));

	REQUIRE(mesh.materials.size() == 1);
	REQUIRE(mesh.materials[0] == "Materials/suzanne.bmaterial");
	REQUIRE(mesh.submeshes[0].material == 0);

	// Attaching the same material again is a no-op, not a duplicate slot.
	REQUIRE_FALSE(attachMaterial(mesh, 0, "Materials/suzanne.bmaterial"));
	REQUIRE(mesh.materials.size() == 1);
}

TEST_CASE("a material document is canonical text", "[bmaterial][io]")
{
	BMaterial mat;
	mat.name                 = "canon";
	mat.pbr.baseColorTexture = "Textures/canon_basecolor.ktx2";

	const auto once  = serializeMaterial(mat);
	const auto again = serializeMaterial(deserializeMaterial(once));

	// One document, one byte sequence: two checkouts that agree on the content agree on the
	// file, which is what lets git merge it like code.
	CHECK(once == again);
	REQUIRE_FALSE(once.empty());
	CHECK(static_cast<char>(once.front()) == '{');
	CHECK(static_cast<char>(once.back()) == '\n');
}

TEST_CASE("a material document preserves the keys this build does not know", "[bmaterial][io]")
{
	const std::string_view text = R"({
	"name": "future",
	"sheenFactor": 0.25,
	"shadingModel": "pbr"
}
)";

	const BMaterial material =
		deserializeMaterial(std::as_bytes(std::span(text.data(), text.size())));
	CHECK(material.name == "future");

	// The unknown key rides extraJson through the round-trip -- a sibling branch's field
	// survives a reader that has never heard of it.
	const auto        resaved = serializeMaterial(material);
	const std::string out(reinterpret_cast<const char*>(resaved.data()), resaved.size());
	CHECK(out.find("\"sheenFactor\"") != std::string::npos);
}

TEST_CASE("a minimal hand-authored document defaults what it omits", "[bmaterial][io]")
{
	const std::string_view text = "{\n\t\"shadingModel\": \"pbr\"\n}\n";

	const BMaterial material =
		deserializeMaterial(std::as_bytes(std::span(text.data(), text.size())));
	CHECK(material.pbr.baseColorFactor == glm::vec4(1.0f));
	CHECK(material.pbr.metallicFactor == 1.0f);
	CHECK(material.pbr.alphaMode == AlphaMode::kOpaque);
	CHECK(material.pbr.baseColorTexture.empty());
	CHECK(material.editorGraph.empty());
}

TEST_CASE("a document naming an unknown enum is refused, never defaulted", "[bmaterial][io]")
{
	const std::string_view alpha = R"({"alphaMode": "translucent"})";
	CHECK_THROWS_WITH(
		deserializeMaterial(std::as_bytes(std::span(alpha.data(), alpha.size()))),
		Catch::Matchers::ContainsSubstring("translucent"));

	const std::string_view model = R"({"shadingModel": "toon"})";
	CHECK_THROWS_WITH(
		deserializeMaterial(std::as_bytes(std::span(model.data(), model.size()))),
		Catch::Matchers::ContainsSubstring("toon"));
}

TEST_CASE("unknown keys survive at every depth, the editor's save included", "[bmaterial][io]")
{
	// A sibling branch's field inside a route or the baked triplet, not just at the top level --
	// dropping it on merge is the loss the document format exists to prevent.
	const std::string_view text = R"({
	"baked": { "baseColor": "Textures/b.ktx2", "sheenMap": "Textures/s.ktx2" },
	"routes": { "ao": { "texture": "textures_src/ao.png", "blurRadius": 2 } },
	"shadingModel": "pbr"
}
)";

	const BMaterial material =
		deserializeMaterial(std::as_bytes(std::span(text.data(), text.size())));
	CHECK(material.pbr.baseColorTexture == "Textures/b.ktx2");
	CHECK(material.pbr.routes[4].texture == "textures_src/ao.png");

	const auto        resaved = serializeMaterial(material);
	const std::string out(reinterpret_cast<const char*>(resaved.data()), resaved.size());
	CHECK(out.find("\"sheenMap\"") != std::string::npos);
	CHECK(out.find("\"blurRadius\"") != std::string::npos);

	// And the round of the round-trip: the second read still holds both halves together.
	const BMaterial again = deserializeMaterial(resaved);
	CHECK(again.pbr.routes[4].texture == "textures_src/ao.png");
	CHECK(serializeMaterial(again) == resaved);
}

TEST_CASE("a corrupt extraJson refuses the save rather than writing half a file", "[bmaterial][io]")
{
	BMaterial material;
	material.extraJson = "not json";
	CHECK_THROWS_WITH(serializeMaterial(material), Catch::Matchers::ContainsSubstring("extraJson"));
}

TEST_CASE("a preserved route outlives the channel it decorated", "[bmaterial][io]")
{
	// The struct routes nothing on this channel, so only the sibling branch's key is there --
	// the one branch of the merge where an edit could lose data without a test noticing.
	const std::string_view text = R"({
	"routes": { "ao": { "blurRadius": 2 } },
	"shadingModel": "pbr"
}
)";

	const BMaterial material =
		deserializeMaterial(std::as_bytes(std::span(text.data(), text.size())));
	CHECK(material.pbr.routes[4].texture.empty());

	const auto        resaved = serializeMaterial(material);
	const std::string out(reinterpret_cast<const char*>(resaved.data()), resaved.size());
	CHECK(out.find("\"blurRadius\"") != std::string::npos);
	CHECK(serializeMaterial(deserializeMaterial(resaved)) == resaved);
}
