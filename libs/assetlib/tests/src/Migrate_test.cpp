#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/container_info.h>
#include <assetlib/migrate.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/magic.h>

#include "AssetSchemaBuilder.h"
#include "CacheTamper.h"
#include "ImportUnitGroup.h"
#include "SkinnedGltf.h"
#include "chunk_io.h"
#include "env_route_io.h"

#include <core/str/string_pool.h>
#include <schema/SchemaBuilder.h>

#include <catch2/catch_approx.hpp>

#include <core/file/file.h>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;
using Catch::Matchers::ContainsSubstring;

namespace
{
	struct Project
	{
		std::filesystem::path root;

		Project()
		{
			root = std::filesystem::temp_directory_path() / "assetlib_migrate_test";
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / "Materials");
		}

		~Project() { std::filesystem::remove_all(root); }

		void
		Write(const std::filesystem::path& relative, std::span<const std::byte> bytes) const
		{
			std::ofstream out(root / relative, std::ios::binary);
			out.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}

		std::vector<std::byte>
		Read(const std::filesystem::path& relative) const
		{
			return core::file::read_file_bytes((root / relative).string());
		}
	};

	std::vector<std::byte>
	MaterialBytes(std::string_view name)
	{
		BMaterial material;
		material.name = std::string(name);
		return serializeMaterial(material);
	}

	/** The same content spelled non-canonically: what a hand edit or a merge leaves behind. */
	std::vector<std::byte>
	Older()
	{
		constexpr std::string_view c_Older = R"({"shadingModel": "pbr", "name": "older"})";
		const auto                 data = std::as_bytes(std::span(c_Older.data(), c_Older.size()));
		return { data.begin(), data.end() };
	}
}

TEST_CASE(
	"migrate rewrites what is not current, leaves what is, and reports what it cannot read",
	"[migrate]")
{
	const Project project;
	project.Write("Materials/current.bmaterial", MaterialBytes("current"));
	project.Write("Materials/older.bmaterial", Older());
	const std::vector<std::byte> flat = { std::byte{ 'B' },
		                                  std::byte{ 'M' },
		                                  std::byte{ 'A' },
		                                  std::byte{ 'T' } };
	project.Write("Materials/flat.bmaterial", flat);  // a stream from before the schema chunk
	project.Write("notes.txt", flat);                 // not a container at all

	SECTION("a dry run reports and writes nothing")
	{
		const auto before = project.Read("Materials/older.bmaterial");
		const auto report = migrateProject(project.root, true);
		CHECK(report.Count(MigratedFile::Outcome::kUnchanged) == 1);
		CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 1);
		CHECK(report.Count(MigratedFile::Outcome::kFailed) == 1);
		CHECK(project.Read("Materials/older.bmaterial") == before);
		CHECK(report.files.size() == 3);  // notes.txt was never a candidate
	}

	SECTION("a real run rewrites once, and the second run finds nothing to do")
	{
		const auto first = migrateProject(project.root, false);
		CHECK(first.Count(MigratedFile::Outcome::kRewritten) == 1);
		CHECK(first.Count(MigratedFile::Outcome::kFailed) == 1);

		const auto rewritten = project.Read("Materials/older.bmaterial");
		CHECK(rewritten == MaterialBytes("older"));  // stamped current again
		CHECK(deserializeMaterial(rewritten).name == "older");

		const auto second = migrateProject(project.root, false);
		CHECK(second.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(second.Count(MigratedFile::Outcome::kUnchanged) == 2);
		CHECK(second.Count(MigratedFile::Outcome::kFailed) == 1);
		CHECK(project.Read("Materials/flat.bmaterial") == flat);  // never half-written
	}

	SECTION("the failure says which file, and why")
	{
		const auto report = migrateProject(project.root, true);
		const auto failed =
			std::ranges::find(report.files, MigratedFile::Outcome::kFailed, &MigratedFile::outcome);
		REQUIRE(failed != report.files.end());
		CHECK(failed->path.filename() == "flat.bmaterial");
		CHECK_THAT(failed->message, ContainsSubstring("bmaterial:"));
	}
}

TEST_CASE("migrate refuses a root that is not a directory", "[migrate]")
{
	REQUIRE_THROWS_WITH(
		migrateProject(std::filesystem::temp_directory_path() / "no_such_project_dir", true),
		ContainsSubstring("is not a directory"));
}

TEST_CASE(
	"a container says what it is and what it stores, without being loaded",
	"[migrate][describe]")
{
	// No live writer emits the schema format any more, so the stream is crafted -- standing in
	// for the on-disk legacy files inspect still serves until the schema system goes.
	struct Record
	{
		uint32_t a;
		float    b;
	};
	const schema::Schema schema =
		schema::SchemaBuilder()
			.AddLayout<Record>(
				"SkyRecord",
				[](auto& layout) { layout.AddField("a", &Record::a).AddField("b", &Record::b); })
			.Finish();

	chunk::Writer writer(schema);
	writer.Add(1u, std::vector<Record>{ { 7, 0.5f } });
	const auto bytes = writer.Finish(magic::c_BSky, 3, 0);

	const auto info = inspectContainer(bytes);
	CHECK(info.magic == magic::c_BSky);
	CHECK(info.schema.Find("SkyRecord") != nullptr);

	const std::string text = describe(info.schema);
	CHECK_THAT(text, ContainsSubstring("SkyRecord"));

	REQUIRE_THROWS_WITH(
		inspectContainer(std::span(bytes).first(8)),
		ContainsSubstring("stream shorter than a header"));
}

TEST_CASE("migrate regenerates a stale group on disk, once", "[migrate][regen]")
{
	const Project           project;
	const test::SkinnedGltf source("bernini_migrate_regen_gltf");
	test::ImportUnitGroup(project.root, source.PackGlb());

	const auto meshPath  = project.root / "Meshes/unit.bmesh";
	const auto bskelPath = project.root / "Skeletons/unit.bskel";
	const auto banimPath = project.root / "Animations/unit.banim";

	test::TamperHeaderByte(meshPath, test::c_TokenOffset);
	test::TamperHeaderByte(bskelPath, test::c_TokenOffset);
	test::TamperHeaderByte(banimPath, test::c_TokenOffset);

	SECTION("the replay: one run rewrites the group, the second finds nothing to do")
	{
		const auto first = migrateProject(project.root, false);
		CHECK(first.Count(MigratedFile::Outcome::kRewritten) == 3);
		CHECK(first.Count(MigratedFile::Outcome::kFailed) == 0);

		// Written current: the loads that refused the tampered files read them plainly now.
		CHECK(load(meshPath).source.key == "meshes_src/unit.glb");
		CHECK_FALSE(loadAnimations(banimPath).clips.empty());

		const auto second = migrateProject(project.root, false);
		CHECK(second.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(second.Count(MigratedFile::Outcome::kFailed) == 0);
	}

	SECTION("a stale group whose source is gone is a per-file failure, never a guess")
	{
		std::filesystem::remove(project.root / "meshes_src/unit.glb");

		const auto report = migrateProject(project.root, false);
		CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(report.Count(MigratedFile::Outcome::kFailed) == 3);
	}
}

TEST_CASE("a rebind reaches disk through migrate without a regeneration", "[migrate][regen]")
{
	const Project           project;
	const test::SkinnedGltf source("bernini_migrate_rebind_gltf");
	test::ImportUnitGroup(project.root, source.PackGlb());

	// The source is gone, so what follows cannot be a regeneration -- the document alone
	// carries the rebind onto the disk bytes.
	std::filesystem::remove(project.root / "meshes_src/unit.glb");
	rebindSubmeshInDocument(
		project.root,
		"meshes_src/unit.glb",
		"body",
		"Materials/blue.bmaterial");

	const auto report = migrateProject(project.root, false);
	CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 1);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);

	const BMesh mesh = load(project.root / "Meshes/unit.bmesh");
	REQUIRE(mesh.materials.size() == 1);
	CHECK(mesh.materials[0] == "Materials/blue.bmaterial");
}

namespace
{
	// Replicas of the chunk-era layouts the io readers keep file-local, byte-compatible by the
	// same static_asserts they carry.
	struct LegacySkyRecord
	{
		uint32_t       nameOffset;
		uint32_t       mipLevel;
		float          rotationY;
		EnvRouteRecord sky;
	};

	static_assert(sizeof(LegacySkyRecord) == 40);

	struct LegacyLightingRecord
	{
		uint32_t       nameOffset;
		float          exposure;
		uint32_t       exposureAuthored;
		float          exposureOverride;
		EnvRouteRecord prefilter;
		EnvRouteRecord irradiance;
	};

	static_assert(sizeof(LegacyLightingRecord) == 64);

	struct LegacyEnvRecord
	{
		uint32_t nameOffset;
		uint32_t skyOffset;
		uint32_t lightingOffset;
	};

	static_assert(sizeof(LegacyEnvRecord) == 12);

	EnvMapRoute
	SomeRoute(std::string_view stem)
	{
		EnvMapRoute route;
		route.source = std::format("textures_src/{}.hdr", stem);
		route.baked  = std::format("Textures/{}.ktx2", stem);
		route.stamp  = SourceStamp{ 4, 0xabc };
		return route;
	}

	std::vector<std::byte>
	LegacySkyBytes(std::string_view name, uint32_t mipLevel, float rotationY)
	{
		core::string_pool pool;
		LegacySkyRecord   record{};
		record.nameOffset = pool.add(name);
		record.mipLevel   = mipLevel;
		record.rotationY  = rotationY;
		record.sky        = packRoute(SomeRoute("sky"), pool);

		const schema::Schema schema =
			AssetSchemaBuilder()
				.AddSourceStamp()
				.AddLayout<EnvRouteRecord>("EnvMapRoute", describeEnvRoute)
				.AddLayout<LegacySkyRecord>(
					"SkyRecord",
					[](auto& layout) {
						layout.AddField("nameOffset", &LegacySkyRecord::nameOffset)
							.AddField("mipLevel", &LegacySkyRecord::mipLevel)
							.AddField("rotationY", &LegacySkyRecord::rotationY)
							.AddField("sky", &LegacySkyRecord::sky);
					})
				.Finish();
		chunk::Writer writer(schema);
		writer.Add(1u, std::vector<LegacySkyRecord>{ record });
		writer.Add(2u, pool.bytes());
		return writer.Finish(magic::c_BSky, 3, 0);
	}

	std::vector<std::byte>
	LegacyLightingBytes(std::string_view name, float exposure, std::optional<float> authored)
	{
		core::string_pool    pool;
		LegacyLightingRecord record{};
		record.nameOffset       = pool.add(name);
		record.exposure         = exposure;
		record.exposureAuthored = authored.has_value() ? 1u : 0u;
		record.exposureOverride = authored.value_or(0.0f);
		record.prefilter        = packRoute(SomeRoute("pre"), pool);
		record.irradiance       = packRoute(SomeRoute("irr"), pool);

		const schema::Schema schema =
			AssetSchemaBuilder()
				.AddSourceStamp()
				.AddLayout<EnvRouteRecord>("EnvMapRoute", describeEnvRoute)
				.AddLayout<LegacyLightingRecord>(
					"LightingRecord",
					[](auto& layout) {
						layout.AddField("nameOffset", &LegacyLightingRecord::nameOffset)
							.AddField("exposure", &LegacyLightingRecord::exposure)
							.AddField("exposureAuthored", &LegacyLightingRecord::exposureAuthored)
							.AddField("exposureOverride", &LegacyLightingRecord::exposureOverride)
							.AddField("prefilter", &LegacyLightingRecord::prefilter)
							.AddField("irradiance", &LegacyLightingRecord::irradiance);
					})
				.Finish();
		chunk::Writer writer(schema);
		writer.Add(1u, std::vector<LegacyLightingRecord>{ record });
		writer.Add(2u, pool.bytes());
		return writer.Finish(magic::c_BEnvL, 3, 0);
	}

	std::vector<std::byte>
	LegacyEnvBytes(std::string_view name, std::string_view sky, std::string_view lighting)
	{
		core::string_pool pool;
		LegacyEnvRecord   record{};
		record.nameOffset     = pool.add(name);
		record.skyOffset      = pool.add(sky);
		record.lightingOffset = pool.add(lighting);

		const schema::Schema schema =
			schema::SchemaBuilder()
				.AddLayout<LegacyEnvRecord>(
					"EnvRecord",
					[](auto& layout) {
						layout.AddField("nameOffset", &LegacyEnvRecord::nameOffset)
							.AddField("skyOffset", &LegacyEnvRecord::skyOffset)
							.AddField("lightingOffset", &LegacyEnvRecord::lightingOffset);
					})
				.Finish();
		chunk::Writer writer(schema);
		writer.Add(1u, std::vector<LegacyEnvRecord>{ record });
		writer.Add(2u, pool.bytes());
		return writer.Finish(magic::c_BEnv, 3, 0);
	}
}

// The knobs a chunk-era sky and lighting carried are authored state, and the re-save that turns
// those files into cache entries drops them -- so the one migrate that converts the family is the
// only moment they can move onto the document.
TEST_CASE("migrate lifts the chunk-era env knobs onto the document, once", "[migrate][benv]")
{
	const Project project;
	std::filesystem::create_directories(project.root / "Environments");
	std::filesystem::create_directories(project.root / "Sky");
	std::filesystem::create_directories(project.root / "EnvLighting");

	project.Write(
		"Environments/forest.benv",
		LegacyEnvBytes("forest", "Sky/forest.bsky", "EnvLighting/forest.benvl"));
	project.Write("Sky/forest.bsky", LegacySkyBytes("forest", 2, 0.5f));
	project.Write("EnvLighting/forest.benvl", LegacyLightingBytes("forest", 1.375f, 2.5f));

	const MigrateReport report = migrateProject(project.root, false);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 3);

	const BEnv env = loadEnv(project.root / "Environments/forest.benv");
	CHECK(env.name == "forest");
	CHECK(env.sky == "Sky/forest.bsky");
	CHECK(env.lighting == "EnvLighting/forest.benvl");
	CHECK(env.skyMipLevel == 2);
	CHECK(env.skyRotationY == Catch::Approx(0.5f));
	REQUIRE(env.exposureOverride.has_value());
	CHECK(*env.exposureOverride == Catch::Approx(2.5f));

	const BSky sky = loadSky(project.root / "Sky/forest.bsky");
	CHECK(sky.name == "forest");
	CHECK(sky.sky == SomeRoute("sky"));

	const BEnvLighting lighting = loadEnvLighting(project.root / "EnvLighting/forest.benvl");
	CHECK(lighting.name == "forest");
	CHECK(lighting.exposure == 1.375f);
	CHECK(lighting.prefilter == SomeRoute("pre"));
	CHECK(lighting.irradiance == SomeRoute("irr"));

	SECTION("a second run changes nothing")
	{
		const MigrateReport again = migrateProject(project.root, false);
		CHECK(again.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(again.Count(MigratedFile::Outcome::kFailed) == 0);
	}

	SECTION("an override never authored stays absent")
	{
		project.Write(
			"EnvLighting/forest.benvl",
			LegacyLightingBytes("forest", 1.375f, std::nullopt));
		project.Write(
			"Environments/forest.benv",
			LegacyEnvBytes("forest", "Sky/forest.bsky", "EnvLighting/forest.benvl"));

		static_cast<void>(migrateProject(project.root, false));
		CHECK_FALSE(
			loadEnv(project.root / "Environments/forest.benv").exposureOverride.has_value());
	}
}
